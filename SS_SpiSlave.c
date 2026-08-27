
/** @addtogroup SPI_Slave
 *  @{
 */

/**
 * @file    SS_SpiSlave.c
 * @author
 *
 */

#include "sdk_config.h"
#include "nrf_drv_spis.h"
#include "nrf_gpio.h"
#include "nrf_delay.h"
#include "boards.h"
#include "app_error.h"
#include <string.h>
#include <stdio.h>
#include "ble_types.h"
#include "app_scheduler.h"
#include "app_timer.h"

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

#include "SS_SpiSlave.h"

/*******************************************************************************/
/*
 * This module owns the nRF52832 SPIS interface.
 *
 * Important behavior:
 * - The nRF is the SPI slave.
 * - The STM32 is the SPI master.
 * - The nRF can prepare bytes for transfer, but it cannot start a transfer.
 * - The STM32 starts every transfer by pulling CS low and driving SCK.
 * - Normal SPI traffic forwards STM32 data to BLE.
 * - Phone/LT traffic goes the other direction: BLE phone data is queued and
 *   delivered to the STM32 during the next safe SPI master transaction.
 */
#define SPIS_INSTANCE               1                                       /**< Nordic SPIS peripheral instance index. */
#define MISO_NOTIFY_PULSE_US        10                                      /**< Width of the MISO-low "data ready" pulse driven before re-arming SPIS. */
#define MISO_NOTIFY_SETUP_DELAY_US  100                                     /**< Minimum time after CS rises before MISO is pulsed, so the STM32 can wrap up the completed transfer (DMA/ISR housekeeping, EXTI re-arm) before being signaled. */
#define PHONE_TX_TIMEOUT_MS         2000                                    /**< Discard a pending or armed phone packet and restore normal SPIS if the STM32 has not read it within this time. */
#define PHONE_TX_MAX_RETRIES        3                                       /**< How many times a phone packet consumed by a normal poll is re-armed before being discarded. */
#define SPI_STREAM_IDLE_MS          100                                     /**< If a transfer completed less than this long ago, the STM32 is actively polling and MISO notification is deferred to the next XFER_DONE. */

/*******************************************************************************/
static const nrf_drv_spis_t spis = NRF_DRV_SPIS_INSTANCE(SPIS_INSTANCE);    /**< Nordic driver object for the selected SPIS instance. */

static uint8_t       m_tx_buf[SPIS_DATA_TX_BUFFER_SIZE];                    /**< Buffer currently armed in the SPIS driver for bytes sent from nRF to STM32 on MISO. */
static uint8_t       m_normal_tx_buf[SPIS_DATA_TX_BUFFER_SIZE];             /**< Default/normal TX buffer used during regular STM32-initiated polling transactions. */
static uint8_t       m_phone_tx_buf[SPIS_DATA_TX_BUFFER_SIZE];              /**< Queued phone/LT packet that must be delivered from nRF to STM32. */
static uint8_t       m_rx_buf[SPIS_DATA_RX_BUFFER_SIZE];                    /**< Buffer currently armed in the SPIS driver for bytes received from STM32 on MOSI. */
static const uint8_t m_rx_length = SPIS_DATA_RX_BUFFER_SIZE;                /**< Number of RX bytes provided to the SPIS driver. */
static const uint8_t m_tx_length = SPIS_DATA_TX_BUFFER_SIZE;                /**< Number of TX bytes provided to the SPIS driver. */

static SS_DATA_RECEIVE_CB _SpiSlaveDataRxCb = NULL;                         /**< Application callback used to forward normal STM32 SPI RX data to BLE. */
static SS_DEBUG_NOTIFY_CB _DebugNotifyCb = NULL;                            /**< Optional BLE debug notification callback for short LT status strings. */
static uint16_t      _connHandle = BLE_CONN_HANDLE_INVALID;                 /**< Current BLE connection handle, used only to encode connection status in normal SPI TX buffer. */
static volatile bool          _spiSlaveInit = false;                                 /**< True after nrf_drv_spis_init() succeeds and false after SPIS uninit. */
static bool          _phoneTxPending = false;                               /**< True when a phone/LT packet is waiting to be delivered to STM32. */
static bool          _phoneTxArmed = false;                                 /**< True when the active SPIS TX buffer is the queued phone/LT packet. */
static bool          _phoneTimerCreated = false;                            /**< True after the one-shot phone-delivery timeout timer has been created. */
static uint8_t       _phoneTxRetries = 0;                                   /**< Re-arm attempts for the current phone packet after it was consumed by a normal poll. */
static volatile bool     _spiXferSeen = false;                              /**< True once at least one SPIS transfer has completed; qualifies _lastXferDoneTicks. */
static volatile uint32_t _lastXferDoneTicks = 0;                            /**< app_timer tick count captured at the most recent SPIS XFER_DONE interrupt. */

APP_TIMER_DEF(m_phone_tx_timeout_timer);                                    /**< One-shot timer bounding how long a phone packet may stay armed waiting for the STM32 to read it. */

/*******************************************************************************/
static void SSi_SpisEventHandler(nrf_drv_spis_event_t event);

static void SSi_LtDebugNotify(const char *msg)
{
#if ENABLE_LT_BLE_DEBUG
    /*
     * Debug notifications are intentionally best-effort. This helper only
     * calls the registered callback if one exists; failures are ignored by the
     * callback implementation in main.c so SPI behavior is never affected.
     */
    if (_DebugNotifyCb != NULL)
    {
        _DebugNotifyCb(msg);
    }
#else
    UNUSED_PARAMETER(msg);
#endif
}

static bool SSi_IsCsLow(void)
{
    /*
     * The STM32 controls CS because it is the SPI master.
     * CS low means the master is currently selecting the nRF SPIS device or
     * is about to clock a transaction.
     */
    return (nrf_gpio_pin_read(APP_SPIS_CS_PIN) == 0);
}

static void SSi_SetConnectionStatus(uint8_t *pBuffer)
{
    /*
     * The last byte of the normal TX buffer reports BLE connection state to
     * the STM32 during regular SPI polling:
     *   0x01 = BLE connected
     *   0x00 = BLE disconnected
     *
     * Phone/LT packets are not modified here; they must stay exactly 10 bytes
     * as received from BLE.
     */
    if (_connHandle != BLE_CONN_HANDLE_INVALID)
    {
        pBuffer[m_tx_length - 1] = 0x01;
    }
    else
    {
        pBuffer[m_tx_length - 1] = 0x00;
    }
}

static ret_code_t SSi_ArmTxBuffer(uint8_t const *pTxBuffer)
{
    /*
     * SPIS is a slave peripheral, so this does not start an SPI transfer.
     * It only tells the Nordic driver which bytes should be shifted out on
     * MISO when the STM32 master later pulls CS low and provides SCK clocks.
     */
    memcpy(m_tx_buf, pTxBuffer, sizeof(m_tx_buf));
    return nrf_drv_spis_buffers_set(&spis, m_tx_buf, sizeof(m_tx_buf), m_rx_buf, sizeof(m_rx_buf));
}

static void SSi_ClearPhoneTx(bool notifyDiscard)
{
    /*
     * Clear all state related to a phone/LT packet. The caller chooses whether
     * this should be reported as a discard. Successful completion clears the
     * state without a discard notification.
     */
    bool had_phone_packet = (_phoneTxPending == true) || (_phoneTxArmed == true);

    /* Cancel the delivery timeout; phone state is being torn down. Safe to call
     * on a created-but-not-running timer. */
    if (_phoneTimerCreated == true)
    {
        (void)app_timer_stop(m_phone_tx_timeout_timer);
    }

    _phoneTxPending = false;
    _phoneTxArmed = false;
    _phoneTxRetries = 0;
    memset(m_phone_tx_buf, 0, sizeof(m_phone_tx_buf));

    if ((notifyDiscard == true) && (had_phone_packet == true))
    {
        SSi_LtDebugNotify("DBG:LT_DISCARD");
    }
}

static ret_code_t SSi_InitSpis(void)
{
    ret_code_t err_code;
    nrf_drv_spis_config_t spisConfig = NRF_DRV_SPIS_DEFAULT_CONFIG;

    /*
     * Bind the Nordic SPIS peripheral to the board pins shared with STM32.
     * The master owns CS and SCK; the nRF drives MISO only while SPIS is
     * enabled or when this module temporarily uses MISO as a GPIO notification.
     */
    spisConfig.csn_pin               = APP_SPIS_CS_PIN;
    spisConfig.miso_pin              = APP_SPIS_MISO_PIN;
    spisConfig.mosi_pin              = APP_SPIS_MOSI_PIN;
    spisConfig.sck_pin               = APP_SPIS_SCK_PIN;

    /*
     * The low-level SPIS interrupt handler is SSi_SpisEventHandler(). It posts
     * the event into the app scheduler so heavier work runs in main context.
     */
    err_code = nrf_drv_spis_init(&spis, &spisConfig, SSi_SpisEventHandler);
    if (err_code == NRF_SUCCESS)
    {
        _spiSlaveInit = true;
    }

    return err_code;
}

static void SSi_UninitSpisOnly(void)
{
    /*
     * Uninitialize only the Nordic SPIS driver. Pin cleanup is handled by
     * SS_UninitSpiSlave() when the whole SPI module is shutting down.
     */
    if (_spiSlaveInit == true)
    {
        nrf_drv_spis_uninit(&spis);
        _spiSlaveInit = false;
    }
}

static ret_code_t SSi_RestoreNormalSpis(void)
{
    ret_code_t err_code;

    /*
     * Restore the SPI slave to its normal polling state after an LT delivery
     * failure, timeout, or cleanup path. This re-enables SPIS and arms the
     * normal TX buffer so the next STM32 transaction sees normal status data.
     */
    SSi_UninitSpisOnly();

    err_code = SSi_InitSpis();
    if (err_code != NRF_SUCCESS)
    {
        return err_code;
    }

    return SSi_ArmTxBuffer(m_normal_tx_buf);
}

static ret_code_t SSi_StartPhoneTxTimeout(void)
{
    /*
     * (Re)start the one-shot timer that bounds how long a phone packet may
     * stay pending or armed. Stop first so restarting an already running
     * timer is always accepted.
     */
    (void)app_timer_stop(m_phone_tx_timeout_timer);
    return app_timer_start(m_phone_tx_timeout_timer, APP_TIMER_TICKS(PHONE_TX_TIMEOUT_MS), NULL);
}

static bool SSi_IsSpiStreamingActive(void)
{
    uint32_t elapsed_ticks;

    /*
     * The STM32 is considered to be actively polling when a transfer completed
     * within the last SPI_STREAM_IDLE_MS. While it is polling, the MISO
     * notification must be issued right after an XFER_DONE (the start of the
     * master's idle gap), never at an arbitrary time that can race the
     * master's next CS assertion.
     */
    if (_spiXferSeen != true)
    {
        return false;
    }

    elapsed_ticks = app_timer_cnt_diff_compute(app_timer_cnt_get(), _lastXferDoneTicks);
    return (elapsed_ticks < APP_TIMER_TICKS(SPI_STREAM_IDLE_MS));
}

static void SSi_PhoneTxTimeoutHandler(void *p_context)
{
    UNUSED_PARAMETER(p_context);

    /*
     * The STM32 master never read the armed phone packet within the timeout: it
     * missed the MISO notification pulse or was not servicing its EXTI. Discard
     * the packet and restore normal SPIS polling so later BLE payloads are not
     * rejected as NRF_ERROR_BUSY indefinitely. Runs in app_scheduler context
     * (APP_TIMER_CONFIG_USE_SCHEDULER), the same context as the SPIS event
     * handler, so re-initializing SPIS here is safe.
     */
    if ((_phoneTxArmed == true) || (_phoneTxPending == true))
    {
        bool phone_was_armed = _phoneTxArmed;

        SSi_LtDebugNotify("DBG:LT_CS_TIMEOUT");
        SSi_ClearPhoneTx(true);

        /*
         * Only rebuild SPIS when the phone packet was armed as the active TX
         * buffer. A packet that was merely pending never replaced the normal
         * buffer, and an unnecessary uninit/init here could swallow a
         * transaction the master is clocking right now.
         */
        if (phone_was_armed == true)
        {
            (void)SSi_RestoreNormalSpis();
        }

        /*
         * The master stopped polling (e.g., device on charger). Treat the bus
         * as idle again so the next phone packet is signaled immediately.
         */
        _spiXferSeen = false;
    }
}

static ret_code_t SSi_InterruptStm32AndArmPhoneTx(void)
{
    ret_code_t err_code;

    /*
     * To notify the STM32 that the nRF has a phone-originated packet ready,
     * temporarily release the SPIS peripheral and drive MISO as a GPIO.
     * Pulling MISO low is the out-of-band "data ready" signal to the master.
     */
    SSi_UninitSpisOnly();

    /*
     * The caller checked CS before calling, but the master may have started a
     * normal streaming transaction in the meantime. If CS fell, do not pulse
     * MISO into a transaction the STM32 is already clocking: put SPIS back,
     * keep the packet pending and let the XFER_DONE path (or the timeout)
     * deliver or discard it.
     */
    if (SSi_IsCsLow() == true)
    {
        err_code = SSi_InitSpis();
        if (err_code != NRF_SUCCESS)
        {
            SSi_LtDebugNotify("DBG:LT_SPIS_INIT_FAIL");
            SSi_ClearPhoneTx(true);
            (void)SSi_RestoreNormalSpis();
            return err_code;
        }

        (void)SSi_ArmTxBuffer(m_normal_tx_buf);

        err_code = SSi_StartPhoneTxTimeout();
        if (err_code != NRF_SUCCESS)
        {
            SSi_ClearPhoneTx(true);
            return err_code;
        }

        NRF_LOG_INFO("Phone SPI bridge notify deferred: CS fell before MISO pulse");
        return NRF_SUCCESS;
    }

    nrf_gpio_cfg_output(APP_SPIS_MISO_PIN);
    nrf_gpio_pin_clear(APP_SPIS_MISO_PIN);
    SSi_LtDebugNotify("DBG:LT_IRQ_LOW");

    nrf_delay_us(MISO_NOTIFY_PULSE_US);

    /*
     * Re-enable the SPIS peripheral and arm the phone packet now, while CS is
     * still high. The nRF52 SPIS latches its TX buffer on the CS falling edge,
     * so arming here lets the STM32's next CS-low edge shift out the real data.
     * The STM32 guarantees, via its own guard delay, that it will not assert CS
     * until this arming has completed. This replaces the previous approach of
     * waiting for CS low and then arming, which armed too late (after the
     * latching edge) and shifted out the SPIS DEF character (0xFF).
     */
    err_code = SSi_InitSpis();
    if (err_code != NRF_SUCCESS)
    {
        SSi_LtDebugNotify("DBG:LT_SPIS_INIT_FAIL");
        SSi_ClearPhoneTx(true);
        (void)SSi_RestoreNormalSpis();
        return err_code;
    }

    _phoneTxArmed = true;
    /*
     * The 10-byte phone packet is now armed. Actual bytes leave the nRF only as
     * the STM32 clocks the bus during the transaction it starts next.
     */
    err_code = SSi_ArmTxBuffer(m_phone_tx_buf);
    if (err_code != NRF_SUCCESS)
    {
        SSi_LtDebugNotify("DBG:LT_ARM_FAIL");
        SSi_ClearPhoneTx(true);
        (void)SSi_RestoreNormalSpis();
        return err_code;
    }

    /*
     * Bound the wait for the STM32 read without blocking. If the master missed
     * the MISO pulse (or is not servicing its EXTI), this one-shot timer fires
     * and SSi_PhoneTxTimeoutHandler() discards the packet and restores normal
     * SPIS. It is cancelled in SSi_ClearPhoneTx() once the packet is delivered.
     */
    ret_code_t timer_err = SSi_StartPhoneTxTimeout();
    if (timer_err != NRF_SUCCESS)
    {
        SSi_LtDebugNotify("DBG:LT_TIMER_FAIL");
        SSi_ClearPhoneTx(true);
        (void)SSi_RestoreNormalSpis();
        return timer_err;
    }

    return err_code;
}

/**
 * @brief Scheduled SPIS event handler.
 *
 * The Nordic SPIS interrupt handler posts events here through app_scheduler.
 * This keeps BLE forwarding, logging, and buffer re-arming out of interrupt
 * context.
 *
 * @param pData Pointer to nrf_drv_spis_event_t copied by app_scheduler.
 * @param length Length of scheduler event data.
 */
static void SSi_SpisScheduledEvent(void *pData, uint16_t length)
{
    /* Copy the scheduler payload into a local event object. */
    nrf_drv_spis_event_t event = *(nrf_drv_spis_event_t*)pData;
    ret_code_t err_code;

    UNUSED_PARAMETER(length);

    /*
     * The SPI module may have been torn down (BLE disconnect ->
     * SS_UninitSpiSlave) after this event was queued but before the scheduler
     * drained it. Every exit path below re-arms SPIS, which would touch an
     * uninitialized driver instance. Drop the stale event instead.
     */
    if (_spiSlaveInit != true)
    {
        NRF_LOG_WARNING("SPIS event dropped: module uninitialized");
        return;
    }

    /*
     * NRF_DRV_SPIS_XFER_DONE means the STM32 master completed a transaction.
     * At this point event.tx_amount tells how many bytes were clocked out from
     * the nRF TX buffer, and m_rx_buf contains bytes clocked in from MOSI.
     */
    if (event.evt_type == NRF_DRV_SPIS_XFER_DONE)
    {
        /*
         * Snapshot this before clearing phone state. It tells us whether the
         * just-finished transaction was a phone/LT delivery or a normal SPI
         * transaction.
         */
        bool phone_tx_was_armed = _phoneTxArmed;

        /*
         * The STM32 reads a phone packet with a short, dedicated transaction
         * (at most SPIS_DATA_TX_BUFFER_SIZE bytes) issued in response to the
         * MISO pulse. If the master clocked more RX bytes than that while the
         * phone packet was armed, this was a regular streaming poll that
         * consumed the phone bytes in place of the normal status frame: the
         * packet was NOT handled by the STM32 phone-read path.
         */
        bool phone_consumed_by_normal_poll = (phone_tx_was_armed == true) &&
                                             (event.rx_amount > SPIS_DATA_TX_BUFFER_SIZE);

        NRF_LOG_INFO("SS_Xfer_done");

        /*
         * Normal SPI transactions carry STM32-to-phone data, so forward the
         * received SPI RX buffer to BLE. Phone/LT transactions are different:
         * they are nRF-to-STM32 deliveries, so do not echo their RX buffer to
         * BLE as normal STM32 payload. A normal poll that swallowed the phone
         * packet still carried real STM32 data, so forward that as usual.
         */
        if (((phone_tx_was_armed != true) || (phone_consumed_by_normal_poll == true)) &&
            (_SpiSlaveDataRxCb != NULL))
        {
            _SpiSlaveDataRxCb(m_rx_buf, sizeof(m_rx_buf));
        }

        memset(m_rx_buf, 0, sizeof(m_rx_buf));

        if (phone_tx_was_armed == true)
        {
            if (phone_consumed_by_normal_poll == true)
            {
                /*
                 * The armed packet went out inside a normal streaming poll, so
                 * the STM32 never processed it as phone data (and its status
                 * parser saw phone bytes instead of a status frame). Re-arm
                 * and pulse again a bounded number of times so the packet is
                 * still delivered through a dedicated read.
                 */
                if (_phoneTxRetries < PHONE_TX_MAX_RETRIES)
                {
                    _phoneTxRetries++;
                    _phoneTxArmed = false;
                    _phoneTxPending = true;
                    NRF_LOG_WARNING("Phone SPI bridge packet consumed by normal poll, retry %u", (unsigned int)_phoneTxRetries);
                    SSi_LtDebugNotify("DBG:LT_RETRY");
                }
                else
                {
                    NRF_LOG_WARNING("Phone SPI bridge packet consumed by normal polls, discarding");
                    SSi_ClearPhoneTx(true);
                }
            }
            else
            {
                /*
                 * event.tx_amount is the number of bytes the STM32 actually
                 * clocked out of the nRF TX buffer. A complete LT delivery needs
                 * all 10 bytes; fewer bytes means the master stopped clocking
                 * before the whole packet was transferred.
                 */
                if (event.tx_amount >= SPIS_DATA_TX_BUFFER_SIZE)
                {
                    NRF_LOG_INFO("Phone SPI bridge transfer complete");
                    SSi_LtDebugNotify("DBG:LT_SENT_10");
                }
                else
                {
                    NRF_LOG_WARNING("Phone SPI bridge transfer incomplete: %u bytes", (unsigned int)event.tx_amount);

#if ENABLE_LT_BLE_DEBUG
                    /*
                     * Keep the debug string short enough for BLE notification use.
                     * The largest expected rendered form is DBG:LT_INCOMPLETE_255.
                     */
                    char incomplete_msg[sizeof("DBG:LT_INCOMPLETE_255")];

                    snprintf(incomplete_msg, sizeof(incomplete_msg), "DBG:LT_INCOMPLETE_%u", (unsigned int)event.tx_amount);
                    SSi_LtDebugNotify(incomplete_msg);
#endif
                }

                SSi_ClearPhoneTx(false);
            }
        }

        /*
         * Refresh the normal status byte after every transaction. BLE may have
         * connected or disconnected since the previous SPI transaction.
         */
        SSi_SetConnectionStatus(m_normal_tx_buf);

        if (_phoneTxPending == true)
        {
            /*
             * A queued phone packet is delivered from here because END fires
             * on the CS rising edge: this is the start of the master's idle
             * gap, the safest moment to pulse MISO and arm the packet. If the
             * master has already started another transaction, do not block
             * the scheduler waiting for it; keep the packet pending and retry
             * at the next XFER_DONE. The delivery timeout bounds the total
             * wait, so a stopped master cannot leave the packet stuck.
             */
            if (SSi_IsCsLow() == true)
            {
                NRF_LOG_INFO("Phone SPI bridge notify deferred: CS already low");
                (void)SSi_ArmTxBuffer(m_normal_tx_buf);
                return;
            }

            /*
             * CS just rose, but the STM32 still needs time to finish its
             * transfer-complete handling before it can accept the MISO
             * notification. Scheduler latency between the END interrupt and
             * this point is variable and can be near zero, so enforce the
             * setup time explicitly. A CS re-check follows inside
             * SSi_InterruptStm32AndArmPhoneTx() in case the master started a
             * new transaction during this delay.
             */
            nrf_delay_us(MISO_NOTIFY_SETUP_DELAY_US);

            err_code = SSi_InterruptStm32AndArmPhoneTx();
            if (err_code != NRF_SUCCESS)
            {
                NRF_LOG_WARNING("Phone SPI bridge arm failed: 0x%08X", (unsigned int)err_code);
            }
        }
        else
        {
            /*
             * No phone packet is waiting, so return to normal STM32 polling
             * behavior by arming the normal TX buffer again.
             */
            (void)SSi_ArmTxBuffer(m_normal_tx_buf);
        }
    }
}

/**
 * @brief Low-level SPIS driver event handler.
 *
 * This function is called from the Nordic driver context. It only posts the
 * event to the app scheduler; the real processing happens later in
 * SSi_SpisScheduledEvent().
 *
 * @param event SPIS event from the Nordic driver.
 */
static void SSi_SpisEventHandler(nrf_drv_spis_event_t event)
{
    ret_code_t err_code;

    if ((event.evt_type == NRF_DRV_SPIS_XFER_DONE) && (_spiSlaveInit == true))
    {
        /*
         * Timestamp the completed transfer here (not in the deferred handler)
         * so SS_SetPhoneTxbuffer() sees master polling activity without
         * scheduler latency.
         */
        _lastXferDoneTicks = app_timer_cnt_get();
        _spiXferSeen = true;
    }

    err_code = app_sched_event_put(&event, sizeof(nrf_drv_spis_event_t), SSi_SpisScheduledEvent);
    if (err_code != NRF_SUCCESS)
    {
        /*
         * Scheduler queue full (e.g., BLE connection-setup burst while the
         * STM32 streams at full rate). Resetting the device here would drop
         * the BLE link, so degrade gracefully instead: re-arm the current
         * buffers directly (the SDK SPIS examples call buffers_set from this
         * context) so the SPI link stays alive, and drop this frame's RX
         * data. Phone packet state, if any, is bounded by the delivery
         * timeout timer.
         */
        if ((event.evt_type == NRF_DRV_SPIS_XFER_DONE) && (_spiSlaveInit == true))
        {
            (void)nrf_drv_spis_buffers_set(&spis, m_tx_buf, sizeof(m_tx_buf), m_rx_buf, sizeof(m_rx_buf));
        }

        NRF_LOG_WARNING("SPIS event dropped, scheduler queue full: 0x%08X", (unsigned int)err_code);
    }
}

/*******************************************************************************/
/**
 * @brief Function to set optional debug notification callback.
 *
 * @param[in] debugNotifyCb Function to call for short debug messages.
 *
 * @return None
 */
void SS_SetDebugNotifyCallback(SS_DEBUG_NOTIFY_CB debugNotifyCb)
{
    /*
     * main.c registers LT_DebugNotify() here. Passing NULL disables callbacks
     * even when ENABLE_LT_BLE_DEBUG is compiled on.
     */
    _DebugNotifyCb = debugNotifyCb;
}

/*******************************************************************************/
/**
 * @brief Function to setting Tx buffer of spi slave
 *
 * @param[in] connHandle, BLE connection handle
 *
 * @return None
 */
void SS_SetConnectionHandle(uint16_t connHandle)
{
    /*
     * Store current BLE connection state and mirror it into the normal SPI TX
     * buffer for STM32 status polling.
     */
    _connHandle = connHandle;
    SSi_SetConnectionStatus(m_normal_tx_buf);
}

/*******************************************************************************/
/**
 * @brief Function to setting Tx buffer of spi slave
 *
 * @param pData, pointer to the data to transfer
 * @param length, length of data to transfer
 *
 * @return Return NRF_SUCCESS else error code
 */
ret_code_t SS_SetTxbuffer(uint8_t *pData, uint16_t length)
{
    /*
     * This API updates the normal SPI TX buffer. It is used for regular status
     * data sent from nRF to STM32 during normal master polling transactions.
     */
    if (_spiSlaveInit != true)
    {
        return NRF_ERROR_INVALID_STATE;
    }

    /*
     * Reserve the final byte for BLE connection status, so caller data can use
     * at most SPIS_DATA_TX_BUFFER_SIZE - 1 bytes.
     */
    uint16_t copy_len = (length < (SPIS_DATA_TX_BUFFER_SIZE - 1))
                        ? length
                        : (SPIS_DATA_TX_BUFFER_SIZE - 1);

    /* Rebuild the normal buffer from caller data plus current BLE status. */
    memset(m_normal_tx_buf, 0, sizeof(m_normal_tx_buf));
    memcpy(m_normal_tx_buf, pData, copy_len);
    SSi_SetConnectionStatus(m_normal_tx_buf);

    if (_phoneTxPending == true)
    {
        /*
         * A phone/LT delivery has priority. Update the saved normal buffer but
         * do not replace the pending phone packet in the active SPI path.
         */
        return NRF_SUCCESS;
    }

    /*
     * No phone packet is pending, so the normal buffer can be armed for the
     * next STM32 master transaction.
     */
    _phoneTxArmed = false;
    return SSi_ArmTxBuffer(m_normal_tx_buf);
}

/*******************************************************************************/
/**
 * @brief Function to set phone bridge Tx buffer of spi slave
 *
 * @param pData, pointer to the data to transfer
 * @param length, length of data to transfer
 *
 * @return Return NRF_SUCCESS else error code
 */
ret_code_t SS_SetPhoneTxbuffer(uint8_t *pData, uint16_t length)
{
    ret_code_t err_code;

    if (_spiSlaveInit != true)
    {
        return NRF_ERROR_INVALID_STATE;
    }

    /*
     * Phone-originated packets must be exactly one SPIS TX buffer long.
     * Payload contents are forwarded unchanged.
     */
    if (length != SPIS_DATA_TX_BUFFER_SIZE)
    {
        return NRF_ERROR_DATA_SIZE;
    }

    if ((_phoneTxPending == true) || (_phoneTxArmed == true))
    {
        return NRF_ERROR_BUSY;
    }

    /*
     * Store the phone packet separately from the normal TX buffer. This keeps
     * an LT delivery from corrupting any currently armed normal SPI transfer.
     */
    memset(m_phone_tx_buf, 0, sizeof(m_phone_tx_buf));
    memcpy(m_phone_tx_buf, pData, length);
    _phoneTxPending = true;
    _phoneTxRetries = 0;

    if ((SSi_IsCsLow() == true) || (SSi_IsSpiStreamingActive() == true))
    {
        /*
         * Either the STM32 master is mid-transaction (CS low) or it completed
         * a transfer recently and will poll again shortly. Pulsing MISO now
         * would race the master's next CS assertion, so defer instead: the
         * XFER_DONE path pulses MISO right after CS rises, at the start of
         * the master's idle gap. The timeout bounds the wait in case the
         * master stops polling before the next transaction.
         */
        err_code = SSi_StartPhoneTxTimeout();
        if (err_code != NRF_SUCCESS)
        {
            SSi_ClearPhoneTx(true);
            return err_code;
        }

        NRF_LOG_INFO("Phone SPI bridge payload queued for next safe SPI transaction");
        return NRF_SUCCESS;
    }

    /*
     * The SPI bus is idle (no transaction in progress and none recently), so
     * signal the STM32 immediately and arm the phone packet for the read it
     * starts in response.
     */
    return SSi_InterruptStm32AndArmPhoneTx();
}

/*******************************************************************************/
/**
 * @brief Compatibility wrapper for the old time update API.
 *
 * @param pData, pointer to the data to transfer
 * @param length, length of data to transfer
 *
 * @return Return NRF_SUCCESS else error code
 */
ret_code_t SS_SetTimeUpdateTxbuffer(uint8_t *pData, uint16_t length)
{
    /*
     * Backward-compatible name for older LT/time-update code. The behavior is
     * exactly the same as SS_SetPhoneTxbuffer().
     */
    return SS_SetPhoneTxbuffer(pData, length);
}

/*******************************************************************************/
/**
 * @brief Function to uninitialize spi slave
 *
 * @param None
 *
 * @return Return none
 */
void SS_UninitSpiSlave(void)
{
    /*
     * Called when BLE disconnects or the SPI bridge is being shut down. Clear
     * any pending phone packet because there is no active bridge session.
     */
    SSi_UninitSpisOnly();
    SSi_ClearPhoneTx(true);

    /* Return SPI input pins to default GPIO state. */
    nrf_gpio_cfg_default(APP_SPIS_CS_PIN);
    nrf_gpio_cfg_default(APP_SPIS_MOSI_PIN);
    nrf_gpio_cfg_default(APP_SPIS_SCK_PIN);

    /*
     * Leave MISO high as an idle state. MISO low is reserved for the temporary
     * phone/LT "data ready" notification to the STM32.
     */
    nrf_gpio_cfg_output(APP_SPIS_MISO_PIN);
    nrf_gpio_pin_set(APP_SPIS_MISO_PIN);
}

/*******************************************************************************/
/**
 * @brief Function to initialize spi slave
 *
 * @param data receive call back
 *
 * @return Return NRF_SUCCESS if initialize successfully else error code
 */
ret_code_t SS_InitSpiSlave(SS_DATA_RECEIVE_CB dataRxCb)
{
    ret_code_t err_code = NRF_SUCCESS;

    /*
     * Save the callback that receives normal STM32-to-nRF SPI data after each
     * completed normal transaction. main.c uses this to forward SPI data to BLE.
     */
    _SpiSlaveDataRxCb = dataRxCb;

    /* Create the one-shot phone-delivery timeout timer once (app_timer_init()
     * is done by main.c before this runs). Created before any SSi_ClearPhoneTx
     * call so that stopping it is always safe. */
    if (_phoneTimerCreated == false)
    {
        err_code = app_timer_create(&m_phone_tx_timeout_timer, APP_TIMER_MODE_SINGLE_SHOT, SSi_PhoneTxTimeoutHandler);
        if (err_code != NRF_SUCCESS)
        {
            return err_code;
        }
        _phoneTimerCreated = true;
    }

    /* Configure and enable the Nordic SPIS peripheral. */
    err_code = SSi_InitSpis();
    if (err_code != NRF_SUCCESS)
    {
        return err_code;
    }

    /*
     * Start from a clean phone/LT state. notifyDiscard=true reports if a stale
     * phone packet somehow existed before initialization.
     */
    SSi_ClearPhoneTx(true);

    /*
     * Clear all transfer buffers. The normal TX buffer is then initialized with
     * the current BLE connection-status byte and armed for the first STM32
     * transaction.
     */
    memset(m_rx_buf, 0, m_rx_length);
    memset(m_tx_buf, 0, sizeof(m_tx_buf));
    memset(m_normal_tx_buf, 0, sizeof(m_normal_tx_buf));
    SSi_SetConnectionStatus(m_normal_tx_buf);

    return SSi_ArmTxBuffer(m_normal_tx_buf);
}

/** @} */

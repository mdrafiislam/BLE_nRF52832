
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
#include "boards.h"
#include "app_error.h"
#include <string.h>
#include <stdio.h>
#include "ble_types.h"
#include "app_scheduler.h"

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
#define CS_NOTIFY_WAIT_MAX          10000000UL                              /**< Busy-wait loop count while waiting for STM32 to pull CS low after MISO-low notification. */

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
static bool          _spiSlaveInit = false;                                 /**< True after nrf_drv_spis_init() succeeds and false after SPIS uninit. */
static bool          _phoneTxPending = false;                               /**< True when a phone/LT packet is waiting to be delivered to STM32. */
static bool          _phoneTxArmed = false;                                 /**< True when the active SPIS TX buffer is the queued phone/LT packet. */

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

    _phoneTxPending = false;
    _phoneTxArmed = false;
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

static void SSi_WaitForCsHigh(void)
{
    /*
     * If a phone packet arrives while CS is low, a master transaction is
     * already in progress. Wait for the STM32 to release CS before changing
     * SPIS state or using MISO as a notification GPIO.
     */
    while (SSi_IsCsLow() == true)
    {
        /* Wait until the active master transaction releases CS. */
    }
}

static ret_code_t SSi_InterruptStm32AndArmPhoneTx(void)
{
    ret_code_t err_code;
    uint32_t checks;

    /*
     * To notify the STM32 that the nRF has a phone-originated packet ready,
     * temporarily release the SPIS peripheral and drive MISO as a GPIO.
     * Pulling MISO low is the out-of-band "data ready" signal to the master.
     */
    SSi_UninitSpisOnly();

    nrf_gpio_cfg_output(APP_SPIS_MISO_PIN);
    nrf_gpio_pin_clear(APP_SPIS_MISO_PIN);
    SSi_LtDebugNotify("DBG:LT_IRQ_LOW");

    /*
     * The nRF is still an SPI slave. After signaling on MISO, it must wait
     * for the STM32 master to respond by pulling CS low. Only the master can
     * start the SPI transaction because only the master drives SCK.
     */
    for (checks = 0; checks < CS_NOTIFY_WAIT_MAX; checks++)
    {
        if (SSi_IsCsLow() == true)
        {
            SSi_LtDebugNotify("DBG:LT_CS_LOW");
            /*
             * CS is now low, so the STM32 has started the transaction.
             * Re-enable the SPIS peripheral on the pins before arming the
             * phone packet as the active TX buffer.
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
             * The 10-byte phone packet is now ready to shift out on MISO.
             * Actual bytes leave the nRF only as the STM32 clocks the bus.
             */
            err_code = SSi_ArmTxBuffer(m_phone_tx_buf);
            if (err_code != NRF_SUCCESS)
            {
                SSi_LtDebugNotify("DBG:LT_ARM_FAIL");
                SSi_ClearPhoneTx(true);
                (void)SSi_RestoreNormalSpis();
            }

            return err_code;
        }
    }

    SSi_LtDebugNotify("DBG:LT_CS_TIMEOUT");
    /*
     * The STM32 did not respond to the MISO-low notification before the wait
     * limit expired. Discard the pending phone packet and restore normal SPIS
     * operation so regular SPI polling can continue.
     */
    SSi_ClearPhoneTx(true);
    return SSi_RestoreNormalSpis();
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

    UNUSED_PARAMETER(length);

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

        NRF_LOG_INFO("SS_Xfer_done");

        /*
         * Normal SPI transactions carry STM32-to-phone data, so forward the
         * received SPI RX buffer to BLE. Phone/LT transactions are different:
         * they are nRF-to-STM32 deliveries, so do not echo their RX buffer to
         * BLE as normal STM32 payload.
         */
        if ((phone_tx_was_armed != true) && (_SpiSlaveDataRxCb != NULL))
        {
            _SpiSlaveDataRxCb(m_rx_buf, sizeof(m_rx_buf));
        }

        memset(m_rx_buf, 0, sizeof(m_rx_buf));

        if (phone_tx_was_armed == true)
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
                /*
                 * Keep the debug string short enough for BLE notification use.
                 * The largest expected rendered form is DBG:LT_INCOMPLETE_255.
                 */
                char incomplete_msg[sizeof("DBG:LT_INCOMPLETE_255")];

                NRF_LOG_WARNING("Phone SPI bridge transfer incomplete: %u bytes", (unsigned int)event.tx_amount);
                snprintf(incomplete_msg, sizeof(incomplete_msg), "DBG:LT_INCOMPLETE_%u", (unsigned int)event.tx_amount);
                SSi_LtDebugNotify(incomplete_msg);
            }

            SSi_ClearPhoneTx(false);
        }

        /*
         * Refresh the normal status byte after every transaction. BLE may have
         * connected or disconnected since the previous SPI transaction.
         */
        SSi_SetConnectionStatus(m_normal_tx_buf);

        if (_phoneTxPending == true)
        {
            /*
             * A phone packet may arrive while CS is already low, meaning the
             * STM32 is in the middle of an existing SPI transaction. In that
             * case SS_SetPhoneTxbuffer() only queues the packet. After the
             * current transaction completes, wait for CS to return high, then
             * signal the STM32 and arm the queued phone packet for the next
             * safe master transaction.
             */
            SSi_WaitForCsHigh();
            (void)SSi_InterruptStm32AndArmPhoneTx();
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
    app_sched_event_put(&event, sizeof(nrf_drv_spis_event_t), SSi_SpisScheduledEvent);
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
    if (_spiSlaveInit != true)
    {
        return NRF_ERROR_INVALID_STATE;
    }

    /*
     * Phone-originated packets must be exactly one SPIS TX buffer long.
     * For LT this is the 10-byte payload: "LT" plus 8 uppercase hex chars.
     */
    if (length != SPIS_DATA_TX_BUFFER_SIZE)
    {
        return NRF_ERROR_DATA_SIZE;
    }

    /*
     * Store the phone packet separately from the normal TX buffer. This keeps
     * an LT delivery from corrupting any currently armed normal SPI transfer.
     */
    memset(m_phone_tx_buf, 0, sizeof(m_phone_tx_buf));
    memcpy(m_phone_tx_buf, pData, length);
    _phoneTxPending = true;

    if (SSi_IsCsLow() == true)
    {
        /*
         * CS low means the STM32 master is already performing a transaction.
         * Do not uninitialize SPIS, pull MISO low, or replace the active TX
         * buffer while that transaction is in progress. Leave the packet
         * pending; SSi_SpisScheduledEvent() will handle it after XFER_DONE.
         */
        NRF_LOG_INFO("Phone SPI bridge payload queued for next safe SPI transaction");
        return NRF_SUCCESS;
    }

    /*
     * No SPI transaction is active, so signal the STM32 immediately and arm
     * the phone packet when the STM32 responds by pulling CS low.
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

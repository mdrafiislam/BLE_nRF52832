
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
#define SPIS_INSTANCE               1                                       /**< SPIS instance index. */
#define CS_NOTIFY_WAIT_MAX          10000000UL

/*******************************************************************************/
static const nrf_drv_spis_t spis = NRF_DRV_SPIS_INSTANCE(SPIS_INSTANCE);    /**< SPIS instance. */

static uint8_t       m_tx_buf[SPIS_DATA_TX_BUFFER_SIZE];                    /**< Active TX buffer. */
static uint8_t       m_normal_tx_buf[SPIS_DATA_TX_BUFFER_SIZE];             /**< Normal TX buffer. */
static uint8_t       m_lt_tx_buf[SPIS_DATA_TX_BUFFER_SIZE];                 /**< Pending LT TX buffer. */
static uint8_t       m_rx_buf[SPIS_DATA_RX_BUFFER_SIZE];                    /**< RX buffer. */
static const uint8_t m_rx_length = SPIS_DATA_RX_BUFFER_SIZE;                /**< Transfer RX length. */
static const uint8_t m_tx_length = SPIS_DATA_TX_BUFFER_SIZE;                /**< Transfer TX length. */

static SS_DATA_RECEIVE_CB _SpiSlaveDataRxCb = NULL;
static SS_DEBUG_NOTIFY_CB _DebugNotifyCb = NULL;
static uint16_t      _connHandle = BLE_CONN_HANDLE_INVALID;                 /**< Handle of the current connection. */
static bool          _spiSlaveInit = false;
static bool          _ltPending = false;
static bool          _ltArmed = false;

/*******************************************************************************/
static void SSi_SpisEventHandler(nrf_drv_spis_event_t event);

static void SSi_LtDebugNotify(const char *msg)
{
#if ENABLE_LT_BLE_DEBUG
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
    return (nrf_gpio_pin_read(APP_SPIS_CS_PIN) == 0);
}

static void SSi_SetConnectionStatus(uint8_t *pBuffer)
{
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
    memcpy(m_tx_buf, pTxBuffer, sizeof(m_tx_buf));
    return nrf_drv_spis_buffers_set(&spis, m_tx_buf, sizeof(m_tx_buf), m_rx_buf, sizeof(m_rx_buf));
}

static void SSi_DiscardPendingLt(void)
{
    bool had_lt_packet = (_ltPending == true) || (_ltArmed == true);

    _ltPending = false;
    _ltArmed = false;
    memset(m_lt_tx_buf, 0, sizeof(m_lt_tx_buf));

    if (had_lt_packet == true)
    {
        SSi_LtDebugNotify("DBG:LT_DISCARD");
    }
}

static ret_code_t SSi_InitSpis(void)
{
    ret_code_t err_code;
    nrf_drv_spis_config_t spisConfig = NRF_DRV_SPIS_DEFAULT_CONFIG;

    spisConfig.csn_pin               = APP_SPIS_CS_PIN;
    spisConfig.miso_pin              = APP_SPIS_MISO_PIN;
    spisConfig.mosi_pin              = APP_SPIS_MOSI_PIN;
    spisConfig.sck_pin               = APP_SPIS_SCK_PIN;

    err_code = nrf_drv_spis_init(&spis, &spisConfig, SSi_SpisEventHandler);
    if (err_code == NRF_SUCCESS)
    {
        _spiSlaveInit = true;
    }

    return err_code;
}

static void SSi_UninitSpisOnly(void)
{
    if (_spiSlaveInit == true)
    {
        nrf_drv_spis_uninit(&spis);
        _spiSlaveInit = false;
    }
}

static ret_code_t SSi_RestoreNormalSpis(void)
{
    ret_code_t err_code;

    SSi_UninitSpisOnly();

    err_code = SSi_InitSpis();
    if (err_code != NRF_SUCCESS)
    {
        return err_code;
    }

    return SSi_ArmTxBuffer(m_normal_tx_buf);
}

static ret_code_t SSi_InterruptStm32AndArmLt(void)
{
    ret_code_t err_code;
    uint32_t checks;

    SSi_UninitSpisOnly();

    nrf_gpio_cfg_output(APP_SPIS_MISO_PIN);
    nrf_gpio_pin_clear(APP_SPIS_MISO_PIN);
    SSi_LtDebugNotify("DBG:LT_IRQ_LOW");

    for (checks = 0; checks < CS_NOTIFY_WAIT_MAX; checks++)
    {
        if (SSi_IsCsLow() == true)
        {
            SSi_LtDebugNotify("DBG:LT_CS_LOW");
            err_code = SSi_InitSpis();
            if (err_code != NRF_SUCCESS)
            {
                SSi_LtDebugNotify("DBG:LT_SPIS_INIT_FAIL");
                SSi_DiscardPendingLt();
                (void)SSi_RestoreNormalSpis();
                return err_code;
            }

            _ltArmed = true;
            err_code = SSi_ArmTxBuffer(m_lt_tx_buf);
            if (err_code != NRF_SUCCESS)
            {
                SSi_LtDebugNotify("DBG:LT_ARM_FAIL");
                SSi_DiscardPendingLt();
                (void)SSi_RestoreNormalSpis();
            }

            return err_code;
        }
    }

    SSi_LtDebugNotify("DBG:LT_CS_TIMEOUT");
    SSi_DiscardPendingLt();
    return SSi_RestoreNormalSpis();
}

/**
 * @brief SPIS user event handler.
 *
 * @param event
 */
static void SSi_SpisScheduledEvent(void *pData, uint16_t length)
{
    nrf_drv_spis_event_t event = *(nrf_drv_spis_event_t*)pData;

    UNUSED_PARAMETER(length);

    if (event.evt_type == NRF_DRV_SPIS_XFER_DONE)
    {
        NRF_LOG_INFO("SS_Xfer_done");

        if (_SpiSlaveDataRxCb != NULL)
        {
            _SpiSlaveDataRxCb(m_rx_buf, sizeof(m_rx_buf));
        }

        memset(m_rx_buf, 0, sizeof(m_rx_buf));

        if (_ltArmed == true)
        {
            if (event.tx_amount >= SPIS_DATA_TX_BUFFER_SIZE)
            {
                NRF_LOG_INFO("LT SPI transfer complete");
                SSi_LtDebugNotify("DBG:LT_SENT_10");
            }
            else
            {
                char incomplete_msg[sizeof("DBG:LT_INCOMPLETE_255")];

                NRF_LOG_WARNING("LT SPI transfer incomplete: %u bytes", (unsigned int)event.tx_amount);
                snprintf(incomplete_msg, sizeof(incomplete_msg), "DBG:LT_INCOMPLETE_%u", (unsigned int)event.tx_amount);
                SSi_LtDebugNotify(incomplete_msg);
            }

            SSi_DiscardPendingLt();
        }

        SSi_SetConnectionStatus(m_normal_tx_buf);

        if (_ltPending == true)
        {
            _ltArmed = true;
            (void)SSi_ArmTxBuffer(m_lt_tx_buf);
        }
        else
        {
            (void)SSi_ArmTxBuffer(m_normal_tx_buf);
        }
    }
}

/**
 * @brief SPIS user event handler.
 *
 * @param event
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
    if (_spiSlaveInit != true)
    {
        return NRF_ERROR_INVALID_STATE;
    }

    uint16_t copy_len = (length < (SPIS_DATA_TX_BUFFER_SIZE - 1))
                        ? length
                        : (SPIS_DATA_TX_BUFFER_SIZE - 1);

    memset(m_normal_tx_buf, 0, sizeof(m_normal_tx_buf));
    memcpy(m_normal_tx_buf, pData, copy_len);
    SSi_SetConnectionStatus(m_normal_tx_buf);

    if (_ltPending == true)
    {
        return NRF_SUCCESS;
    }

    _ltArmed = false;
    return SSi_ArmTxBuffer(m_normal_tx_buf);
}

/*******************************************************************************/
/**
 * @brief Function to setting time update Tx buffer of spi slave
 *
 * @param pData, pointer to the data to transfer
 * @param length, length of data to transfer
 *
 * @return Return NRF_SUCCESS else error code
 */
ret_code_t SS_SetTimeUpdateTxbuffer(uint8_t *pData, uint16_t length)
{
    if (_spiSlaveInit != true)
    {
        return NRF_ERROR_INVALID_STATE;
    }

    if (length > SPIS_DATA_TX_BUFFER_SIZE)
    {
        return NRF_ERROR_DATA_SIZE;
    }

    memset(m_lt_tx_buf, 0, sizeof(m_lt_tx_buf));
    memcpy(m_lt_tx_buf, pData, length);
    _ltPending = true;

    if (SSi_IsCsLow() == true)
    {
        NRF_LOG_INFO("LT queued for next safe SPI transaction");
        return NRF_SUCCESS;
    }

    return SSi_InterruptStm32AndArmLt();
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
    SSi_UninitSpisOnly();
    SSi_DiscardPendingLt();

    nrf_gpio_cfg_default(APP_SPIS_CS_PIN);
    nrf_gpio_cfg_default(APP_SPIS_MOSI_PIN);
    nrf_gpio_cfg_default(APP_SPIS_SCK_PIN);

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

    _SpiSlaveDataRxCb = dataRxCb;

    err_code = SSi_InitSpis();
    if (err_code != NRF_SUCCESS)
    {
        return err_code;
    }

    SSi_DiscardPendingLt();

    memset(m_rx_buf, 0, m_rx_length);
    memset(m_tx_buf, 0, sizeof(m_tx_buf));
    memset(m_normal_tx_buf, 0, sizeof(m_normal_tx_buf));
    SSi_SetConnectionStatus(m_normal_tx_buf);

    return SSi_ArmTxBuffer(m_normal_tx_buf);
}

/** @} */

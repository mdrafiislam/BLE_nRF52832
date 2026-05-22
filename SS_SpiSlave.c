
/** @addtogroup SPI_Slave
 *  @{
 */

/**
 * @file 	SS_SpiSlave.c
 * @author
 *
 */

#include "sdk_config.h"
#include "nrf_drv_spis.h"
#include "nrf_gpio.h"
#include "boards.h"
#include "app_error.h"
#include <string.h>
#include "ble_types.h"
#include "app_scheduler.h"

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

#include "SS_SpiSlave.h"

/*******************************************************************************/
#define SPIS_INSTANCE               1                                       /**< SPIS instance index. */

/*******************************************************************************/
static const nrf_drv_spis_t spis = NRF_DRV_SPIS_INSTANCE(SPIS_INSTANCE);    /**< SPIS instance. */

static uint8_t       m_tx_buf[SPIS_DATA_TX_BUFFER_SIZE];                     /**< TX buffer. */
static uint8_t       m_rx_buf[SPIS_DATA_RX_BUFFER_SIZE];                       /**< RX buffer. */
static const uint8_t m_rx_length = SPIS_DATA_RX_BUFFER_SIZE;                           /**< Transfer RX length. */
static const uint8_t m_tx_length = SPIS_DATA_TX_BUFFER_SIZE;                           /**< Transfer TX length. */

static SS_DATA_RECEIVE_CB _SpiSlaveDataRxCb = NULL;
static uint16_t      _connHandle = BLE_CONN_HANDLE_INVALID;                 /**< Handle of the current connection. */
static bool         _spiSlaveInit = false;

/*******************************************************************************/

/**
 * @brief SPIS user event handler.
 *
 * @param event
 */
static void SSi_SpisScheduledEvent(void *pData, uint16_t length)
{
    nrf_drv_spis_event_t event = *(nrf_drv_spis_event_t*)pData;

    if (event.evt_type == NRF_DRV_SPIS_XFER_DONE)
    {
        NRF_LOG_INFO("SS_Xfer_done");

        if (_SpiSlaveDataRxCb != NULL)
        {
            _SpiSlaveDataRxCb(m_rx_buf, sizeof(m_rx_buf));
        }         

        memset(m_rx_buf, 0, sizeof(m_rx_buf));

        if (_connHandle != BLE_CONN_HANDLE_INVALID)
        {
            m_tx_buf[m_tx_length - 1] = 0x01;
        }
        else
        {
            m_tx_buf[m_tx_length - 1] = 0x00;
        }

        nrf_drv_spis_buffers_set(&spis, m_tx_buf, sizeof(m_tx_buf), m_rx_buf, sizeof(m_rx_buf));
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
 * @brief Function to setting Tx buffer of spi slave
 *
 * @param[in] connHandle, BLE connection handle
 *
 * @return None
 */
void SS_SetConnectionHandle(uint16_t connHandle)
{
    _connHandle = connHandle;
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

    memset(m_tx_buf, 0, sizeof(m_tx_buf));
    memcpy(m_tx_buf, pData, copy_len);

    if (_connHandle != BLE_CONN_HANDLE_INVALID)
    {
        m_tx_buf[m_tx_length - 1] = 0x01;
    }
    else
    {
        m_tx_buf[m_tx_length - 1] = 0x00;
    }

    return nrf_drv_spis_buffers_set(&spis, m_tx_buf, sizeof(m_tx_buf), m_rx_buf, sizeof(m_rx_buf)); 
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

    memset(m_tx_buf, 0, sizeof(m_tx_buf));
    memcpy(m_tx_buf, pData, length);

    return nrf_drv_spis_buffers_set(&spis, m_tx_buf, sizeof(m_tx_buf), m_rx_buf, sizeof(m_rx_buf));
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
    nrf_drv_spis_uninit(&spis);
    _spiSlaveInit = false;

    nrf_gpio_cfg_default(APP_SPIS_CS_PIN);
    nrf_gpio_cfg_default(APP_SPIS_MOSI_PIN);
    nrf_gpio_cfg_default(APP_SPIS_SCK_PIN);

    nrf_gpio_cfg_output(APP_SPIS_MISO_PIN);
    nrf_gpio_pin_clear(APP_SPIS_MISO_PIN);
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

    nrf_drv_spis_config_t spisConfig = NRF_DRV_SPIS_DEFAULT_CONFIG;
    spisConfig.csn_pin               = APP_SPIS_CS_PIN;
    spisConfig.miso_pin              = APP_SPIS_MISO_PIN;
    spisConfig.mosi_pin              = APP_SPIS_MOSI_PIN;
    spisConfig.sck_pin               = APP_SPIS_SCK_PIN;

    err_code = nrf_drv_spis_init(&spis, &spisConfig, SSi_SpisEventHandler);
    if (err_code != NRF_SUCCESS)
    {
        return err_code;
    }

    _spiSlaveInit = true;

    memset(m_rx_buf, 0, m_rx_length);
    memset(m_tx_buf, 0, sizeof(m_tx_buf));

    if (_connHandle != BLE_CONN_HANDLE_INVALID)
    {
        m_tx_buf[m_tx_length - 1] = 0x01;
    }
    else
    {
        m_tx_buf[m_tx_length - 1] = 0x00;
    }

    err_code = nrf_drv_spis_buffers_set(&spis, m_tx_buf, sizeof(m_tx_buf), m_rx_buf, sizeof(m_rx_buf));

    return err_code;
}

/** @} */

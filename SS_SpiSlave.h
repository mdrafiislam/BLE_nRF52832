
/** @addtogroup SS_SpiSlave
 *  @{
 */

/**
 * @file 		SS_SpiSlave.h
 * @author		
 *
 * @brief 		File handles various function related to SPI Slave
 *
 */

#ifndef _SS_SPI_SLAVE_H
#define _SS_SPI_SLAVE_H

#include <stdint.h>

#define SPIS_DATA_RX_BUFFER_SIZE       244 // Bytes
#define SPIS_DATA_TX_BUFFER_SIZE       10 // Bytes

#ifndef ENABLE_LT_BLE_DEBUG
#define ENABLE_LT_BLE_DEBUG            1
#endif

/**
 * @brief User event handler prototype to receive SPI slave data.
 *
 * This function is called when the data receive on spi slave.
 *
 * @param[in] 	pData 		Pointer to received spi slave data
 * @param[in] 	length 		Received data length
 */
typedef void (*SS_DATA_RECEIVE_CB)(uint8_t *pData, uint16_t length);
typedef void (*SS_DEBUG_NOTIFY_CB)(const char *msg);

/**
 * @brief Function to set optional debug notification callback.
 *
 * @param[in] debugNotifyCb Function to call for short debug messages.
 *
 * @return None
 */
void SS_SetDebugNotifyCallback(SS_DEBUG_NOTIFY_CB debugNotifyCb);

/**
 * @brief Function to setting Tx buffer of spi slave
 *
 * @param[in] connHandle, BLE connection handle
 *
 * @return None
 */
void SS_SetConnectionHandle(uint16_t connHandle);

/**
 * @brief Function to uninitialize spi slave
 *
 * @param None
 *
 * @return Return none
 */
void SS_UninitSpiSlave(void);

/**
 * @brief Function to setting Tx buffer of spi slave
 *
 * @param pData, pointer to the data to transfer
 * @param length, length of data to transfer
 *
 * @return Return NRF_SUCCESS else error code
 */
ret_code_t SS_SetTxbuffer(uint8_t *pData, uint16_t length);

/**
 * @brief Queue a time update Tx buffer for spi slave delivery
 *
 * If CS is high, SPIS is temporarily uninitialized and MISO is driven low
 * until CS goes low or CS_NOTIFY_WAIT_MAX expires. If CS is already low, the
 * LT packet is queued for the next safe SPI transaction.
 *
 * @param pData, pointer to the 10-byte time update data to transfer
 * @param length, length of data to transfer
 *
 * @return Return NRF_SUCCESS else error code
 */
ret_code_t SS_SetTimeUpdateTxbuffer(uint8_t *pData, uint16_t length);

/**
 * @author 
 *
 * @brief 	Function to initialize spi slave
 *
 * @param[in] 	Function to call when data receive in SPI slave
 *
 * @return 	NRF_SUCCESS or error code.
 */
ret_code_t SS_InitSpiSlave(SS_DATA_RECEIVE_CB dataRxCb);

#endif	/* _SS_SPI_SLAVE_H */

/** @} */

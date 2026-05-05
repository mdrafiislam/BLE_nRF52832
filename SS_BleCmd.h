
/** @addtogroup SS_BleCmd
 *  @{
 */

/**
 * @file 		SS_BleCmd.h
 * @author		
 *
 * @brief 		File handles various function related to command receive over BLE
 *
 */

#ifndef _SS_BLE_CMD_H
#define _SS_BLE_CMD_H

#include <stdint.h>

/**
 * @brief Function to process BLE command
 *
 * @param[in] pData, pointer to the data to process
 * @param[in] length, length of data to process
 *
 * @return Return NRF_SUCCESS else error code
 */
ret_code_t BC_ProcessBleCommand(uint8_t *pData, uint16_t length);

/**
 * @brief Check if data is a valid UT time-update command
 *
 * Validates: starts with "UT", exactly 18 bytes total,
 * bytes 2-17 are valid hex characters (case-insensitive).
 *
 * @param[in]  pData   Pointer to received data
 * @param[in]  length  Length of received data
 *
 * @return true if valid UT command, false otherwise
 */
bool BC_IsUtPrefix(const uint8_t *pData, uint16_t length);

/**
 * @author
 *
 * @brief  Function to initialize BLE command
 *
 * @param[in] 	None
 *
 * @return 	NRF_SUCCESS or error code.
 */
ret_code_t BC_InitBleCommand(void);

#endif	/* _SS_BLE_CMD_H */

/** @} */

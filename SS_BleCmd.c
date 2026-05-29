
/** @addtogroup BLE_Command
 *  @{
 */

/**
 * @file 	SS_BleCmd.c
 * @author
 *
 */

#include "sdk_config.h"
#include <string.h>
#include "app_timer.h"
#include "nrf_soc.h"


#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

#include "SS_BleCmd.h"

/*******************************************************************************/
#define DFU_START           "STARTDFU"
#define RESET_DEVICE        "STARTRST"

// Following three declaration is necessary for Bootloader
#define BOOTLOADER_DFU_GPREGRET                 (0xB0)
#define BOOTLOADER_DFU_START_BIT_MASK           (0x01)
#define BOOTLOADER_DFU_START                    (BOOTLOADER_DFU_GPREGRET | BOOTLOADER_DFU_START_BIT_MASK)

/*******************************************************************************/
APP_TIMER_DEF(_bleCmdTmId);
static const char _dfuStart[] = DFU_START;
static const char _rstDevice[] = RESET_DEVICE;

/*******************************************************************************/
/**
 * @brief BLE command timer timeout function
 *
 */
static void BCi_BleCmdTimeOut(void *pContext)
{
    NVIC_SystemReset();
}

/*******************************************************************************/
/**
 * @brief Function to process BLE command
 *
 * @param[in] pData, pointer to the data to process
 * @param[in] length, length of data to process
 *
 * @return Return NRF_SUCCESS else error code
 */
ret_code_t BC_ProcessBleCommand(uint8_t *pData, uint16_t length)
{
    if (strncmp(pData, _dfuStart, strlen(_dfuStart)) == 0)
    {
        NRF_LOG_INFO("Start DFU");
        sd_power_gpregret_clr(0, 0xffffffff);
        sd_power_gpregret_set(0, BOOTLOADER_DFU_START);

        app_timer_start(_bleCmdTmId, APP_TIMER_TICKS(2000), NULL);
    }
    else if (strncmp(pData, _rstDevice, strlen(_rstDevice)) == 0)
    {
        NRF_LOG_INFO("Restart device");
        app_timer_start(_bleCmdTmId, APP_TIMER_TICKS(2000), NULL);
    }

    return NRF_SUCCESS;
}

/*******************************************************************************/
/**
 * @author 
 *
 * @brief 	Function to initialize BLE command
 *
 * @param[in] 	None
 *
 * @return 	NRF_SUCCESS or error code.
 */
ret_code_t BC_InitBleCommand(void)
{
    ret_code_t err_code = NRF_SUCCESS;

    err_code = app_timer_create(&_bleCmdTmId, APP_TIMER_MODE_SINGLE_SHOT, BCi_BleCmdTimeOut);

    return err_code;
}

/*******************************************************************************/
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
bool BC_IsUtPrefix(const uint8_t *pData, uint16_t length)
{
    if (length != 18)
    {
        return false;
    }

    if (pData[0] != 'U' && pData[0] != 'u')
    {
        return false;
    }
    if (pData[1] != 'T' && pData[1] != 't')
    {
        return false;
    }

    for (int i = 2; i < 18; i++)
    {
        uint8_t c = pData[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
        {
            return false;
        }
    }

    return true;
}

/** @} */

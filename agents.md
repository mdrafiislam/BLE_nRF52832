# Agent Notes

## Current Firmware Context

This repository is an nRF52832 SPI-to-BLE bridge based on nRF5 SDK 17.1.0. The active branch is `time_update`.

Recent phone-to-SPI bridge work added temporary BLE debug notifications so delivery and discard paths can be observed from the nRF Connect app without a debugger. The legacy `DBG:LT_*` message names remain in use for compatibility, but the payload is no longer LT-specific.

## BLE-to-SPI Phone Payloads

- `STARTDFU` and `STARTRST` are handled as exact BLE commands before bridge forwarding.
- Any other exact 10-byte NUS RX payload is copied into an app-scheduler event and forwarded unchanged to STM32 over SPI MISO.
- Do not require an `LT` prefix or hexadecimal content.
- Only one phone payload may be pending or armed. A second payload is rejected with `NRF_ERROR_BUSY`.
- If CS is already low, delivery waits until the active STM32 transaction completes.
- Otherwise, SPIS is temporarily released and MISO is driven low to notify STM32. CS-low and CS-high waits are bounded by `CS_NOTIFY_WAIT_MAX`.
- Keep the STM32-side protocol unchanged and do not add CT acknowledgment behavior.

## Debug Notifications

The debug feature is macro-gated by `ENABLE_LT_BLE_DEBUG`, currently defaulting to `0` in `SS_SpiSlave.h`.

Debug messages are sent through the existing Nordic UART Service TX notification path. They must remain short, best-effort, and non-fatal:

- Do not use `APP_ERROR_CHECK()` for debug BLE send failures.
- Silently skip when disconnected or notifications are not enabled.
- Do not add debug notifications inside the CS polling loop.
- Do not change normal SPI-to-BLE forwarding behavior.

Current debug messages:

- `DBG:LT_RX`: 10-byte phone payload received in `main.c`.
- `DBG:LT_IRQ_LOW`: MISO pulled low to notify STM32 in `SS_SpiSlave.c`.
- `DBG:LT_CS_LOW`: CS observed low before timeout.
- `DBG:LT_CS_TIMEOUT`: CS did not go low before `CS_NOTIFY_WAIT_MAX`.
- `DBG:LT_CS_HIGH_TIMEOUT`: CS did not return high before `CS_NOTIFY_WAIT_MAX`.
- `DBG:LT_SPIS_INIT_FAIL`: SPIS reinitialization failed after CS went low.
- `DBG:LT_ARM_FAIL`: phone TX buffer arming failed.
- `DBG:LT_SENT_10`: phone transfer completed with at least 10 bytes sent.
- `DBG:LT_INCOMPLETE_XX`: phone transfer completed with fewer than 10 bytes sent.
- `DBG:LT_DISCARD`: pending or armed phone packet was discarded.

## Files To Check For Bridge Work

- `main.c`: NUS receive path, `LT_DebugNotify()`, callback registration.
- `SS_SpiSlave.c`: phone SPI delivery, timeout, failure, completion, and discard paths.
- `SS_SpiSlave.h`: debug macro and callback API.
- `SS_BleCmd.c` / `SS_BleCmd.h`: DFU and reset command handling.

## Build

Use this command from the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File .\build_nrf.ps1
```

Direct `.\build_nrf.ps1` may fail on systems where PowerShell script execution is disabled.

## nRF Connect Observation

To observe bridge debug messages:

1. Connect to device `AIM21`.
2. Open Nordic UART Service.
3. Enable notifications on the NUS TX characteristic.
4. Write an exact 10-byte payload to the NUS RX characteristic.
5. Watch for `DBG:...` notifications.

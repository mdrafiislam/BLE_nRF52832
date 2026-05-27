# Agent Notes

## Current Firmware Context

This repository is an nRF52832 SPI-to-BLE bridge based on nRF5 SDK 17.1.0. The active branch is `time_update`.

Recent LT time-update work added temporary BLE debug notifications so LT delivery and discard paths can be observed from the nRF Connect app without a debugger.

## LT Debug Notifications

The debug feature is macro-gated by `ENABLE_LT_BLE_DEBUG`, currently defaulting to `1` in `SS_SpiSlave.h`.

Debug messages are sent through the existing Nordic UART Service TX notification path. They must remain short, best-effort, and non-fatal:

- Do not use `APP_ERROR_CHECK()` for debug BLE send failures.
- Silently skip when disconnected or notifications are not enabled.
- Do not add debug notifications inside the CS polling loop.
- Do not change normal SPI-to-BLE forwarding behavior.
- Do not change the STM32-side protocol.
- Do not change the LT payload format: it must remain exactly 10 bytes, `LT` plus 8 uppercase hex characters.
- Do not add CT acknowledgment behavior.

Current debug messages:

- `DBG:LT_RX`: valid LT command received in `main.c`.
- `DBG:LT_IRQ_LOW`: MISO pulled low to notify STM32 in `SS_SpiSlave.c`.
- `DBG:LT_CS_LOW`: CS observed low before timeout.
- `DBG:LT_CS_TIMEOUT`: CS did not go low before `CS_NOTIFY_WAIT_MAX`.
- `DBG:LT_SPIS_INIT_FAIL`: SPIS reinitialization failed after CS went low.
- `DBG:LT_ARM_FAIL`: LT TX buffer arming failed.
- `DBG:LT_SENT_10`: LT transfer completed with at least 10 bytes sent.
- `DBG:LT_INCOMPLETE_XX`: LT transfer completed with fewer than 10 bytes sent.
- `DBG:LT_DISCARD`: pending or armed LT packet was discarded.

## Files To Check For LT Work

- `main.c`: NUS receive path, `LT_DebugNotify()`, callback registration.
- `SS_SpiSlave.c`: LT SPI delivery, timeout, failure, completion, and discard paths.
- `SS_SpiSlave.h`: debug macro and callback API.
- `SS_BleCmd.c` / `SS_BleCmd.h`: LT command validation.

## Build

Use this command from the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File .\build_nrf.ps1
```

Direct `.\build_nrf.ps1` may fail on systems where PowerShell script execution is disabled.

## nRF Connect Observation

To observe LT debug messages:

1. Connect to device `AIM21`.
2. Open Nordic UART Service.
3. Enable notifications on the NUS TX characteristic.
4. Write a valid LT command to the NUS RX characteristic.
5. Watch for `DBG:...` notifications.

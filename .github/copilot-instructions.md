# Project Overview

This is an embedded firmware project for the **nRF52832** SoC that implements an **SPI-to-BLE bridge**. The device operates as an SPI slave and BLE peripheral, forwarding data between an external SPI master and a BLE central (e.g., MATLAB or a mobile phone) via the Nordic UART Service (NUS). It also advertises a Device Information Service (DIS).

---

# Repository Structure

- **`main.c`** — Application entry point. Initializes all subsystems, BLE event handling, NUS data callbacks, DIS, and SPI-to-BLE bridging.
- **`SS_SpiSlave.c` / `SS_SpiSlave.h`** — SPI slave driver wrapper using SPIS instance 1. RX buffer 244 bytes, TX buffer 10 bytes.
- **`SS_BleCmd.c` / `SS_BleCmd.h`** — BLE command processor. Handles "STARTDFU" (triggers bootloader) and "STARTRST" (system reset).
- **`pca10040/s132/ses/`** — SEGGER Embedded Studio project (`ble_app_spis_pca10040_s132.emProject`), linker config (`flash_placement.xml`), board config.
- **`pca10040/s132/config/sdk_config.h`** — All SDK feature flags, pin mappings, and system configuration.
- **`Merge_and_zip/`** — DFU pipeline: SoftDevice hex, bootloader hex, private signing key, merge script (`ble_spis_merge_zip_Generator.bat`), and output HEX/ZIP files.
- **`MatlabReceiveCode/`** — MATLAB scripts for BLE connection and data display.
- **`Documents/`** — Reference docs: DFU instructions, RAM address changes, hex loading guide.
- **`build_nrf.ps1`** — PowerShell build script invoking SEGGER emBuild.
- **`readme.txt`** — Quick device configuration summary.

---

# Development Environment

- **Language**: C (C99)
- **SDK**: nRF5 SDK v17.1.0
- **SoftDevice**: S132 v7.2.0 (BLE peripheral)
- **MCU**: nRF52832_xxAA, Cortex-M4 with FPU, hard-float ABI
- **IDE**: SEGGER Embedded Studio for ARM v5.34
- **Compiler**: GCC (arm-none-eabi-gcc)
- **Board**: PCA10040 development kit
- **External tools**: nrfutil, nRF Command Line Tools (mergehex), Python 3.7+, J-Link
- **No unit test framework** — this is embedded firmware with no test infrastructure.

---

# Build, Run, and Test Commands

- **Build**: `powershell -ExecutionPolicy Bypass -File .\build_nrf.ps1`
  - Invokes: `emBuild -config Debug -rebuild .\pca10040\s132\ses\ble_app_spis_pca10040_s132.emProject -verbose`
- **Output HEX**: `pca10040\s132\ses\Output\Debug\Exe\ble_app_spis_pca10040_s132.hex`
- **DFU merge**: `Merge_and_zip\ble_spis_merge_zip_Generator.bat` (requires nrfutil, mergehex, and private key)
- **No test commands found.**

---

# Coding Guidelines

- **File headers**: Nordic BSD-style 5-clause license block in `main.c`; custom block comments with `@file`, `@brief`, `@param` in custom files.
- **Naming conventions**:
  - Static internal functions: `BCi_`, `SSi_` prefix (e.g., `BCi_BleCmdTimeOut`, `SSi_SpisScheduledEvent`)
  - Public functions: `BC_`, `SS_` prefix (e.g., `BC_ProcessBleCommand`, `SS_InitSpiSlave`, `SS_SetTxbuffer`)
  - Static variables: `_` prefix (e.g., `_connHandle`, `_SpiSlaveDataRxCb`)
  - Macros/constants: `ALL_CAPS_WITH_UNDERSCORES`
  - Defines: `#define` with descriptive names (e.g., `DEVICE_NAME`, `APP_ADV_INTERVAL`)
- **Include order**: `sdk_config.h` first, then standard library, then Nordic SDK headers, then local headers.
- **Error handling**: `APP_ERROR_CHECK()` and `APP_ERROR_HANDLER()` macros throughout.
- **BLE macros**: `BLE_NUS_DEF`, `NRF_BLE_GATT_DEF`, `BLE_ADVERTISING_DEF` for instance declarations.
- **Return codes**: `ret_code_t` from Nordic SDK for all API calls.
- **Logging**: `NRF_LOG_INFO()` via SEGGER RTT backend.
- **Callback pattern**: Function pointers registered at init (e.g., `SS_DATA_RECEIVE_CB`).
- **Scheduler**: `app_scheduler` defers SPI events to main loop; `app_timer2` with RTC1.
- **Guard macros**: `#ifndef/_define/#endif` header guards with `_FILENAME_H` pattern. Doxygen `@addtogroup` grouping.

---

# Firmware Rules

- **SPI configuration**: SPIS instance 1, pins: SCK=2, MISO=3, MOSI=29, CS=4, Mode 0. RX buffer 244 bytes, TX buffer 10 bytes (normal traffic reserves last byte for BLE connection status; BLE-to-SPI payloads use all 10 bytes).
- **BLE config**: Advertising interval 3200 units (2000ms). Connection interval 60–70ms. Slave latency 1. hvn_tx_queue_size 20.
- **NUS UUID**: `6E400000-B5A3-F393-E0A9-E50E24DCCA9E`. The `NUS_BASE_UUID` in SDK `ble_nus.c` (line 64) is expected to be `{{0x31, 0xAC, 0x24, 0x85, 0x9B, 0x50, 0x22, 0xB5, 0xD3, 0x48, 0x2D, 0xD9, 0xA9, 0xD2, 0xA1, 0x4A}}`; this is already true in this checkout, but must be checked when using a fresh SDK.
- **DIS**: Model="nRF52832", HW rev="AIM21", FW rev="20q16cst1sl63ci10rotLT". Added to advertising scan response.
- **RAM layout**: `RAM_START=0x20004650` (moved from default `0x20002AD8` to avoid overlap with enlarged `hvn_tx_queue`). Flash app starts at `0x26000`.
- **Command protocol**:
  - Exact "STARTDFU" — sets GPREGRET register `0xB0` bit 0, triggers bootloader after 2s delay.
  - Exact "STARTRST" — triggers system reset after 2s delay.
  - Any other exact 10-byte NUS payload — copied into the app scheduler and forwarded unchanged via SPI MISO.
- **BLE -> SPI delivery**: only one phone payload may be pending or armed. If CS is high, the nRF temporarily releases SPIS and pulls MISO low to notify STM32. CS waits are bounded by `CS_NOTIFY_WAIT_MAX`; timeout and discard paths restore normal polling.
- **Bridge debug notifications**: `ENABLE_LT_BLE_DEBUG` defaults to `0`. Keep the legacy `DBG:LT_*` names short, best-effort, and non-fatal.
- **sdk_config.h** is the single source of truth for all SDK feature flags and pin assignments. Do not change without understanding downstream effects on memory layout and BLE behavior.
- **`NRF_SDH_BLE_GAP_EVENT_LENGTH`** is set to 10 (not default 6) — increasing radio on-time. Do not reduce without checking connection stability.

---

# Safety Rules for Copilot

- Do not delete existing functionality unless explicitly asked.
- Do not hardcode secrets, paths, credentials, or API keys. The file `Merge_and_zip/Key/private.key` is a tracked DFU signing key in this repository — do not expose it outside the repo or duplicate it elsewhere.
- Preserve existing public function signatures (`BC_*`, `SS_*`) unless a change is requested.
- Prefer minimal, targeted edits to `main.c`, `SS_SpiSlave.c`, `SS_BleCmd.c`, and `sdk_config.h`.
- Explain any major code change after editing, especially changes to BLE timing, SPI pin assignments, or memory layout.
- When modifying firmware or BLE logic, keep original pipeline behavior (SPI-to-BLE forwarding, command handling) unless explicitly asked to modify it.
- Do not change `RAM_START` or flash placement without explicit instruction — it affects BLE stack memory overlap.
- Do not remove Nordic SDK license headers.
- Build artifacts in `pca10040/s132/ses/Output/` are gitignored — do not commit them.

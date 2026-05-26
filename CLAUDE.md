# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is an nRF52832 firmware project implementing an SPI-to-BLE bridge using the nRF5 SDK 17.1.0. The device operates as an SPI slave that forwards data to a BLE central via the Nordic UART Service (NUS), and also forwards BLE data back to the SPI master. A Device Information Service (DIS) is also advertised.

**Target**: nRF52832_xxAA on PCA10040 dev kit
**SoftDevice**: S132 v7.2.0 (BLE peripheral)
**Toolchain**: SEGGER Embedded Studio for ARM v5.34 + GCC (arm-none-eabi-gcc), Cortex-M4F hard-float
**Build system**: SES emBuild (not Makefile/CMake)

## Key Directories

| Directory | Purpose |
|-----------|---------|
| `pca10040/s132/ses/` | SEGGER Embedded Studio project files, linker config, flash placement |
| `pca10040/s132/config/` | `sdk_config.h` — all SDK feature flags and pin mappings |
| `Merge_and_zip/` | DFU pipeline: SD hex, bootloader hex, merge script, private key |
| `MatlabReceiveCode/` | MATLAB client code for connecting to and receiving data from the device |
| `Documents/` | Reference docs (DFU instructions, RAM address changes, loading hex) |

## Custom Source Files (in project root)

- **`main.c`** (~693 lines) — Application entry point. Initializes all subsystems, runs the BLE event handler (`ble_evt_handler`), handles NUS data callbacks (`nus_data_handler`), manages DIS, and bridges SPI <-> BLE data.
- **`SS_SpiSlave.c` / `SS_SpiSlave.h`** — SPI slave wrapper using SPIS instance 1. RX buffer: 244 bytes, TX buffer: 10 bytes (normal traffic reserves last byte for BLE connection status; LT time responses use all 10 bytes). Uses `app_scheduler` to defer SPI events to the main loop.
- **`SS_BleCmd.c` / `SS_BleCmd.h`** — BLE command processor. Recognizes "STARTDFU" (triggers bootloader via GPREGRET) and "STARTRST" (system reset after 2s delay). Validates "LT" prefix time-sync commands (18 bytes: "LT" + 16 hex chars representing milliseconds, converted to unix seconds).

## Architecture

```
External Master (SPI)                    nRF52832 (SPI2BLE)              BLE Central (MATLAB/Phone)
     |                                         |                                    |
     |  SPI MOSI (pin 29)                       |                                    |
     +---------------------------------------> SPI Slave (SS_SpiSlave.c)             |
     |                                         |                                    |
     |  SPI MISO (pin 3)                        |  BLE NUS Service (ble_nus.c)       |
     +<---------------------------------------+------------------------------------->
     |                                         |                                    |
     |  "LT" + 8 hex chars (time-sync resp)    |  Receives from NUS, forwards to SPI  |
```

**Two data directions:**
1. **SPI -> BLE**: External master writes SPI -> nRF52 captures via SPIS -> sends over BLE NUS notification from the SPI receive callback
2. **BLE -> SPI**: Central writes BLE NUS -> nRF52 receives -> forwards to external master via SPI MISO. Special "LT" prefix commands trigger time-sync response.

**SPI Pin mapping** (from `sdk_config.h`): SCK=2, MISO=3, MOSI=29, CS=4. Mode 0.

**BLE Services**:
- **NUS** (UUID `6E400000-B5A3-F393-E0A9-E50E24DCCA9E`): Primary data channel
- **DIS** (UUID `0x180A`): Model="nRF52832", HW rev="AIM21", FW rev="20q16cst1sl63ci10rotLT"

## Build Commands

```powershell
# Build via script
powershell -ExecutionPolicy Bypass -File .\build_nrf.ps1

# Manual build (from repository root)
& "C:\Program Files\SEGGER\SEGGER Embedded Studio for ARM 5.34\bin\emBuild.exe" -config Debug -rebuild .\pca10040\s132\ses\ble_app_spis_pca10040_s132.emProject -verbose
```

**Output**: `pca10040\s132\ses\Output\Debug\Exe\ble_app_spis_pca10040_s132.hex` (application-only, ~114KB, no bootloader)

**DFU merge pipeline** (from `Merge_and_zip/`):
```batch
nrfutil settings generate --family NRF52 --application ble_app_spis_pca10040_s132.hex --application-version 0 --bootloader-version 0 --bl-settings-version 2 bl_setting.hex
mergehex -m SD_hex\s132_nrf52_7.2.0_softdevice.hex ble_app_spis_pca10040_s132.hex -o SD_APP.hex
mergehex -m SD_APP.hex BL_hex\secure_bootloader_ble_s132_pca10040.hex -o SD_APP_BL.hex
mergehex -m SD_APP_BL.hex bl_setting.hex -o merge_ble_app_ble_app_spis_pca10040_s132.hex
nrfutil pkg generate --hw-version 52 --sd-req 0x0101 --application-version 1 --application ble_app_spis_pca10040_s132.hex --key-file Key\private.key ble_app_spis_pca10040_s132.zip
```

Run `Merge_and_zip\ble_spis_merge_zip_Generator.bat` to execute the full pipeline.

## Memory Layout

- **FLASH**: 0x0-0x80000 (512KB), application starts at 0x26000
- **RAM**: 0x20000000-0x20010000 (64KB), application RAM starts at 0x20004650 (adjusted from default 0x20002AD8 to accommodate larger hvn_tx_queue_size=20)
- Heap: 8192 bytes, Stack: 8192 bytes

## Key sdk_config.h Settings

| Setting | Value | Notes |
|---------|-------|-------|
| `NRF_SDH_BLE_GAP_EVENT_LENGTH` | 10 | Increased from default 6 for more radio on-time |
| `NRF_SDH_BLE_GATT_MAX_MTU_SIZE` | 247 | Maximum GATT MTU |
| `BLE_DIS_ENABLED` | 1 | Device Information Service |
| `BLE_NUS_ENABLED` | 1 | Nordic UART Service |
| `SPIS_ENABLED` | 1 | SPI Slave |
| `SPIS1_ENABLED` | 1 | Using SPIS instance 1 |
| `NRF_SDH_BLE_TOTAL_LINK_COUNT` | 1 | Single connection |
| `APP_TIMER_V2` | Yes | RTC1-based timer |

## Important Notes

- The SDK `ble_nus.c` UUID is expected to be set to `#define NUS_BASE_UUID {{0x31, 0xAC, 0x24, 0x85, 0x9B, 0x50, 0x22, 0xB5, 0xD3, 0x48, 0x2D, 0xD9, 0xA9, 0xD2, 0xA1, 0x4A}}` on line 64; this is already true in this checkout, but must be checked when using a fresh SDK.
- DCDC is enabled at runtime via `sd_power_dcdc_mode_set(NRF_POWER_DCDC_ENABLE)` (disabled by default in `sdk_config.h`)
- Advertising interval: 3200 units (2000ms). Connection interval: 60-70ms. Slave latency: 1.
- No test infrastructure exists — this is embedded firmware with no unit test framework.
- Build artifacts in `pca10040/s132/ses/Output/` are gitignored.

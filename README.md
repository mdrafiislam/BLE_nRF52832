# SPI2BLE-UpWork

## Contents:
1. [Device Configuration](README.md#Device-Configuration)
2. [Steps to build and run the program](README.md#steps-to-build-and-run-the-program)
3. [Receiving Data in Matlab](README.md#receiving-data-in-matlab)

## Device Configuration 
nRF52832 is in SPI slave and BLE peripheral mode. Current consumption is 6uA in advertising mode and 400uA 
in data transmission mode. The device configurations are given below. 

DFU start command = STARTDFU\
Reset command = STARTRST

SPI Config: \
CS Pin = 4\
MISO Pin = 3\
MOSI Pin = 29\
SCK Pin = 2\
Mode = 0\
Other config = default\
RX buffer = 244 bytes\
TX buffer = 10 bytes (normal byte 9 is BLE connection status; UT time-update uses all 10 bytes)\
SPIS_INSTANCE 1

BLE Config:\
TX buffer = 244 bytes\
RX buffer = 244 bytes\
Advertising interval = 3200 (2000 ms)\
Connection interval = 63.5 ms\
Advertising duration = unlimited\
hvn_tx_queue_size = 20\
DCDC regulator = on\
Connection Event Length Extension = on

UUID = 6E400000-B5A3-F393-E0A9-E50E24DCCA9E (ble_nus.c)

NRF_SDH_BLE_GAP_EVENT_LENGTH 10 (sdk_config.h)\
NRF_SDH_BLE_GATT_MAX_MTU_SIZE 247 (sdk_config.h)

RAM_START=0x20004650


*Making hvn_tx_queue_size 20 allowed more data to be buffered in BLE queue which in tern allowed us to have 127.5ms connection interval.\
**As we increased hvn_tx_queue_size, we had to increase RAM_START address to ensure there was no RAM overlapping region between ble_stack and main program. Otherwise, the BLE device will not advertise.

## Steps to build and run the program:
1. Download and unzip SDK 17.1.0 from the [Nordic Semiconductor website](www.nordicsemi.com/Products/Development-software/nrf5-sdk/download)
2. Download and unzip this repository and place it inside the "ble_peripheral" folder inside the SDK 17.1.0. The path to the "ble_peripheral" folder should be: "...\nRF5_SDK_17.1.0_ddde560\examples\ble_peripheral." After pasting, the folder format should look like this: "...\nRF5_SDK_17.1.0_ddde560\examples\ble_peripheral\SPI2BLE-UpWork."
3. Download, install, and run [Segger Embedded Studio for Arm v5.34](https://www.segger.com/downloads/embedded-studio/)
4. In Segger Embedded Studio (SES) goto File>Open Solution and open ble_app_spis_pca10040_s132.emProject. Path to this file should be: "...\nRF5_SDK_17.1.0_ddde560\example\ble_peripheral\SPI2BLE-UpWork\pca10040\s132."
5. In SES, goto View>Project Explorer. In "Project Explorer" on the left hand side of the SES screen, open "ble_nus.c" under "nRF_BLE_Services" section. Inside ble_nus.c on line 64, change #define NUS_BASE_UUID to {{0x31, 0xAC, 0x24, 0x85, 0x9B, 0x50, 0x22, 0xB5, 0xD3, 0x48, 0x2D, 0xD9, 0xA9, 0xD2, 0xA1, 0x4A}}. It should look like this: #define NUS_BASE_UUID {{0x31, 0xAC, 0x24, 0x85, 0x9B, 0x50, 0x22, 0xB5, 0xD3, 0x48, 0x2D, 0xD9, 0xA9, 0xD2, 0xA1, 0x4A}}
6. Again, in "Project Explorer" under "Application" section, go to sdk_config.h and change #define NRF_SDH_BLE_GAP_EVENT_LENGTH 6 to #define NRF_SDH_BLE_GAP_EVENT_LENGTH 10. This will increase the radio on time to accommodate all the packets in the connection interval.
7. Again, in "Project Explorer" right click on "nRF_BLE_Services" section, then click on "Add Existing File..." Then go to components\ble\ble_services\ble_dis and select ble_dis.c and click open.
8. Use [Changing RAM Starting Address.docx](/Documents/) inside the "Document" folder of this repository to change the RAM_START from 0x20002ad8 to 0x20004650.
9. In SES, goto Build>Build Solution, the program should build without any issue and generate "ble_app_spis_pca10040_s132.hex." This HEX file **do not have bootloader to perform wireless DFU.**
### Steps to merge program hex and bootloader for wireless DFU 
1. Download and install [python 3.7 or later](https://www.python.org/downloads/). Add python path to Environment Path Variable
2. Use pip to install [nrfutil.](https://infocenter.nordicsemi.com/index.jsp?topic=%2Fug_nrfutil%2FUG%2Fnrfutil%2Fnrfutil_installing.html) Open a command prompt and type nrfutil and press enter. If the nrfutil installation was successful, the command prompt should display Options and Commands of nrfutil.
3. Install [nRF Command Line Tools](https://www.nordicsemi.com/Products/Development-tools/nrf-command-line-tools/download). Add "bin" folder of the "nRF command line tool" to Environment Path Variable. Usual location of "bin" folder is: C:\Program Files\Nordic Semiconductor\nrf-command-line-tools\bin
4. Copy the ble_app_spis_pca10040_s132.hex from "...\nRF5_SDK_17.1.0_ddde560\example\ble_peripheral\SPI2BLE-UpWork\pca10040\s132\ses\Output\Release\Exe" (or, "...\SPI2BLE-UpWork\pca10040\s132\ses\Output\Debug\Exe") and paste it inside "Merge_and_Zip" folder. Run "ble_spis_merge_zip_Generator.bat." This will create two files, i.e., "merge_ble_app_spis_pca10040_s132.hex" that contains the application and bootloader for wireless DFU, and "ble_app_spis_pca10040_s132.zip" for wireless DFU.
5. To load HEX file, download and install [nRF Connect for desktop](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-desktop/Download#infotabs). Download and install [J-Link](https://www.segger.com/downloads/jlink/). Use [load_hex_nrf52832.docx](/Documents/) inside the "Document" folder of this repository to load "merge_ble_app_spis_pca10040_s132.hex" in AIM device. Follow step: d-to-g of [load_hex_nrf52832.docx](/Documents/) document to load hex file to any other nRF52832 device.
6. To perform wireless DFU, the nRF52832 has to have OTA DFU bootloader. So before performing wireless DFU, the "merge_ble_app_spis_pca10040_s132.hex" has to be loaded to nRF52832 device at least once by following step 5. After that, follow [BLE DFU.docx](/Documents/) document in the "Documents" folder of this repository to perform wireless DFU.

## Receiving Data in Matlab
**All Matlab codes are in [MatlabReceiveCode](/MatlabReceiveCode/) folder.
1. Run "checkAvailableBLEDevices.m". After Run completes, copy the AIM21 device address from command window.
2. Open "ble_connect.m" and paste the AIM21 device address inside "ble" function on line 3. If the AIM21 device address is "E88AFE919714" the "ble" function on line 3 should look like this: b = ble("E88AFE919714"); 
3. Run "connect_V5.m"

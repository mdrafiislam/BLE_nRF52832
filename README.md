# SPI2BLE-UpWork

## Contents:

1. [Device Configuration](README.md#Device-Configuration)
2. [Steps to build and run the program](README.md#steps-to-build-and-run-the-program)
3. [Receiving Data in Matlab](README.md#receiving-data-in-matlab)

## Device Configuration

nRF52832 is in SPI slave and BLE peripheral mode. Current consumption is 6uA in advertising mode and 400uA
in data transmission mode. The device configurations are given below.

DFU start command = STARTDFU  
Reset command = STARTRST

SPI Config:   
CS Pin = 4  
MISO Pin = 3  
MOSI Pin = 29  
SCK Pin = 2  
Mode = 0  
Other config = default  
RX buffer = 244 bytes  
TX buffer = 10 bytes (normal byte 9 is BLE connection status; BLE-to-SPI payloads use all 10 bytes)  
SPIS\_INSTANCE 1

BLE Config:  
TX buffer = 244 bytes  
RX buffer = 10 bytes  
Advertising interval = 3200 (2000 ms)  
Connection interval = 63.5 ms  
Advertising duration = unlimited  
hvn\_tx\_queue\_size = 20  
DCDC regulator = on  
Connection Event Length Extension = on

UUID = 6E400000-B5A3-F393-E0A9-E50E24DCCA9E (ble\_nus.c)

NRF\_SDH\_BLE\_GAP\_EVENT\_LENGTH 10 (sdk\_config.h)  
NRF\_SDH\_BLE\_GATT\_MAX\_MTU\_SIZE 247 (sdk\_config.h)

RAM\_START=0x20004650



\*Making hvn\_tx\_queue\_size 20 allowed more data to be buffered in BLE queue which in tern allowed us to have 127.5ms connection interval.  
\*\*As we increased hvn\_tx\_queue\_size, we had to increase RAM\_START address to ensure there was no RAM overlapping region between ble\_stack and main program. Otherwise, the BLE device will not advertise.

## Steps to build and run the program:

1. Download and unzip SDK 17.1.0 from the [Nordic Semiconductor website](www.nordicsemi.com/Products/Development-software/nrf5-sdk/download)
2. Download and unzip this repository and place it inside the "ble\_peripheral" folder inside the SDK 17.1.0. The path to the "ble\_peripheral" folder should be: "...\\nRF5\_SDK\_17.1.0\_ddde560\\examples\\ble\_peripheral." After pasting, the folder format should look like this: "...\\nRF5\_SDK\_17.1.0\_ddde560\\examples\\ble\_peripheral\\SPI2BLE-UpWork."
3. Download, install, and run [Segger Embedded Studio for Arm v5.34](https://www.segger.com/downloads/embedded-studio/)
4. In Segger Embedded Studio (SES) goto File>Open Solution and open ble\_app\_spis\_pca10040\_s132.emProject. Path to this file should be: "...\\nRF5\_SDK\_17.1.0\_ddde560\\example\\ble\_peripheral\\SPI2BLE-UpWork\\pca10040\\s132."
5. In SES, goto View>Project Explorer. In "Project Explorer" on the left hand side of the SES screen, open "ble\_nus.c" under "nRF\_BLE\_Services" section. Inside ble\_nus.c on line 64, change #define NUS\_BASE\_UUID to {{0x31, 0xAC, 0x24, 0x85, 0x9B, 0x50, 0x22, 0xB5, 0xD3, 0x48, 0x2D, 0xD9, 0xA9, 0xD2, 0xA1, 0x4A}}. It should look like this: #define NUS\_BASE\_UUID {{0x31, 0xAC, 0x24, 0x85, 0x9B, 0x50, 0x22, 0xB5, 0xD3, 0x48, 0x2D, 0xD9, 0xA9, 0xD2, 0xA1, 0x4A}}
6. Again, in "Project Explorer" under "Application" section, go to sdk\_config.h and change #define NRF\_SDH\_BLE\_GAP\_EVENT\_LENGTH 6 to #define NRF\_SDH\_BLE\_GAP\_EVENT\_LENGTH 10. This will increase the radio on time to accommodate all the packets in the connection interval.
7. Again, in "Project Explorer" right click on "nRF\_BLE\_Services" section, then click on "Add Existing File..." Then go to components\\ble\\ble\_services\\ble\_dis and select ble\_dis.c and click open.
8. Use [Changing RAM Starting Address.docx](/Documents/) inside the "Document" folder of this repository to change the RAM\_START from 0x20002ad8 to 0x20004650 and RAM\_SIZE from 0xd528 to 0xb9b0
9. In SES, goto Build>Build Solution, the program should build without any issue and generate "ble\_app\_spis\_pca10040\_s132.hex." This HEX file **do not have bootloader to perform wireless DFU.**

### Steps to merge program hex and bootloader for wireless DFU

1. Download and install [python 3.7 or later](https://www.python.org/downloads/). Add python path to Environment Path Variable
2. Use pip to install [nrfutil.](https://infocenter.nordicsemi.com/index.jsp?topic=/ug_nrfutil/UG/nrfutil/nrfutil_installing.html) Open a command prompt and type nrfutil and press enter. If the nrfutil installation was successful, the command prompt should display Options and Commands of nrfutil.
3. Install [nRF Command Line Tools](https://www.nordicsemi.com/Products/Development-tools/nrf-command-line-tools/download). Add "bin" folder of the "nRF command line tool" to Environment Path Variable. Usual location of "bin" folder is: C:\\Program Files\\Nordic Semiconductor\\nrf-command-line-tools\\bin
4. Copy the ble\_app\_spis\_pca10040\_s132.hex from "...\\nRF5\_SDK\_17.1.0\_ddde560\\example\\ble\_peripheral\\SPI2BLE-UpWork\\pca10040\\s132\\ses\\Output\\Release\\Exe" (or, "...\\SPI2BLE-UpWork\\pca10040\\s132\\ses\\Output\\Debug\\Exe") and paste it inside "Merge\_and\_Zip" folder. Run "ble\_spis\_merge\_zip\_Generator.bat." This will create two files, i.e., "merge\_ble\_app\_spis\_pca10040\_s132.hex" that contains the application and bootloader for wireless DFU, and "ble\_app\_spis\_pca10040\_s132.zip" for wireless DFU.
5. To load HEX file, download and install [nRF Connect for desktop](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-desktop/Download#infotabs). Download and install [J-Link](https://www.segger.com/downloads/jlink/). Use [load\_hex\_nrf52832.docx](/Documents/) inside the "Document" folder of this repository to load "merge\_ble\_app\_spis\_pca10040\_s132.hex" in AIM device. Follow step: d-to-g of [load\_hex\_nrf52832.docx](/Documents/) document to load hex file to any other nRF52832 device.
6. To perform wireless DFU, the nRF52832 has to have OTA DFU bootloader. So before performing wireless DFU, the "merge\_ble\_app\_spis\_pca10040\_s132.hex" has to be loaded to nRF52832 device at least once by following step 5. After that, follow [BLE DFU.docx](/Documents/) document in the "Documents" folder of this repository to perform wireless DFU.

## Receiving Data in Matlab

\*\*All Matlab codes are in [MatlabReceiveCode](/MatlabReceiveCode/) folder.

1. Run "checkAvailableBLEDevices.m". After Run completes, copy the AIM21 device address from command window.
2. Open "ble\_connect.m" and paste the AIM21 device address inside "ble" function on line 3. If the AIM21 device address is "E88AFE919714" the "ble" function on line 3 should look like this: b = ble("E88AFE919714");
3. Run "connect\_V5.m"


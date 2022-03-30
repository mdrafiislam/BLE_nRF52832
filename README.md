# SPI2BLE-UpWork
The BLE stack for AIM from UpWork
nRF52832 is in SPI slave and BLE peripheral mode. Current consumption is 6uA in advertising mode and 400uA 
in data transmission mode. The device configurations are given below. 

DFU start command = STARTDFU
Reset command = STARTRST

SPI Config: 
CS Pin = 4
MISO Pin = 3
MOSI Pin = 29
SCK Pin = 4
Mode = 0
Other config = default
RX buffer = 244 bytes
TX buffer = 244 bytes
SPIS_INSTANCE 1

BLE Config:
TX buffer = 244 bytes
RX buffer = 244 bytes
Advertising interval = 3200 (2000 ms)
Connection interval = 127.5 ms
Advertising duration = unlimited
hvn_tx_queue_size = 20
DCDC regulator = off
Connection Event Length Extension = on

UUID = 6E400000-B5A3-F393-E0A9-E50E24DCCA9E (ble_nus.c)

NRF_SDH_BLE_GAP_EVENT_LENGTH 6 (sdk_config.h)
NRF_SDH_BLE_GATT_MAX_MTU_SIZE 247 (sdk_config.h)

RAM_START=0x20004650

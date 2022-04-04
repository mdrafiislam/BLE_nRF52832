set appHex=ble_app_spis_pca10040_s132
set blHex=secure_bootloader_ble_s132_pca10040
set sdHex=s132_nrf52_7.2.0_softdevice
set blSetHex=bl_setting
set mergeHex=merge_ble_app_ble_app_spis_pca10040_s132

del "%appHex%.zip"
del "%mergeHex%.zip"

nrfutil settings generate --family NRF52 --application %appHex%.hex --application-version 0 --bootloader-version 0 --bl-settings-version 2 %blSetHex%.hex

mergehex -m SD_hex\%sdHex%.hex %appHex%.hex -o SD_APP.hex
mergehex -m SD_APP.hex BL_hex\%blHex%.hex -o SD_APP_BL.hex
mergehex -m SD_APP_BL.hex %blSetHex%.hex -o %mergeHex%.hex

nrfutil pkg generate --hw-version 52 --sd-req 0x0101 --application-version 1 --application %appHex%.hex --key-file Key\private.key %appHex%.zip

del SD_APP.hex 
del SD_APP_BL.hex
del %blSetHex%.hex


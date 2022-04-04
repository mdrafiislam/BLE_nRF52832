function [b,c,c1,d] = ble_connect()
blelist("Timeout",10)
b = ble("E88AFE919714"); %AIM21 Rafi
%  b = ble("E66725D11AF0"); %ES AIM
% b = ble("CFCAD3C2EBD4"); %ES current rig
% b = ble("E09ED95CC5D8"); %nRF52832 Current rig
% b = ble("C18195565356"); %nRF52840
% b = ble("AIM21"); %nRF52832 Current rig
% c = characteristic(b,"4AA10001-D92D-48D3-B522-509B8524AC31","4AA10003-D92D-48D3-B522-509B8524AC31"); %read characterstics
c = characteristic(b,"6E400001-B5A3-F393-E0A9-E50E24DCCA9E","6E400003-B5A3-F393-E0A9-E50E24DCCA9E"); %read characterstics
subscribe(c);
% c1 = characteristic(b,"4AA10001-D92D-48D3-B522-509B8524AC31","4AA10002-D92D-48D3-B522-509B8524AC31"); %write characterstics
c1 = characteristic(b,"6E400001-B5A3-F393-E0A9-E50E24DCCA9E","6E400002-B5A3-F393-E0A9-E50E24DCCA9E"); %write characterstics
d = descriptor(c,"Client Characteristic Configuration"); %descriptor
end

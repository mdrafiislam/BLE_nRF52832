% This Program will connect to a specific BLE device and store all the data in 'received_data' variable
% First run this program and then immediately start the BLE device. 
% This program will stop when BLE device stops sending data. To interrupt the program, press 'ctrl + c' in the command window
close all;%clear all; 

% Make BLE Connection
if ~exist('b')
    fprintf('Connecting...\n');
    [b,c,c1,d]=ble_connect_ES(); %b=ble object; c = read characterstic; c1 = %write characterstics; d = descriptor
    fprintf('Connection established\n');
elseif(b.Connected==0)
    fprintf('Reconnecting...\n');
    clear b;
    [b,c,c1,d]=ble_connect_ES();
    fprintf('Connection established\n');
end

% Store received data in the 'received_data' variable
received_data = [];
timestamp_all = [];
first_time_1 = 1;
first_time_2 = 1;
mismatch_count = 0;
row_count = 1;
tic
while(row_count<=200000)
%     packet = uint8(read(c,'oldest')); %read ble packet; packet will the uint8 type
%     packet = read(c,'oldest'); %uncomment this line to receive packet data as double type
    [packet,timestamp]=read(c,'oldest');
    timestamp_all = [timestamp_all;timestamp];
    received_data = [received_data; uint8(packet)]; %append received packets
    if packet(1)==1
        current_pac_no_1 =typecast(uint8([packet(3) packet(4)]),'uint16');
        if (first_time_1 == 1)
            previous_pac_no_1 = current_pac_no_1 - 1;
            first_time_1 = 0;
            fprintf('Receiving packet type 1\n');
        end
        if (current_pac_no_1 ~= previous_pac_no_1 + 1) && (current_pac_no_1 ~=0)
            mismatch_count = mismatch_count + double((current_pac_no_1 - previous_pac_no_1 + 1));
            fprintf('Total Missing packets: %.0f. ', mismatch_count);
            fprintf('Last Found in row: %.0f of received_data variable. Loss percentage: %.4f%%\n', row_count, (mismatch_count*100)/(row_count+mismatch_count));
        end
        previous_pac_no_1 = current_pac_no_1;
    elseif packet(1) == 2
        current_pac_no_2 =typecast(uint8([packet(3) packet(4)]),'uint16');
        if (first_time_2 == 1)
            previous_pac_no_2 = current_pac_no_2 - 1;
            first_time_2 = 0;
            fprintf('Receiving packet type 2\n');
        end
        if (current_pac_no_2 ~= previous_pac_no_2 + 1) && (current_pac_no_2 ~=0)
            mismatch_count = mismatch_count + double((current_pac_no_2 - previous_pac_no_2 + 1));
            fprintf('Total Missing packets: %.0f. ', mismatch_count);
            fprintf('Last Found in row: %.0f of received_data variable. Loss percentage: %.4f%%\n', row_count, (mismatch_count*100)/(row_count+mismatch_count));
        end
        previous_pac_no_2 = current_pac_no_2;    
    end
    row_count = row_count + 1;
end
toc

%Uncommant the lies below to check sequential data
%     current_pac_no = typecast(uint8([packet(4) packet(3) packet(2) packet(1)]),'uint32');
%      if (first_time == 1)
%         previous_pac_no = current_pac_no - 1;
%         first_time = 0;
%     end
%     if current_pac_no ~= previous_pac_no + 1
%         mismatch_count = mismatch_count + 1;
%         fprintf('Total Mismatch: %.0f. ', mismatch_count);
%         fprintf('Last Found in row: %.0f of received_data variable\n', row_count);
%     end
%     previous_pac_no = current_pac_no;
%     row_count = row_count + 1;
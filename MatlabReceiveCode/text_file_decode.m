%This code will generate a text file with only the notification (received
%data) values. It will also check for received sequence loss.
%First create a folder named 'Mobile Log' in your workspace directory and save the receive data text file in that folder.
%Second, run this code and input the text file name without .txt

close all; clear; clc;

received_log_directory = strcat(pwd,'\Mobile Log\');
if ~exist(received_log_directory, 'dir')
    fprintf("No folder found named 'Mobile Log'. Plsease save the receive data text file in 'Mobile log' folder in your workspace directory\n");
    mkdir(received_log_directory);
end
txt_file_name_input = input('Enter Text File Name(without ".txt"): ','s');
txt_file_name = strcat(received_log_directory,txt_file_name_input,'.txt');
text_file = fopen(txt_file_name);
data_val_u8 = [];
data_val_u16 = [];
kk = 1;
line_ex = 10; %arbitary value in line_ex to make it ~= -1 so that while loop runs (below)
value_from_data_line = [];

txt_file_saved = strcat(received_log_directory,txt_file_name_input,'_temp.txt');
fileID = fopen(txt_file_saved,'w');
ii=1;
total_similar=[];
similar=1;
first_time=1;
while (line_ex ~=-1)
% while (ii<2000)
    line_ex = fgetl(text_file);
    line_len = length(line_ex);
    if ~isempty(line_ex) && (line_len>16) && (line_ex(1) == 'V') && (line_ex(16) == 'D') && (line_ex(29) == '.') %stop at the word 'Disconnecting...'
        break;
    end
    if ~isempty(line_ex) && (line_len>16) && (line_ex(1) == 'I') && (line_ex(16) == 'N')  % Line containing the data contains more than 91 characters
        %if (line_ex(88:91) == '(0x)') %value in the text file always starts after (0x) 
            fprintf(fileID,line_ex(1:end)); %value in the text file always starts at 93
            fprintf(fileID,'\n');
        %end
        if first_time==1
            first_test_char = line_ex(9:10);
            first_time=0;
        else
            second_test_char = line_ex(9:10);
            if first_test_char == second_test_char
                similar = similar + 1;
            else
                fprintf('total packets received in %sth second = %0.0f\n', first_test_char, similar);
                first_test_char = line_ex(9:10);
                total_similar = [total_similar, similar];
                similar = 1;
            end
        end
    end
    ii=ii+1;
end
fprintf('\nPackets received per second on average = %0.0f\n', mean(total_similar(1:end-1)));
fprintf('Converting received data from text to uint8...\n');
fclose('all');
save('inputfilename.mat','txt_file_name_input')

clear;
% received_log_directory = 'C:\Users\MD RAFI ISLAM\Documents\Drive\PhD\BLE\nRF\Mobile Log\';
% txt_file_name_input = input('Text File Name: ','s');
% txt_file_name = strcat(received_log_directory,txt_file_name_input,'_temp.txt');
% text_file = fopen(txt_file_name);
load inputfilename.mat
text_file = fopen(strcat(pwd,'\Mobile Log\',txt_file_name_input,'_temp.txt'));
first_sensor = 1; 
first_image = 1;
line_ex = 10;
line_no = 1;
count = 0;
loss = 0;
received_data=[];
while (line_ex ~=-1)
    line_ex = fgetl(text_file);
    %clock_time = strsplit(line_ex,':');
    if (line_ex ==-1)
        fprintf('\nTotal loss: %0.0f out of %0.0f packets i.e. %0.3f%%. \nReceived data can be found in "received_data" matrix\n', double(count), line_no, loss);
        break;
    end
    data_line = strsplit(line_ex,'-');
    packet_no_str = strsplit(data_line{5},') ');
    packet_no = uint8(hex2dec(packet_no_str{2}));
%     val(4) = uint8(0);
%     val(3) = uint8(hex2dec(data_line{6}));
    val(1) = uint8(hex2dec(data_line{7}));
    val(2) = uint8(hex2dec(data_line{8}));
    current_pac_no = typecast(uint8(val),'uint16');
    received_data_temp = hex2dec(packet_no_str{2});
    for jj = 6:length(data_line)
        received_data_temp(jj-4) = hex2dec(data_line{jj});
    end
    received_data = [received_data; uint8(received_data_temp)]; 
    if packet_no == 1
        if (first_sensor == 1)
            previous_pac_no_sensor = current_pac_no - 1;
            previous_line_ex = line_ex;
            first_sensor = 0;
        end
        if (current_pac_no ~= previous_pac_no_sensor + 1) && (current_pac_no ~=0) %&& (current_pac_no ==23)
            fprintf('Mismatch Found\n');
            previous_line_ex
            line_ex
            count = count + current_pac_no - previous_pac_no_sensor + 1;
            loss = double((count*100))/line_no;
        end
         previous_pac_no_sensor = current_pac_no;
         previous_line_ex = line_ex;
         line_no = line_no + 1;
    end
    if packet_no == 2
        if (first_image == 1)
            previous_pac_no_image = current_pac_no - 1;
            previous_line_ex = line_ex;
            first_image = 0;
        end
        if (current_pac_no ~= previous_pac_no_image + 1) && (current_pac_no ~=0) %&& (current_pac_no ==23)
            fprintf('Mismatch Found\n');
            previous_line_ex
            line_ex
            count = count + current_pac_no - previous_pac_no_image + 1;
            loss = double((count*100))/line_no;
        end
         previous_pac_no_image = current_pac_no;
         previous_line_ex = line_ex;
         line_no = line_no + 1;
    end
end

fclose('all');
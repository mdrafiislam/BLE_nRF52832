% /2 bytes * 4 sensors * 29 readings = 232 bytes
% //12 bytes header, 244 total
% struct BLE_Sensor_Packet_Structure{
%                            uint16_t Packet_Type;     //2 bytes
%                            uint16_t PacketNumber;    //4 bytes //Starts at 0 for each buffer being sent
%                            uint32_t BufferID;        //8 bytes (POSIX timestamp)
%                            uint16_t Value1;          //10 bytes //One of the infrequent sensor readings, decoded by packet type
%                            uint16_t Value2;          //12 bytes //One of the infrequent sensor readings, decoded by packet type
%                            uint16_t Chew_Sensor_value[BLE_LengthOfSensorData];
%                            int16_t X[BLE_LengthOfSensorData];
%                            int16_t Y[BLE_LengthOfSensorData];
%                            int16_t Z[BLE_LengthOfSensorData]; // 244 bytes
%                };
% 
% //232 bytes of image data
% //12 bytes header
% struct BLE_Image_Packet_Structure{
%                            uint16_t Packet_Type;     //2 byte
%                            uint16_t PacketNumber;    //4 bytes
%                            uint32_t FileID;          //8 bytes (POSIX timestamp)
%                            uint32_t FileSize;        //12 bytes
%                            uint8_t ImageData[LengthOfImageData]; //244 bytes
% };


function display_data_with_fill(data_packet)

    global sensor_optical;
    global sensor_ACCX;
    global sensor_ACCY;
    global sensor_ACCZ;
    global image_Name;
    global image_Blob;
    global image_previous_packet_number;
    global sensor_previous_packet_number;
    global missing_packet_counter;
    global display_name;

%Packet number
packet_number=typecast(uint8([data_packet(3) data_packet(4)]),'uint16');

%Recover timestamp
packet_timestamp=typecast(uint8([data_packet(5) data_packet(6) data_packet(7) data_packet(8)]),'uint32');

%Sensor data
%Even packet - distance
%Odd packet - voltage
if(data_packet(1)==1)
    sensor_value1=typecast(uint8([data_packet(9) data_packet(10)]),'uint16');
    sensor_value2=typecast(uint8([data_packet(11) data_packet(12)]),'uint16');
    
    if (packet_number ~= sensor_previous_packet_number+1) && (packet_number~=0)
        disp(['Break in sensor packet sequence: ' num2str(sensor_previous_packet_number) ' ' num2str(packet_number) ]);
                       
        missing_packet_counter=missing_packet_counter+1;
    end
        sensor_previous_packet_number=packet_number;
    
    sensor_optical=[sensor_optical typecast(data_packet(13:70),'uint16')];
    sensor_ACCX=[sensor_ACCX typecast(data_packet(71:128),'uint16')];
    sensor_ACCY=[sensor_ACCY typecast(data_packet(129:186),'uint16')];
    sensor_ACCZ=[sensor_ACCZ typecast(data_packet(187:244),'uint16')];

    
end

%Image data
if(data_packet(1)==2)
    if packet_number==0 
        
        %save previous image
        fileID = fopen(image_Name,'w');
        fwrite(fileID,image_Blob);
        fclose(fileID);
        display_name=image_Name;
        
        image_Name=sprintf('%8X.jpg',packet_timestamp);
        image_Blob=data_packet(13:244);
    else
        if packet_number ~= image_previous_packet_number+1  && packet_number ~=0
            disp(['Break in image packet sequence: ' num2str(image_previous_packet_number) ' ' num2str(packet_number) ]);
            missing_packet_counter=missing_packet_counter+1;
           %Fill missing data
            number_of_missing_packets=packet_number-image_previous_packet_number-1;
            for i=1:number_of_missing_packets
                %image_Blob=[image_Blob 255*ones(1, 232)];
                %image_Blob=[image_Blob zeros(1, 232)];
                %image_Blob=[image_Blob data_packet(13:244)];
            end
        end       
        image_Blob=[image_Blob data_packet(13:244)];
    end
    
    image_previous_packet_number=packet_number;
end

return
clear all;
close all;
clc

warning('off','all');

load matlab.mat
% load sample1.mat
%load received_data_64ms_20_110_v2.mat

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
    
    image_previous_packet_number=-1;
    sensor_previous_packet_number=-1;
    missing_packet_counter=0;
    
    sensor_optical=[];
    sensor_ACCX=[];
    sensor_ACCY=[];
    sensor_ACCZ=[];
    image_Name='firstImage';
    image_Blob=[];

    
    
   figure;
   hold on;
   
for i=1:length(received_data)
    display_data(received_data(i,:));
   %display_data_with_fill(received_data(i,:));
   
   if(rem(i,50)==1)
        var_length=length(sensor_optical);
            if(var_length>1000)
            subplot(4,2,1);
            plot(sensor_optical(var_length-1000:var_length));
            subplot(4,2,3);
            plot(sensor_ACCX(var_length-1000:var_length));
            subplot (4,2,5);
            plot(sensor_ACCY(var_length-1000:var_length));
            subplot(4,2,7);
            plot(sensor_ACCZ(var_length-1000:var_length));
            subplot(4,2,[2 4 6 8]);
            if(~strcmp(display_name,'firstImage'))
                imshow(display_name);
            else
                imshow('corn.tif');
            end
            drawnow
            end
   end

end
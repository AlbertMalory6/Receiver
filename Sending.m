clear all;

t = 0:1/44100:1; % a temp time for 1 second
f_p = [linspace(10*10^3-8*10^3,10*10^3,220),linspace(10*10^3,10*10^3-8*10^3,220)]; 
omega = 2*pi.*cumtrapz(t(1:440),f_p);
preamble = sin(omega);

%% Transmitter

output_track = [];
demodulated_info = [];

filename = 'INPUT.txt';  % 替换为你的txt文件路径
fileID = fopen(filename, 'r');
data_str = fscanf(fileID, '%s');  % 关键：用%s读取整个字符串
fclose(fileID);

% 检查文件是否为空
if isempty(data_str)
    error('文件内容为空');
end

% 将字符串拆分为单个字符，再转换为数字数组
data = zeros(1, length(data_str));
for i = 1:length(data_str)
    % 逐个字符转换为数字
    data(i) = str2double(data_str(i));
    % 检查是否为有效0/1
    if isnan(data(i)) || data(i) < 0 || data(i) > 1 || data(i) ~= round(data(i))
        error(['文件中包含无效字符: ', data_str(i)]);
    end
end

% 确保数据不超过10000个
max_len = 100 * 100;
if length(data) > max_len
    warning('数据超过10000个，将截断为前10000个');
    data = data(1:max_len);
end

% 初始化frames矩阵
frames = zeros(100, 108);
frame_data = zeros(100,100);
frame_id = zeros(100,8);

% 填充数据到frames（逐帧填充）
data_len = length(data);
num_frames = min(ceil(data_len / 100), 100);  % 计算需要的帧数（最多100帧）
for i = 1:num_frames
    start_idx = (i-1)*100 + 1;
    end_idx = min(i*100, data_len);
    frame_data(i, 1:(end_idx - start_idx + 1)) = data(start_idx:end_idx);
end

% set first 8 bits to id

for i = 1:100
    tempstr = dec2bin(i,8);
    for j = 1:8
        frame_id(i,j) = int32(str2double(tempstr(j)));
    end
end

frames = [frame_id,frame_data];

fc = 10*10^3; %carrier frequency 10kHz
carrier = sin(2*pi*fc*t); %about 1 sencond
crc8 = crc.generator([1,1,0,1,0,0,1,1,1]); %x^8+x^7+x^5+x^2+x+1 

% preamble 440 samples
f_p = [linspace(10*10^3-8*10^3,10*10^3,220),linspace(10*10^3,10*10^3-8*10^3,220)]; 
omega = 2*pi.*cumtrapz(t(1:440),f_p);
preamble = sin(omega);

% figure;
% plot(preamble);


for i = 1:100
    frame = frames(i,:);
    % add crc
    frame_crc = generate(crc8,frame')'; %108 bit
%     frame_crc_check = generate(crc8, frame_crc(1:100)')';  % crc check
%     if sum(frame_crc_check ~= frame_crc) ~= 0
%        disp('error');
%     end

    %modulation
    frame_wave = zeros(1,length(frame_crc)*44);
    for j = 0:length(frame_crc)-1
        frame_wave(1+j*44:44+j*44) = carrier(1+j*44:44+j*44)*(frame_crc(j+1)*2-1); % baudrate 44/44100 about 1000bps
    end
%     figure;  % plot the frame
%     hold on;
%     plot(frame_wave);
%     plot((1:length(frame_crc))*44-22, frame_crc,'r*');
%     hold off;

    %add preabmle
	frame_wave_pre = [preamble, frame_wave];
    
    
    %append to output_track
    output_track = [output_track,zeros(1,int32(rand()*100))]; % add some random inter frame space 
    output_track = [output_track,frame_wave_pre];
    output_track = [output_track,zeros(1,int32(rand()*100))];
end 

sound(output_track,44100);
% 
% temp = xcorr(output_track,preamble);
% plot(temp);
% 
% figure;
% plot(output_track);


% %% Record
% Fs = 44100;          % 采样率 (Hz)
% bitsPerSample = 16;  % 采样位数
% 
% recorder = audiorecorder(Fs, bitsPerSample, 1);
% 
% disp('开始录制音频...');
% disp('按回车键结束录制');
% 
% record(recorder);
% 
% pause;
% 
% stop(recorder);
% disp('录制已结束');
% audioData = getaudiodata(recorder);
% filename = 'OUTPUT.wav';
% audiowrite(filename, audioData, Fs);
% 
% %% Receiver
% [soundTrack,fs] = audioread('OUTPUT.wav');
% RxFIFO = output_track;
% 
% RxFIFO = RxFIFO(:)'; 
% 
% f_pass_norm = [5000, 15000] / (fs/2);  
% b_bp = fir1(128, f_pass_norm, 'bandpass');  
% RxFIFO = filtfilt(b_bp, 1, RxFIFO);  
% 
% max_amp = max(abs(RxFIFO));
% if max_amp > eps 
%     gain = 0.8 / max_amp;
%     RxFIFO = RxFIFO * gain;  
% end
% 
% demodulated_info = [];
% 
% dc_offset = mean(RxFIFO);
% RxFIFO = RxFIFO - dc_offset;  
% 
% pre_emphasis_coeff = 0.95; 
% RxFIFO(2:end) = RxFIFO(2:end) - pre_emphasis_coeff * RxFIFO(1:end-1);
% 
% power = 0;
% start_index = 0;
% syncFIFO = zeros(1,440);
% syncPower_localMax = 0;
% 
% decodeFIFO = [];
% correctFrameNum = 0;
% state = 0;
% 
% for i = 1:length(RxFIFO)
%     current_sample = RxFIFO(i);
%     
%     power = power*(1-1/64) + current_sample^2/64;
%     
%     if state == 0
%         syncFIFO = [syncFIFO(2:end), current_sample];
%         syncPower = sum(syncFIFO.*preamble)/200;
% 
%         if (syncPower > power*2) && (syncPower > syncPower_localMax) && (syncPower > 0.05)
%             syncPower_localMax = syncPower;
%             start_index = i;
%         elseif (i - start_index > 200) && (start_index ~= 0)
%             syncPower_localMax = 0;
%             syncFIFO = zeros(1, length(syncFIFO));
%             state = 1;  
%             tempBuffer = RxFIFO(start_index+1:i);
%             decodeFIFO = tempBuffer;
%         end
%     elseif state == 1
%         decodeFIFO = [decodeFIFO, current_sample];
%         
%         if length(decodeFIFO) == 44*116
%             decodeFIFO_removecarrier = smooth(decodeFIFO.*carrier(1:length(decodeFIFO)),10);
%             
%             decodeFIFO_power_bit = zeros(1,116);
%             for j = 0:115
%                 decodeFIFO_power_bit(j+1) = sum(decodeFIFO_removecarrier(10+j*44:30+j*44));
%             end
%             decodeFIFO_power_bit = decodeFIFO_power_bit > 0;
%             
%             crc_check = generate(crc8, decodeFIFO_power_bit(1:108)')';
%             if sum(crc_check(109:end) ~= decodeFIFO_power_bit(109:end)) == 0
%                 frames_data = decodeFIFO_power_bit(9:108);
%                 demodulated_info = [demodulated_info, frames_data];
%                 correctFrameNum = correctFrameNum + 1; 
%             end
%             
%             start_index = 0;
%             decodeFIFO = [];
%             state = 0;
%         end 
%     end
% end
% 
% % 写入OUTPUT.txt并覆盖原有内容
% fileID = fopen('OUTPUT.txt', 'w');
% fprintf(fileID, '%d', demodulated_info);
% fclose(fileID);
% 
% fprintf('Total Correct: %d\n', correctFrameNum);









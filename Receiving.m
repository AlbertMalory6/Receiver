clear all;

t = 0:1/44100:1; % a temp time for 1 second
fc=10*10^3;
f_p = [linspace(10*10^3-8*10^3,10*10^3,220),linspace(10*10^3,10*10^3-8*10^3,220)]; 
omega = 2*pi.*cumtrapz(t(1:440),f_p);
preamble = sin(omega);
carrier = sin(2*pi*fc*t);
crc8 = comm.CRCGenerator([1,1,0,1,0,0,1,1,1]);

%% Record
Fs = 44100;          % 采样率 (Hz)
bitsPerSample = 16;  % 采样位数

% 创建音频输入对象
recorder = audiorecorder(Fs, bitsPerSample, 1);

% 显示提示信息
disp('开始录制音频...');
disp('按回车键结束录制');

% 开始录制
record(recorder);

% 等待用户按回车键
pause;

% 停止录制
stop(recorder);
disp('录制已结束');

% 从录制对象中获取音频数据
audioData = getaudiodata(recorder);

% 保存音频数据为WAV文件（保存在当前目录）
filename = 'OUTPUT.wav';
audiowrite(filename, audioData, Fs);

%% Receiver
[soundTrack,fs] = audioread('OUTPUT.wav');
RxFIFO = soundTrack;

RxFIFO = RxFIFO(:)'; 

f_pass_norm = [5000, 15000] / (fs/2);  
b_bp = fir1(128, f_pass_norm, 'bandpass');  
RxFIFO = filtfilt(b_bp, 1, RxFIFO);  

max_amp = max(abs(RxFIFO));
if max_amp > eps 
    gain = 0.8 / max_amp;
    RxFIFO = RxFIFO * gain;  
end

demodulated_info = [];

dc_offset = mean(RxFIFO);
RxFIFO = RxFIFO - dc_offset;  

pre_emphasis_coeff = 0.95; 
RxFIFO(2:end) = RxFIFO(2:end) - pre_emphasis_coeff * RxFIFO(1:end-1);

power = 0;
start_index = 0;
syncFIFO = zeros(1,440);
syncPower_localMax = 0;

decodeFIFO = [];
correctFrameNum = 0;
state = 0;
id = 0;

for i = 1:length(RxFIFO)
    current_sample = RxFIFO(i);
    
    power = power*(1-1/64) + current_sample^2/64;
    
    if state == 0
        syncFIFO = [syncFIFO(2:end), current_sample];
        syncPower = sum(syncFIFO.*preamble)/200;

        if (syncPower > power*2) && (syncPower > syncPower_localMax) 
            syncPower_localMax = syncPower;
            start_index = i;
        elseif (i - start_index > 200) && (start_index ~= 0)
            syncPower_localMax = 0;
            syncFIFO = zeros(1, length(syncFIFO));
            state = 1;  
            tempBuffer = RxFIFO(start_index+1:i);
            decodeFIFO = tempBuffer;
        end
    elseif state == 1
        decodeFIFO = [decodeFIFO, current_sample];
        
        if length(decodeFIFO) == 44*116
            decodeFIFO_removecarrier = smooth(decodeFIFO.*carrier(1:length(decodeFIFO)),10);
            
            decodeFIFO_power_bit = zeros(1,116);
            for j = 0:115
                decodeFIFO_power_bit(j+1) = sum(decodeFIFO_removecarrier(10+j*44:30+j*44));
            end
            decodeFIFO_power_bit = decodeFIFO_power_bit > 0;
            
            crc_check = crc8(decodeFIFO_power_bit(1:108)');
            crc_check = crc_check';  
            if sum(crc_check(109:end) ~= decodeFIFO_power_bit(109:end)) == 0
                frames_data = decodeFIFO_power_bit(9:108);
                id_bits = decodeFIFO_power_bit(1:8);
                id_decimal = 0;
                for k = 1:8
                    id_decimal = id_decimal + id_bits(k) * 2^(8 - k);
                end
                if id_decimal - id > 1
                    Zeros = zeros(1,100*(id_decimal - id - 1));
                    demodulated_info = [demodulated_info,Zeros];
                end
                id = id_decimal;

                demodulated_info = [demodulated_info, frames_data];
                correctFrameNum = correctFrameNum + 1; 
            end
            
            start_index = 0;
            decodeFIFO = [];
            state = 0;
        end 
    end
end

% 写入OUTPUT.txt并覆盖原有内容
fileID = fopen('OUTPUT.txt', 'w');
fprintf(fileID, '%d', demodulated_info);
fclose(fileID);

fprintf('Total Correct: %d\n', correctFrameNum);





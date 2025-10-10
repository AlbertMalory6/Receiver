/*
  ==============================================================================
    Integrated Sender/Receiver with FSK Modulation, Chirp Preamble, and CRC
    For CS120 Project 1 Testing
  ==============================================================================
*/

#include <JuceHeader.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>

#define PI 3.14159265358979323846

using namespace juce;

// ============================================================================
// CRC-8 Implementation
// ============================================================================
class CRC8 {
public:
    CRC8() : polynomial(0x1D1) {} // x^8 + x^7 + x^5 + x^2 + x + 1
    
    std::vector<int> generate(const std::vector<int>& data) {
        std::vector<int> result = data;
        for (int i = 0; i < 8; i++) {
            result.push_back(0);
        }
        
        for (size_t i = 0; i < data.size(); i++) {
            if (result[i] == 1) {
                for (int j = 0; j < 9; j++) {
                    int bit = (polynomial >> (8 - j)) & 1;
                    result[i + j] ^= bit;
                }
            }
        }
        
        return result;
    }
    
    bool check(const std::vector<int>& dataWithCRC) {
        if (dataWithCRC.size() < 8) return false;
        
        std::vector<int> data(dataWithCRC.begin(), dataWithCRC.end() - 8);
        std::vector<int> computed = generate(data);
        
        for (size_t i = data.size(); i < computed.size(); i++) {
            if (computed[i] != dataWithCRC[i]) {
                return false;
            }
        }
        return true;
    }
    
private:
    int polynomial;
};

// ============================================================================
// Integrated Audio System - Sender + Receiver
// ============================================================================
class IntegratedTransceiver : public AudioIODeviceCallback, 
                              private HighResolutionTimer 
{
public:
    IntegratedTransceiver() {
        // System parameters
        sampleRate = 48000;
        
        // FSK parameters (using two frequencies like the reference)
        carrierFreqA = 5000;  // Frequency for bit 0
        carrierFreqB = 10000; // Frequency for bit 1
        samplesPerBit = 48;   // 48 samples per bit = 1000 bps at 48kHz
        
        // Frame parameters
        bitsPerFrame = 1000;   // Data bits per frame
        headerLength = 480;   // Chirp preamble length
        
        // State
        isPlaying = false;
        isRecording = false;
        playingSampleNum = 0;
        recordedSampleNum = 0;
        
        // Statistics
        correctFrameCount = 0;
        errorFrameCount = 0;
        totalFramesDetected = 0;
        
        // Generate carrier waves and chirp
        generateCarrierWaves();
        generateChirpPreamble();
        
        std::cout << "\n=== Integrated Transceiver Initialized ===" << std::endl;
        std::cout << "Sample Rate: " << sampleRate << " Hz" << std::endl;
        std::cout << "Carrier A: " << carrierFreqA << " Hz (bit 0)" << std::endl;
        std::cout << "Carrier B: " << carrierFreqB << " Hz (bit 1)" << std::endl;
        std::cout << "Samples per bit: " << samplesPerBit << std::endl;
        std::cout << "Bit rate: " << (sampleRate / samplesPerBit) << " bps" << std::endl;
        std::cout << "Chirp length: " << headerLength << " samples" << std::endl;
    }
    
    ~IntegratedTransceiver() {
        stopTimer();
    }
    
    // ========================================================================
    // Chirp Preamble Generation (following SamplePHY.m)
    // ========================================================================
    void generateChirpPreamble() {
        int startFreq = 2000;
        int endFreq = 10000;
        float freqStep = (float)(endFreq - startFreq) / (headerLength / 2);
        float timeGap = 1.0f / sampleRate;
        
        std::vector<float> fp(headerLength);
        std::vector<float> omega(headerLength);
        
        // Generate frequency sweep: up then down
        fp[0] = startFreq;
        fp[headerLength / 2] = endFreq;
        
        for (int i = 1; i < headerLength / 2; i++) {
            fp[i] = fp[i - 1] + freqStep;
        }
        for (int i = headerLength / 2 + 1; i < headerLength; i++) {
            fp[i] = fp[i - 1] - freqStep;
        }
        
        // Integrate to get phase
        omega[0] = 0;
        for (int i = 1; i < headerLength; i++) {
            omega[i] = omega[i - 1] + (fp[i] + fp[i - 1]) / 2.0f * timeGap;
        }
        
        // Generate chirp signal
        chirpPreamble.clear();
        for (int i = 0; i < headerLength; i++) {
            chirpPreamble.add(std::sin(2 * PI * omega[i]));
        }
        
        std::cout << "Chirp preamble generated: " << chirpPreamble.size() << " samples" << std::endl;
    }
    
    // ========================================================================
    // FSK Carrier Wave Generation
    // ========================================================================
    void generateCarrierWaves() {
        carrierWaveA.clear();
        carrierWaveB.clear();
        
        for (int i = 0; i < samplesPerBit; i++) {
            float t = (float)i / sampleRate;
            carrierWaveA.add(std::sin(2 * PI * carrierFreqA * t));
            carrierWaveB.add(std::sin(2 * PI * carrierFreqB * t));
        }
    }
    
    // ========================================================================
    // Test Data Generation
    // ========================================================================
    void generateTestData() {
        testData.clear();
        Random rng(Time::currentTimeMillis());
        
        // Generate random test bits
        for (int i = 0; i < bitsPerFrame; i++) {
            testData.push_back(rng.nextInt(2));
        }
        
        std::cout << "\nTest data generated: " << testData.size() << " bits" << std::endl;
        std::cout << "First 20 bits: ";
        for (int i = 0; i < std::min(20, (int)testData.size()); i++) {
            std::cout << testData[i];
        }
        std::cout << std::endl;
    }
    
    // ========================================================================
    // FSK Modulation
    // ========================================================================
    void modulateFrame(const std::vector<int>& frameBits, AudioBuffer<float>& buffer, int startPos) {
        for (size_t i = 0; i < frameBits.size(); i++) {
            const Array<float>& carrier = (frameBits[i] == 0) ? carrierWaveA : carrierWaveB;
            
            for (int j = 0; j < samplesPerBit; j++) {
                int bufPos = startPos + i * samplesPerBit + j;
                if (bufPos < buffer.getNumSamples()) {
                    buffer.setSample(0, bufPos, carrier[j]);
                }
            }
        }
    }
    
    // ========================================================================
    // Frame Preparation with CRC
    // ========================================================================
    void prepareTransmission() {
        const ScopedLock sl(lock);
        
        std::cout << "\n=== Preparing Transmission ===" << std::endl;
        
        // Generate test data
        generateTestData();
        
        // Add CRC
        CRC8 crc;
        std::vector<int> frameWithCRC = crc.generate(testData);
        
        std::cout << "Frame with CRC: " << frameWithCRC.size() << " bits" << std::endl;
        std::cout << "CRC bits: ";
        for (size_t i = testData.size(); i < frameWithCRC.size(); i++) {
            std::cout << frameWithCRC[i];
        }
        std::cout << std::endl;
        
        // Calculate buffer size
        int frameDataLength = frameWithCRC.size() * samplesPerBit;
        int totalLength = headerLength + frameDataLength + 1000; // Extra space
        
        outputBuffer.setSize(1, totalLength);
        outputBuffer.clear();
        
        // Add chirp preamble
        for (int i = 0; i < headerLength; i++) {
            outputBuffer.setSample(0, i, chirpPreamble[i]);
        }
        
        // Add modulated data
        modulateFrame(frameWithCRC, outputBuffer, headerLength);
        
        std::cout << "Output buffer prepared: " << outputBuffer.getNumSamples() << " samples" << std::endl;
        std::cout << "Expected duration: " << (float)totalLength / sampleRate << " seconds" << std::endl;
    }
    
    // ========================================================================
    // Start Transmission
    // ========================================================================
    void startTransmission() {
        prepareTransmission();
        
        const ScopedLock sl(lock);
        playingSampleNum = 0;
        isPlaying = true;
        startTimer(100);
        
        std::cout << "\n*** TRANSMISSION STARTED ***\n" << std::endl;
    }
    
    // ========================================================================
    // Start Recording
    // ========================================================================
    void startRecording() {
        const ScopedLock sl(lock);
        
        recordedSound.setSize(1, 15 * sampleRate); // 15 seconds max
        recordedSound.clear();
        recordedSampleNum = 0;
        isRecording = true;
        
        // Reset sync state
        syncFIFO.clear();
        syncPower_localMax = 0;
        state = 0;
        power = 0;
        decodeFIFO.clear();
        
        std::cout << "\n*** RECORDING STARTED ***\n" << std::endl;
    }
    
    // ========================================================================
    // Stop Recording and Process
    // ========================================================================
    void stopRecording() {
        const ScopedLock sl(lock);
        
        if (!isRecording) return;
        
        isRecording = false;
        
        std::cout << "\n*** RECORDING STOPPED ***" << std::endl;
        std::cout << "Recorded samples: " << recordedSampleNum << std::endl;
        std::cout << "Duration: " << (float)recordedSampleNum / sampleRate << " seconds" << std::endl;
        
        // Process the recorded audio
        processRecordedAudio();
    }
    
    // ========================================================================
    // Audio Callback
    // ========================================================================
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
        int numInputChannels,
        float* const* outputChannelData,
        int numOutputChannels,
        int numSamples,
        const AudioIODeviceCallbackContext& context) override
    {
        const ScopedLock sl(lock);
        
        // Handle output (playback)
        for (int ch = 0; ch < numOutputChannels; ++ch) {
            if (outputChannelData[ch] != nullptr) {
                for (int i = 0; i < numSamples; ++i) {
                    if (isPlaying && playingSampleNum < outputBuffer.getNumSamples()) {
                        outputChannelData[ch][i] = outputBuffer.getSample(0, playingSampleNum);
                    } else {
                        outputChannelData[ch][i] = 0.0f;
                    }
                    if (ch == 0) playingSampleNum++;
                }
            }
        }
        
        // Handle input (recording)
        if (isRecording) {
            auto* recordBuffer = recordedSound.getWritePointer(0);
            
            for (int i = 0; i < numSamples; ++i) {
                if (recordedSampleNum < recordedSound.getNumSamples()) {
                    float sample = 0.0f;
                    
                    for (int ch = 0; ch < numInputChannels; ++ch) {
                        if (inputChannelData[ch] != nullptr) {
                            sample += inputChannelData[ch][i];
                        }
                    }
                    
                    if (numInputChannels > 0) {
                        sample /= numInputChannels;
                    }
                    
                    recordBuffer[recordedSampleNum++] = sample;
                }
            }
        }
    }
    
    void audioDeviceAboutToStart(AudioIODevice* device) override {
        sampleRate = device->getCurrentSampleRate();
        std::cout << "Audio device started at " << sampleRate << " Hz" << std::endl;
    }
    
    void audioDeviceStopped() override {
        std::cout << "Audio device stopped" << std::endl;
    }
    
    // ========================================================================
    // Timer Callback
    // ========================================================================
    void hiResTimerCallback() override {
        if (isPlaying && playingSampleNum >= outputBuffer.getNumSamples()) {
            isPlaying = false;
            stopTimer();
            std::cout << "\n*** TRANSMISSION COMPLETED ***\n" << std::endl;
        }
    }
    
    // ========================================================================
    // Signal Processing - Preamble Detection
    // ========================================================================
    void processRecordedAudio() {
        std::cout << "\n=== Processing Recorded Audio ===" << std::endl;
        
        const float* samples = recordedSound.getReadPointer(0);
        
        // Debug file
        std::ofstream debugFile("debug_processing.txt");
        std::ofstream nccFile("ncc_values.txt");
        
        state = 0; // 0 = sync, 1 = decode
        power = 0;
        syncFIFO.clear();
        decodeFIFO.clear();
        syncPower_localMax = 0;
        int startIndex = 0;
        
        std::vector<float> powerHistory;
        std::vector<float> nccHistory;
        
        for (int i = 0; i < recordedSampleNum; ++i) {
            float currentSample = samples[i];
            
            // Update power estimate
            power = power * (1 - 1.0f / 64.0f) + currentSample * currentSample / 64.0f;
            powerHistory.push_back(power);
            
            if (state == 0) {
                // Packet sync state
                syncFIFO.add(currentSample);
                
                if (syncFIFO.size() > headerLength) {
                    syncFIFO.remove(0);
                }
                
                if (syncFIFO.size() == headerLength) {
                    // Calculate normalized cross-correlation
                    float ncc = 0;
                    for (int j = 0; j < headerLength; j++) {
                        ncc += syncFIFO[j] * chirpPreamble[j];
                    }
                    ncc /= 200.0f; // Normalization factor
                    
                    nccHistory.push_back(ncc);
                    nccFile << i << " " << ncc << " " << power << "\n";
                    
                    // Detection criteria (following SamplePHY.m)
                    bool criteriaA = 1 ;//ncc > power * 2
                    bool criteriaB = ncc > syncPower_localMax;
                    bool criteriaC = ncc > 0.01;
                    
                    if (criteriaA && criteriaB && criteriaC) {
                        syncPower_localMax = ncc;
                        startIndex = i;
                        
                        debugFile << "Peak candidate at " << i << ", NCC=" << ncc 
                                 << ", power=" << power << "\n";
                    }
                    else if ((i - startIndex > 200) && (startIndex != 0)) {
                        // Peak detected!
                        std::cout << "\n*** PREAMBLE DETECTED ***" << std::endl;
                        std::cout << "Position: " << startIndex << " samples" << std::endl;
                        std::cout << "Time: " << (float)startIndex / sampleRate << " seconds" << std::endl;
                        std::cout << "Peak NCC value: " << syncPower_localMax << std::endl;
                        std::cout << "Power at detection: " << power << std::endl;
                        
                        totalFramesDetected++;
                        
                        // Collect samples after preamble
                        decodeFIFO.clear();
                        for (int j = startIndex + 1; j <= i && j < recordedSampleNum; j++) {
                            decodeFIFO.add(samples[j]);
                        }
                        
                        syncPower_localMax = 0;
                        syncFIFO.clear();
                        state = 1;
                    }
                }
            }
            else if (state == 1) {
                // Data collection state
                decodeFIFO.add(currentSample);
                
                int expectedSamples = (bitsPerFrame + 8) * samplesPerBit; // Data + CRC
                
                if (decodeFIFO.size() >= expectedSamples) {
                    std::cout << "\nCollected " << decodeFIFO.size() << " samples for decoding" << std::endl;
                    
                    // Decode the frame
                    decodeFrame(decodeFIFO, debugFile);
                    
                    // Reset for next frame
                    decodeFIFO.clear();
                    state = 0;
                    startIndex = 0;
                }
            }
        }
        
        debugFile.close();
        nccFile.close();
        
        // Final statistics
        std::cout << "\n=== Processing Complete ===" << std::endl;
        std::cout << "Frames detected: " << totalFramesDetected << std::endl;
        std::cout << "Frames decoded correctly: " << correctFrameCount << std::endl;
        std::cout << "Frames with errors: " << errorFrameCount << std::endl;
        
        if (totalFramesDetected == 0) {
            std::cout << "\n*** WARNING: No preamble detected! ***" << std::endl;
            std::cout << "Max NCC value encountered: " << syncPower_localMax << std::endl;
            std::cout << "Average power: " << std::accumulate(powerHistory.begin(), powerHistory.end(), 0.0f) / powerHistory.size() << std::endl;
            
            if (!nccHistory.empty()) {
                float maxNCC = *std::max_element(nccHistory.begin(), nccHistory.end());
                std::cout << "Maximum NCC in entire recording: " << maxNCC << std::endl;
                std::cout << "Threshold used: NCC > power*2 AND NCC > 0.05" << std::endl;
            }
        }
        
        std::cout << "\nDebug files written:" << std::endl;
        std::cout << "  - debug_processing.txt" << std::endl;
        std::cout << "  - ncc_values.txt" << std::endl;
    }
    
    // ========================================================================
    // FSK Demodulation and CRC Check
    // ========================================================================
    void decodeFrame(const Array<float>& frameData, std::ofstream& debugFile) {
        std::cout << "\n=== Decoding Frame ===" << std::endl;
        
        int totalBits = bitsPerFrame + 8; // Data + CRC
        std::vector<int> decodedBits;
        
        // Try different sync offsets
        std::vector<int> offsetsToTry = {0, -4, -2, -1, 1, 2, 4, 8, -8, 16, -16};
        
        for (int offset : offsetsToTry) {
            decodedBits.clear();
            bool validDecoding = true;
            
            std::cout << "\nTrying offset: " << offset << " samples" << std::endl;
            
            for (int i = 0; i < totalBits; i++) {
                int startPos = i * samplesPerBit + offset;
                
                if (startPos < 0 || startPos + samplesPerBit > frameData.size()) {
                    validDecoding = false;
                    break;
                }
                
                // Correlate with both carriers
                float corrA = 0, corrB = 0;
                
                for (int j = 0; j < samplesPerBit; j++) {
                    float sample = frameData[startPos + j];
                    corrA += sample * carrierWaveA[j];
                    corrB += sample * carrierWaveB[j];
                }
                
                // Decide bit based on which correlation is stronger
                int bit = (corrB > corrA) ? 1 : 0;
                decodedBits.push_back(bit);
                
                if (i < 10) {
                    debugFile << "Bit " << i << " @ offset " << offset 
                             << ": corrA=" << corrA << ", corrB=" << corrB 
                             << " -> " << bit << "\n";
                }
            }
            
            if (!validDecoding) continue;
            
            // Check CRC
            CRC8 crc;
            bool crcOk = crc.check(decodedBits);
            
            std::cout << "Offset " << offset << ": CRC " << (crcOk ? "PASS" : "FAIL") << std::endl;
            
            if (crcOk) {
                correctFrameCount++;
                
                std::cout << "\n*** FRAME DECODED SUCCESSFULLY ***" << std::endl;
                std::cout << "Sync offset used: " << offset << " samples" << std::endl;
                std::cout << "Data bits: " << bitsPerFrame << std::endl;
                std::cout << "First 20 bits: ";
                for (int i = 0; i < std::min(20, (int)decodedBits.size()); i++) {
                    std::cout << decodedBits[i];
                }
                std::cout << std::endl;
                
                // Compare with transmitted data
                int errors = 0;
                for (int i = 0; i < bitsPerFrame && i < (int)testData.size(); i++) {
                    if (decodedBits[i] != testData[i]) {
                        errors++;
                    }
                }
                
                std::cout << "Bit errors: " << errors << " / " << bitsPerFrame << std::endl;
                std::cout << "Bit error rate: " << (100.0f * errors / bitsPerFrame) << "%" << std::endl;
                
                return; // Success!
            }
        }
        
        // All offsets failed
        errorFrameCount++;
        
        std::cout << "\n*** FRAME DECODE FAILED ***" << std::endl;
        std::cout << "CRC check failed for all sync offsets" << std::endl;
        std::cout << "Possible issues:" << std::endl;
        std::cout << "  - Synchronization offset out of range" << std::endl;
        std::cout << "  - Signal distortion or noise" << std::endl;
        std::cout << "  - Incorrect demodulation" << std::endl;
        
        // Show what we got with offset 0
        std::cout << "\nDecoded bits (offset 0, first 20): ";
        for (int i = 0; i < std::min(20, (int)decodedBits.size()); i++) {
            std::cout << decodedBits[i];
        }
        std::cout << std::endl;
    }
    
    // Public members for external control
    bool isPlaying;
    bool isRecording;
    
private:
    // Audio parameters
    int sampleRate;
    int carrierFreqA, carrierFreqB;
    int samplesPerBit;
    int bitsPerFrame;
    int headerLength;
    
    // Carrier waves and preamble
    Array<float> carrierWaveA, carrierWaveB;
    Array<float> chirpPreamble;
    
    // Buffers
    AudioBuffer<float> outputBuffer;
    AudioBuffer<float> recordedSound;
    
    // State
    int playingSampleNum;
    int recordedSampleNum;
    int state; // 0 = sync, 1 = decode
    float power;
    float syncPower_localMax;
    
    // FIFOs
    Array<float> syncFIFO;
    Array<float> decodeFIFO;
    
    // Test data
    std::vector<int> testData;
    
    // Statistics
    int correctFrameCount;
    int errorFrameCount;
    int totalFramesDetected;
    
    // Thread safety
    CriticalSection lock;
};

// ============================================================================
// Main Application
// ============================================================================
int main(int argc, char* argv[])
{
    std::cout << "\n";
    std::cout << "========================================\n";
    std::cout << "  CS120 - Integrated Transceiver Test\n";
    std::cout << "  FSK Modulation + Chirp + CRC\n";
    std::cout << "========================================\n";
    std::cout << "\n";
    
    // Initialize JUCE
    MessageManager::getInstance();
    
    // Setup audio device
    AudioDeviceManager deviceManager;
    deviceManager.initialiseWithDefaultDevices(1, 1); // 1 input, 1 output
    
    AudioDeviceManager::AudioDeviceSetup setup;
    setup = deviceManager.getAudioDeviceSetup();
    setup.sampleRate = 48000;
    setup.bufferSize = 512;
    deviceManager.setAudioDeviceSetup(setup, true);
    
    std::cout << "Audio device initialized:" << std::endl;
    std::cout << "  Sample rate: " << setup.sampleRate << " Hz" << std::endl;
    std::cout << "  Buffer size: " << setup.bufferSize << " samples" << std::endl;
    
    if (auto* device = deviceManager.getCurrentAudioDevice()) {
        std::cout << "  Device: " << device->getName() << std::endl;
        std::cout << "  Input channels: " << device->getActiveInputChannels().countNumberOfSetBits() << std::endl;
        std::cout << "  Output channels: " << device->getActiveOutputChannels().countNumberOfSetBits() << std::endl;
    }
    
    // Create transceiver
    std::unique_ptr<IntegratedTransceiver> transceiver;
    transceiver.reset(new IntegratedTransceiver());
    
    deviceManager.addAudioCallback(transceiver.get());
    
    // Main menu
    bool running = true;
    while (running) {
        std::cout << "\n========================================\n";
        std::cout << "Options:\n";
        std::cout << "  1. Start Transmission (play audio)\n";
        std::cout << "  2. Start Recording (record audio)\n";
        std::cout << "  3. Stop Recording (process audio)\n";
        std::cout << "  4. Run Full Test (transmit + receive)\n";
        std::cout << "  5. Exit\n";
        std::cout << "========================================\n";
        std::cout << "Choose option: ";
        
        int choice;
        std::cin >> choice;
        std::cin.ignore();
        
        switch (choice) {
            case 1:
                transceiver->startTransmission();
                std::cout << "Press ENTER when transmission is complete..." << std::endl;
                std::cin.get();
                break;
                
            case 2:
                transceiver->startRecording();
                std::cout << "Recording... Press ENTER to stop." << std::endl;
                std::cin.get();
                break;
                
            case 3:
                transceiver->stopRecording();
                break;
                
            case 4:
                std::cout << "\n=== FULL TEST MODE ===" << std::endl;
                std::cout << "This will transmit and receive simultaneously." << std::endl;
                std::cout << "Make sure your speaker and microphone are enabled!" << std::endl;
                std::cout << "Press ENTER to start..." << std::endl;
                std::cin.get();
                
                transceiver->startRecording();
                Thread::sleep(100); // Small delay
                transceiver->startTransmission();
                
                std::cout << "Waiting for transmission to complete..." << std::endl;
                while (transceiver->isPlaying) {
                    Thread::sleep(100);
                }
                
                Thread::sleep(500); // Extra time to capture everything
                transceiver->stopRecording();
                break;
                
            case 5:
                running = false;
                break;
                
            default:
                std::cout << "Invalid option!" << std::endl;
        }
    }
    
    // Cleanup
    deviceManager.removeAudioCallback(transceiver.get());
    transceiver.reset();
    
    DeletedAtShutdown::deleteAll();
    MessageManager::deleteInstance();
    
    std::cout << "\nProgram terminated successfully." << std::endl;
    
    return 0;
}


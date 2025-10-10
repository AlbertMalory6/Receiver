// FIXES FOR RECEIVER - Apply these changes to Receiver/Source/Main.cpp

// =============================================================================
// FIX 1: Make receiver adapt to actual payload size (not hardcoded 5000)
// =============================================================================

// CHANGE in namespace FSK (around line 7-17):
namespace FSK {
    constexpr double sampleRate = 44100.0;
    constexpr double f0 = 2000.0;
    constexpr double f1 = 4000.0;
    constexpr double bitRate = 1000.0;
    constexpr int samplesPerBit = static_cast<int>(sampleRate / bitRate);
    constexpr int preambleSamples = 440;
    
    // REMOVE hardcoded payloadBits - calculate dynamically instead
    // constexpr int payloadBits = 5000;  // ← DELETE THIS
    
    constexpr int crcBits = 8;
    
    // Helper to calculate frame size from payload
    inline int totalFrameBits(int payloadBits) { return payloadBits + crcBits; }
    inline int totalFrameDataSamples(int payloadBits) { return totalFrameBits(payloadBits) * samplesPerBit; }

    uint8_t calculateCRC8(const std::vector<bool>& data) {
        const uint8_t polynomial = 0xD7;
        uint8_t crc = 0;
        for (bool bit : data) {
            crc ^= (bit ? 0x80 : 0x00);
            for (int i = 0; i < 8; ++i) {
                if (crc & 0x80) crc = (crc << 1) ^ polynomial;
                else crc <<= 1;
            }
        }
        return crc;
    }
}

// =============================================================================
// FIX 2: Update FSKOfflineProcessor to detect actual payload size
// =============================================================================

class FSKOfflineProcessor
{
public:
    FSKOfflineProcessor() {
        generatePreambleTemplate();
        preambleTemplateEnergy = calculateEnergy(preambleTemplate.getReadPointer(0), FSK::preambleSamples);
    }

    void analyzeRecording(const juce::AudioBuffer<float>& recordedAudio, int expectedPayloadBits)
    {
        std::cout << "\n--- Analyzing Recorded Audio & Detecting Frames ---" << std::endl;
        std::cout << "Expected payload size: " << expectedPayloadBits << " bits" << std::endl;
        
        const int totalFrameBits = FSK::totalFrameBits(expectedPayloadBits);
        const int totalFrameDataSamples = FSK::totalFrameDataSamples(expectedPayloadBits);
        
        std::cout << "Frame size: " << totalFrameBits << " bits (" << totalFrameDataSamples << " samples)" << std::endl;
        
        const float* signal = recordedAudio.getReadPointer(0);

        // --- NCC Detection with Peak Tracking ---
        juce::File nccFile = juce::File::getCurrentWorkingDirectory().getChildFile("debug_ncc_output.csv");
        std::ofstream nccStream(nccFile.getFullPathName().toStdString());

        juce::AudioBuffer<float> circularBuffer(1, FSK::preambleSamples * 2);
        int circularBufferPos = 0;
        
        // Synchronization state
        double syncPowerLocalMax = 0.0;
        int peakSampleIndex = 0;
        static constexpr double NCC_DETECTION_THRESHOLD = 0.40;  // Raised from 0.35
        int detectionCount = 0;
        
        std::cout << "Detection threshold: " << NCC_DETECTION_THRESHOLD << std::endl;
        std::cout << "Recording length: " << recordedAudio.getNumSamples() << " samples ("
                  << std::fixed << std::setprecision(2) 
                  << (double)recordedAudio.getNumSamples() / FSK::sampleRate << "s)" << std::endl;

        for (int i = 0; i < recordedAudio.getNumSamples(); ++i)
        {
            circularBuffer.setSample(0, circularBufferPos, signal[i]);
            double ncc = calculateNormalizedCrossCorrelation(circularBuffer, circularBufferPos);
            nccStream << i << "," << ncc << "\n";
            
            // --- Peak Detection Logic with LONGER hysteresis to prevent multiple detections ---
            if (ncc > syncPowerLocalMax && ncc > NCC_DETECTION_THRESHOLD) {
                syncPowerLocalMax = ncc;
                peakSampleIndex = i;
            }
            else if (peakSampleIndex != 0 && (i - peakSampleIndex) > FSK::preambleSamples) {  // Full preamble length
                // Peak confirmed - but only process if we haven't detected a frame recently
                if (detectionCount == 0 || (i - lastDetectionSample) > totalFrameDataSamples) {
                    
                    std::cout << "\n*** PREAMBLE #" << (detectionCount + 1) << " DETECTED at sample " << peakSampleIndex 
                              << " (t=" << std::fixed << std::setprecision(3) 
                              << (double)peakSampleIndex / FSK::sampleRate << "s)" << std::endl;
                    std::cout << "    Peak NCC: " << std::setprecision(4) << syncPowerLocalMax << std::endl;
                    
                    // Calculate frame start with offset correction
                    int frameStartSample = findOptimalFrameStart(recordedAudio, peakSampleIndex);
                    
                    // Check if we have enough samples
                    int samplesAvailable = recordedAudio.getNumSamples() - frameStartSample;
                    std::cout << "    Samples available: " << samplesAvailable << ", need: " << totalFrameDataSamples << std::endl;
                    
                    if (frameStartSample + totalFrameDataSamples <= recordedAudio.getNumSamples()) {
                        juce::AudioBuffer<float> frameData(1, totalFrameDataSamples);
                        frameData.copyFrom(0, 0, recordedAudio, 0, frameStartSample, totalFrameDataSamples);
                        demodulateFrame(frameData, recordedAudio, frameStartSample, expectedPayloadBits);
                        detectionCount++;
                        lastDetectionSample = i;
                    }
                    else {
                        std::cerr << "    ERROR: Not enough samples for full frame." << std::endl;
                        std::cerr << "    This likely means the recording stopped too early." << std::endl;
                        std::cerr << "    Recording should be at least " 
                                  << std::fixed << std::setprecision(2)
                                  << (double)(peakSampleIndex + totalFrameDataSamples) / FSK::sampleRate 
                                  << " seconds long." << std::endl;
                    }
                }
                
                // Reset for next peak search
                peakSampleIndex = 0;
                syncPowerLocalMax = 0.0;
            }
            
            circularBufferPos = (circularBufferPos + 1) % circularBuffer.getNumSamples();
        }
        
        nccStream.close();
        std::cout << "\nTotal frames detected: " << detectionCount << std::endl;
        std::cout << "Diagnostic NCC waveform saved to: " << nccFile.getFullPathName() << std::endl;

        // Save recording
        juce::File recordingFile = juce::File::getCurrentWorkingDirectory().getChildFile("debug_loopback_recording.wav");
        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::AudioFormatWriter> writer(wavFormat.createWriterFor(new juce::FileOutputStream(recordingFile), 44100.0, 1, 16, {}, 0));
        if (writer != nullptr) {
            writer->writeFromAudioSampleBuffer(recordedAudio, 0, recordedAudio.getNumSamples());
            std::cout << "Full loopback recording saved to: " << recordingFile.getFullPathName() << std::endl;
        }

        std::cout << "\n--- Analysis Complete ---" << std::endl;
    }

private:
    int lastDetectionSample = 0;  // Track last detection to prevent duplicates

    // ... (rest of methods remain the same but update demodulateFrame signature)
    
    void demodulateFrame(const juce::AudioBuffer<float>& frameData, 
                        const juce::AudioBuffer<float>& originalRecording, 
                        int frameStartSample,
                        int expectedPayloadBits)
    {
        const int totalFrameBits = FSK::totalFrameBits(expectedPayloadBits);
        
        std::vector<bool> receivedBits;
        receivedBits.reserve(totalFrameBits);
        const float* data = frameData.getReadPointer(0);

        std::cout << "\n--- Demodulating " << totalFrameBits << " bits ---" << std::endl;
        
        double avgConfidence = 0.0;
        int weakBits = 0;
        
        for (int i = 0; i < totalFrameBits; ++i) {
            const float* bitSamples = data + (i * FSK::samplesPerBit);
            double mag_f0 = goertzelMagnitude(FSK::samplesPerBit, FSK::f0, bitSamples);
            double mag_f1 = goertzelMagnitude(FSK::samplesPerBit, FSK::f1, bitSamples);
            bool bit = mag_f1 > mag_f0;
            receivedBits.push_back(bit);

            double confidence = (mag_f0 > mag_f1) ? (mag_f0 / mag_f1) : (mag_f1 / mag_f0);
            avgConfidence += confidence;
            if (confidence < 1.5) weakBits++;
            
            if (i < 30) {
                std::cout << "Bit " << std::setw(3) << i << ": " << (bit ? "1" : "0") 
                          << " (f0=" << std::setprecision(1) << mag_f0 
                          << ", f1=" << mag_f1 
                          << ", conf=" << std::setprecision(2) << confidence << ")" << std::endl;
            }
        }
        
        avgConfidence /= totalFrameBits;
        std::cout << "\n--- Demodulation Statistics ---" << std::endl;
        std::cout << "Average confidence: " << std::setprecision(2) << avgConfidence << std::endl;
        std::cout << "Weak bits (conf < 1.5): " << weakBits << " / " << totalFrameBits 
                  << " (" << std::setprecision(1) << (100.0 * weakBits / totalFrameBits) << "%)" << std::endl;

        // --- CRC Check ---
        std::vector<bool> payload(receivedBits.begin(), receivedBits.begin() + expectedPayloadBits);
        uint8_t receivedCrcByte = 0;
        for (int i = 0; i < FSK::crcBits; ++i)
            if (receivedBits[expectedPayloadBits + i])
                receivedCrcByte |= (1 << (7 - i));

        uint8_t calculatedCrc = FSK::calculateCRC8(payload);

        std::cout << "\n--- CRC Validation ---" << std::endl;
        std::cout << "Received CRC:   0x" << std::hex << std::setw(2) << std::setfill('0') << (int)receivedCrcByte << std::endl;
        std::cout << "Calculated CRC: 0x" << std::hex << std::setw(2) << std::setfill('0') << (int)calculatedCrc << std::dec << std::endl;

        if (calculatedCrc == receivedCrcByte) {
            std::cout << "✅ CRC OK! Writing " << payload.size() << " bits to OUTPUT.txt" << std::endl;
            std::ofstream outputFile("OUTPUT.txt");
            for (bool b : payload) outputFile << (b ? '1' : '0');
            outputFile.close();
        }
        else {
            std::cout << "❌ CRC FAIL! Frame discarded." << std::endl;
            std::cout << "   Bit errors detected - synchronization or noise issue" << std::endl;
        }

        // Generate debug beep track
        createBeepTrack(originalRecording, frameStartSample, totalFrameBits);
    }

    void createBeepTrack(const juce::AudioBuffer<float>& originalRecording, int frameStartSample, int totalBits)
    {
        juce::File beepFile = juce::File::getCurrentWorkingDirectory().getChildFile("debug_beeps.wav");
        juce::AudioBuffer<float> beepBuffer(originalRecording.getNumChannels(), originalRecording.getNumSamples());
        beepBuffer.copyFrom(0, 0, originalRecording, 0, 0, originalRecording.getNumSamples());

        for (int i = 0; i < totalBits; ++i) {
            int beepPos = frameStartSample + (i * FSK::samplesPerBit);
            if (beepPos + 5 < beepBuffer.getNumSamples()) {
                for (int j = 0; j < 5; ++j) {
                    beepBuffer.setSample(0, beepPos + j, 1.0f);
                }
            }
        }

        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::AudioFormatWriter> writer(wavFormat.createWriterFor(new juce::FileOutputStream(beepFile), 44100.0, 1, 16, {}, 0));
        if (writer != nullptr) {
            writer->writeFromAudioSampleBuffer(beepBuffer, 0, beepBuffer.getNumSamples());
            std::cout << "Diagnostic beep track saved to: " << beepFile.getFullPathName() << std::endl;
        }
    }

    // ... (other helper methods remain the same)
};

// =============================================================================
// FIX 3: Update main() to pass payload size
// =============================================================================

int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juce_init;

    std::vector<bool> bits_to_send;
    std::ifstream inputFile("INPUT.txt");
    if (!inputFile.is_open()) { 
        std::cerr << "Error: Could not open INPUT.txt." << std::endl; 
        return 1; 
    }
    
    char bitChar;
    while (inputFile.get(bitChar)) {
        if (bitChar == '0') bits_to_send.push_back(false);
        else if (bitChar == '1') bits_to_send.push_back(true);
    }
    inputFile.close();
    
    int payloadBits = bits_to_send.size();
    std::cout << "Read " << payloadBits << " bits from INPUT.txt" << std::endl;

    AcousticLoopbackTester tester(bits_to_send);
    juce::AudioDeviceManager deviceManager;
    deviceManager.initialiseWithDefaultDevices(1, 2);

    std::cout << "Acoustic loopback test is ready." << std::endl;
    std::cout << "Ensure your microphone can hear your speakers." << std::endl;
    std::cout << "Press ENTER to start the play/record process..." << std::endl;
    std::cin.get();

    deviceManager.addAudioCallback(&tester);
    std::cout << "--- Playing and recording simultaneously... ---" << std::endl;

    while (!tester.isTestFinished()) {
        juce::Thread::sleep(100);
    }

    deviceManager.removeAudioCallback(&tester);
    std::cout << "--- Play/Record finished. ---" << std::endl;

    FSKOfflineProcessor processor;
    processor.analyzeRecording(tester.getRecording(), payloadBits);  // ← Pass payload size!

    std::cout << "Press ENTER to exit." << std::endl;
    std::cin.get();
    return 0;
}


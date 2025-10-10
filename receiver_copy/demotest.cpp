#include <JuceHeader.h>
#include <fstream>
#include <iostream>
#include <iomanip>

/*
 * DEMODULATION TEST MODE
 * - Transmits 0.5s silence + preamble + FSK data
 * - Records everything
 * - Demodulates ENTIRE recording bit-by-bit
 * - Outputs all bits regardless of CRC
 * 
 * VERIFICATION METHODS:
 * 1. Check OUTPUT_ALL_BITS.txt contains all demodulated bits
 * 2. Compare timing: should see silence, then preamble, then data
 * 3. Check bit pattern consistency with INPUT.txt
 * 4. Verify samplesPerBit alignment is correct
 */

// --- SHARED PARAMETERS AND FUNCTIONS (must be identical for sender and receiver logic) ---
namespace FSK {
    constexpr double sampleRate = 44100.0;
    constexpr double f0 = 4000.0;
    constexpr double f1 = 8000.0;
    constexpr double bitRate = 1000.0;
    constexpr int samplesPerBit = static_cast<int>(sampleRate / bitRate);
    constexpr int preambleSamples = 440;
    constexpr int payloadBits = 2000;
    constexpr int crcBits = 8;
    constexpr int totalFrameBits = payloadBits + crcBits;
    constexpr int totalFrameDataSamples = totalFrameBits * samplesPerBit;

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

// ==============================================================================
//  1. FSK Signal Generator (Plays the audio)
// ==============================================================================
class FSKSignalSource : public juce::AudioSource
{
public:
    FSKSignalSource(const std::vector<bool>& bitsToSend) {
        generateFullSignal(bitsToSend);
    }

    bool isFinished() const { return isPlaybackFinished; }
    int getNumSamples() const { return signalBuffer.getNumSamples(); }

    void prepareToPlay(int, double) override {
        position = 0;
        isPlaybackFinished = false;
    }
    void releaseResources() override {}
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override
    {
        if (isPlaybackFinished) {
            bufferToFill.clearActiveBufferRegion();
            return;
        }
        auto remainingSamples = signalBuffer.getNumSamples() - position;
        auto samplesThisTime = juce::jmin(bufferToFill.numSamples, remainingSamples);

        if (samplesThisTime > 0) {
            for (int chan = 0; chan < bufferToFill.buffer->getNumChannels(); ++chan)
                bufferToFill.buffer->copyFrom(chan, bufferToFill.startSample, signalBuffer, 0, position, samplesThisTime);
            position += samplesThisTime;
        }
        if (samplesThisTime < bufferToFill.numSamples) {
            bufferToFill.buffer->clear(bufferToFill.startSample + samplesThisTime, bufferToFill.numSamples - samplesThisTime);
            isPlaybackFinished = true;
        }
    }
private:
    void FSKSignalSource::generateFullSignal(const std::vector<bool>& bits)
    {
        // TIMING: 0.5s silence before preamble allows receiver to stabilize
        const int silentLeaderSamples = FSK::sampleRate * 0.5; // 0.5 seconds of silence

        auto payloadWithCrcBits = bits;
        uint8_t crc = FSK::calculateCRC8(bits);
        for (int i = 7; i >= 0; --i) payloadWithCrcBits.push_back((crc >> i) & 1);

        const int totalDataSamples = payloadWithCrcBits.size() * FSK::samplesPerBit;
        const int totalSignalSamples = silentLeaderSamples + FSK::preambleSamples + totalDataSamples;

        signalBuffer.setSize(1, totalSignalSamples);
        signalBuffer.clear(); // Zeros out the buffer, creating the silent leader

        auto* signal = signalBuffer.getWritePointer(0);
        double currentPhase = 0.0;

        // Generate preamble AFTER the silent leader
        for (int i = 0; i < FSK::preambleSamples; ++i) {
            double freq;
            if (i < FSK::preambleSamples / 2)
                freq = juce::jmap((double)i, 0.0, (double)FSK::preambleSamples / 2.0, FSK::f0 - 1000.0, FSK::f1 + 1000.0);
            else
                freq = juce::jmap((double)i, (double)FSK::preambleSamples / 2.0, (double)FSK::preambleSamples, FSK::f1 + 1000.0, FSK::f0 - 1000.0);
            double phaseIncrement = 2.0 * juce::MathConstants<double>::pi * freq / FSK::sampleRate;
            currentPhase += phaseIncrement;
            signal[silentLeaderSamples + i] = std::sin(currentPhase) * 0.5;
        }

        // Generate FSK data after the preamble
        int sampleIndex = silentLeaderSamples + FSK::preambleSamples;
        for (bool bit : payloadWithCrcBits) {
            double freq = bit ? FSK::f1 : FSK::f0;
            double phaseIncrement = 2.0 * juce::MathConstants<double>::pi * freq / FSK::sampleRate;
            for (int i = 0; i < FSK::samplesPerBit; ++i) {
                signal[sampleIndex++] = std::sin(currentPhase);
                currentPhase += phaseIncrement;
            }
        }
        signalBuffer.applyGain(0.9f / signalBuffer.getMagnitude(0, signalBuffer.getNumSamples()));
    }

    juce::AudioBuffer<float> signalBuffer;
    int position = 0;
    bool isPlaybackFinished = true;
};

// ==============================================================================
//  2. Offline Analyzer (Demodulates the ENTIRE recording)
// ==============================================================================
class FSKOfflineProcessor
{
public:
    FSKOfflineProcessor() {}

    void analyzeRecording(const juce::AudioBuffer<float>& recordedAudio)
    {
        std::cout << "\n========================================" << std::endl;
        std::cout << "  FULL DEMODULATION MODE" << std::endl;
        std::cout << "========================================" << std::endl;
        
        const int totalSamples = recordedAudio.getNumSamples();
        const float durationSeconds = totalSamples / FSK::sampleRate;
        
        std::cout << "\nRecording Info:" << std::endl;
        std::cout << "  Total samples: " << totalSamples << std::endl;
        std::cout << "  Duration: " << std::setprecision(2) << durationSeconds << " seconds" << std::endl;
        std::cout << "  Sample rate: " << FSK::sampleRate << " Hz" << std::endl;
        
        // Calculate expected timing
        const int silenceSamples = FSK::sampleRate * 0.5;
        const int preambleSamples = FSK::preambleSamples;
        const int dataSamples = FSK::totalFrameDataSamples;
        
        const float silenceEnd = silenceSamples / FSK::sampleRate;
        const float preambleEnd = (silenceSamples + preambleSamples) / FSK::sampleRate;
        const float dataEnd = (silenceSamples + preambleSamples + dataSamples) / FSK::sampleRate;
        
        std::cout << "\nExpected Timing Markers:" << std::endl;
        std::cout << "  [0.000s - " << std::setprecision(3) << silenceEnd << "s]: SILENCE (" << silenceSamples << " samples)" << std::endl;
        std::cout << "  [" << silenceEnd << "s - " << preambleEnd << "s]: PREAMBLE (" << preambleSamples << " samples)" << std::endl;
        std::cout << "  [" << preambleEnd << "s - " << dataEnd << "s]: FSK DATA (" << dataSamples << " samples, " << FSK::totalFrameBits << " bits)" << std::endl;
        
        // Demodulate EVERYTHING as bits (treating entire recording as FSK data)
        std::cout << "\n========================================" << std::endl;
        std::cout << "  DEMODULATING ENTIRE RECORDING" << std::endl;
        std::cout << "========================================" << std::endl;
        
        demodulateEntireRecording(recordedAudio);
    }
private:

    // Demodulate entire recording sample by sample
    void demodulateEntireRecording(const juce::AudioBuffer<float>& recording)
    {
        const float* samples = recording.getReadPointer(0);
        const int totalSamples = recording.getNumSamples();
        const int maxBits = totalSamples / FSK::samplesPerBit;
        
        std::vector<bool> allBits;
        std::vector<double> confidences;
        
        std::cout << "\nDemodulating up to " << maxBits << " bits..." << std::endl;
        
        // Demodulate every samplesPerBit block as a bit
        for (int bitIdx = 0; bitIdx < maxBits; ++bitIdx) {
            int startSample = bitIdx * FSK::samplesPerBit;
            
            if (startSample + FSK::samplesPerBit > totalSamples) break;
            
            const float* bitSamples = samples + startSample;
            double mag_f0 = goertzelMagnitude(FSK::samplesPerBit, FSK::f0, bitSamples);
            double mag_f1 = goertzelMagnitude(FSK::samplesPerBit, FSK::f1, bitSamples);
            
            bool bit = mag_f1 > mag_f0;
            allBits.push_back(bit);

            double confidence = (mag_f0 > mag_f1) ? (mag_f0 / mag_f1) : (mag_f1 / mag_f0);
            confidences.push_back(confidence);
            
            // Show first 50 bits with details
            if (bitIdx < 50) {
                float timeStamp = startSample / FSK::sampleRate;
                std::cout << "Bit " << std::setw(4) << bitIdx 
                         << " @ " << std::setprecision(3) << std::setw(6) << timeStamp << "s: " 
                         << (bit ? "1" : "0")
                         << " (f0=" << std::setprecision(1) << std::setw(6) << mag_f0
                         << ", f1=" << std::setw(6) << mag_f1
                    << ", conf=" << std::setprecision(2) << confidence << ")" << std::endl;
            }
        }

        // Calculate statistics
        double avgConfidence = std::accumulate(confidences.begin(), confidences.end(), 0.0) / confidences.size();
        int weakBits = std::count_if(confidences.begin(), confidences.end(), [](double c) { return c < 1.5; });
        
        std::cout << "\n========================================" << std::endl;
        std::cout << "  DEMODULATION STATISTICS" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "Total bits demodulated: " << allBits.size() << std::endl;
        std::cout << "Average confidence: " << std::setprecision(2) << avgConfidence << std::endl;
        std::cout << "Weak bits (conf < 1.5): " << weakBits << " (" 
                 << std::setprecision(1) << (100.0 * weakBits / allBits.size()) << "%)" << std::endl;
        
        // Save ALL bits to file
        std::ofstream allBitsFile("OUTPUT_ALL_BITS.txt");
        for (bool bit : allBits) {
            allBitsFile << (bit ? '1' : '0');
        }
        allBitsFile.close();
        
        std::cout << "\n✓ All " << allBits.size() << " bits saved to OUTPUT_ALL_BITS.txt" << std::endl;
        
        // Also show bit pattern for the expected data region
        const int silenceSamples = FSK::sampleRate * 0.5;
        const int preambleSamples = FSK::preambleSamples;
        const int expectedDataStartBit = (silenceSamples + preambleSamples) / FSK::samplesPerBit;
        
        if (expectedDataStartBit < (int)allBits.size()) {
            std::cout << "\nExpected data region starts at bit " << expectedDataStartBit 
                     << " (" << std::setprecision(3) << (expectedDataStartBit * FSK::samplesPerBit / FSK::sampleRate) << "s)" << std::endl;
            std::cout << "First 100 bits from expected data start:" << std::endl;
            
            for (int i = 0; i < 100 && (expectedDataStartBit + i) < (int)allBits.size(); ++i) {
                std::cout << (allBits[expectedDataStartBit + i] ? '1' : '0');
                if ((i + 1) % 10 == 0) std::cout << " ";
                if ((i + 1) % 50 == 0) std::cout << std::endl;
            }
            std::cout << std::endl;
        }
    }
    
    // Goertzel algorithm for frequency detection
    double goertzelMagnitude(int numSamplesInBlock, int targetFrequency, const float* data) {
        double k = 0.5 + ((double)numSamplesInBlock * targetFrequency / FSK::sampleRate);
        double w = (2.0 * juce::MathConstants<double>::pi / numSamplesInBlock) * k;
        double cosine = std::cos(w);
        double coeff = 2.0 * cosine;
        double q0 = 0, q1 = 0, q2 = 0;
        for (int i = 0; i < numSamplesInBlock; i++) {
            q0 = coeff * q1 - q2 + data[i];
            q2 = q1;
            q1 = q0;
        }
        double real = q1 - q2 * cosine;
        double imag = q1 * std::sin(w);
        return std::sqrt(real * real + imag * imag);
    }
};

// ==============================================================================
//  3. The Loopback Test Manager (Handles simultaneous play/record)
// ==============================================================================
class AcousticLoopbackTester : public juce::AudioIODeviceCallback
{
public:
    AcousticLoopbackTester(const std::vector<bool>& bitsToSend) : fskSource(bitsToSend) {
        int requiredSamples = fskSource.getNumSamples() + (int)(FSK::sampleRate * 1.0);
        recordedAudio.setSize(1, requiredSamples);
        recordedAudio.clear();
    }
    bool isTestFinished() const { return testFinished; }
    juce::AudioBuffer<float>& getRecording() { return recordedAudio; }

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override {
        fskSource.prepareToPlay(device->getCurrentBufferSizeSamples(), device->getCurrentSampleRate());
    }
    void audioDeviceStopped() override {}
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
        float* const* outputChannelData, int numOutputChannels,
        int numSamples, const juce::AudioIODeviceCallbackContext&) override {
        if (numOutputChannels > 0) {
            //juce::AudioSourceChannelInfo bufferToFill(outputChannelData, numOutputChannels, numSamples);
            juce::AudioSourceChannelInfo bufferToFill;
            juce::AudioBuffer<float> tempBuffer(const_cast<float**>(outputChannelData), numOutputChannels, numSamples);
            bufferToFill.buffer = &tempBuffer;            bufferToFill.startSample = 0;
            bufferToFill.numSamples = numSamples;
            fskSource.getNextAudioBlock(bufferToFill);
        }
        if (numInputChannels > 0) {
            if (samplesRecorded + numSamples <= recordedAudio.getNumSamples()) {
                recordedAudio.copyFrom(0, samplesRecorded, inputChannelData[0], numSamples);
                samplesRecorded += numSamples;
            }
        }
        if (fskSource.isFinished() && !testFinished) {
            if (++postPlaybackSamples > FSK::sampleRate * 0.2) {
                testFinished = true;
            }
        }
    }
private:
    FSKSignalSource fskSource;
    juce::AudioBuffer<float> recordedAudio;
    int samplesRecorded = 0;
    int postPlaybackSamples = 0;
    std::atomic<bool> testFinished{ false };
};

// ==============================================================================
//  4. Main Application
// ==============================================================================
int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juce_init;

    std::cout << "\n========================================" << std::endl;
    std::cout << "  FSK DEMODULATION TEST" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // Load input bits
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
    
    std::cout << "\n✓ Loaded " << bits_to_send.size() << " bits from INPUT.txt" << std::endl;
    std::cout << "  First 20 bits: ";
    for (int i = 0; i < 20 && i < (int)bits_to_send.size(); ++i) {
        std::cout << (bits_to_send[i] ? '1' : '0');
    }
    std::cout << "..." << std::endl;
    
    // Setup audio
    AcousticLoopbackTester tester(bits_to_send);
    juce::AudioDeviceManager deviceManager;
    deviceManager.initialiseWithDefaultDevices(1, 2);
    
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device) {
        std::cout << "\n✓ Audio Device: " << device->getName() << std::endl;
        std::cout << "  Sample Rate: " << device->getCurrentSampleRate() << " Hz" << std::endl;
    }

    std::cout << "\n========================================" << std::endl;
    std::cout << "  READY TO TEST" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Ensure your microphone can hear your speakers." << std::endl;
    std::cout << "\nPress ENTER to start transmission and recording..." << std::endl;
    std::cin.get();

    deviceManager.addAudioCallback(&tester);
    std::cout << "\n>>> Transmitting and recording..." << std::endl;
    
    std::cin.get();

    deviceManager.removeAudioCallback(&tester);
    std::cout << ">>> Transmission complete." << std::endl;

    // Process the recording
    FSKOfflineProcessor processor;
    processor.analyzeRecording(tester.getRecording());

    std::cout << "\n========================================" << std::endl;
    std::cout << "  TEST COMPLETE" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "\nPress ENTER to exit." << std::endl;
    std::cin.get();
    return 0;
}
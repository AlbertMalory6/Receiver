#include <JuceHeader.h>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>

// ==============================================================================
//  CONFIGURATION & VERIFICATION GUIDE
// ==============================================================================

// --- MODIFICATION SWITCHES ---
// Set to 1 to enable Triple Redundancy encoding and Voting demodulation.
#define USE_TRIPLE_REDUNDANCY 1

namespace FSK {
    constexpr double sampleRate = 44100.0;
    constexpr double f0 = 2000.0;
    constexpr double f1 = 4000.0;

    // Base parameters
    constexpr double effectiveBitRate = 1000.0;
    constexpr int redundancyFactor = USE_TRIPLE_REDUNDANCY ? 3 : 1;

    // --- EFFECTIVE BITRATE IS 3X HIGHER TO KEEP AUDIO LENGTH THE SAME ---
    constexpr double bitRate = effectiveBitRate * redundancyFactor;
    constexpr int samplesPerBit = static_cast<int>(sampleRate / bitRate);

    constexpr int preambleSamples = 440;
    constexpr int payloadBits = 1200; // Original number of bits before encoding
    constexpr int crcBits = 8;

    // The number of bits in one frame *before* redundancy
    constexpr int totalBaseFrameBits = payloadBits + crcBits;
    // The total number of bits actually transmitted
    constexpr int totalTransmittedBits = totalBaseFrameBits * redundancyFactor;
    // The total number of audio samples for the data portion
    constexpr int totalFrameDataSamples = totalTransmittedBits * samplesPerBit;

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
//  CLASS 1: FSK Signal Generator
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
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override {
        if (isPlaybackFinished) { bufferToFill.clearActiveBufferRegion(); return; }
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
    void generateFullSignal(const std::vector<bool>& bits) { // Expects the already-encoded bits
        const int silentLeaderSamples = static_cast<int>(FSK::sampleRate * 0.5);
        const int totalDataSamples = static_cast<int>(bits.size()) * FSK::samplesPerBit;
        const int totalSignalSamples = silentLeaderSamples + FSK::preambleSamples + totalDataSamples;
        signalBuffer.setSize(1, totalSignalSamples);
        signalBuffer.clear();
        auto* signal = signalBuffer.getWritePointer(0);
        double currentPhase = 0.0;
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
        int sampleIndex = silentLeaderSamples + FSK::preambleSamples;
        for (bool bit : bits) {
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
//  CLASS 2: Matched Filter Demodulator with Voting Logic
// ==============================================================================
class MatchedFilterFSKDemodulator
{
private:
    juce::AudioBuffer<float> reference_f0, reference_f1;

    void generateReferences() {
        reference_f0.setSize(1, FSK::samplesPerBit);
        reference_f1.setSize(1, FSK::samplesPerBit);
        auto* ref0 = reference_f0.getWritePointer(0);
        auto* ref1 = reference_f1.getWritePointer(0);
        for (int i = 0; i < FSK::samplesPerBit; ++i) {
            double phase0 = 2.0 * juce::MathConstants<double>::pi * FSK::f0 * i / FSK::sampleRate;
            double phase1 = 2.0 * juce::MathConstants<double>::pi * FSK::f1 * i / FSK::sampleRate;
            ref0[i] = std::sin(phase0);
            ref1[i] = std::sin(phase1);
        }
    }

    void saveBitsToFile(const std::vector<bool>& finalBits, const juce::String& outputFileName)
    {
        std::cout << "\n--- Final Decoded Bitstream (after voting) ---" << std::endl;
        std::cout << "Saving payload (" << finalBits.size() << " bits) to " << outputFileName << std::endl;

        std::ofstream outputFile(outputFileName.toStdString());
        for (size_t i = 0; i < finalBits.size(); ++i) {
            char bitChar = finalBits[i] ? '1' : '0';
            std::cout << bitChar;
            outputFile << bitChar;
            if ((i + 1) % 8 == 0) std::cout << " ";
            if ((i + 1) % 64 == 0) std::cout << std::endl;
        }
        std::cout << std::endl;
        outputFile.close();
    }

public:
    MatchedFilterFSKDemodulator() { generateReferences(); }

    void demodulateAndSave(const juce::AudioBuffer<float>& frameData, const juce::String& outputFileName, int testNumber) {
        std::vector<bool> receivedRawBits;
        receivedRawBits.reserve(FSK::totalTransmittedBits);
        const float* data = frameData.getReadPointer(0);

        const float* ref0 = reference_f0.getReadPointer(0);
        const float* ref1 = reference_f1.getReadPointer(0);

        for (int i = 0; i < FSK::totalTransmittedBits; ++i) {
            const float* bitSamples = data + (i * FSK::samplesPerBit);
            double corr_f0 = 0.0, corr_f1 = 0.0;
            for (int j = 0; j < FSK::samplesPerBit; ++j) {
                corr_f0 += bitSamples[j] * ref0[j];
                corr_f1 += bitSamples[j] * ref1[j];
            }
            receivedRawBits.push_back(corr_f1 > corr_f0);
        }

#if USE_TRIPLE_REDUNDANCY
        std::cout << "Performing majority vote decoding..." << std::endl;
        std::vector<bool> finalDecodedBits;
        finalDecodedBits.reserve(FSK::totalBaseFrameBits);

        for (size_t i = 0; i < receivedRawBits.size(); i += 3) {
            if (i + 2 < receivedRawBits.size()) {
                int voteSum = receivedRawBits[i] + receivedRawBits[i + 1] + receivedRawBits[i + 2];
                finalDecodedBits.push_back(voteSum >= 2); // Majority vote: if 2 or 3 are '1', the result is '1'
            }
        }
        saveBitsToFile(finalDecodedBits, outputFileName);
#else
        saveBitsToFile(receivedRawBits, outputFileName);
#endif
    }
};

// ==============================================================================
//  CLASS 3: Offline Analyzer (Orchestrator)
// ==============================================================================
class FSKOfflineProcessor
{
public:
    FSKOfflineProcessor() {}

    void analyzeRecording(const juce::AudioBuffer<float>& recordedAudio, const juce::String& outputFileName, int testNumber) {
        std::cout << "\n--- Analyzing Recorded Audio for Test N=" << testNumber << " ---" << std::endl;
        int frameStartSample = static_cast<int>(FSK::sampleRate * 0.5) + FSK::preambleSamples;
        if (frameStartSample + FSK::totalFrameDataSamples > recordedAudio.getNumSamples()) {
            std::cerr << "Error: Not enough samples in recording for a full frame." << std::endl;
            return;
        }
        juce::AudioBuffer<float> frameData(1, FSK::totalFrameDataSamples);
        frameData.copyFrom(0, 0, recordedAudio, 0, frameStartSample, FSK::totalFrameDataSamples);
        demodulator.demodulateAndSave(frameData, outputFileName, testNumber);
    }
private:
    MatchedFilterFSKDemodulator demodulator;
};

// ==============================================================================
//  CLASS 4: Loopback Test Manager
// ==============================================================================
class AcousticLoopbackTester : public juce::AudioIODeviceCallback {
public:
    AcousticLoopbackTester(const std::vector<bool>& bitsToSend) : fskSource(bitsToSend) {
        int requiredSamples = fskSource.getNumSamples() + static_cast<int>(FSK::sampleRate * 1.0);
        recordedAudio.setSize(1, requiredSamples);
        recordedAudio.clear();
    }
    bool isTestFinished() const { return testFinished; }
    juce::AudioBuffer<float>& getRecording() { return recordedAudio; }
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override {
        fskSource.prepareToPlay(device->getCurrentBufferSizeSamples(), device->getCurrentSampleRate());
    }
    void audioDeviceStopped() override {}
    void audioDeviceIOCallbackWithContext(const float* const* input, int numIn, float* const* output, int numOut, int numSamples, const juce::AudioIODeviceCallbackContext&) override {
        if (numOut > 0) {
            juce::AudioBuffer<float> tempBuffer(output, numOut, numSamples);
            juce::AudioSourceChannelInfo info(&tempBuffer, 0, numSamples);
            fskSource.getNextAudioBlock(info);
        }
        if (numIn > 0 && input[0] != nullptr) {
            if (samplesRecorded + numSamples <= recordedAudio.getNumSamples()) {
                recordedAudio.copyFrom(0, samplesRecorded, input[0], numSamples);
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
//  Main Application - Automated Test Suite
// ==============================================================================
int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juce_init;

    for (int n = 1; n <= 8; ++n)
    {
        std::cout << "\n==========================================================" << std::endl;
        std::cout << "             PERFORMING TEST FOR N = " << n << std::endl;
        std::cout << "==========================================================" << std::endl;

        std::vector<bool> base_bits;
        base_bits.reserve(FSK::payloadBits);
        bool currentBit = false;
        while (base_bits.size() < FSK::payloadBits) {
            for (int i = 0; i < n && base_bits.size() < FSK::payloadBits; ++i) {
                base_bits.push_back(currentBit);
            }
            currentBit = !currentBit;
        }

        std::vector<bool> bits_to_send;
#if USE_TRIPLE_REDUNDANCY
        std::cout << "Applying Triple Redundancy encoding..." << std::endl;
        for (bool bit : base_bits) {
            bits_to_send.push_back(bit);
            bits_to_send.push_back(bit);
            bits_to_send.push_back(bit);
        }
#else
        bits_to_send = base_bits;
#endif
        std::cout << "Generated " << base_bits.size() << " base bits -> " << bits_to_send.size() << " bits to transmit for pattern N=" << n << std::endl;

        AcousticLoopbackTester tester(bits_to_send);
        juce::AudioDeviceManager deviceManager;
        deviceManager.initialiseWithDefaultDevices(1, 2);

        std::cout << "\nPress ENTER to begin play/record for N=" << n << "..." << std::endl;
        std::cin.get();

        deviceManager.addAudioCallback(&tester);
        std::cout << "--- Playing and recording simultaneously... ---" << std::endl;

        std::cout << "Press ENTER to stop recording and analyze..." << std::endl;
        std::cin.get();

        deviceManager.removeAudioCallback(&tester);
        std::cout << "--- Play/Record finished. ---" << std::endl;

        FSKOfflineProcessor processor;
        juce::String outputFileName = "OUTPUT_" + juce::String(n) + ".txt";
        processor.analyzeRecording(tester.getRecording(), outputFileName, n);
    }

    std::cout << "\nAll tests complete. Press ENTER to exit." << std::endl;
    std::cin.get();
    return 0;
}
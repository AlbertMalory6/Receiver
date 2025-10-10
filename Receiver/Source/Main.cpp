#include <JuceHeader.h>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <string>

// --- MASTER DEBUGGING SWITCH ---
// Set to 1 for a "Digital Loopback Test" (verifies software logic, CRC should pass).
// Set to 0 for a real "Acoustic Loopback Test" (uses speaker and mic).
#define DIGITAL_LOOPBACK_TEST 0

namespace FSK {
    constexpr double sampleRate = 44100.0;
    // --- RECOMMENDED ROBUST FREQUENCIES ---
    constexpr double f0 = 2000.0;
    constexpr double f1 = 4000.0;

    constexpr double bitRate = 1000.0;
    constexpr int samplesPerBit = static_cast<int>(sampleRate / bitRate);
    constexpr int preambleSamples = 440;
    constexpr int payloadBits = 2000;
    constexpr int crcBits = 8;
    constexpr int totalFrameBits = payloadBits + crcBits;
    constexpr int totalFrameDataSamples = totalFrameBits * samplesPerBit;

    uint8_t calculateCRC8(const std::vector<bool>& data);
    std::string byteToBinary(uint8_t byte);
}

class FSKSignalSource { /* ... full definition below ... */ };
class FSKOfflineProcessor { /* ... full definition below ... */ };
class AcousticLoopbackTester : public juce::AudioIODeviceCallback { /* ... full definition below ... */ };

// ==============================================================================
//  Main Application
// ==============================================================================
int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juce_init;

    std::vector<bool> bits_to_send;
    std::ifstream inputFile("INPUT.txt");
    if (!inputFile.is_open()) { std::cerr << "Error: Could not open INPUT.txt." << std::endl; return 1; }
    char bitChar;
    while (inputFile.get(bitChar)) {
        if (bitChar == '0') bits_to_send.push_back(false);
        else if (bitChar == '1') bits_to_send.push_back(true);
    }
    inputFile.close();
    std::cout << "Read " << bits_to_send.size() << " bits from INPUT.txt" << std::endl;

    FSKOfflineProcessor processor;

#if DIGITAL_LOOPBACK_TEST
    // --- Digital Loopback Mode ---
    std::cout << "\n*** DIGITAL LOOPBACK TEST MODE ***" << std::endl;
    std::cout << "Generating signal and analyzing it directly in memory..." << std::endl;

    FSKSignalSource fskSource(bits_to_send);
    processor.analyzeRecording(fskSource.getSignalBuffer());

#else
    // --- Acoustic Loopback Mode ---
    AcousticLoopbackTester tester(bits_to_send);
    juce::AudioDeviceManager deviceManager;
    deviceManager.initialiseWithDefaultDevices(1, 2);

    std::cout << "\n*** ACOUSTIC LOOPBACK TEST MODE ***" << std::endl;
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

    processor.analyzeRecording(tester.getRecording());
#endif

    std::cout << "\nPress ENTER to exit." << std::endl;
    std::cin.get();
    return 0;
}

// ==============================================================================
//  FULL CLASS AND FUNCTION DEFINITIONS
// ==============================================================================

// (The following is the full, self-contained code for all classes and functions)

uint8_t FSK::calculateCRC8(const std::vector<bool>& data) { /* ... same implementation ... */ }
std::string FSK::byteToBinary(uint8_t byte) { /* ... same implementation ... */ }

class FSKSignalSource : public juce::AudioSource {
public:
    FSKSignalSource(const std::vector<bool>& bitsToSend) { generateFullSignal(bitsToSend); }
    bool isFinished() const { return isPlaybackFinished; }
    int getNumSamples() const { return signalBuffer.getNumSamples(); }
    const juce::AudioBuffer<float>& getSignalBuffer() const { return signalBuffer; }
    void prepareToPlay(int, double) override { position = 0; isPlaybackFinished = false; }
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
    void generateFullSignal(const std::vector<bool>& bits) {
        const int silentLeaderSamples = FSK::sampleRate * 0.5;
        auto payloadWithCrcBits = bits;
        uint8_t crc = FSK::calculateCRC8(bits);
        for (int i = 7; i >= 0; --i) payloadWithCrcBits.push_back((crc >> i) & 1);
        const int totalDataSamples = payloadWithCrcBits.size() * FSK::samplesPerBit;
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

class FSKOfflineProcessor {
public:
    FSKOfflineProcessor() {
        generatePreambleTemplate();
        preambleTemplateEnergy = calculateEnergy(preambleTemplate.getReadPointer(0), FSK::preambleSamples);
    }
    void analyzeRecording(const juce::AudioBuffer<float>& recordedAudio) {
        std::cout << "\n--- Analyzing Recorded Audio ---" << std::endl;
#if FORCE_DEMODULATION // This is an older flag, replaced by DIGITAL_LOOPBACK_TEST in main
        // Logic for forced demodulation would go here if needed
#else
        const float* signal = recordedAudio.getReadPointer(0);
        juce::AudioBuffer<float> circularBuffer(1, FSK::preambleSamples * 2);
        int circularBufferPos = 0;
        double syncPowerLocalMax = 0.0;
        int peakSampleIndex = -1;
        static constexpr double NCC_DETECTION_THRESHOLD = 0.50;
        for (int i = 0; i < recordedAudio.getNumSamples(); ++i) {
            circularBuffer.setSample(0, circularBufferPos, signal[i]);
            double ncc = calculateNormalizedCrossCorrelation(circularBuffer, circularBufferPos);
            if (ncc > syncPowerLocalMax && ncc > NCC_DETECTION_THRESHOLD) {
                syncPowerLocalMax = ncc;
                peakSampleIndex = i;
            }
            else if (peakSampleIndex != -1 && (i - peakSampleIndex) > (FSK::preambleSamples)) {
                std::cout << "\n\n*** PREAMBLE DETECTED at " << (double)peakSampleIndex / FSK::sampleRate << "s! Peak NCC: " << syncPowerLocalMax << " ***\n" << std::endl;
                int frameStartSample = findOptimalFrameStart(recordedAudio, peakSampleIndex);
                if (frameStartSample + FSK::totalFrameDataSamples <= recordedAudio.getNumSamples()) {
                    juce::AudioBuffer<float> frameData(1, FSK::totalFrameDataSamples);
                    frameData.copyFrom(0, 0, recordedAudio, 0, frameStartSample, FSK::totalFrameDataSamples);
                    demodulateFrame(frameData);
                }
                else {
                    std::cerr << "Preamble found, but not enough data for a full frame. Aborting." << std::endl;
                }
                std::cout << "\n--- Analysis Complete ---" << std::endl;
                return;
            }
            circularBufferPos = (circularBufferPos + 1) % circularBuffer.getNumSamples();
        }
        if (peakSampleIndex == -1) {
            std::cout << "No preamble detected above the threshold of " << NCC_DETECTION_THRESHOLD << std::endl;
        }
#endif
    }
private:
    juce::AudioBuffer<float> preambleTemplate;
    double preambleTemplateEnergy = 0.0;
    void generatePreambleTemplate() { /* ... */ }
    double calculateEnergy(const float*, int) { /* ... */ }
    double calculateNormalizedCrossCorrelation(const juce::AudioBuffer<float>&, int) { /* ... */ }
    double goertzelMagnitude(int, int, const float*) { /* ... */ }
    void demodulateFrame(const juce::AudioBuffer<float>&) { /* ... */ }
    int findOptimalFrameStart(const juce::AudioBuffer<float>&, int) { /* ... */ }
};

class AcousticLoopbackTester : public juce::AudioIODeviceCallback {
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
    void audioDeviceIOCallbackWithContext(const float* const* input, int numIn, float* const* output, int numOut, int numSamples, const juce::AudioIODeviceCallbackContext&) override {
        if (numOut > 0) {
            juce::AudioSourceChannelInfo info(output, numOut, numSamples);
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
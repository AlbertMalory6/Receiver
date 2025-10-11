#include <JuceHeader.h>
#include <fstream>
#include <iostream>
#include <iomanip>

// --- SHARED PARAMETERS AND FUNCTIONS (must be identical for sender and receiver logic) ---
namespace FSK {
    constexpr double sampleRate = 44100.0;
    constexpr double f0 = 2000.0;
    constexpr double f1 = 4000.0;
    constexpr double bitRate = 1000.0;
    constexpr int samplesPerBit = static_cast<int>(sampleRate / bitRate);
    constexpr int preambleSamples = 440;
    constexpr int payloadBits = 5000;
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
    FSKSignalSource(const std::vector<bool>& bitsToSend) { generateFullSignal(bitsToSend); }
    bool isFinished() const { return isPlaybackFinished; }
    int getNumSamples() const { return signalBuffer.getNumSamples(); }
    void prepareToPlay(int, double) override { position = 0; isPlaybackFinished = false; }
    void releaseResources() override {}
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override
    {
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
    void generateFullSignal(const std::vector<bool>&);
    juce::AudioBuffer<float> signalBuffer;
    int position = 0;
    bool isPlaybackFinished = true;
};

// ==============================================================================
//  2. Offline Analyzer (Demodulates the recording)
// ==============================================================================
class FSKOfflineProcessor
{
public:
    FSKOfflineProcessor() {
        generatePreambleTemplate();
        preambleTemplateEnergy = calculateEnergy(preambleTemplate.getReadPointer(0), FSK::preambleSamples);
    }

    void analyzeRecording(const juce::AudioBuffer<float>& recordedAudio)
    {
        std::cout << "\n--- Analyzing Recorded Audio ---" << std::endl;
        const float* signal = recordedAudio.getReadPointer(0);

        const int samplesPerWindow = FSK::sampleRate * 0.5;
        int samplesProcessedInWindow = 0;
        double peakNCCInWindow = 0.0;

        juce::AudioBuffer<float> circularBuffer(1, FSK::preambleSamples * 2);
        int circularBufferPos = 0;

        for (int i = 0; i < recordedAudio.getNumSamples(); ++i)
        {
            circularBuffer.setSample(0, circularBufferPos, signal[i]);
            double ncc = calculateNormalizedCrossCorrelation(circularBuffer, circularBufferPos);
            if (ncc > peakNCCInWindow) {
                peakNCCInWindow = ncc;
            }

            samplesProcessedInWindow++;
            if (samplesProcessedInWindow >= samplesPerWindow) {
                std::cout << "\rPeak NCC in last 0.5s: " << std::fixed << std::setprecision(4) << peakNCCInWindow << "     ";
                peakNCCInWindow = 0.0;
                samplesProcessedInWindow = 0;
            }

            // NOTE: Add your full detection and demodulation logic here, using the
            // peak-finding method from the previous real-time receiver version.

            circularBufferPos = (circularBufferPos + 1) % circularBuffer.getNumSamples();
        }
        std::cout << "\n--- Analysis Complete ---" << std::endl;
    }
private:
    juce::AudioBuffer<float> preambleTemplate;
    double preambleTemplateEnergy = 0.0;

    void generatePreambleTemplate();
    double calculateEnergy(const float*, int);
    double calculateNormalizedCrossCorrelation(const juce::AudioBuffer<float>&, int);
};

// ==============================================================================
//  3. The Loopback Test Manager (Handles simultaneous play/record)
// ==============================================================================
class AcousticLoopbackTester : public juce::AudioIODeviceCallback
{
public:
    AcousticLoopbackTester(const std::vector<bool>& bitsToSend) : fskSource(bitsToSend)
    {
        // Allocate buffer for recording, add a little extra for latency
        int requiredSamples = fskSource.getNumSamples() + (int)(FSK::sampleRate * 1.0); // 1 second buffer
        recordedAudio.setSize(1, requiredSamples);
        recordedAudio.clear();
    }

    bool isTestFinished() const { return testFinished; }

    juce::AudioBuffer<float>& getRecording() { return recordedAudio; }

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override
    {
        fskSource.prepareToPlay(device->getCurrentBufferSizeSamples(), device->getCurrentSampleRate());
    }

    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
        float* const* outputChannelData, int numOutputChannels,
        int numSamples, const juce::AudioIODeviceCallbackContext&) override
    {
        // --- Playback Logic ---
        if (numOutputChannels > 0)
        {
            //juce::AudioSourceChannelInfo bufferToFill(outputChannelData, numOutputChannels, numSamples);
            juce::AudioSourceChannelInfo bufferToFill;
            juce::AudioBuffer<float> tempBuffer(const_cast<float**>(outputChannelData), numOutputChannels, numSamples);
            bufferToFill.buffer = &tempBuffer;            bufferToFill.startSample = 0;
            bufferToFill.numSamples = numSamples;
            fskSource.getNextAudioBlock(bufferToFill);
        }

        // --- Recording Logic ---
        if (numInputChannels > 0)
        {
            if (samplesRecorded + numSamples <= recordedAudio.getNumSamples())
            {
                recordedAudio.copyFrom(0, samplesRecorded, inputChannelData[0], numSamples);
                samplesRecorded += numSamples;
            }
        }

        // --- Stop Condition ---
        if (fskSource.isFinished() && !testFinished)
        {
            // Wait a moment after playback finishes to capture any echo, then stop.
            if (++postPlaybackSamples > FSK::sampleRate * 0.2) // 200ms grace period
            {
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

    // 1. Read bits from file
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

    // 2. Setup the loopback test
    AcousticLoopbackTester tester(bits_to_send);
    juce::AudioDeviceManager deviceManager;
    deviceManager.initialiseWithDefaultDevices(1, 2); // 1 input, 2 outputs

    std::cout << "Acoustic loopback test is ready." << std::endl;
    std::cout << "Ensure your microphone can hear your speakers." << std::endl;
    std::cout << "Press ENTER to start the play/record process..." << std::endl;
    std::cin.get();

    deviceManager.addAudioCallback(&tester);
    std::cout << "--- Playing and recording simultaneously... ---" << std::endl;

    // 3. Wait for the test to complete
    while (!tester.isTestFinished())
    {
        juce::Thread::sleep(100);
    }

    deviceManager.removeAudioCallback(&tester);
    std::cout << "--- Play/Record finished. ---" << std::endl;

    // 4. Analyze the resulting recording
    FSKOfflineProcessor processor;
    processor.analyzeRecording(tester.getRecording());

    std::cout << "Press ENTER to exit." << std::endl;
    std::cin.get();
    return 0;
}

// ==============================================================================
//  5. Full Function Definitions
// ==============================================================================

// FSKSignalSource Method
void FSKSignalSource::generateFullSignal(const std::vector<bool>& bits) {
    auto payloadWithCrcBits = bits;
    uint8_t crc = FSK::calculateCRC8(bits);
    for (int i = 7; i >= 0; --i) payloadWithCrcBits.push_back((crc >> i) & 1);
    const int totalDataSamples = payloadWithCrcBits.size() * FSK::samplesPerBit;
    signalBuffer.setSize(1, FSK::preambleSamples + totalDataSamples);
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
        signal[i] = std::sin(currentPhase) * 0.5;
    }
    int sampleIndex = FSK::preambleSamples;
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

// FSKOfflineProcessor Methods
void FSKOfflineProcessor::generatePreambleTemplate() {
    preambleTemplate.setSize(1, FSK::preambleSamples);
    auto* signal = preambleTemplate.getWritePointer(0);
    double currentPhase = 0.0;
    for (int i = 0; i < FSK::preambleSamples; ++i) {
        double freq;
        if (i < FSK::preambleSamples / 2)
            freq = juce::jmap((double)i, 0.0, (double)FSK::preambleSamples / 2.0, FSK::f0 - 1000.0, FSK::f1 + 1000.0);
        else
            freq = juce::jmap((double)i, (double)FSK::preambleSamples / 2.0, (double)FSK::preambleSamples, FSK::f1 + 1000.0, FSK::f0 - 1000.0);
        double phaseIncrement = 2.0 * juce::MathConstants<double>::pi * freq / FSK::sampleRate;
        currentPhase += phaseIncrement;
        signal[i] = std::sin(currentPhase) * 0.5;
    }
}
double FSKOfflineProcessor::calculateEnergy(const float* buffer, int numSamples) {
    double energy = 0.0;
    for (int i = 0; i < numSamples; ++i) { energy += buffer[i] * buffer[i]; }
    return energy;
}
double FSKOfflineProcessor::calculateNormalizedCrossCorrelation(const juce::AudioBuffer<float>& circularBuffer, int circularBufferPos) {
    double dotProduct = 0.0;
    float tempSignalWindow[FSK::preambleSamples];
    for (int i = 0; i < FSK::preambleSamples; ++i) {
        int bufferIndex = (circularBufferPos - FSK::preambleSamples + i + circularBuffer.getNumSamples()) % circularBuffer.getNumSamples();
        tempSignalWindow[i] = circularBuffer.getSample(0, bufferIndex);
    }
    double signalEnergy = calculateEnergy(tempSignalWindow, FSK::preambleSamples);
    if (signalEnergy < 1e-9 || preambleTemplateEnergy < 1e-9) return 0.0;
    const float* preamble = preambleTemplate.getReadPointer(0);
    for (int i = 0; i < FSK::preambleSamples; ++i) {
        dotProduct += preamble[i] * tempSignalWindow[i];
    }
    return dotProduct / std::sqrt(signalEnergy * preambleTemplateEnergy);
}
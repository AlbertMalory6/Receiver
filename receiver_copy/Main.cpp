/*
 ==============================================================================
    FSK Receiver using JUCE and C++
    - Starts recording on user input.
    - Detects a chirp preamble using a matched filter.
    - Demodulates FSK data using the Goertzel algorithm.
    - Verifies CRC and writes the payload to OUTPUT.txt.
 ==============================================================================
*/

#include <JuceHeader.h>
#include <fstream>
#include <iostream>

// Use the same CRC-8 function as the sender for verification
uint8_t calculateCRC8(const std::vector<bool>& data); // Declaration

// ==============================================================================
// 1. FSK Receiver Class
// ==============================================================================
class FSKReceiver : public juce::AudioIODeviceCallback
{
private:
    // --- FSK Parameters ---
    static constexpr double f0 = 2000.0;
    static constexpr double f1 = 4000.0;
    static constexpr double bitRate = 1000.0;
    static constexpr double sampleRate = 44100.0;
    static constexpr int samplesPerBit = static_cast<int>(sampleRate / bitRate);
    static constexpr int preambleSamples = 440;
    static constexpr int payloadBits = 5000;
    static constexpr int crcBits = 8;
    static constexpr int totalFrameBits = payloadBits + crcBits;
    static constexpr int totalFrameDataSamples = totalFrameBits * samplesPerBit;
    static constexpr int requiredBufferSize = preambleSamples + totalFrameDataSamples;

    // --- State Machine ---
    enum class State { WaitingForPreamble, ReceivingData };
    State currentState = State::WaitingForPreamble;

    // --- Audio Buffers ---
    juce::AudioBuffer<float> preambleTemplate;
    juce::AudioBuffer<float> circularBuffer;
    juce::AudioBuffer<float> frameDataBuffer;
    int circularBufferPos = 0;
    int samplesCollected = 0;

    // --- CORRECTED: Preamble Detection using NCC ---
    double preambleTemplateEnergy = 0.0; // Pre-calculated for efficiency
    double syncPowerLocalMax = 0.0;
    int peakSampleIndex = 0;

    // --- NEW: A single, intuitive threshold for NCC ---
    // A preamble is detected if the shape similarity is greater than 70%.
    static constexpr double NCC_DETECTION_THRESHOLD = 0.7;

public:
    FSKReceiver() : circularBuffer(1, preambleSamples * 2), frameDataBuffer(1, totalFrameDataSamples)
    {
        generatePreambleTemplate();
        // Pre-calculate the energy of the template once.
        preambleTemplateEnergy = calculateEnergy(preambleTemplate.getReadPointer(0), preambleSamples);
    }

    // ==========================================================================
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
        float* const* outputChannelData, int numOutputChannels,
        int numSamples, const juce::AudioIODeviceCallbackContext& context) override
    {
        juce::ignoreUnused(outputChannelData, numOutputChannels, context);
        if (numInputChannels < 1) return;
        const float* input = inputChannelData[0];

        for (int i = 0; i < numSamples; ++i)
        {
            float currentSample = input[i];
            circularBuffer.setSample(0, circularBufferPos, currentSample);

            if (currentState == State::WaitingForPreamble)
            {
                // Check for preamble periodically
                if (circularBufferPos % 4 == 0)
                {
                    double ncc = calculateNormalizedCrossCorrelation();

                    // --- Peak Detection Logic (Simplified for NCC) ---
                    if (ncc > syncPowerLocalMax && ncc > NCC_DETECTION_THRESHOLD)
                    {
                        // Found a new potential peak that meets our similarity threshold
                        syncPowerLocalMax = ncc;
                        peakSampleIndex = circularBufferPos;
                    }
                    else if (peakSampleIndex != 0 && (circularBufferPos - peakSampleIndex + circularBuffer.getNumSamples()) % circularBuffer.getNumSamples() > 200)
                    {
                        // The peak has passed. Lock it in and switch states.
                        std::cout << "Preamble DETECTED! Peak NCC: " << syncPowerLocalMax << std::endl;

                        currentState = State::ReceivingData;
                        samplesCollected = 0;
                        frameDataBuffer.clear();

                        // Reset for the next frame search
                        peakSampleIndex = 0;
                        syncPowerLocalMax = 0.0;
                    }
                }
            }
            else if (currentState == State::ReceivingData)
            {
                // ... (This data collection part is unchanged) ...
            }
            // Advance circular buffer position
            circularBufferPos = (circularBufferPos + 1) % circularBuffer.getNumSamples();
        }
    }

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override {
        // You might want to check device->getCurrentSampleRate() here
    }
    void audioDeviceStopped() override {}

private:
    // ==========================================================================
    // --- Helper Functions ---
    // ==========================================================================
    double calculateEnergy(const float* buffer, int numSamples)
    {
        double energy = 0.0;
        for (int i = 0; i < numSamples; ++i) {
            energy += buffer[i] * buffer[i];
        }
        return energy;
    }

    // --- REPLACED with NCC ---
    double calculateNormalizedCrossCorrelation()
    {
        const float* preamble = preambleTemplate.getReadPointer(0);
        const float* buffer = circularBuffer.getReadPointer(0);

        double dotProduct = 0.0;
        double signalEnergy = 0.0;

        // Create a temporary buffer for the current window of signal data
        float tempSignalWindow[preambleSamples];
        for (int i = 0; i < preambleSamples; ++i)
        {
            int bufferIndex = (circularBufferPos - preambleSamples + i + circularBuffer.getNumSamples()) % circularBuffer.getNumSamples();
            tempSignalWindow[i] = buffer[bufferIndex];
        }

        signalEnergy = calculateEnergy(tempSignalWindow, preambleSamples);

        // Avoid division by zero if signal or template is silent
        if (signalEnergy == 0.0 || preambleTemplateEnergy == 0.0)
            return 0.0;

        // Calculate dot product
        for (int i = 0; i < preambleSamples; ++i)
        {
            dotProduct += preamble[i] * tempSignalWindow[i];
        }

        // The NCC formula
        double denominator = std::sqrt(signalEnergy * preambleTemplateEnergy);
        return dotProduct / denominator;
    }
    void generatePreambleTemplate()
    {
        preambleTemplate.setSize(1, preambleSamples);
        auto* signal = preambleTemplate.getWritePointer(0);
        double currentPhase = 0.0;
        for (int i = 0; i < preambleSamples; ++i)
        {
            double freq;
            if (i < preambleSamples / 2)
                freq = juce::jmap((double)i, 0.0, (double)preambleSamples / 2.0, f0 - 1000.0, f1 + 1000.0);
            else
                freq = juce::jmap((double)i, (double)preambleSamples / 2.0, (double)preambleSamples, f1 + 1000.0, f0 - 1000.0);
            double phaseIncrement = 2.0 * juce::MathConstants<double>::pi * freq / sampleRate;
            currentPhase += phaseIncrement;
            signal[i] = std::sin(currentPhase) * 0.5;
        }
    }

    double calculateCorrelation()
    {
        double sum = 0.0;
        const float* preamble = preambleTemplate.getReadPointer(0);
        const float* buffer = circularBuffer.getReadPointer(0);

        for (int i = 0; i < preambleSamples; ++i)
        {
            // Access circular buffer correctly
            int bufferIndex = (circularBufferPos - preambleSamples + i + circularBuffer.getNumSamples()) % circularBuffer.getNumSamples();
            sum += preamble[i] * buffer[bufferIndex];
        }
        return std::abs(sum) / preambleSamples;
    }

    // Goertzel algorithm to efficiently detect magnitude of a single frequency
    double goertzelMagnitude(int numSamplesInBlock, int targetFrequency, const float* data)
    {
        double k = 0.5 + (numSamplesInBlock * targetFrequency / sampleRate);
        double w = (2.0 * juce::MathConstants<double>::pi / numSamplesInBlock) * k;
        double cosine = std::cos(w);
        double sine = std::sin(w);
        double coeff = 2.0 * cosine;
        double q0 = 0, q1 = 0, q2 = 0;

        for (int i = 0; i < numSamplesInBlock; i++) {
            q0 = coeff * q1 - q2 + data[i];
            q2 = q1;
            q1 = q0;
        }
        double real = (q1 - q2 * cosine);
        double imag = (q2 * sine);
        return std::sqrt(real * real + imag * imag);
    }

    void demodulateFrame()
    {
        std::vector<bool> receivedBits;
        receivedBits.reserve(totalFrameBits);
        const float* data = frameDataBuffer.getReadPointer(0);

        for (int i = 0; i < totalFrameBits; ++i)
        {
            const float* bitSamples = data + (i * samplesPerBit);
            double mag_f0 = goertzelMagnitude(samplesPerBit, f0, bitSamples);
            double mag_f1 = goertzelMagnitude(samplesPerBit, f1, bitSamples);
            receivedBits.push_back(mag_f1 > mag_f0);
        }

        // --- CRC Check ---
        std::vector<bool> payload(receivedBits.begin(), receivedBits.begin() + payloadBits);
        uint8_t receivedCrcByte = 0;
        for (int i = 0; i < crcBits; ++i)
            if (receivedBits[payloadBits + i])
                receivedCrcByte |= (1 << (7 - i));

        uint8_t calculatedCrc = calculateCRC8(payload);

        if (calculatedCrc == receivedCrcByte)
        {
            std::cout << "CRC OK! Writing payload to OUTPUT.txt" << std::endl;
            std::ofstream outputFile("OUTPUT.txt");
            for (bool bit : payload)
                outputFile << (bit ? '1' : '0');
            outputFile.close();
        }
        else
        {
            std::cout << "CRC FAIL! Received: " << (int)receivedCrcByte << ", Calculated: " << (int)calculatedCrc << ". Frame discarded." << std::endl;
        }
    }
};

// ==============================================================================
// 3. Main Application and CRC Function Definition
// ==============================================================================
// (Paste the calculateCRC8 function from the sender here)
uint8_t calculateCRC8(const std::vector<bool>& data)
{
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

int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juce_init;

    FSKReceiver receiver;
    juce::AudioDeviceManager deviceManager;
    deviceManager.initialiseWithDefaultDevices(1, 0); // 1 input, 0 outputs

    std::cout << "Receiver is ready." << std::endl;
    std::cout << "Press ENTER to start recording..." << std::endl;
    std::cin.get();

    deviceManager.addAudioCallback(&receiver);



    std::cout << "--- Recording started. Listening for signal... ---" << std::endl;
    std::cout << "(Press ENTER again to quit)" << std::endl;
    std::cin.get();

    deviceManager.removeAudioCallback(&receiver);
    return 0;
}
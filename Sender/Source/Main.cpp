/*
 ==============================================================================
    FSK Sender - CORRECTED FOR PERFORMANCE REQUIREMENTS
    - Reads 10,000 bits from INPUT.txt.
    - Uses a 1000 bps bit rate to transmit in < 15 seconds.
 ==============================================================================
*/

#include <JuceHeader.h>
#include <fstream>  // Required for file reading
#include <iostream> // For console output
#include <filesystem>

// ==============================================================================
// 1. CRC-8 Implementation (Unchanged)
// ==============================================================================
uint8_t calculateCRC8(const std::vector<bool>& data)
{
    const uint8_t polynomial = 0xD7;
    uint8_t crc = 0;

    for (bool bit : data)
    {
        crc ^= (bit ? 0x80 : 0x00);
        for (int i = 0; i < 8; ++i)
        {
            if (crc & 0x80)
                crc = (crc << 1) ^ polynomial;
            else
                crc <<= 1;
        }
    }
    return crc;
}

// ==============================================================================
// 2. FSK Audio Source 
// ==============================================================================
class FSKSignalSource : public juce::AudioSource
{
public:
    FSKSignalSource(const std::vector<bool>& bitsToSend)
    {
        generateFullSignal(bitsToSend);
    }

    bool isFinished() const
    {
        return isPlaybackFinished;
    }

    // --- AudioSource overrides ---
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override
    {
        currentSampleRate = sampleRate;
        position = 0;
        isPlaybackFinished = false;
    }

    void releaseResources() override {}

    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override
    {
        // playback logic 
        if (isPlaybackFinished) { bufferToFill.clearActiveBufferRegion(); return; }
        auto remainingSamples = signalBuffer.getNumSamples() - position;
        auto samplesThisTime = juce::jmin(bufferToFill.numSamples, remainingSamples);
        if (samplesThisTime > 0)
        {
            bufferToFill.buffer->copyFrom(0, bufferToFill.startSample, signalBuffer, 0, position, samplesThisTime);
            if (bufferToFill.buffer->getNumChannels() > 1)
                bufferToFill.buffer->copyFrom(1, bufferToFill.startSample, signalBuffer, 0, position, samplesThisTime);
            position += samplesThisTime;
        }
        if (samplesThisTime < bufferToFill.numSamples)
        {
            bufferToFill.buffer->clear(bufferToFill.startSample + samplesThisTime, bufferToFill.numSamples - samplesThisTime);
            isPlaybackFinished = true;
        }
    }

private:
    void generateFullSignal(const std::vector<bool>& bits)
    {
        // --- Parameters ---
        const double f0 = 2000.0;
        const double f1 = 4000.0;

        // *** BIT RATE INCREASED FOR SPEED ***
        const double bitRate = 1000.0;

        const double sampleRate = 44100.0;
        const int samplesPerBit = static_cast<int>(sampleRate / bitRate);
        const int preambleSamples = 440;

        // --- CRC ---
        auto payloadWithCrcBits = bits;
        uint8_t crc = calculateCRC8(bits);
        for (int i = 7; i >= 0; --i)
            payloadWithCrcBits.push_back((crc >> i) & 1);

        // --- Signal Generation ---
        const int totalDataSamples = payloadWithCrcBits.size() * samplesPerBit;
        signalBuffer.setSize(1, preambleSamples + totalDataSamples);
        signalBuffer.clear();
        auto* signal = signalBuffer.getWritePointer(0);

        // 1. Preamble (Chirp)
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

        // 2. FSK Data 
        int sampleIndex = preambleSamples;
        for (bool bit : payloadWithCrcBits)
        {
            double freq = bit ? f1 : f0;
            double phaseIncrement = 2.0 * juce::MathConstants<double>::pi * freq / sampleRate;
            for (int i = 0; i < samplesPerBit; ++i)
            {
                signal[sampleIndex++] = std::sin(currentPhase);
                currentPhase += phaseIncrement;
            }
        }
        signalBuffer.applyGain(1.0f / (signalBuffer.getMagnitude(0, signalBuffer.getNumSamples()) * 1.1f));
    }

    juce::AudioBuffer<float> signalBuffer;
    int position = 0;
    double currentSampleRate = 44100.0;
    bool isPlaybackFinished = true;
};

// ==============================================================================
// 3. Main Application Logic (Modified to read from INPUT.txt)
// ==============================================================================
int main(int argc, char* argv[])
{

    juce::ScopedJuceInitialiser_GUI juce_init;

    // --- *** READ BITS FROM INPUT.TXT *** ---
    std::vector<bool> bits_to_send;

    std::ifstream inputFile("INPUT.txt");

    if (!inputFile.is_open())
    {
        std::cerr << "Error: Could not open INPUT.txt. Please create the file in the same directory as the executable." << std::endl;
        return 1;
    }
    char bitChar;
    while (inputFile.get(bitChar))
    {
        if (bitChar == '0')
            bits_to_send.push_back(false);
        else if (bitChar == '1')
            bits_to_send.push_back(true);
    }
    inputFile.close();
    std::cout << "Read " << bits_to_send.size() << " bits from INPUT.txt" << std::endl;

    // --- Audio Setup ---
    juce::AudioDeviceManager deviceManager;
    deviceManager.initialiseWithDefaultDevices(0, 2);

    // --- Create and Play Source ---
    FSKSignalSource fskSource(bits_to_send);
    juce::AudioSourcePlayer audioSourcePlayer;
    audioSourcePlayer.setSource(&fskSource);

    std::cout << "Playing FSK signal... (Estimated time: " << (double)bits_to_send.size() / 1000.0 << " seconds)" << std::endl;

    std::cout << "Press any ENTER to start transmitting.\n";
    getchar();
    deviceManager.addAudioCallback(&audioSourcePlayer);

    while (!fskSource.isFinished())
    {
        juce::Thread::sleep(100);
    }
    std::cout << "Playback finished." << std::endl;

    deviceManager.removeAudioCallback(&audioSourcePlayer);
    return 0;
}
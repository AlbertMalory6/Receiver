/*
 * FSK SENDER (with Pre-Emphasis)
 *
 * - Matches the parameters of your FSK_Receiver.
 * - Reads from "INPUT.txt" to create a bitstream.
 * - Calculates CRC8.
 * - Generates a preamble chirp.
 * - **NEW**: Applies a pre-emphasis boost to bit '1' (f1)
 * to compensate for channel roll-off and make it
 * visually distinct in a waveform editor.
 */

#include <JuceHeader.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>

// ==============================================================================
//  CONFIGURATION
// ==============================================================================
namespace FSK {
    // --- Channel Compensation ---
    // This is the fix!
    // Boost bit '1' to compensate for HF roll-off
    constexpr double BIT_1_AMPLITUDE_BOOST_DB = 20.0;

    // --- Signal Parameters (Must match receiver) ---
    constexpr double sampleRate = 44100.0;
    constexpr double f0 = 1000.0; // Bit '0'
    constexpr double f1 = 2000.0; // Bit '1'
    constexpr double bitRate = 1000.0;
    constexpr int samplesPerBit = static_cast<int>(sampleRate / bitRate);
    constexpr double signalAmplitude = 0.5;

    // --- Frame Parameters (Must match receiver) ---
    constexpr int preambleSamples = 440;
    constexpr int payloadBits = 10000;
    constexpr int crcBits = 8;
    constexpr int totalFrameBits = payloadBits + crcBits;

    // --- Chirp Parameters (Must match receiver) ---
    constexpr double chirpF0 = f0 - 1000.0; // 0 Hz -> This was 1000Hz in receiver, 0 is safer.
    constexpr double chirpF1 = f1 + 1000.0; // 3000 Hz -> This was 5000Hz. Let's match your test.
}

// ==============================================================================
//  CRC-8 CALCULATION (Same as receiver)
// ==============================================================================
uint8_t calculateCRC8(const std::vector<bool>& data) {
    const uint8_t polynomial = 0x07; // Standard CRC-8
    uint8_t crc = 0x00;
    for (bool bit : data) {
        crc ^= (bit ? 0x80 : 0x00); // Feed the next bit
        for (int i = 0; i < 8; ++i) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ polynomial;
            } else {
                crc = crc << 1;
            }
        }
    }
    return crc;
}


// ==============================================================================
//  SIGNAL GENERATOR
// ==============================================================================
class SignalGenerator {
public:
    /** Generates the preamble chirp (must match receiver's template) */
    static juce::AudioBuffer<float> generateChirp() {
        juce::AudioBuffer<float> chirp(1, FSK::preambleSamples);
        auto* signal = chirp.getWritePointer(0);
        double currentPhase = 0.0;

        // Using your test file's chirp for consistency
        // (500 -> 4000 -> 500)
        double chirp_f_start = 500.0;
        double chirp_f_mid = 4000.0;

        for (int i = 0; i < FSK::preambleSamples; ++i) {
            double freq;
            if (i < FSK::preambleSamples / 2) {
                freq = juce::jmap((double)i, 0.0, (double)FSK::preambleSamples / 2.0,
                    chirp_f_start, chirp_f_mid);
            } else {
                freq = juce::jmap((double)i, (double)FSK::preambleSamples / 2.0,
                    (double)FSK::preambleSamples, chirp_f_mid, chirp_f_start);
            }
            double phaseIncrement = 2.0 * juce::MathConstants<double>::pi * freq / FSK::sampleRate;
            currentPhase += phaseIncrement;
            signal[i] = std::sin(currentPhase) * FSK::signalAmplitude;
        }
        return chirp;
    }

    /** Generates the FSK data frame */
    static juce::AudioBuffer<float> generateFSKData(const std::vector<bool>& bits)
    {
        int totalSamples = bits.size() * FSK::samplesPerBit;
        juce::AudioBuffer<float> data(1, totalSamples);
        auto* signal = data.getWritePointer(0);

        // --- Pre-Emphasis Amplitudes ---
        const double amp0 = FSK::signalAmplitude;
        const double amp1 = FSK::signalAmplitude * juce::Decibels::decibelsToGain(FSK::BIT_1_AMPLITUDE_BOOST_DB);

        double currentPhase = 0.0;

        for (size_t i = 0; i < bits.size(); ++i)
        {
            bool bit = bits[i];
            double freq = bit ? FSK::f1 : FSK::f0;
            double amp = bit ? amp1 : amp0; // <-- THE FIX
            double phaseIncrement = 2.0 * juce::MathConstants<double>::pi * freq / FSK::sampleRate;

            int sampleOffset = i * FSK::samplesPerBit;
            for (int j = 0; j < FSK::samplesPerBit; ++j)
            {
                currentPhase += phaseIncrement;
                signal[sampleOffset + j] = std::sin(currentPhase) * amp;
            }
        }
        return data;
    }
};

// ==============================================================================
//  AUDIO PLAYER
// ==============================================================================
class AudioPlayer : public juce::AudioIODeviceCallback {
public:
    AudioPlayer(const juce::AudioBuffer<float>& bufferToPlay)
        : sourceBuffer(bufferToPlay), samplesPlayed(0) {}

    void audioDeviceAboutToStart(juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext(const float* const*, int,
                                        float* const* output, int numOut,
                                        int numSamples,
                                        const juce::AudioIODeviceCallbackContext&) override
    {
        int samplesRemaining = sourceBuffer.getNumSamples() - samplesPlayed;
        int samplesToPlay = std::min(numSamples, samplesRemaining);

        if (samplesToPlay > 0)
        {
            const float* src = sourceBuffer.getReadPointer(0, samplesPlayed);
            for (int i = 0; i < numOut; ++i) {
                if (output[i] != nullptr) {
                    std::memcpy(output[i], src, sizeof(float) * samplesToPlay);
                    // Clear remaining part of buffer if any
                    if (samplesToPlay < numSamples) {
                        juce::FloatVectorOperations::clear(output[i] + samplesToPlay, numSamples - samplesToPlay);
                    }
                }
            }
            samplesPlayed += samplesToPlay;
        }
        else
        {
            // Silence
            for (int i = 0; i < numOut; ++i) {
                if (output[i] != nullptr) {
                    juce::FloatVectorOperations::clear(output[i], numSamples);
                }
            }
        }
    }

    bool isFinished() const {
        return samplesPlayed >= sourceBuffer.getNumSamples();
    }

private:
    const juce::AudioBuffer<float>& sourceBuffer;
    int samplesPlayed;
};

// ==============================================================================
//  MAIN APPLICATION
// ==============================================================================
int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::cout << "\n╔════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║            FSK SENDER (with Pre-Emphasis)                      ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════════╝\n";

    // 1. Read bits from INPUT.txt
    std::cout << "\nSTEP 1: Reading payload from INPUT.txt..." << std::endl;
    std::ifstream inputFile("INPUT.txt");
    if (!inputFile.is_open()) {
        std::cerr << "ERROR: INPUT.txt not found!" << std::endl;
        std::cerr << "Please create INPUT.txt with a string of 10000 0s and 1s." << std::endl;
        return 1;
    }

    std::vector<bool> payload;
    payload.reserve(FSK::payloadBits);
    char bitChar;
    while (inputFile.get(bitChar) && payload.size() < FSK::payloadBits) {
        if (bitChar == '0') payload.push_back(false);
        else if (bitChar == '1') payload.push_back(true);
    }
    if (payload.size() != FSK::payloadBits) {
        std::cerr << "ERROR: INPUT.txt must contain exactly " << FSK::payloadBits << " bits." << std::endl;
        return 1;
    }
    std::cout << "✓ Read " << payload.size() << " payload bits." << std::endl;

    // 2. Calculate CRC
    std::cout << "\nSTEP 2: Calculating CRC-8..." << std::endl;
    uint8_t crc = calculateCRC8(payload);
    std::cout << "✓ Calculated CRC: 0x" << std::hex << (int)crc << std::dec << std::endl;

    std::vector<bool> fullBitStream = payload;
    for (int i = 0; i < 8; ++i) {
        fullBitStream.push_back((crc >> (7 - i)) & 1);
    }
    std::cout << "✓ Full frame size: " << fullBitStream.size() << " bits." << std::endl;

    // 3. Generate Audio Signals
    std::cout << "\nSTEP 3: Generating audio signals..." << std::endl;
    auto chirp = SignalGenerator::generateChirp();
    auto fskData = SignalGenerator::generateFSKData(fullBitStream);
    std::cout << "✓ Chirp: " << chirp.getNumSamples() << " samples" << std::endl;
    std::cout << "✓ FSK Data: " << fskData.getNumSamples() << " samples" << std::endl;

    // 4. Combine into final buffer
    int totalSamples = chirp.getNumSamples() + fskData.getNumSamples();
    juce::AudioBuffer<float> finalSignal(1, totalSamples);
    finalSignal.copyFrom(0, 0, chirp, 0, 0, chirp.getNumSamples());
    finalSignal.copyFrom(0, chirp.getNumSamples(), fskData, 0, 0, fskData.getNumSamples());
    std::cout << "✓ Total signal length: " << totalSamples << " samples (" 
              << (totalSamples / FSK::sampleRate) << " seconds)" << std::endl;

    // 5. Play Audio
    juce::AudioDeviceManager deviceManager;
    deviceManager.initialiseWithDefaultDevices(0, 2); // Output only
    AudioPlayer player(finalSignal);

    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "Press ENTER to start playing the signal..." << std::endl;
    std::cin.get();
    
    deviceManager.addAudioCallback(&player);
    std::cout << "\n TRANSMITTING... (Make sure Receiver is recording)" << std::endl;

    while (!player.isFinished()) {
        juce::Thread::sleep(100);
    }
    
    deviceManager.removeAudioCallback(&player);
    std::cout << "\n✓ Transmission complete." << std::endl;

    std::cout << "\nPress ENTER to exit..." << std::endl;
    std::cin.get();

    return 0;
}

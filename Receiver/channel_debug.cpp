/*
 * CHANNEL_DEBUG.CPP - Test Signal Generator for Channel Diagnostics
 * 
 * PURPOSE: Generate specific test signals to diagnose channel issues
 * 
 * TEST SIGNALS:
 * 1. Multiple chirps with silence - Tests chirp detection sensitivity
 * 2. Continuous alternating bits (010101...) - Tests bit demodulation
 * 3. All ones frame - Tests high signal consistency
 * 4. All zeros frame - Tests low signal consistency
 * 5. Pattern frame (repeating 10101010) - Tests phase coherence
 * 
 * USAGE: Compile and run, choose test signal type, play through audio system
 *        Record on receiver, analyze detection quality
 * 
 * OUTPUT: Saves test signals to debug_pic/ASK/test_*.wav
 */

#include <JuceHeader.h>
#include <iostream>
#include <fstream>
#include <cmath>
#include <vector>

namespace TestSignal {
    constexpr double sampleRate = 44100.0;
    constexpr int preambleSamples = 440;
    constexpr double chirp_f_start = 2000.0;
    constexpr double chirp_f_end = 10000.0;
    constexpr double carrierFreq = 10000.0;
    constexpr int samplesPerBit = 44;
    constexpr int bitsPerFrame = 108;
    
    const std::string outputPath = "D:\\fourth_year\\cs120\\debug_pic\\ASK\\";
}

// ==============================================================================
//  SIGNAL GENERATORS
// ==============================================================================
class TestSignalGenerator {
public:
    /** Generate chirp preamble */
    static juce::AudioBuffer<float> generatePreamble() {
        juce::AudioBuffer<float> preamble(1, TestSignal::preambleSamples);
        auto* signal = preamble.getWritePointer(0);

        std::vector<double> freqSweep;
        for (int i = 0; i < TestSignal::preambleSamples / 2; ++i) {
            double freq = juce::jmap((double)i, 0.0, (double)(TestSignal::preambleSamples / 2 - 1),
                TestSignal::chirp_f_start, TestSignal::chirp_f_end);
            freqSweep.push_back(freq);
        }
        for (int i = TestSignal::preambleSamples / 2; i < TestSignal::preambleSamples; ++i) {
            double freq = juce::jmap((double)i, (double)(TestSignal::preambleSamples / 2),
                (double)(TestSignal::preambleSamples - 1),
                TestSignal::chirp_f_end, TestSignal::chirp_f_start);
            freqSweep.push_back(freq);
        }

        double currentPhase = 0.0;
        for (int i = 0; i < TestSignal::preambleSamples; ++i) {
            double phaseIncrement = 2.0 * juce::MathConstants<double>::pi * freqSweep[i] / TestSignal::sampleRate;
            currentPhase += phaseIncrement;
            signal[i] = std::sin(currentPhase);
        }

        return preamble;
    }

    /** Generate carrier-modulated bit pattern */
    static juce::AudioBuffer<float> generateModulatedBits(const std::vector<bool>& bits) {
        int numSamples = (int)bits.size() * TestSignal::samplesPerBit;
        juce::AudioBuffer<float> buffer(1, numSamples);
        auto* signal = buffer.getWritePointer(0);

        double phase = 0.0;
        double phaseIncrement = 2.0 * juce::MathConstants<double>::pi * TestSignal::carrierFreq / TestSignal::sampleRate;

        for (size_t bitIdx = 0; bitIdx < bits.size(); ++bitIdx) {
            bool bitValue = bits[bitIdx];
            int bitStart = (int)bitIdx * TestSignal::samplesPerBit;
            
            for (int i = 0; i < TestSignal::samplesPerBit; ++i) {
                signal[bitStart + i] = std::sin(phase) * (bitValue ? 1.0f : -1.0f);
                phase += phaseIncrement;
                if (phase > 2.0 * juce::MathConstants<double>::pi) {
                    phase -= 2.0 * juce::MathConstants<double>::pi;
                }
            }
        }

        return buffer;
    }

    /** Generate silence */
    static juce::AudioBuffer<float> generateSilence(int numSamples) {
        juce::AudioBuffer<float> buffer(1, numSamples);
        buffer.clear();
        return buffer;
    }

    /** Generate white noise */
    static juce::AudioBuffer<float> generateNoise(int numSamples, float amplitude = 0.1f) {
        juce::AudioBuffer<float> buffer(1, numSamples);
        auto* signal = buffer.getWritePointer(0);
        
        juce::Random random;
        for (int i = 0; i < numSamples; ++i) {
            signal[i] = (random.nextFloat() * 2.0f - 1.0f) * amplitude;
        }

        return buffer;
    }
};

// ==============================================================================
//  TEST SIGNAL BUILDERS
// ==============================================================================
class TestSignalBuilder {
public:
    /** Test 1: Multiple chirps with silence between - NO DATA */
    static juce::AudioBuffer<float> buildChirpDetectionTest(int numChirps = 100, int silenceBetween = 5000) {
        auto chirp = TestSignalGenerator::generatePreamble();
        
        std::vector<float> signal;
        
        std::cout << "\nBuilding Chirp-Only Detection Test:" << std::endl;
        std::cout << "  Number of chirps: " << numChirps << std::endl;
        std::cout << "  Silence between: " << silenceBetween << " samples (" 
                  << (silenceBetween / 44100.0 * 1000) << " ms)" << std::endl;
        std::cout << "  Total duration: " << ((numChirps * (440 + silenceBetween)) / 44100.0) << " seconds" << std::endl;

        for (int i = 0; i < numChirps; ++i) {
            // Add chirp (NO noise before, for clean detection)
            for (int j = 0; j < chirp.getNumSamples(); ++j) {
                signal.push_back(chirp.getSample(0, j));
            }

            // Add silence after
            for (int j = 0; j < silenceBetween; ++j) {
                signal.push_back(0.0f);
            }
        }

        juce::AudioBuffer<float> result(1, (int)signal.size());
        for (size_t i = 0; i < signal.size(); ++i) {
            result.setSample(0, (int)i, signal[i]);
        }

        return result;
    }
    
    /** Test 1b: Continuous chirps with minimal gap - stress test */
    static juce::AudioBuffer<float> buildContinuousChirpsTest(int numChirps = 50) {
        auto chirp = TestSignalGenerator::generatePreamble();
        
        std::vector<float> signal;
        
        std::cout << "\nBuilding Continuous Chirps Test:" << std::endl;
        std::cout << "  Number of chirps: " << numChirps << std::endl;
        std::cout << "  Gap between: 100 samples (2.3 ms)" << std::endl;

        for (int i = 0; i < numChirps; ++i) {
            // Add chirp
            for (int j = 0; j < chirp.getNumSamples(); ++j) {
                signal.push_back(chirp.getSample(0, j));
            }

            // Minimal gap (just enough for detector to reset)
            for (int j = 0; j < 100; ++j) {
                signal.push_back(0.0f);
            }
        }

        juce::AudioBuffer<float> result(1, (int)signal.size());
        for (size_t i = 0; i < signal.size(); ++i) {
            result.setSample(0, (int)i, signal[i]);
        }

        return result;
    }

    /** Test 2: Continuous alternating bits (010101...) */
    static juce::AudioBuffer<float> buildAlternatingBitsTest(int numBits = 1000) {
        std::vector<bool> bits(numBits);
        for (int i = 0; i < numBits; ++i) {
            bits[i] = (i % 2 == 1);
        }

        std::cout << "\nBuilding Alternating Bits Test Signal:" << std::endl;
        std::cout << "  Number of bits: " << numBits << " (pattern: 010101...)" << std::endl;

        auto modulated = TestSignalGenerator::generateModulatedBits(bits);
        
        // Add chirp at the beginning for sync
        auto chirp = TestSignalGenerator::generatePreamble();
        juce::AudioBuffer<float> result(1, chirp.getNumSamples() + modulated.getNumSamples());
        
        for (int i = 0; i < chirp.getNumSamples(); ++i) {
            result.setSample(0, i, chirp.getSample(0, i));
        }
        for (int i = 0; i < modulated.getNumSamples(); ++i) {
            result.setSample(0, chirp.getNumSamples() + i, modulated.getSample(0, i));
        }

        return result;
    }

    /** Test 3: All ones frame */
    static juce::AudioBuffer<float> buildAllOnesTest(int numBits = 108) {
        std::vector<bool> bits(numBits, true);

        std::cout << "\nBuilding All-Ones Test Signal:" << std::endl;
        std::cout << "  Number of bits: " << numBits << " (all 1s)" << std::endl;

        auto chirp = TestSignalGenerator::generatePreamble();
        auto modulated = TestSignalGenerator::generateModulatedBits(bits);
        
        juce::AudioBuffer<float> result(1, chirp.getNumSamples() + modulated.getNumSamples());
        
        for (int i = 0; i < chirp.getNumSamples(); ++i) {
            result.setSample(0, i, chirp.getSample(0, i));
        }
        for (int i = 0; i < modulated.getNumSamples(); ++i) {
            result.setSample(0, chirp.getNumSamples() + i, modulated.getSample(0, i));
        }

        return result;
    }

    /** Test 4: All zeros frame */
    static juce::AudioBuffer<float> buildAllZerosTest(int numBits = 108) {
        std::vector<bool> bits(numBits, false);

        std::cout << "\nBuilding All-Zeros Test Signal:" << std::endl;
        std::cout << "  Number of bits: " << numBits << " (all 0s)" << std::endl;

        auto chirp = TestSignalGenerator::generatePreamble();
        auto modulated = TestSignalGenerator::generateModulatedBits(bits);
        
        juce::AudioBuffer<float> result(1, chirp.getNumSamples() + modulated.getNumSamples());
        
        for (int i = 0; i < chirp.getNumSamples(); ++i) {
            result.setSample(0, i, chirp.getSample(0, i));
        }
        for (int i = 0; i < modulated.getNumSamples(); ++i) {
            result.setSample(0, chirp.getNumSamples() + i, modulated.getSample(0, i));
        }

        return result;
    }

    /** Test 5: Repeating pattern (11001100...) */
    static juce::AudioBuffer<float> buildPatternTest(const std::vector<bool>& pattern, int repeats = 100) {
        std::vector<bool> bits;
        for (int i = 0; i < repeats; ++i) {
            bits.insert(bits.end(), pattern.begin(), pattern.end());
        }

        std::cout << "\nBuilding Pattern Test Signal:" << std::endl;
        std::cout << "  Pattern length: " << pattern.size() << " bits" << std::endl;
        std::cout << "  Repeats: " << repeats << std::endl;
        std::cout << "  Total bits: " << bits.size() << std::endl;

        auto chirp = TestSignalGenerator::generatePreamble();
        auto modulated = TestSignalGenerator::generateModulatedBits(bits);
        
        juce::AudioBuffer<float> result(1, chirp.getNumSamples() + modulated.getNumSamples());
        
        for (int i = 0; i < chirp.getNumSamples(); ++i) {
            result.setSample(0, i, chirp.getSample(0, i));
        }
        for (int i = 0; i < modulated.getNumSamples(); ++i) {
            result.setSample(0, chirp.getNumSamples() + i, modulated.getSample(0, i));
        }

        return result;
    }
};

// ==============================================================================
//  AUDIO PLAYER
// ==============================================================================
class SimplePlayer : public juce::AudioIODeviceCallback {
public:
    SimplePlayer(const juce::AudioBuffer<float>& bufferToPlay)
        : sourceBuffer(bufferToPlay), samplesPlayed(0) {}

    void audioDeviceAboutToStart(juce::AudioIODevice*) override {
        samplesPlayed = 0;
    }

    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
        float* const* outputChannelData, int numOutputChannels,
        int numSamples,
        const juce::AudioIODeviceCallbackContext& context) override {
        juce::ignoreUnused(inputChannelData, numInputChannels, context);

        int samplesRemaining = sourceBuffer.getNumSamples() - samplesPlayed;
        int samplesToPlay = std::min(numSamples, samplesRemaining);

        if (samplesToPlay > 0) {
            const float* src = sourceBuffer.getReadPointer(0, samplesPlayed);
            for (int i = 0; i < numOutputChannels; ++i) {
                if (outputChannelData[i] != nullptr) {
                    std::memcpy(outputChannelData[i], src, sizeof(float) * samplesToPlay);
                    if (samplesToPlay < numSamples) {
                        juce::FloatVectorOperations::clear(outputChannelData[i] + samplesToPlay,
                            numSamples - samplesToPlay);
                    }
                }
            }
            samplesPlayed += samplesToPlay;
        }
        else {
            for (int i = 0; i < numOutputChannels; ++i) {
                if (outputChannelData[i] != nullptr) {
                    juce::FloatVectorOperations::clear(outputChannelData[i], numSamples);
                }
            }
        }
    }

    bool isFinished() const { return samplesPlayed >= sourceBuffer.getNumSamples(); }

private:
    const juce::AudioBuffer<float>& sourceBuffer;
    int samplesPlayed;
};

// ==============================================================================
//  FILE UTILITIES
// ==============================================================================
void saveWavFile(const juce::AudioBuffer<float>& buffer, const juce::File& file) {
    juce::WavAudioFormat wavFormat;
    if (std::unique_ptr<juce::OutputStream> fileStream{ file.createOutputStream() }) {
        using Opts = juce::AudioFormatWriterOptions;
        if (auto writer = wavFormat.createWriterFor(fileStream.release(),
            TestSignal::sampleRate, 1, 16, {}, 0)) {
            writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
            std::cout << "✓ Saved to: " << file.getFullPathName() << std::endl;
            std::cout << "  Samples: " << buffer.getNumSamples() << std::endl;
            std::cout << "  Duration: " << std::fixed << std::setprecision(2) 
                      << (buffer.getNumSamples() / TestSignal::sampleRate) << " seconds" << std::endl;
        }
    }
}

// ==============================================================================
//  MAIN
// ==============================================================================
int main(int argc, char* argv[]) {
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::cout << "\n╔════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║         CHANNEL DEBUG - TEST SIGNAL GENERATOR                 ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════════╝\n";

    // Ensure output directory exists
    juce::File outputDir(TestSignal::outputPath);
    if (!outputDir.exists()) {
        outputDir.createDirectory();
    }

    bool running = true;
    while (running) {
        std::cout << "\n" << std::string(60, '=') << std::endl;
        std::cout << "SELECT TEST SIGNAL:" << std::endl;
        std::cout << "  1. Chirps ONLY - 100 chirps (RECOMMENDED for channel test)" << std::endl;
        std::cout << "  2. Continuous chirps - 50 chirps (stress test)" << std::endl;
        std::cout << "  3. Alternating bits 010101... (test bit demodulation)" << std::endl;
        std::cout << "  4. All ones frame (test high signal)" << std::endl;
        std::cout << "  5. All zeros frame (test low signal)" << std::endl;
        std::cout << "  6. Pattern 11001100... (test phase coherence)" << std::endl;
        std::cout << "  0. Exit" << std::endl;
        std::cout << "Enter choice: ";

        int choice;
        std::cin >> choice;
        std::cin.ignore();

        if (choice == 0) {
            running = false;
            continue;
        }

        juce::AudioBuffer<float> testSignal;
        std::string filename;

        switch (choice) {
        case 1:
            testSignal = TestSignalBuilder::buildChirpDetectionTest(100, 5000);
            filename = "test_chirps_only.wav";
            break;
        case 2:
            testSignal = TestSignalBuilder::buildContinuousChirpsTest(50);
            filename = "test_chirps_continuous.wav";
            break;
        case 3:
            testSignal = TestSignalBuilder::buildAlternatingBitsTest(1000);
            filename = "test_alternating.wav";
            break;
        case 4:
            testSignal = TestSignalBuilder::buildAllOnesTest(108);
            filename = "test_all_ones.wav";
            break;
        case 5:
            testSignal = TestSignalBuilder::buildAllZerosTest(108);
            filename = "test_all_zeros.wav";
            break;
        case 6: {
            std::vector<bool> pattern = {true, true, false, false};
            testSignal = TestSignalBuilder::buildPatternTest(pattern, 100);
            filename = "test_pattern.wav";
            break;
        }
        default:
            std::cout << "Invalid choice." << std::endl;
            continue;
        }

        // Save the signal
        saveWavFile(testSignal, outputDir.getChildFile(filename));

        // Ask if user wants to play it
        std::cout << "\nPlay this signal? (y/n): ";
        char response;
        std::cin >> response;
        std::cin.ignore();

        if (response == 'y' || response == 'Y') {
            // Setup audio device
            juce::AudioDeviceManager deviceManager;
            auto result = deviceManager.initialiseWithDefaultDevices(0, 2);
            if (result.isNotEmpty()) {
                std::cerr << "ERROR: " << result << std::endl;
                continue;
            }

            std::cout << "Press ENTER to start playback..." << std::endl;
            std::cin.get();

            SimplePlayer player(testSignal);
            deviceManager.addAudioCallback(&player);

            std::cout << "... PLAYING ...\nPress ENTER to stop." << std::endl;
            std::cin.get();

            deviceManager.removeAudioCallback(&player);
            std::cout << "Playback stopped." << std::endl;
        }

        std::cout << "\nGenerate another test signal? (y/n): ";
        std::cin >> response;
        std::cin.ignore();
        running = (response == 'y' || response == 'Y');
    }

    std::cout << "\nExiting..." << std::endl;
    return 0;
}


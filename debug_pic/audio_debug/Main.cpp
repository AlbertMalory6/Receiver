/*
 * PRECISE AUDIO QUALITY TESTER (v4 - Recording Logic Corrected)
 *
 * This version corrects a critical flaw in the audio recording logic.
 *
 * THE FIX:
 * 1.  Single-Channel Recording: Instead of summing all available input
 * channels (which mixes in noise and unwanted signals), this version
 * records *only* from the first available input channel (inputChannelData[0]).
 * This is the standard and correct approach for capturing a clean signal.
 *
 * 2.  Efficient Block Copying: The inefficient sample-by-sample `for` loop
 * has been replaced with `recordedAudio.copyFrom()`. This is a highly
 * optimized, single function call that copies a block of audio data,
 * reducing CPU overhead and potential for errors.
 *
 * 3.  Robust Buffer Handling: The code now calculates the exact number of
 * samples to copy in each block to prevent any possibility of writing
 * past the end of the destination buffer.
 */

#include <JuceHeader.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <string>

 // ==============================================================================
 //  CONFIGURATION
 // ==============================================================================
namespace Config {
    constexpr double desiredSampleRate = 44100.0;
    constexpr int desiredBufferSize = 512;
    constexpr int durationSeconds = 5;
    // IMPORTANT: Change this to a valid path on your system!
    const std::string outputPath = "D:\\fourth_year\\cs120\\debug_pic\\audio_debug\\";
    //"D:\fourth_year\cs120\debug_pic\audio_debug\recorded_audio.wav"
}

// ==============================================================================
//  HELPER FUNCTIONS
// ==============================================================================
#pragma region HelperFunctions
juce::AudioBuffer<float> generateTestAudio() {
    int numSamples = static_cast<int>(Config::desiredSampleRate * Config::durationSeconds);
    juce::AudioBuffer<float> buffer(1, numSamples);
    auto* signal = buffer.getWritePointer(0);
    double currentPhase = 0.0;
    std::cout << "Generating test audio signal (" << numSamples << " samples)..." << std::endl;

    for (int i = 0; i < numSamples; ++i) {
        double time = i / Config::desiredSampleRate;
        double freq = 0.0;
        if (time < 0.5) freq = 500.0;
        else if (time >= 0.75 && time < 1.25) freq = 1500.0;
        else if (time >= 1.5 && time < 2.0) freq = 3000.0;
        else if (time >= 2.5 && time < 3.5) freq = juce::jmap(time, 2.5, 3.5, 500.0, 4000.0);
        else if (time >= 3.5 && time < 4.5) freq = juce::jmap(time, 3.5, 4.5, 4000.0, 500.0);

        if (freq > 0.0) {
            double phaseIncrement = 2.0 * juce::MathConstants<double>::pi * freq / Config::desiredSampleRate;
            currentPhase += phaseIncrement;
            signal[i] = static_cast<float>(std::sin(currentPhase) * 0.5);
        }
        else {
            signal[i] = 0.0f;
            currentPhase = 0.0;
        }
    }
    return buffer;
}

bool saveWavFile(const std::string& filePath, const juce::AudioBuffer<float>& buffer, double sampleRate) {
    juce::File outFile(filePath);
    if (outFile.exists()) outFile.deleteFile();

    auto range = buffer.findMinMax(0, 0, buffer.getNumSamples());
    std::cout << "\nSaving buffer to " << filePath << std::endl;
    std::cout << "  - NumSamples: " << buffer.getNumSamples() << std::endl;
    std::cout << "  - SampleRate: " << sampleRate << std::endl;
    std::cout << "  - Buffer Range: " << range.getStart() << " to " << range.getEnd() << std::endl;
    if (range.getStart() == 0.0f && range.getEnd() == 0.0f)
    {
        std::cout << "  - >>> WARNING: Buffer is completely silent! <<<" << std::endl;
    }

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wavFormat.createWriterFor(new juce::FileOutputStream(outFile),
            sampleRate, 1, 16, {}, 0));
    if (writer != nullptr) {
        writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
        std::cout << "  -> Successfully saved." << std::endl;
        return true;
    }
    std::cerr << "  -> ERROR: Could not create writer for file." << std::endl;
    return false;
}
#pragma endregion

// ==============================================================================
//  AUDIO TESTER WITH CORRECTED RECORDING LOGIC
// ==============================================================================
class AudioTester : public juce::AudioIODeviceCallback {
public:
    AudioTester(const juce::AudioBuffer<float>& source)
        : audioToPlay(source), actualSampleRate(Config::desiredSampleRate) {
        recordedAudio.setSize(1, source.getNumSamples());
        recordedAudio.clear();
    }

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override {
        actualSampleRate = device->getCurrentSampleRate();
        std::cout << "\n" << std::string(50, '*') << std::endl;
        std::cout << "AUDIO DEVICE STARTED" << std::endl;
        std::cout << "  Actual Sample Rate: " << actualSampleRate << " Hz" << std::endl;
        std::cout << "  Actual Buffer Size: " << device->getCurrentBufferSizeSamples() << " samples" << std::endl;
        std::cout << std::string(50, '*') << "\n" << std::endl;

        if (actualSampleRate != Config::desiredSampleRate) {
            std::cout << "WARNING: Sample Rate Mismatch!" << std::endl;
            int actualSamples = static_cast<int>(actualSampleRate * Config::durationSeconds);
            recordedAudio.setSize(1, actualSamples, false, true, true);
            recordedAudio.clear();
            std::cout << "  Resized recording buffer to " << actualSamples << " samples." << std::endl;
        }
    }

    void audioDeviceStopped() override {}

    // ---
    // !!! THIS FUNCTION CONTAINS THE CORRECTED RECORDING LOGIC !!!
    // ---
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
        float* const* outputChannelData, int numOutputChannels,
        int numSamples,
        const juce::AudioIODeviceCallbackContext&) {

        // --- Recording (Corrected) ---
        // We only record if there is at least one input channel available and its data pointer is valid.
        if (numInputChannels > 0 && inputChannelData[0] != nullptr)
        {
            // Check if there is still space left in our recording buffer.
            if (samplesRecorded < recordedAudio.getNumSamples())
            {
                // Calculate how many samples we can safely copy without overflowing the buffer.
                int samplesToCopy = std::min(numSamples, recordedAudio.getNumSamples() - samplesRecorded);

                // **THE FIX**: Copy a block of audio from the *first input channel only*.
                // This avoids summing channels, which was the source of noise and interference.
                // This method is also far more efficient than a sample-by-sample loop.
                recordedAudio.copyFrom(0,                  // Destination channel index
                    samplesRecorded,    // Start sample in destination
                    inputChannelData[0],// Source buffer (channel 0)
                    samplesToCopy);     // Number of samples to copy

                // Increment our counter by the number of samples we actually copied.
                samplesRecorded += samplesToCopy;
            }
        }

        // --- Playback (Logic from original test harness) ---
        // This part plays the generated test tone.
        if (samplesPlayed < audioToPlay.getNumSamples()) {
            int samplesToPlay = std::min(numSamples, audioToPlay.getNumSamples() - samplesPlayed);

            for (int chan = 0; chan < numOutputChannels; ++chan) {
                if (outputChannelData[chan] != nullptr)
                {
                    // 复制音频数据到输出缓冲区
                    std::memcpy(outputChannelData[chan],
                        audioToPlay.getReadPointer(0, samplesPlayed),
                        sizeof(float) * samplesToPlay);
                }
            }
            samplesPlayed += samplesToPlay;
        }
        else {
            // If playback is finished, just output silence.
            for (int chan = 0; chan < numOutputChannels; ++chan) {
                if (outputChannelData[chan] != nullptr) {
                    juce::FloatVectorOperations::clear(outputChannelData[chan], numSamples);
                }
            }
        }
    }

    const juce::AudioBuffer<float>& getRecording() const { return recordedAudio; }
    double getActualSampleRate() const { return actualSampleRate; }

private:
    const juce::AudioBuffer<float>& audioToPlay;
    juce::AudioBuffer<float> recordedAudio;
    int samplesPlayed{ 0 };
    int samplesRecorded{ 0 };
    double actualSampleRate;
};

// ==============================================================================
//  MAIN APPLICATION
// ==============================================================================
int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::cout << "\n╔══════════════════════════════════════════╗\n";
    std::cout << "║   PRECISE AUDIO RECORDING TEST (v4-FIXED)  ║\n";
    std::cout << "╚══════════════════════════════════════════╝\n\n";

    auto originalAudio = generateTestAudio();
    saveWavFile(Config::outputPath + "original_audio.wav", originalAudio, Config::desiredSampleRate);

    juce::AudioDeviceManager deviceManager;

    auto setup = deviceManager.getAudioDeviceSetup();
    setup.sampleRate = Config::desiredSampleRate;
    setup.bufferSize = Config::desiredBufferSize;
    // Request at least 1 input channel and 2 output channels
    setup.inputChannels.setRange(0, 1, true);
    setup.outputChannels.setRange(0, 2, true);

    juce::String initError = deviceManager.initialise(
        setup.inputChannels.countNumberOfSetBits(),
        setup.outputChannels.countNumberOfSetBits(),
        nullptr, true, "", &setup
    );

    if (!initError.isEmpty()) {
        std::cerr << "ERROR initializing device: " << initError.toStdString() << std::endl;
        return 1;
    }

    std::cout << "Device initialized with requested settings:" << std::endl;
    std::cout << "  Input Device:  " << deviceManager.getCurrentAudioDevice()->getName() << std::endl;
    std::cout << "  Output Device: " << deviceManager.getCurrentAudioDevice()->getName() << std::endl;


    AudioTester tester(originalAudio);

    std::cout << "\n>>> IMPORTANT: Connect audio output to input for loopback test. <<<" << std::endl;
    std::cout << ">>> 1. Set your 'Line In' or 'Stereo Mix' as the DEFAULT Recording Device." << std::endl;
    std::cout << ">>> 2. Ensure its volume is NOT muted." << std::endl;
    std::cout << ">>> Press ENTER to start the " << Config::durationSeconds << "-second play/record test..." << std::endl;
    std::cin.get();

    deviceManager.addAudioCallback(&tester);
    std::cout << "\n🔴 Playing and Recording... Please wait." << std::endl;

    juce::Thread::sleep((Config::durationSeconds + 1) * 1000);

    deviceManager.removeAudioCallback(&tester);
    std::cout << "\n✓ Test finished." << std::endl;

    saveWavFile(Config::outputPath + "recorded_audio.wav",
        tester.getRecording(),
        tester.getActualSampleRate());

    std::cout << "\nTest complete. The 'recorded_audio.wav' should now be clean." << std::endl;
    std::cout << "Press ENTER to exit..." << std::endl;
    std::cin.get();

    return 0;
}

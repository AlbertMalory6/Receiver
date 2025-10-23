/*
 * STANDALONE AUDIO QUALITY TESTER
 *
 * This program performs an audio loopback test to verify recording quality.
 *
 * How it works:
 * 1. Generates a test audio signal with distinct tones and a chirp.
 * 2. Saves this original signal to 'original_audio.wav'.
 * 3. Plays the signal through the default audio output device.
 * 4. Simultaneously records from the default audio input device.
 * 5. Saves the recorded signal to 'recorded_audio.wav'.
 *
 * !!! IMPORTANT !!!
 * You MUST connect your audio output to your audio input for this test to work.
 * This can be done with:
 * - A physical audio cable (line-out to line-in).
 * - A virtual audio cable software (e.g., VB-CABLE, VoiceMeeter).
 * - Your system's "Stereo Mix" or "What U Hear" recording device if available.
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
    constexpr double sampleRate = 44100.0;
    constexpr int durationSeconds = 5;
    constexpr int numSamples = static_cast<int>(sampleRate * durationSeconds);
    const std::string outputPath = "D:\\fourth_year\\cs120\\debug_pic\\audio_debug\\";
}

// ==============================================================================
//  HELPER FUNCTIONS
// ==============================================================================

/** Generates the test audio signal. */
juce::AudioBuffer<float> generateTestAudio() {
    juce::AudioBuffer<float> buffer(1, Config::numSamples);
    auto* signal = buffer.getWritePointer(0);
    double currentPhase = 0.0;

    std::cout << "Generating test audio signal..." << std::endl;

    for (int i = 0; i < Config::numSamples; ++i) {
        double time = i / Config::sampleRate;
        double freq = 0.0;

        if (time < 0.5) freq = 500.0;         // Tone 1
        else if (time >= 0.75 && time < 1.25) freq = 1500.0; // Tone 2
        else if (time >= 1.5 && time < 2.0) freq = 3000.0; // Tone 3
        else if (time >= 2.5 && time < 3.5) { // Up-chirp
            freq = juce::jmap(time, 2.5, 3.5, 500.0, 4000.0);
        }
        else if (time >= 3.5 && time < 4.5) { // Down-chirp
            freq = juce::jmap(time, 3.5, 4.5, 4000.0, 500.0);
        }

        if (freq > 0.0) {
            double phaseIncrement = 2.0 * juce::MathConstants<double>::pi * freq / Config::sampleRate;
            currentPhase += phaseIncrement;
            signal[i] = static_cast<float>(std::sin(currentPhase) * 0.5);
        }
        else {
            signal[i] = 0.0f; // Silence
            currentPhase = 0.0; // Reset phase during silence
        }
    }
    std::cout << "Test signal generated." << std::endl;
    return buffer;
}

/** Saves an audio buffer to a WAV file. */
bool saveWavFile(const std::string& filePath, const juce::AudioBuffer<float>& buffer) {
    juce::File outFile(filePath);
    if (outFile.exists()) {
        outFile.deleteFile();
    }
    
    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wavFormat.createWriterFor(new juce::FileOutputStream(outFile),
            Config::sampleRate, 1, 16, {}, 0));

    if (writer != nullptr) {
        writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
        std::cout << "Successfully saved audio to: " << filePath << std::endl;
        return true;
    }

    std::cerr << "ERROR: Could not save audio to: " << filePath << std::endl;
    return false;
}


// ==============================================================================
//  AUDIO LOOPBACK TESTER
// ==============================================================================
class AudioLoopbackTester : public juce::AudioIODeviceCallback {
public:
    AudioLoopbackTester(const juce::AudioBuffer<float>& source)
        : audioToPlay(source) {
        recordedAudio.setSize(1, source.getNumSamples());
        recordedAudio.clear();
    }

    void audioDeviceAboutToStart(juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
        float* const* outputChannelData, int numOutputChannels,
        int numSamples,
        const juce::AudioIODeviceCallbackContext&) override {
        // --- Recording ---
        if (numInputChannels > 0 && inputChannelData[0] != nullptr) {
            if (samplesRecorded + numSamples <= recordedAudio.getNumSamples()) {
                recordedAudio.copyFrom(0, samplesRecorded, inputChannelData[0], numSamples);
            }
        }
        samplesRecorded += numSamples;

        // --- Playback ---
        if (numOutputChannels > 0 && outputChannelData[0] != nullptr) {
            if (samplesPlayed + numSamples <= audioToPlay.getNumSamples()) {
                // Copy from source to output
                for (int chan = 0; chan < numOutputChannels; ++chan) {
                     outputChannelData[chan] = audioToPlay.getReadPointer(0, samplesPlayed);
                }
            } else {
                // Clear remaining buffer to avoid noise
                for(int chan = 0; chan < numOutputChannels; ++chan) {
                    juce::FloatVectorOperations::clear(outputChannelData[chan], numSamples);
                }
            }
        }
        samplesPlayed += numSamples;
    }

    const juce::AudioBuffer<float>& getRecording() const { return recordedAudio; }

private:
    const juce::AudioBuffer<float>& audioToPlay;
    juce::AudioBuffer<float> recordedAudio;
    int samplesPlayed{ 0 };
    int samplesRecorded{ 0 };
};


// ==============================================================================
//  MAIN APPLICATION
// ==============================================================================
int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::cout << "\n╔══════════════════════════════════════════╗\n";
    std::cout << "║         AUDIO RECORDING QUALITY TEST         ║\n";
    std::cout << "╚══════════════════════════════════════════╝\n\n";

    // 1. Generate and save the original audio
    auto originalAudio = generateTestAudio();
    saveWavFile(Config::outputPath + "original_audio.wav", originalAudio);

    // 2. Set up audio device for playback and recording
    juce::AudioDeviceManager deviceManager;
    deviceManager.initialiseWithDefaultDevices(1, 2); // 1 input, 2 outputs

    AudioLoopbackTester tester(originalAudio);

    std::cout << "\n>>> IMPORTANT: Connect audio output to input now. <<<" << std::endl;
    std::cout << "Press ENTER to start the " << Config::durationSeconds << "-second play/record test..." << std::endl;
    std::cin.get();

    deviceManager.addAudioCallback(&tester);
    std::cout << "\n🔴 Playing and Recording... Please wait." << std::endl;

    // Wait for the test duration + a small buffer
    juce::Thread::sleep((Config::durationSeconds + 1) * 1000);

    deviceManager.removeAudioCallback(&tester);
    std::cout << "\n✓ Test finished." << std::endl;

    // 3. Save the recorded audio
    saveWavFile(Config::outputPath + "recorded_audio.wav", tester.getRecording());

    std::cout << "\nTest complete. You can now run the Python script to compare the waveforms." << std::endl;
    std::cout << "Press ENTER to exit..." << std::endl;
    std::cin.get();

    return 0;
}
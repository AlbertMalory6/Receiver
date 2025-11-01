/*
 * CHIRP DETECTION - STANDALONE TEST
 *
 * This program performs an isolated test of the chirp detection logic:
 * 1. Generates a test signal: [0.5s Silence] + [Click] + [Chirp] + [Click]
 * 2. Saves the generated signal
 * 3. Plays and records simultaneously
 * 4. Saves the recorded signal
 * 5. Analyzes recording and logs correlation scores throughout
 * 6. Saves correlation data to CSV
 *
 * All files saved to: D:\fourth_year\cs120\debug_pic\chirp\
 */

#include <JuceHeader.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <iomanip>

// ==============================================================================
//  CONFIGURATION
// ==============================================================================
#define USE_NCC_DETECTION 1 // 1 = NCC, 0 = Dot Product

namespace ChirpTest {
    constexpr double sampleRate = 48000.0;
    constexpr int preambleSamples = 440;
    
    // Chirp parameters (500-4000-500 Hz triangular sweep)
    constexpr double chirp_f_start = 500.0;
    constexpr double chirp_f_mid = 4000.0;
    
    // Output path
    const std::string outputPath = "D:\\fourth_year\\cs120\\debug_pic\\chirp\\";
}

// ==============================================================================
//  SIGNAL GENERATOR
// ==============================================================================
class SignalGenerator {
public:
    /** Generates the preamble chirp */
    static juce::AudioBuffer<float> generateChirp() {
        juce::AudioBuffer<float> chirp(1, ChirpTest::preambleSamples);
        auto* signal = chirp.getWritePointer(0);
        
        double currentPhase = 0.0;
        for (int i = 0; i < ChirpTest::preambleSamples; ++i) {
            double freq;
            if (i < ChirpTest::preambleSamples / 2) {
                freq = juce::jmap((double)i, 0.0, (double)ChirpTest::preambleSamples / 2.0,
                    ChirpTest::chirp_f_start, ChirpTest::chirp_f_mid);
            } else {
                freq = juce::jmap((double)i, (double)ChirpTest::preambleSamples / 2.0,
                    (double)ChirpTest::preambleSamples, ChirpTest::chirp_f_mid, ChirpTest::chirp_f_start);
            }
            double phaseIncrement = 2.0 * juce::MathConstants<double>::pi * freq / ChirpTest::sampleRate;
            currentPhase += phaseIncrement;
            signal[i] = std::sin(currentPhase) * 0.5;
        }
        return chirp;
    }
    
    /** Generates a short, loud click marker */
    static juce::AudioBuffer<float> generateClick(int numSamples = 5) {
        juce::AudioBuffer<float> click(1, numSamples);
        juce::FloatVectorOperations::fill(click.getWritePointer(0), 0.8f, numSamples);
        return click;
    }
    
    /** Generates silence */
    static juce::AudioBuffer<float> generateSilence(int numSamples) {
        juce::AudioBuffer<float> silence(1, numSamples);
        silence.clear();
        return silence;
    }
};

// ==============================================================================
//  AUDIO HANDLER (Combined Playback and Recording)
// ==============================================================================
class AudioHandler : public juce::AudioIODeviceCallback {
public:
    AudioHandler(const juce::AudioBuffer<float>& bufferToPlay, int durationSeconds)
        : sourceBuffer(bufferToPlay), samplesPlayed(0) {
        int requiredSamples = static_cast<int>(ChirpTest::sampleRate * durationSeconds);
        recordedAudio.setSize(1, requiredSamples);
        recordedAudio.clear();
        samplesRecorded = 0;
    }
    
    void audioDeviceAboutToStart(juce::AudioIODevice*) override {
        samplesPlayed = 0;  // Reset playback position
        samplesRecorded = 0;  // Reset recording position
    }
    
    void audioDeviceStopped() override {}
    
    void audioDeviceIOCallbackWithContext(const float* const* input, int numIn,
        float* const* output, int numOut,
        int numSamples,
        const juce::AudioIODeviceCallbackContext&) override {
        // Handle playback (output)
        int samplesRemaining = sourceBuffer.getNumSamples() - samplesPlayed;
        int samplesToPlay = std::min(numSamples, samplesRemaining);
        
        if (samplesToPlay > 0) {
            const float* src = sourceBuffer.getReadPointer(0, samplesPlayed);
            for (int i = 0; i < numOut; ++i) {
                if (output[i] != nullptr) {
                    std::memcpy(output[i], src, sizeof(float) * samplesToPlay);
                    if (samplesToPlay < numSamples) {
                        juce::FloatVectorOperations::clear(output[i] + samplesToPlay, numSamples - samplesToPlay);
                    }
                }
            }
            samplesPlayed += samplesToPlay;
        } else {
            // Playback finished, output silence
            for (int i = 0; i < numOut; ++i) {
                if (output[i] != nullptr) {
                    juce::FloatVectorOperations::clear(output[i], numSamples);
                }
            }
        }
        
        // Handle recording (input)
        if (numIn > 0 && input[0] != nullptr) {
            if (samplesRecorded + numSamples <= recordedAudio.getNumSamples()) {
                recordedAudio.copyFrom(0, samplesRecorded, input[0], numSamples);
                samplesRecorded += numSamples;
            }
        }
    }
    
    const juce::AudioBuffer<float>& getRecording() const { return recordedAudio; }
    int getSamplesRecorded() const { return samplesRecorded; }
    
private:
    const juce::AudioBuffer<float>& sourceBuffer;
    int samplesPlayed;
    juce::AudioBuffer<float> recordedAudio;
    int samplesRecorded;
};

// ==============================================================================
//  CHIRP DETECTOR (Modified for full analysis logging)
// ==============================================================================
class ChirpDetector {
private:
    juce::AudioBuffer<float> template_;
    double templateEnergy_;
    
    double calculateEnergy(const float* buffer, int numSamples) {
        double energy = 0.0;
        for (int i = 0; i < numSamples; ++i) {
            energy += buffer[i] * buffer[i];
        }
        return energy;
    }
    
public:
    ChirpDetector() {
        template_ = SignalGenerator::generateChirp();
        templateEnergy_ = calculateEnergy(template_.getReadPointer(0), template_.getNumSamples());
        std::cout << "Chirp template generated: " << template_.getNumSamples() << " samples" << std::endl;
        std::cout << "Template energy: " << templateEnergy_ << std::endl;
    }
    
    /**
     * Analyzes the entire signal and logs correlation scores at every sample index
     */
    void logCorrelation(const juce::AudioBuffer<float>& signal, const std::string& logFilePath) {
        const float* sigData = signal.getReadPointer(0);
        const float* tempData = template_.getReadPointer(0);
        const int sigLen = signal.getNumSamples();
        const int tempLen = template_.getNumSamples();
        
        std::ofstream logFile(logFilePath);
        if (!logFile.is_open()) {
            std::cerr << "ERROR: Could not open log file: " << logFilePath << std::endl;
            return;
        }
        
        std::cout << "\nAnalyzing " << sigLen << " samples..." << std::endl;
        std::cout << "Logging correlation scores to: " << logFilePath << std::endl;
        
#if USE_NCC_DETECTION
        logFile << "Sample_Index,NCC_Value\n";
#else
        logFile << "Sample_Index,Dot_Product_Value\n";
#endif
        
        double maxScore = -1e10;
        int maxPos = -1;
        
        for (int i = 0; i <= sigLen - tempLen; ++i) {
            double dotProduct = 0.0;
            double signalEnergy = 0.0;
            double score = 0.0;
            
            for (int j = 0; j < tempLen; ++j) {
                dotProduct += sigData[i + j] * tempData[j];
#if USE_NCC_DETECTION
                signalEnergy += sigData[i + j] * sigData[i + j];
#endif
            }
            
#if USE_NCC_DETECTION
            if (signalEnergy < 1e-10 || templateEnergy_ < 1e-10) {
                score = 0.0;
            } else {
                score = dotProduct / std::sqrt(signalEnergy * templateEnergy_);
            }
#else
            score = dotProduct;
#endif
            
            logFile << i << "," << std::fixed << std::setprecision(8) << score << "\n";
            
            if (score > maxScore) {
                maxScore = score;
                maxPos = i;
            }
            
            if ((i + 1) % 100000 == 0) {
                std::cout << "  Analyzed " << (i + 1) << " / " << sigLen << " samples..." << std::endl;
            }
        }
        
        logFile.close();
        std::cout << "\n✓ Analysis complete." << std::endl;
#if USE_NCC_DETECTION
        std::cout << "  Max NCC Score: " << maxScore << " at sample " << maxPos << std::endl;
#else
        std::cout << "  Max Dot Product: " << maxScore << " at sample " << maxPos << std::endl;
#endif
    }
};

// ==============================================================================
//  UTILITY FUNCTIONS
// ==============================================================================
void saveWavFile(const juce::AudioBuffer<float>& buffer, const juce::File& file, int numSamples) {
    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wavFormat.createWriterFor(new juce::FileOutputStream(file),
            ChirpTest::sampleRate, 1, 16, {}, 0));
    if (writer != nullptr) {
        writer->writeFromAudioSampleBuffer(buffer, 0, numSamples);
        std::cout << "✓ Audio saved to: " << file.getFullPathName() << std::endl;
    } else {
        std::cerr << "ERROR: Could not save to " << file.getFullPathName() << std::endl;
    }
}

// ==============================================================================
//  MAIN APPLICATION
// ==============================================================================
int main(int argc, char* argv[]) {
    juce::ScopedJuceInitialiser_GUI juceInit;
    
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║            CHIRP DETECTION - STANDALONE TEST                   ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════════╝\n";
    
    // Ensure output directory exists
    juce::File outputDir(ChirpTest::outputPath);
    if (!outputDir.exists()) {
        auto result = outputDir.createDirectory();
        if (result.failed()) {
            std::cerr << "CRITICAL ERROR: Could not create output directory!" << std::endl;
            std::cerr << "Path: " << ChirpTest::outputPath << std::endl;
            std::cerr << "Please create this directory manually and try again." << std::endl;
            std::cin.get();
            return 1;
        }
        std::cout << "Created output directory: " << ChirpTest::outputPath << std::endl;
    }
    
    // STEP 1: Generate test signal
    std::cout << "\nSTEP 1: Generating test signal..." << std::endl;
    auto silence = SignalGenerator::generateSilence(static_cast<int>(ChirpTest::sampleRate * 0.5));
    auto click = SignalGenerator::generateClick(5);
    auto chirp = SignalGenerator::generateChirp();
    
    int totalSamples = silence.getNumSamples() + click.getNumSamples() + chirp.getNumSamples() + click.getNumSamples();
    juce::AudioBuffer<float> finalSignal(1, totalSamples);
    
    int currentSample = 0;
    finalSignal.copyFrom(0, currentSample, silence, 0, 0, silence.getNumSamples());
    currentSample += silence.getNumSamples();
    
    finalSignal.copyFrom(0, currentSample, click, 0, 0, click.getNumSamples());
    currentSample += click.getNumSamples();
    
    finalSignal.copyFrom(0, currentSample, chirp, 0, 0, chirp.getNumSamples());
    currentSample += chirp.getNumSamples();
    
    finalSignal.copyFrom(0, currentSample, click, 0, 0, click.getNumSamples());
    
    std::cout << "Signal generated (" << totalSamples << " samples)" << std::endl;
    
    // STEP 2: Save generated signal
    std::cout << "\nSTEP 2: Saving generated signal..." << std::endl;
    saveWavFile(finalSignal, outputDir.getChildFile("generated_chirp_signal.wav"), totalSamples);
    
    // STEP 3: Setup audio
    std::cout << "\nSTEP 3: Setup audio..." << std::endl;
    juce::AudioDeviceManager deviceManager;
    auto result = deviceManager.initialiseWithDefaultDevices(1, 2);

    
    AudioHandler audioHandler(finalSignal, 10); // 10 second recording capacity
    
    // STEP 4: Play and record
    std::cout << "\nSTEP 4: Play and Record" << std::endl;
    std::cout << "Press ENTER to start playing and recording..." << std::endl;
    std::cin.get();
    
    deviceManager.addAudioCallback(&audioHandler);
    
    std::cout << "\n... PLAYING AND RECORDING ...\n" << std::endl;
    std::cout << "(Ensure microphone can hear the speaker)" << std::endl;
    std::cout << "Press ENTER to stop." << std::endl;
    std::cin.get();
    
    deviceManager.removeAudioCallback(&audioHandler);
    std::cout << "\nStopped. Recorded " << audioHandler.getSamplesRecorded() << " samples." << std::endl;
    
    // STEP 5: Save recorded signal
    std::cout << "\nSTEP 5: Saving recorded signal..." << std::endl;
    saveWavFile(audioHandler.getRecording(), outputDir.getChildFile("recorded_chirp_signal.wav"), audioHandler.getSamplesRecorded());
    
    // STEP 6: Analyze recording
    std::cout << "\nSTEP 6: Analyzing recording..." << std::endl;
    ChirpDetector detector;
    detector.logCorrelation(audioHandler.getRecording(), ChirpTest::outputPath + "chirp_correlation_log.csv");
    
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "TEST COMPLETE" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    std::cout << "\nAll files saved to: " << ChirpTest::outputPath << std::endl;
    std::cout << "\nGenerated files:" << std::endl;
    std::cout << "  1. generated_chirp_signal.wav - Original generated signal" << std::endl;
    std::cout << "  2. recorded_chirp_signal.wav - Recorded audio" << std::endl;
    std::cout << "  3. chirp_correlation_log.csv - Correlation scores" << std::endl;
    
    std::cout << "\nPress ENTER to exit..." << std::endl;
    std::cin.get();
    
    return 0;
}


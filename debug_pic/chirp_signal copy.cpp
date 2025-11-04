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
    
    // Number of times to repeat the chirp pattern
    constexpr int chirp_num = 100;
    
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
//  AUDIO RECORDER (Using ThreadedWriter like AudioRecordingDemo)
// ==============================================================================
class AudioRecorder : public juce::AudioIODeviceCallback {
public:
    AudioRecorder() {
        backgroundThread.startThread();
    }

    ~AudioRecorder() override {
        stop();
    }

    void startRecording(const juce::File& file, double sampleRateToUse) {
        stop();

        sampleRate = sampleRateToUse;

        if (sampleRate > 0) {
            file.deleteFile();

            if (std::unique_ptr<juce::OutputStream> fileStream{ file.createOutputStream() }) {
                juce::WavAudioFormat wavFormat;

                using Opts = juce::AudioFormatWriterOptions;
                // createWriterFor takes a reference to unique_ptr and takes ownership if successful
                if (auto writer = wavFormat.createWriterFor(fileStream, 
                    Opts{}.withSampleRate(sampleRate)
                         .withNumChannels(1)
                         .withBitsPerSample(16))) {
                    // Writer now owns the stream (fileStream is set to nullptr by createWriterFor)
                    // Create ThreadedWriter for background writing
                    threadedWriter.reset(new juce::AudioFormatWriter::ThreadedWriter(
                        writer.release(), backgroundThread, 32768));

                    nextSampleNum = 0;

                    const juce::CriticalSection::ScopedLockType sl(writerLock);
                    activeWriter = threadedWriter.get();
                    
                    std::cout << "Recording started to: " << file.getFullPathName() << std::endl;
                    std::cout << "Sample rate: " << sampleRate << " Hz" << std::endl;
                }
            }
        }
    }

    void stop() {
        {
            const juce::CriticalSection::ScopedLockType sl(writerLock);
            activeWriter = nullptr;
        }
        threadedWriter.reset();
        std::cout << "Recording stopped." << std::endl;
    }

    bool isRecording() const {
        return activeWriter.load() != nullptr;
    }

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override {
        // Update sample rate if device provides one
        if (device != nullptr && sampleRate == 0) {
            sampleRate = device->getCurrentSampleRate();
        }
    }

    void audioDeviceStopped() override {
        // Keep sampleRate for file writing
    }

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
                                          float* const* outputChannelData, int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override {
        juce::ignoreUnused(context);

        // Write input to file
        {
            const juce::CriticalSection::ScopedLockType sl(writerLock);
            if (activeWriter.load() != nullptr && numInputChannels > 0) {
                activeWriter.load()->write(inputChannelData, numSamples);
                nextSampleNum += numSamples;
            }
        }

        // Clear output buffers (we don't play anything here)
        for (int i = 0; i < numOutputChannels; ++i) {
            if (outputChannelData[i] != nullptr) {
                juce::FloatVectorOperations::clear(outputChannelData[i], numSamples);
            }
        }
    }

private:
    juce::TimeSliceThread backgroundThread{ "Audio Recorder Thread" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedWriter;
    double sampleRate = 0.0;
    int64 nextSampleNum = 0;

    juce::CriticalSection writerLock;
    std::atomic<juce::AudioFormatWriter::ThreadedWriter*> activeWriter{ nullptr };
};

// ==============================================================================
//  AUDIO PLAYER (Separate class for playback)
// ==============================================================================
class AudioPlayer : public juce::AudioIODeviceCallback {
public:
    AudioPlayer(const juce::AudioBuffer<float>& bufferToPlay)
        : sourceBuffer(bufferToPlay), samplesPlayed(0) {
    }

    void audioDeviceAboutToStart(juce::AudioIODevice*) override {
        samplesPlayed = 0;
    }

    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
                                          float* const* outputChannelData, int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override {
        juce::ignoreUnused(inputChannelData, numInputChannels, context);

        // Handle playback (output)
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
        } else {
            // Playback finished, output silence
            for (int i = 0; i < numOutputChannels; ++i) {
                if (outputChannelData[i] != nullptr) {
                    juce::FloatVectorOperations::clear(outputChannelData[i], numSamples);
                }
            }
        }
    }

private:
    const juce::AudioBuffer<float>& sourceBuffer;
    int samplesPlayed;
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

bool loadWavFile(juce::AudioBuffer<float>& buffer, const juce::File& file) {
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr) {
        std::cerr << "ERROR: Could not read audio file: " << file.getFullPathName() << std::endl;
        return false;
    }

    buffer.setSize((int)reader->numChannels, (int)reader->lengthInSamples);
    if (!reader->read(&buffer, 0, (int)reader->lengthInSamples, 0, true, true)) {
        std::cerr << "ERROR: Failed to read audio data from file" << std::endl;
        return false;
    }

    std::cout << "✓ Audio loaded from: " << file.getFullPathName() << std::endl;
    std::cout << "  Channels: " << buffer.getNumChannels() << std::endl;
    std::cout << "  Samples: " << buffer.getNumSamples() << std::endl;
    std::cout << "  Sample rate: " << reader->sampleRate << " Hz" << std::endl;
    
    return true;
}

void analyzeRecordingDiagnostics(const juce::AudioBuffer<float>& buffer) {
    if (buffer.getNumSamples() == 0) {
        std::cout << "  [Diagnostics] Buffer is empty!" << std::endl;
        return;
    }

    const float* data = buffer.getReadPointer(0);
    int numSamples = buffer.getNumSamples();
    
    // Calculate statistics
    float minVal = data[0], maxVal = data[0];
    double sum = 0.0, sumSq = 0.0;
    
    for (int i = 0; i < numSamples; ++i) {
        float val = data[i];
        minVal = std::min(minVal, val);
        maxVal = std::max(maxVal, val);
        sum += val;
        sumSq += val * val;
    }
    
    double mean = sum / numSamples;
    double variance = (sumSq / numSamples) - (mean * mean);
    double stdDev = std::sqrt(variance);
    
    std::cout << "\n[Recording Diagnostics]" << std::endl;
    std::cout << "  Min value: " << std::fixed << std::setprecision(6) << minVal << std::endl;
    std::cout << "  Max value: " << maxVal << std::endl;
    std::cout << "  Mean: " << mean << std::endl;
    std::cout << "  Std deviation: " << stdDev << std::endl;
    
    // Check if signal is constant (very low variance)
    if (stdDev < 1e-6) {
        std::cout << "  ⚠ WARNING: Signal appears to be CONSTANT (std dev < 1e-6)" << std::endl;
        std::cout << "     This suggests the microphone may not be receiving the audio." << std::endl;
    }
    
    // Sample a few windows to show variation over time
    const int windowSize = std::min(4800, numSamples / 10); // 0.1 second windows
    if (windowSize > 0) {
        std::cout << "\n  RMS values at different time points:" << std::endl;
        for (int i = 0; i < std::min(10, numSamples / windowSize); ++i) {
            int start = i * windowSize;
            double rmsSum = 0.0;
            for (int j = 0; j < windowSize && (start + j) < numSamples; ++j) {
                float val = data[start + j];
                rmsSum += val * val;
            }
            double rms = std::sqrt(rmsSum / windowSize);
            std::cout << "    Window " << i << " (sample " << start << "): RMS = " 
                      << std::fixed << std::setprecision(6) << rms << std::endl;
        }
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
    std::cout << "Repeating chirp pattern " << ChirpTest::chirp_num << " times..." << std::endl;
    
    auto silence = SignalGenerator::generateSilence(static_cast<int>(ChirpTest::sampleRate * 0.5));
    auto click = SignalGenerator::generateClick(5);
    auto chirp = SignalGenerator::generateChirp();
    
    // Calculate total samples: silence + (click + chirp + click) * chirp_num
    int chirpPatternSamples = click.getNumSamples() + chirp.getNumSamples() + click.getNumSamples();
    int totalSamples = silence.getNumSamples() + (chirpPatternSamples * ChirpTest::chirp_num);
    juce::AudioBuffer<float> finalSignal(1, totalSamples);
    
    int currentSample = 0;
    
    // Add initial silence
    finalSignal.copyFrom(0, currentSample, silence, 0, 0, silence.getNumSamples());
    currentSample += silence.getNumSamples();
    
    // Loop to repeat chirp pattern chirp_num times
    for (int i = 0; i < ChirpTest::chirp_num; ++i) {
        // Click marker before chirp
        finalSignal.copyFrom(0, currentSample, click, 0, 0, click.getNumSamples());
        currentSample += click.getNumSamples();
        
        // Chirp
        finalSignal.copyFrom(0, currentSample, chirp, 0, 0, chirp.getNumSamples());
        currentSample += chirp.getNumSamples();
        
        // Click marker after chirp
        finalSignal.copyFrom(0, currentSample, click, 0, 0, click.getNumSamples());
        currentSample += click.getNumSamples();
    }
    
    std::cout << "Signal generated (" << totalSamples << " samples)" << std::endl;
    std::cout << "  - Initial silence: " << silence.getNumSamples() << " samples" << std::endl;
    std::cout << "  - Chirp pattern repeated " << ChirpTest::chirp_num << " times" << std::endl;
    std::cout << "  - Pattern size: " << chirpPatternSamples << " samples per repetition" << std::endl;
    
    // STEP 2: Save generated signal
    std::cout << "\nSTEP 2: Saving generated signal..." << std::endl;
    saveWavFile(finalSignal, outputDir.getChildFile("generated_chirp_signal.wav"), totalSamples);
    
    // STEP 3: Setup audio devices
    std::cout << "\nSTEP 3: Setup audio devices..." << std::endl;
    juce::AudioDeviceManager deviceManager;
    auto result = deviceManager.initialiseWithDefaultDevices(1, 2);
    
    if (result.isNotEmpty()) {
        std::cerr << "WARNING: Audio device initialization: " << result << std::endl;
    }
    
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device != nullptr) {
        std::cout << "Audio device initialized:" << std::endl;
        std::cout << "  Sample rate: " << device->getCurrentSampleRate() << " Hz" << std::endl;
        std::cout << "  Buffer size: " << device->getCurrentBufferSizeSamples() << " samples" << std::endl;
        std::cout << "  Input channels: " << device->getActiveInputChannels().countNumberOfSetBits() << std::endl;
        std::cout << "  Output channels: " << device->getActiveOutputChannels().countNumberOfSetBits() << std::endl;
    }

    // Create separate recorder and player
    AudioRecorder recorder;
    AudioPlayer player(finalSignal);
    
    juce::File recordedFile = outputDir.getChildFile("recorded_chirp_signal.wav");
    
    // STEP 4: Play and record
    std::cout << "\nSTEP 4: Play and Record" << std::endl;
    std::cout << "Press ENTER to start playing and recording..." << std::endl;
    std::cin.get();
    
    // Start recording to file
    recorder.startRecording(recordedFile, ChirpTest::sampleRate);
    
    // Add both callbacks
    deviceManager.addAudioCallback(&recorder);
    deviceManager.addAudioCallback(&player);
    
    std::cout << "\n... PLAYING AND RECORDING ...\n" << std::endl;
    std::cout << "(Ensure microphone can hear the speaker)" << std::endl;
    std::cout << "Press ENTER to stop." << std::endl;
    std::cin.get();
    
    // Stop recording first, then remove callbacks
    recorder.stop();
    deviceManager.removeAudioCallback(&player);
    deviceManager.removeAudioCallback(&recorder);
    
    std::cout << "\nStopped recording." << std::endl;
    
    // Wait a moment for file writing to complete
    juce::Thread::sleep(500);
    
    // STEP 5: Load recorded signal from file
    std::cout << "\nSTEP 5: Loading recorded signal from file..." << std::endl;
    juce::AudioBuffer<float> recordedAudio;
    if (!loadWavFile(recordedAudio, recordedFile)) {
        std::cerr << "ERROR: Failed to load recorded audio. Analysis cannot proceed." << std::endl;
        std::cout << "\nPress ENTER to exit..." << std::endl;
        std::cin.get();
        return 1;
    }
    
    // Run diagnostics on the recording
    analyzeRecordingDiagnostics(recordedAudio);
    
    // STEP 6: Analyze recording
    std::cout << "\nSTEP 6: Analyzing recording..." << std::endl;
    ChirpDetector detector;
    detector.logCorrelation(recordedAudio, ChirpTest::outputPath + "chirp_correlation_log.csv");
    
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


/*
 * REAL-TIME CHIRP PLAY AND RECORD TEST
 *
 * Tests chirp detection with acoustic loopback (speaker -> microphone)
 * Measures sample-level accuracy and calculates offsets for clock synchronization
 *
 * DIFFERENCES FROM OFFLINE TEST:
 * - Actual audio playback through speakers
 * - Real microphone recording
 * - Accounts for acoustic delay and room effects
 * - Measures propagation delay and system latency
 */

#include <JuceHeader.h>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <algorithm>

namespace ChirpConfig {
    constexpr double sampleRate = 44100.0;
    constexpr int chirpSamples = 440;
    constexpr double f0 = 2000.0;
    constexpr double f1 = 4000.0;
    constexpr double duration = chirpSamples / sampleRate;
    constexpr double chirpRate = (f1 - f0) / duration;
}

// ==============================================================================
//  CHIRP GENERATOR (Same as offline version)
// ==============================================================================
class ChirpGenerator {
public:
    static juce::AudioBuffer<float> generateChirp() {
        juce::AudioBuffer<float> chirp(1, ChirpConfig::chirpSamples);
        auto* signal = chirp.getWritePointer(0);

        for (int i = 0; i < ChirpConfig::chirpSamples / 2; ++i) {
            double t = i / ChirpConfig::sampleRate;
            double phase = 2.0 * juce::MathConstants<double>::pi *
                (ChirpConfig::chirpRate / 2.0 * t * t + ChirpConfig::f0 * t);
            signal[i] = std::sin(phase);
        }

        for (int i = ChirpConfig::chirpSamples / 2; i < ChirpConfig::chirpSamples; ++i) {
            signal[i] = -signal[ChirpConfig::chirpSamples - 1 - i];
        }

        float maxVal = chirp.getMagnitude(0, ChirpConfig::chirpSamples);
        if (maxVal > 0.0f) chirp.applyGain(0.8f / maxVal);

        return chirp;
    }
};

// ==============================================================================
//  CHIRP DETECTOR (With offset calculation)
// ==============================================================================
class ChirpDetector {
private:
    juce::AudioBuffer<float> template_;
    double templateEnergy_;

public:
    struct DetectionResult {
        int detectedPosition;      // Where chirp was found
        int expectedPosition;      // Where we expected it
        int offset;                // detectedPosition - expectedPosition
        double score;              // Detection score (NCC value)
        double confidence;         // Confidence ratio
        bool success;              // Whether detection succeeded
        std::string method;
    };

    ChirpDetector(const juce::AudioBuffer<float>& chirpTemplate) {
        template_.makeCopyOf(chirpTemplate);
        templateEnergy_ = calculateEnergy(template_.getReadPointer(0), template_.getNumSamples());
    }

    DetectionResult detectWithNCC(const juce::AudioBuffer<float>& signal,
        int expectedPosition,
        double threshold = 0.7) {
        const float* sigData = signal.getReadPointer(0);
        const float* tempData = template_.getReadPointer(0);
        const int tempLen = template_.getNumSamples();

        double maxNCC = 0.0;
        int maxPos = -1;
        std::vector<double> nccValues;

        for (int i = 0; i <= signal.getNumSamples() - tempLen; ++i) {
            double dotProduct = 0.0;
            double signalEnergy = 0.0;

            for (int j = 0; j < tempLen; ++j) {
                dotProduct += sigData[i + j] * tempData[j];
                signalEnergy += sigData[i + j] * sigData[i + j];
            }

            double ncc = (signalEnergy > 1e-10 && templateEnergy_ > 1e-10) ?
                dotProduct / std::sqrt(signalEnergy * templateEnergy_) : 0.0;

            nccValues.push_back(ncc);

            if (ncc > maxNCC) {
                maxNCC = ncc;
                maxPos = i;
            }
        }

        std::sort(nccValues.rbegin(), nccValues.rend());
        double confidenceRatio = nccValues.size() > 1 ? nccValues[0] / (nccValues[1] + 1e-10) : 1.0;

        DetectionResult result;
        result.detectedPosition = maxPos;
        result.expectedPosition = expectedPosition;
        result.offset = maxPos - expectedPosition;
        result.score = maxNCC;
        result.confidence = confidenceRatio;
        result.success = (maxNCC > threshold);
        result.method = "NCC";

        return result;
    }

    // SamplePHY method with offset correction
    DetectionResult detectWithSamplePHY(const juce::AudioBuffer<float>& signal,
        int expectedPosition,
        double powerThresholdRatio = 2.0,
        double minAbsThreshold = 0.05) {
        const float* sigData = signal.getReadPointer(0);
        const float* tempData = template_.getReadPointer(0);
        const int tempLen = template_.getNumSamples();

        double power = 0.0;
        double syncPowerLocalMax = 0.0;
        int peakIndex = -1;

        std::vector<float> syncFIFO(tempLen, 0.0f);

        for (int i = 0; i < signal.getNumSamples(); ++i) {
            power = power * (1.0 - 1.0 / 64.0) + sigData[i] * sigData[i] / 64.0;

            for (int j = 0; j < tempLen - 1; ++j) {
                syncFIFO[j] = syncFIFO[j + 1];
            }
            syncFIFO[tempLen - 1] = sigData[i];

            if (i >= tempLen) {
                double correlation = 0.0;
                for (int j = 0; j < tempLen; ++j) {
                    correlation += syncFIFO[j] * tempData[j];
                }
                correlation /= 200.0;

                if (correlation > power * powerThresholdRatio &&
                    correlation > syncPowerLocalMax &&
                    correlation > minAbsThreshold) {
                    syncPowerLocalMax = correlation;
                    peakIndex = i;
                }
                else if (peakIndex != -1 && (i - peakIndex) > 200) {
                    break;
                }
            }
        }

        // OFFSET CORRECTION: SamplePHY detects at END of chirp, subtract chirpSamples
        int correctedPosition = peakIndex ;//- ChirpConfig::chirpSamples

        DetectionResult result;
        result.detectedPosition = correctedPosition;
        result.expectedPosition = expectedPosition;
        result.offset = correctedPosition - expectedPosition;
        result.score = syncPowerLocalMax;
        result.confidence = syncPowerLocalMax / (power + 1e-10);
        result.success = (peakIndex != -1);
        result.method = "SamplePHY(corrected)";

        return result;
    }

private:
    double calculateEnergy(const float* buffer, int numSamples) {
        double energy = 0.0;
        for (int i = 0; i < numSamples; ++i) {
            energy += buffer[i] * buffer[i];
        }
        return energy;
    }
};

// ==============================================================================
//  TEST SIGNAL GENERATOR
// ==============================================================================
class TestSignalGenerator : public juce::AudioSource {
public:
    TestSignalGenerator(const juce::AudioBuffer<float>& chirpTemplate, int testOffsetSamples)
        : chirp(chirpTemplate), offset(testOffsetSamples) {

        // Create test signal: silence + chirp + silence
        int totalSamples = offset + chirp.getNumSamples() + 10000;
        testSignal.setSize(1, totalSamples);
        testSignal.clear();
        testSignal.copyFrom(0, offset, chirp, 0, 0, chirp.getNumSamples());

        position = 0;
        finished = false;
    }

    void prepareToPlay(int, double) override {
        position = 0;
        finished = false;
    }

    void releaseResources() override {}

    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override {
        if (finished) {
            bufferToFill.clearActiveBufferRegion();
            return;
        }

        auto remainingSamples = testSignal.getNumSamples() - position;
        auto samplesThisTime = juce::jmin(bufferToFill.numSamples, remainingSamples);

        if (samplesThisTime > 0) {
            for (int chan = 0; chan < bufferToFill.buffer->getNumChannels(); ++chan) {
                bufferToFill.buffer->copyFrom(chan, bufferToFill.startSample,
                    testSignal, 0, position, samplesThisTime);
            }
            position += samplesThisTime;
        }

        if (samplesThisTime < bufferToFill.numSamples) {
            bufferToFill.buffer->clear(bufferToFill.startSample + samplesThisTime,
                bufferToFill.numSamples - samplesThisTime);
            finished = true;
        }
    }

    bool isFinished() const { return finished; }
    int getExpectedPosition() const { return offset; }

private:
    juce::AudioBuffer<float> chirp;
    juce::AudioBuffer<float> testSignal;
    int offset;
    int position;
    bool finished;
};

// ==============================================================================
//  ACOUSTIC LOOPBACK TESTER
// ==============================================================================
class AcousticLoopbackTester : public juce::AudioIODeviceCallback {
public:
    AcousticLoopbackTester(TestSignalGenerator* generator)
        : source(generator) {
        int requiredSamples = static_cast<int>(ChirpConfig::sampleRate * 5.0); // 5 seconds
        recordedAudio.setSize(1, requiredSamples);
        recordedAudio.clear();
        samplesRecorded = 0;
        testFinished = false;
    }

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override {
        source->prepareToPlay(device->getCurrentBufferSizeSamples(), device->getCurrentSampleRate());
    }

    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext(const float* const* input, int numIn,
        float* const* output, int numOut,
        int numSamples,
        const juce::AudioIODeviceCallbackContext&) override {
        // Play test signal
        if (numOut > 0) {
            juce::AudioBuffer<float> tempBuffer(output, numOut, numSamples);
            juce::AudioSourceChannelInfo info(&tempBuffer, 0, numSamples);
            source->getNextAudioBlock(info);
        }

        // Record input
        if (numIn > 0 && input[0] != nullptr) {
            if (samplesRecorded + numSamples <= recordedAudio.getNumSamples()) {
                recordedAudio.copyFrom(0, samplesRecorded, input[0], numSamples);
                samplesRecorded += numSamples;
            }
        }

        // Check if finished
        if (source->isFinished() && samplesRecorded > source->getExpectedPosition() + 10000) {
            testFinished = true;
        }
    }

    bool isTestFinished() const { return testFinished; }
    const juce::AudioBuffer<float>& getRecording() const { return recordedAudio; }

private:
    TestSignalGenerator* source;
    juce::AudioBuffer<float> recordedAudio;
    int samplesRecorded;
    std::atomic<bool> testFinished;
};

// ==============================================================================
//  REAL-TIME TEST FRAMEWORK
// ==============================================================================
class RealtimeChirpTest {
public:
    void runAllTests() {
        std::cout << "\n" << std::string(80, '=') << std::endl;
        std::cout << "REAL-TIME CHIRP DETECTION TEST (Acoustic Loopback)" << std::endl;
        std::cout << std::string(80, '=') << std::endl;

        chirpTemplate = ChirpGenerator::generateChirp();

        std::cout << "\nEnsure your microphone can hear your speakers!" << std::endl;
        std::cout << "Press ENTER to start tests..." << std::endl;
        std::cin.get();

        // Test 1: Short offset
        runSingleTest(5000, "Short Offset (5000 samples = 0.113s)");

        // Test 2: Medium offset
        runSingleTest(22050, "Medium Offset (22050 samples = 0.5s)");

        // Test 3: Long offset  
        runSingleTest(44100, "Long Offset (44100 samples = 1.0s)");

        std::cout << "\n" << std::string(80, '=') << std::endl;
        std::cout << "ALL REAL-TIME TESTS COMPLETE" << std::endl;
        std::cout << std::string(80, '=') << std::endl;

        printSummary();
    }

private:
    juce::AudioBuffer<float> chirpTemplate;
    std::vector<ChirpDetector::DetectionResult> results;

    void runSingleTest(int expectedOffset, const std::string& testName) {
        std::cout << "\n" << std::string(80, '-') << std::endl;
        std::cout << "TEST: " << testName << std::endl;
        std::cout << "Expected chirp at sample " << expectedOffset
            << " (time: " << (expectedOffset / ChirpConfig::sampleRate) << "s)" << std::endl;
        std::cout << std::string(80, '-') << std::endl;

        // Setup audio device
        juce::AudioDeviceManager deviceManager;
        deviceManager.initialiseWithDefaultDevices(1, 1);

        // Create test signal
        TestSignalGenerator generator(chirpTemplate, expectedOffset);

        // Create loopback tester
        AcousticLoopbackTester tester(&generator);

        std::cout << "Playing test signal..." << std::endl;
        deviceManager.addAudioCallback(&tester);

        // Wait for test to complete
        while (!tester.isTestFinished()) {
            juce::Thread::sleep(100);
        }

        deviceManager.removeAudioCallback(&tester);
        std::cout << "Recording complete." << std::endl;

        // Save recording
        std::string filename = "realtime_chirp_" + std::to_string(expectedOffset) + ".wav";
        saveWav(tester.getRecording(), filename);

        // Detect chirp
        ChirpDetector detector(chirpTemplate);

        auto resultNCC = detector.detectWithNCC(tester.getRecording(), expectedOffset);
        auto resultPHY = detector.detectWithSamplePHY(tester.getRecording(), expectedOffset);

        // Print results
        printDetectionResult(resultNCC);
        printDetectionResult(resultPHY);

        // Store for summary
        results.push_back(resultNCC);

        std::cout << "\nPress ENTER for next test..." << std::endl;
        std::cin.get();
    }

    void printDetectionResult(const ChirpDetector::DetectionResult& result) {
        std::cout << "\n--- " << result.method << " Detection ---" << std::endl;
        std::cout << "Expected position: " << result.expectedPosition << " samples" << std::endl;
        std::cout << "Detected position: " << result.detectedPosition << " samples" << std::endl;
        std::cout << "Offset: " << result.offset << " samples";

        if (result.offset != 0) {
            double offsetTime = std::abs(result.offset) / ChirpConfig::sampleRate * 1000.0;
            std::cout << " (" << offsetTime << " ms)";
        }
        std::cout << std::endl;

        std::cout << "Detection score: " << std::fixed << std::setprecision(4) << result.score << std::endl;
        std::cout << "Confidence: " << result.confidence << std::endl;
        std::cout << "Success: " << (result.success ? "YES" : "NO") << std::endl;

        if (std::abs(result.offset) <= 1) {
            std::cout << "SAMPLE-LEVEL ACCURACY ACHIEVED" << std::endl;
        }
        else if (std::abs(result.offset) <= 10) {
            std::cout <<  "Within 10 samples(sub - millisecond accuracy)" << std::endl;
        }
        else {
            std::cout << "Offset exceeds tolerance" << std::endl;
            std::cout << "  Possible causes:" << std::endl;
            std::cout << "  - Acoustic propagation delay" << std::endl;
            std::cout << "  - System audio buffer latency" << std::endl;
            std::cout << "  - Clock drift between playback/record" << std::endl;
        }
    }

    void printSummary() {
        std::cout << "\n" << std::string(80, '=') << std::endl;
        std::cout << "SUMMARY OF ALL TESTS" << std::endl;
        std::cout << std::string(80, '=') << std::endl;

        int perfectCount = 0;
        int goodCount = 0;
        double avgOffset = 0.0;
        double maxOffset = 0.0;

        for (const auto& result : results) {
            int absOffset = std::abs(result.offset);
            avgOffset += absOffset;
            maxOffset = std::max(maxOffset, (double)absOffset);

            if (absOffset <= 1) perfectCount++;
            else if (absOffset <= 10) goodCount++;
        }

        avgOffset /= results.size();

        std::cout << "\nTotal tests: " << results.size() << std::endl;
        std::cout << "Sample-level accurate (≤1 sample): " << perfectCount << std::endl;
        std::cout << "Sub-millisecond accurate (≤10 samples): " << goodCount << std::endl;
        std::cout << "Average offset: " << std::fixed << std::setprecision(2) << avgOffset << " samples" << std::endl;
        std::cout << "Max offset: " << maxOffset << " samples" << std::endl;

        if (avgOffset > 0) {
            double avgOffsetMs = avgOffset / ChirpConfig::sampleRate * 1000.0;
            std::cout << "\n>>> CLOCK OFFSET COMPENSATION: " << std::endl;
            std::cout << "    Apply offset of " << (int)avgOffset << " samples ("
                << avgOffsetMs << " ms)" << std::endl;
            std::cout << "    to achieve sample-level synchronization." << std::endl;
        }

        std::cout << "\n" << std::string(80, '=') << std::endl;
    }

    void saveWav(const juce::AudioBuffer<float>& buffer, const std::string& filename) {
        juce::File outFile = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(filename));
        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wavFormat.createWriterFor(new juce::FileOutputStream(outFile),
                ChirpConfig::sampleRate, 1, 16, {}, 0));
        if (writer != nullptr) {
            writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
            std::cout << "Saved: " << filename << std::endl;
        }
    }
};

// ==============================================================================
//  MAIN
// ==============================================================================
int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║   REAL-TIME CHIRP SYNCHRONIZATION TEST (Acoustic Loopback)    ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════════╝\n";
    std::cout << "\nThis test measures actual acoustic channel characteristics.\n";
    std::cout << "Results will show system latency and clock offsets.\n";

    RealtimeChirpTest tester;
    tester.runAllTests();

    std::cout << "\n\nRecordings saved for inspection:" << std::endl;
    std::cout << "  - realtime_chirp_5000.wav" << std::endl;
    std::cout << "  - realtime_chirp_22050.wav" << std::endl;
    std::cout << "  - realtime_chirp_44100.wav" << std::endl;
    std::cout << "\nPress ENTER to exit..." << std::endl;
    std::cin.get();

    return 0;
}


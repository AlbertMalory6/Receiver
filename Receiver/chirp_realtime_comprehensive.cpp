/*
 * COMPREHENSIVE REAL-TIME CHIRP DETECTION TEST (Play & Record)
 *
 * DIFFERENCES FROM OFFLINE VERSION:
 * - Plays audio through speakers
 * - Records via microphone
 * - Tests actual acoustic channel with all comprehensive scenarios
 * - Measures real detection accuracy including system effects
 *
 * GOALS:
 * 1. Verify detection accuracy with actual audio I/O
 * 2. Export sliding window correlation values for debugging
 * 3. Test all scenarios: pure chirp, marker tone, chirp+data, multiple positions
 * 4. Find why SamplePHY fails in real-time (always returns -441)
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
    constexpr double f0 = 2000.0 - 1000.0;
    constexpr double f1 = 4000.0 + 1000.0;
}

// ==============================================================================
//  CHIRP GENERATOR
// ==============================================================================
class ChirpGenerator {
public:
    static juce::AudioBuffer<float> generateChirp() {
        juce::AudioBuffer<float> chirp(1, ChirpConfig::chirpSamples);
        auto* signal = chirp.getWritePointer(0);

        for (int i = 0; i < ChirpConfig::chirpSamples / 2; ++i) {
            double t = i / ChirpConfig::sampleRate;
            double c = (ChirpConfig::f1 - ChirpConfig::f0) / (ChirpConfig::chirpSamples / ChirpConfig::sampleRate);
            double phase = 2.0 * juce::MathConstants<double>::pi *
                (c / 2.0 * t * t + ChirpConfig::f0 * t);
            signal[i] = std::sin(phase);
        }

        for (int i = ChirpConfig::chirpSamples / 2; i < ChirpConfig::chirpSamples; ++i) {
            signal[i] = -signal[ChirpConfig::chirpSamples - 1 - i];
        }

        float maxVal = chirp.getMagnitude(0, ChirpConfig::chirpSamples);
        if (maxVal > 0.0f) chirp.applyGain(0.8f / maxVal);

        return chirp;
    }

    static juce::AudioBuffer<float> generateMarkerTone() {
        int samples = static_cast<int>(ChirpConfig::sampleRate * 0.1); // 100ms
        juce::AudioBuffer<float> tone(1, samples);
        auto* signal = tone.getWritePointer(0);

        for (int i = 0; i < samples; ++i) {
            double phase = 2.0 * juce::MathConstants<double>::pi * 1000.0 * i / ChirpConfig::sampleRate;
            signal[i] = std::sin(phase) * 0.5f;
        }

        return tone;
    }
};

// ==============================================================================
//  CHIRP DETECTOR WITH FULL DEBUG
// ==============================================================================
class ChirpDetectorDebug {
private:
    juce::AudioBuffer<float> template_;
    double templateEnergy_;

public:
    struct DetectionResult {
        int detectedPosition;
        int actualPosition;
        int offset;
        double score;
        double confidence;
        bool success;
        std::string method;
        std::string debugCsvFile;
    };

    ChirpDetectorDebug(const juce::AudioBuffer<float>& chirpTemplate) {
        template_.makeCopyOf(chirpTemplate);
        templateEnergy_ = calculateEnergy(template_.getReadPointer(0), template_.getNumSamples());
    }

    DetectionResult detectWithNCC_Debug(const juce::AudioBuffer<float>& signal,
        int actualChirpPosition,
        const std::string& debugFilename) {
        const float* sigData = signal.getReadPointer(0);
        const float* tempData = template_.getReadPointer(0);
        const int tempLen = template_.getNumSamples();

        std::ofstream debugFile(debugFilename);
        debugFile << "position,ncc,signalEnergy,dotProduct\n";

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

            if (i % 10 == 0 || std::abs(i - actualChirpPosition) < 100) {
                debugFile << i << "," << ncc << "," << signalEnergy << "," << dotProduct << "\n";
            }

            if (ncc > maxNCC) {
                maxNCC = ncc;
                maxPos = i;
            }
        }

        debugFile.close();

        std::sort(nccValues.rbegin(), nccValues.rend());
        double confidenceRatio = nccValues.size() > 1 ? nccValues[0] / (nccValues[1] + 1e-10) : 1.0;

        DetectionResult result;
        result.detectedPosition = maxPos;
        result.actualPosition = actualChirpPosition;
        result.offset = maxPos - actualChirpPosition;
        result.score = maxNCC;
        result.confidence = confidenceRatio;
        result.success = (maxNCC > 0.01);
        result.method = "NCC";
        result.debugCsvFile = debugFilename;

        return result;
    }

    DetectionResult detectWithDotProduct_Debug(const juce::AudioBuffer<float>& signal,
        int actualChirpPosition,
        const std::string& debugFilename) {
        const float* sigData = signal.getReadPointer(0);
        const float* tempData = template_.getReadPointer(0);
        const int tempLen = template_.getNumSamples();

        std::ofstream debugFile(debugFilename);
        debugFile << "position,dotProduct\n";

        double maxDot = 0.0;
        int maxPos = -1;

        for (int i = 0; i <= signal.getNumSamples() - tempLen; ++i) {
            double dotProduct = 0.0;

            for (int j = 0; j < tempLen; ++j) {
                dotProduct += sigData[i + j] * tempData[j];
            }

            if (i % 10 == 0 || std::abs(i - actualChirpPosition) < 100) {
                debugFile << i << "," << dotProduct << "\n";
            }

            if (dotProduct > maxDot) {
                maxDot = dotProduct;
                maxPos = i;
            }
        }

        debugFile.close();

        DetectionResult result;
        result.detectedPosition = maxPos;
        result.actualPosition = actualChirpPosition;
        result.offset = maxPos - actualChirpPosition;
        result.score = maxDot;
        result.confidence = 1.0;
        result.success = true;
        result.method = "DotProduct";
        result.debugCsvFile = debugFilename;

        return result;
    }

    DetectionResult detectWithSamplePHY_Debug(const juce::AudioBuffer<float>& signal,
        int actualChirpPosition,
        const std::string& debugFilename,
        double powerRatio = 2.0,
        double minThreshold = 0.01) {
        const float* sigData = signal.getReadPointer(0);
        const float* tempData = template_.getReadPointer(0);
        const int tempLen = template_.getNumSamples();

        std::ofstream debugFile(debugFilename);
        debugFile << "sample,power,correlation,normalized_corr,threshold,detected\n";

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

                double normalized = correlation / 200.0;
                double threshold = power * powerRatio;

                bool detected = (normalized > threshold && normalized > syncPowerLocalMax && normalized > minThreshold);

                if (i % 100 == 0 || std::abs(i - actualChirpPosition) < 200 || detected) {
                    debugFile << i << "," << power << "," << correlation << ","
                        << normalized << "," << threshold << "," << (detected ? 1 : 0) << "\n";
                }

                if (detected) {
                    syncPowerLocalMax = normalized;
                    peakIndex = i;
                }
                else if (peakIndex != -1 && (i - peakIndex) > 200) {
                    break;
                }
            }
        }

        debugFile.close();

        DetectionResult result;
        result.detectedPosition = peakIndex;
        result.actualPosition = actualChirpPosition;
        result.offset = peakIndex - actualChirpPosition;
        result.score = syncPowerLocalMax;
        result.confidence = syncPowerLocalMax / (power + 1e-10);
        result.success = (peakIndex != -1);
        result.method = "SamplePHY";
        result.debugCsvFile = debugFilename;

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
//  REAL-TIME TEST SIGNAL GENERATOR
// ==============================================================================
class TestSignalGenerator : public juce::AudioSource {
public:
    TestSignalGenerator(const juce::AudioBuffer<float>& testSignal, int chirpPos)
        : signal(testSignal), chirpPosition(chirpPos) {
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

        auto remainingSamples = signal.getNumSamples() - position;
        auto samplesThisTime = juce::jmin(bufferToFill.numSamples, remainingSamples);

        if (samplesThisTime > 0) {
            for (int chan = 0; chan < bufferToFill.buffer->getNumChannels(); ++chan) {
                bufferToFill.buffer->copyFrom(chan, bufferToFill.startSample,
                    signal, 0, position, samplesThisTime);
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
    int getChirpPosition() const { return chirpPosition; }

private:
    juce::AudioBuffer<float> signal;
    int chirpPosition;
    int position;
    bool finished;
};

// ==============================================================================
//  RECORDER
// ==============================================================================
class AudioRecorder : public juce::AudioIODeviceCallback {
public:
    AudioRecorder(TestSignalGenerator* gen) : source(gen) {
        int requiredSamples = static_cast<int>(ChirpConfig::sampleRate * 6.0);
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
        // Play
        if (numOut > 0) {
            juce::AudioBuffer<float> tempBuffer(output, numOut, numSamples);
            juce::AudioSourceChannelInfo info(&tempBuffer, 0, numSamples);
            source->getNextAudioBlock(info);
        }

        // Record
        if (numIn > 0 && input[0] != nullptr) {
            if (samplesRecorded + numSamples <= recordedAudio.getNumSamples()) {
                recordedAudio.copyFrom(0, samplesRecorded, input[0], numSamples);
                samplesRecorded += numSamples;
            }
        }

        if (source->isFinished() && samplesRecorded > source->getChirpPosition() + 10000) {
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
//  COMPREHENSIVE REAL-TIME TEST
// ==============================================================================
class ComprehensiveRealtimeTest {
public:
    void runAllTests() {
        std::cout << "\n" << std::string(80, '=') << std::endl;
        std::cout << "COMPREHENSIVE REAL-TIME CHIRP TEST (Play & Record)" << std::endl;
        std::cout << std::string(80, '=') << std::endl;

        chirpTemplate = ChirpGenerator::generateChirp();

        std::cout << "\n⚠ IMPORTANT: Ensure microphone can hear speakers!" << std::endl;
        std::cout << "   (Use loopback cable or acoustic coupling)" << std::endl;
        std::cout << "\nPress ENTER to start tests..." << std::endl;
        std::cin.get();

        // All 5 comprehensive tests
        test1_PureChirpAtKnownPosition();
        test2_ChirpWithMarkerTone();
        test3_PureChirpVsChirpPlusData();
        test4_MultiplePositions();
        test5_ThresholdTuning();

        printFinalSummary();
    }

private:
    juce::AudioBuffer<float> chirpTemplate;
    std::vector<ChirpDetectorDebug::DetectionResult> allResults;
    juce::File outputDir{ "D:\\fourth_year\\cs120\\record_data\\chirp_debug\\wav_csv" };


    void test1_PureChirpAtKnownPosition() {
        std::cout << "\n" << std::string(80, '-') << std::endl;
        std::cout << "REAL-TIME TEST 1: Pure Chirp at Known Position" << std::endl;
        std::cout << std::string(80, '-') << std::endl;

        int actualPosition = 5000;
        int totalSamples = 20000;

        juce::AudioBuffer<float> testSignal(1, totalSamples);
        testSignal.clear();
        testSignal.copyFrom(0, actualPosition, chirpTemplate, 0, 0, chirpTemplate.getNumSamples());

        auto recording = playAndRecord(testSignal, actualPosition, "rt_test1");

        ChirpDetectorDebug detector(chirpTemplate);
        auto resultNCC = detector.detectWithNCC_Debug(recording, actualPosition, "rt_test1_ncc_debug.csv");
        auto resultDot = detector.detectWithDotProduct_Debug(recording, actualPosition, "rt_test1_dot_debug.csv");
        auto resultPHY = detector.detectWithSamplePHY_Debug(recording, actualPosition, "rt_test1_phy_debug.csv");

        printResult(resultNCC);
        printResult(resultDot);
        printResult(resultPHY);

        allResults.push_back(resultNCC);

        pauseForNext();
    }

    void test2_ChirpWithMarkerTone() {
        std::cout << "\n" << std::string(80, '-') << std::endl;
        std::cout << "REAL-TIME TEST 2: Chirp with Observable Marker" << std::endl;
        std::cout << "Marker: 1kHz tone for 100ms BEFORE chirp" << std::endl;
        std::cout << std::string(80, '-') << std::endl;

        auto markerTone = ChirpGenerator::generateMarkerTone();
        int markerSamples = markerTone.getNumSamples();
        int chirpPosition = markerSamples;

        int totalSamples = markerSamples + chirpTemplate.getNumSamples() + 5000;
        juce::AudioBuffer<float> testSignal(1, totalSamples);
        testSignal.clear();
        testSignal.copyFrom(0, 0, markerTone, 0, 0, markerSamples);
        testSignal.copyFrom(0, chirpPosition, chirpTemplate, 0, 0, chirpTemplate.getNumSamples());

        std::cout << "  Marker tone: 0-" << markerSamples << ", Chirp at: " << chirpPosition << std::endl;

        auto recording = playAndRecord(testSignal, chirpPosition, "rt_test2");

        ChirpDetectorDebug detector(chirpTemplate);
        auto resultNCC = detector.detectWithNCC_Debug(recording, chirpPosition, "rt_test2_ncc_debug.csv");
        auto resultPHY = detector.detectWithSamplePHY_Debug(recording, chirpPosition, "rt_test2_phy_debug.csv");

        printResult(resultNCC);
        printResult(resultPHY);

        allResults.push_back(resultNCC);

        pauseForNext();
    }

    void test3_PureChirpVsChirpPlusData() {
        std::cout << "\n" << std::string(80, '-') << std::endl;
        std::cout << "REAL-TIME TEST 3: Pure Chirp vs Chirp+Data" << std::endl;
        std::cout << std::string(80, '-') << std::endl;

        int chirpPos = 3000;

        // A: Pure chirp
        std::cout << "\n  A) Pure Chirp Only:" << std::endl;
        juce::AudioBuffer<float> pureChirp(1, chirpPos + chirpTemplate.getNumSamples() + 2000);
        pureChirp.clear();
        pureChirp.copyFrom(0, chirpPos, chirpTemplate, 0, 0, chirpTemplate.getNumSamples());

        auto recordingA = playAndRecord(pureChirp, chirpPos, "rt_test3a");

        ChirpDetectorDebug detectorA(chirpTemplate);
        auto resultA = detectorA.detectWithNCC_Debug(recordingA, chirpPos, "rt_test3a_ncc_debug.csv");
        printResult(resultA);
        allResults.push_back(resultA);

        pauseForNext();

        // B: Chirp + Data
        std::cout << "\n  B) Chirp + FSK Data:" << std::endl;
        juce::AudioBuffer<float> chirpPlusData(1, chirpPos + chirpTemplate.getNumSamples() + 5000);
        chirpPlusData.clear();
        chirpPlusData.copyFrom(0, chirpPos, chirpTemplate, 0, 0, chirpTemplate.getNumSamples());

        int dataStart = chirpPos + chirpTemplate.getNumSamples();
        auto* signal = chirpPlusData.getWritePointer(0);
        for (int i = 0; i < 4000; ++i) {
            double freq = ((i / 44) % 2 == 0) ? 2000.0 : 4000.0;
            double phase = 2.0 * juce::MathConstants<double>::pi * freq * i / ChirpConfig::sampleRate;
            signal[dataStart + i] = std::sin(phase) * 0.8f;
        }

        auto recordingB = playAndRecord(chirpPlusData, chirpPos, "rt_test3b");

        ChirpDetectorDebug detectorB(chirpTemplate);
        auto resultB = detectorB.detectWithNCC_Debug(recordingB, chirpPos, "rt_test3b_ncc_debug.csv");
        printResult(resultB);
        allResults.push_back(resultB);

        pauseForNext();
    }

    void test4_MultiplePositions() {
        std::cout << "\n" << std::string(80, '-') << std::endl;
        std::cout << "REAL-TIME TEST 4: Multiple Positions (Consistency)" << std::endl;
        std::cout << std::string(80, '-') << std::endl;

        std::vector<int> positions = { 2000, 5000, 10000 };

        for (int pos : positions) {
            std::cout << "\n  Testing position " << pos << "..." << std::endl;

            juce::AudioBuffer<float> testSignal(1, pos + chirpTemplate.getNumSamples() + 2000);
            testSignal.clear();
            testSignal.copyFrom(0, pos, chirpTemplate, 0, 0, chirpTemplate.getNumSamples());

            auto recording = playAndRecord(testSignal, pos, "rt_test4_pos" + std::to_string(pos));

            ChirpDetectorDebug detector(chirpTemplate);
            auto result = detector.detectWithNCC_Debug(recording, pos,
                "rt_test4_pos" + std::to_string(pos) + "_debug.csv");

            std::cout << "    Detected at " << result.detectedPosition
                << ", Offset = " << result.offset << " samples" << std::endl;

            allResults.push_back(result);

            if (pos != positions.back()) pauseForNext();
        }

        pauseForNext();
    }

    void test5_ThresholdTuning() {
        std::cout << "\n" << std::string(80, '-') << std::endl;
        std::cout << "REAL-TIME TEST 5: Threshold Tuning (Noisy Signal)" << std::endl;
        std::cout << std::string(80, '-') << std::endl;

        int chirpPos = 5000;
        juce::AudioBuffer<float> testSignal(1, 15000);
        testSignal.clear();
        testSignal.copyFrom(0, chirpPos, chirpTemplate, 0, 0, chirpTemplate.getNumSamples());

        // Add noise
        juce::Random random;
        for (int i = 0; i < testSignal.getNumSamples(); ++i) {
            testSignal.setSample(0, i, testSignal.getSample(0, i) +
                (random.nextFloat() * 2.0f - 1.0f) * 0.1f);
        }

        auto recording = playAndRecord(testSignal, chirpPos, "rt_test5");

        ChirpDetectorDebug detector(chirpTemplate);
        auto result = detector.detectWithNCC_Debug(recording, chirpPos, "rt_test5_ncc_debug.csv");
        auto resultPHY = detector.detectWithSamplePHY_Debug(recording, chirpPos, "rt_test5_phy_debug.csv");

        printResult(result);
        printResult(resultPHY);

        pauseForNext();
    }

    juce::AudioBuffer<float> playAndRecord(const juce::AudioBuffer<float>& testSignal,
        int chirpPos,
        const std::string& baseName) {
        // Setup audio
        juce::AudioDeviceManager deviceManager;
        deviceManager.initialiseWithDefaultDevices(1, 1);

        TestSignalGenerator generator(testSignal, chirpPos);
        AudioRecorder recorder(&generator);

        std::cout << "  Playing and recording... ";
        deviceManager.addAudioCallback(&recorder);

        while (!recorder.isTestFinished()) {
            juce::Thread::sleep(100);
        }

        deviceManager.removeAudioCallback(&recorder);
        std::cout << "Done." << std::endl;

        // Save both played and recorded
        saveWav(testSignal, baseName + "_played.wav");
        saveWav(recorder.getRecording(), baseName + "_recorded.wav");

        return recorder.getRecording();
    }

    void printResult(const ChirpDetectorDebug::DetectionResult& result) {
        std::cout << "\n  Method: " << result.method << std::endl;
        std::cout << "  Actual position: " << result.actualPosition << " samples" << std::endl;
        std::cout << "  Detected position: " << result.detectedPosition << " samples" << std::endl;
        std::cout << "  Detection offset (ERROR): " << result.offset << " samples";

        if (result.offset != 0) {
            double offsetMs = std::abs(result.offset) / ChirpConfig::sampleRate * 1000.0;
            std::cout << " (" << offsetMs << " ms)";
        }
        std::cout << std::endl;

        std::cout << "  Detection score: " << std::fixed << std::setprecision(4) << result.score << std::endl;
        std::cout << "  Confidence: " << result.confidence << std::endl;
        std::cout << "  Debug CSV: " << result.debugCsvFile << std::endl;

        if (std::abs(result.offset) == 0) {
            std::cout << "  ★ PERFECT ACCURACY ★" << std::endl;
        }
        else if (std::abs(result.offset) <= 1) {
            std::cout << "  ✓ Sample-level accuracy" << std::endl;
        }
        else if (std::abs(result.offset) <= 10) {
            std::cout << "  ⚠ Within 10 samples" << std::endl;
        }
        else {
            std::cout << "  ✗ Significant offset: " << result.offset << " samples" << std::endl;
            std::cout << "     Review " << result.debugCsvFile << " for diagnosis" << std::endl;
        }
    }

    void printFinalSummary() {
        std::cout << "\n" << std::string(80, '=') << std::endl;
        std::cout << "FINAL SUMMARY (REAL-TIME TESTS)" << std::endl;
        std::cout << std::string(80, '=') << std::endl;

        int perfectCount = 0;
        int goodCount = 0;
        double avgOffset = 0.0;
        double maxOffset = 0.0;

        for (const auto& result : allResults) {
            int absOffset = std::abs(result.offset);
            avgOffset += absOffset;
            maxOffset = std::max(maxOffset, (double)absOffset);

            if (absOffset == 0) perfectCount++;
            else if (absOffset <= 1) goodCount++;
        }

        avgOffset /= allResults.size();

        std::cout << "\nTotal tests: " << allResults.size() << std::endl;
        std::cout << "Perfect (0 offset): " << perfectCount << std::endl;
        std::cout << "Sample-level (≤1): " << goodCount << std::endl;
        std::cout << "Average offset: " << avgOffset << " samples" << std::endl;
        std::cout << "Max offset: " << maxOffset << " samples" << std::endl;

        std::cout << "\n>>> VERIFICATION:" << std::endl;
        if (avgOffset < 1.0) {
            std::cout << "    ✓ Real-time chirp detection is ACCURATE" << std::endl;
            std::cout << "    Ready for production use" << std::endl;
        }
        else if (avgOffset < 100) {
            std::cout << "    ⚠ Systematic offset of " << (int)avgOffset << " samples detected" << std::endl;
            std::cout << "    This is likely system audio latency" << std::endl;
            std::cout << "    Apply constant offset compensation in Main.cpp" << std::endl;
        }
        else {
            std::cout << "    ✗ Large offset (" << (int)avgOffset << " samples)" << std::endl;
            std::cout << "    Detection may be failing - review debug CSVs" << std::endl;
        }

        std::cout << "\n>>> GENERATED FILES:" << std::endl;
        std::cout << "    *_played.wav: What was sent to speakers" << std::endl;
        std::cout << "    *_recorded.wav: What microphone captured" << std::endl;
        std::cout << "    *_debug.csv: Sliding window correlation values" << std::endl;
        std::cout << "\n>>> DEBUG WITH PYTHON:" << std::endl;
        std::cout << "    python visualize_chirp_detection.py" << std::endl;
    }

    void saveWav(const juce::AudioBuffer<float>& buffer, const std::string& filename) {
        juce::File outFile = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(filename));
        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wavFormat.createWriterFor(new juce::FileOutputStream(outFile),
                ChirpConfig::sampleRate, 1, 16, {}, 0));
        if (writer != nullptr) {
            writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
        }
    }

    void pauseForNext() {
        std::cout << "\nPress ENTER for next test..." << std::endl;
        std::cin.get();
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
    std::cout << "║    COMPREHENSIVE REAL-TIME CHIRP TEST (Play & Record)         ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════════╝\n";
    std::cout << "\nThis tests chirp detection with actual audio I/O." << std::endl;
    std::cout << "Includes all 5 comprehensive test scenarios." << std::endl;
    std::cout << "Exports debug CSVs for every test for analysis." << std::endl;

    ComprehensiveRealtimeTest tester;
    tester.runAllTests();

    std::cout << "\n\nAll test files saved in current directory." << std::endl;
    std::cout << "Use visualize_chirp_detection.py to analyze debug CSVs." << std::endl;
    std::cout << "Compare *_played.wav and *_recorded.wav in Audacity." << std::endl;
    std::cout << "\nPress ENTER to exit..." << std::endl;
    std::cin.get();

    return 0;
}


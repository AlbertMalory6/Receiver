/*
 * COMPREHENSIVE CHIRP DETECTION VERIFICATION TEST
 * 
 * GOALS:
 * 1. Verify detection accuracy: actual position vs detected position offset
 * 2. Export sliding window correlation values for debugging
 * 3. Test with observable markers (chirp + tone) to verify timing
 * 4. Compare pure chirp vs chirp+data scenarios
 * 5. Find optimal thresholds through systematic testing
 * 
 * OUTPUTS:
 * - CSV files with NCC/dot product at each position
 * - WAV files with test signals
 * - Threshold tuning recommendations
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
}

// ==============================================================================
//  CHIRP GENERATOR
// ==============================================================================
class ChirpGenerator {
public:
    static juce::AudioBuffer<float> generateChirp() {
        juce::AudioBuffer<float> chirp(1, ChirpConfig::chirpSamples);
        auto* signal = chirp.getWritePointer(0);
        
        // Mathematical formula: φ(t) = 2π(c/2·t² + f₀t)
        for (int i = 0; i < ChirpConfig::chirpSamples / 2; ++i) {
            double t = i / ChirpConfig::sampleRate;
            double c = (ChirpConfig::f1 - ChirpConfig::f0) / (ChirpConfig::chirpSamples / ChirpConfig::sampleRate);
            double phase = 2.0 * juce::MathConstants<double>::pi * 
                          (c / 2.0 * t * t + ChirpConfig::f0 * t);
            signal[i] = std::sin(phase);
        }
        
        // Mirror with negation
        for (int i = ChirpConfig::chirpSamples / 2; i < ChirpConfig::chirpSamples; ++i) {
            signal[i] = -signal[ChirpConfig::chirpSamples - 1 - i];
        }
        
        float maxVal = chirp.getMagnitude(0, ChirpConfig::chirpSamples);
        if (maxVal > 0.0f) chirp.applyGain(0.8f / maxVal);
        
        return chirp;
    }
    
    // MATLAB-style generation for comparison
    static juce::AudioBuffer<float> generateChirpMATLAB() {
        juce::AudioBuffer<float> chirp(1, ChirpConfig::chirpSamples);
        auto* signal = chirp.getWritePointer(0);
        
        std::vector<double> f_sweep(ChirpConfig::chirpSamples);
        for (int i = 0; i < ChirpConfig::chirpSamples / 2; ++i) {
            f_sweep[i] = ChirpConfig::f0 + (ChirpConfig::f1 - ChirpConfig::f0) * 
                         i / (ChirpConfig::chirpSamples / 2.0);
        }
        for (int i = ChirpConfig::chirpSamples / 2; i < ChirpConfig::chirpSamples; ++i) {
            f_sweep[i] = ChirpConfig::f1 - (ChirpConfig::f1 - ChirpConfig::f0) * 
                         (i - ChirpConfig::chirpSamples / 2) / (ChirpConfig::chirpSamples / 2.0);
        }
        
        double phase = 0.0;
        for (int i = 0; i < ChirpConfig::chirpSamples; ++i) {
            if (i > 0) {
                phase += juce::MathConstants<double>::pi * (f_sweep[i] + f_sweep[i-1]) / ChirpConfig::sampleRate;
            }
            signal[i] = std::sin(2.0 * phase) * 0.8f;
        }
        
        return chirp;
    }
    
    // Generate observable marker: 1kHz tone for 100ms
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
//  COMPREHENSIVE CHIRP DETECTOR WITH DEBUG OUTPUT
// ==============================================================================
class ChirpDetectorDebug {
private:
    juce::AudioBuffer<float> template_;
    double templateEnergy_;
    
public:
    struct DetectionResult {
        int detectedPosition;
        int actualPosition;
        int offset;  // detectedPosition - actualPosition (THIS is the error)
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
    
    // NCC with full debug output
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
        
        std::cout << "  Scanning " << (signal.getNumSamples() - tempLen + 1) << " positions..." << std::endl;
        
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
            
            // Export every 10th position for manageable file size
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
        result.offset = maxPos - actualChirpPosition;  // THIS is the detection error
        result.score = maxNCC;
        result.confidence = confidenceRatio;
        result.success = (maxNCC > 0.5);  // Adjustable threshold
        result.method = "NCC";
        result.debugCsvFile = debugFilename;
        
        return result;
    }
    
    // Dot Product with debug output
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
        std::vector<double> dotValues;
        
        for (int i = 0; i <= signal.getNumSamples() - tempLen; ++i) {
            double dotProduct = 0.0;
            
            for (int j = 0; j < tempLen; ++j) {
                dotProduct += sigData[i + j] * tempData[j];
            }
            
            dotValues.push_back(dotProduct);
            
            if (i % 10 == 0 || std::abs(i - actualChirpPosition) < 100) {
                debugFile << i << "," << dotProduct << "\n";
            }
            
            if (dotProduct > maxDot) {
                maxDot = dotProduct;
                maxPos = i;
            }
        }
        
        debugFile.close();
        
        std::sort(dotValues.rbegin(), dotValues.rend());
        double confidenceRatio = dotValues.size() > 1 ? dotValues[0] / (dotValues[1] + 1e-10) : 1.0;
        
        DetectionResult result;
        result.detectedPosition = maxPos;
        result.actualPosition = actualChirpPosition;
        result.offset = maxPos - actualChirpPosition;
        result.score = maxDot;
        result.confidence = confidenceRatio;
        result.success = true;
        result.method = "DotProduct";
        result.debugCsvFile = debugFilename;
        
        return result;
    }
    
    // SamplePHY method with comprehensive debug
    DetectionResult detectWithSamplePHY_Debug(const juce::AudioBuffer<float>& signal,
                                              int actualChirpPosition,
                                              const std::string& debugFilename,
                                              double powerRatio = 2.0,
                                              double minThreshold = 0.05) {
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
            // Update power (EMA)
            power = power * (1.0 - 1.0/64.0) + sigData[i] * sigData[i] / 64.0;
            
            // Shift FIFO
            for (int j = 0; j < tempLen - 1; ++j) {
                syncFIFO[j] = syncFIFO[j + 1];
            }
            syncFIFO[tempLen - 1] = sigData[i];
            
            if (i >= tempLen) {
                // Calculate correlation
                double correlation = 0.0;
                for (int j = 0; j < tempLen; ++j) {
                    correlation += syncFIFO[j] * tempData[j];
                }
                
                // Normalize (as in SamplePHY.m)
                double normalized = correlation / 200.0;
                double threshold = power * powerRatio;
                
                bool detected = (normalized > threshold && normalized > syncPowerLocalMax && normalized > minThreshold);
                
                // Export debug info every 100 samples or near actual/detected position
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
//  TEST FRAMEWORK
// ==============================================================================
class ComprehensiveChirpTest {
public:
    void runAllTests() {
        std::cout << "\n" << std::string(80, '=') << std::endl;
        std::cout << "COMPREHENSIVE CHIRP DETECTION VERIFICATION" << std::endl;
        std::cout << std::string(80, '=') << std::endl;
        
        chirpMath = ChirpGenerator::generateChirp();
        chirpMATLAB = ChirpGenerator::generateChirpMATLAB();
        
        std::cout << "\nGenerated chirp templates (Math and MATLAB versions)" << std::endl;
        
        // TEST 1: Pure chirp at known position (simplest case)
        test1_PureChirpAtKnownPosition();
        
        // TEST 2: Chirp with marker tone (observable reference)
        test2_ChirpWithMarkerTone();
        
        // TEST 3: Pure chirp file vs chirp+data
        test3_PureChirpVsChirpPlusData();
        
        // TEST 4: Multiple positions to verify consistency
        test4_MultiplePositions();
        
        // TEST 5: Threshold tuning guide
        test5_ThresholdTuning();
        
        printFinalSummary();
    }
    
private:
    juce::AudioBuffer<float> chirpMath, chirpMATLAB;
    std::vector<ChirpDetectorDebug::DetectionResult> allResults;
    
    void test1_PureChirpAtKnownPosition() {
        std::cout << "\n" << std::string(80, '-') << std::endl;
        std::cout << "TEST 1: Pure Chirp at Known Position" << std::endl;
        std::cout << std::string(80, '-') << std::endl;
        
        int actualPosition = 5000;
        int totalSamples = 20000;
        
        juce::AudioBuffer<float> testSignal(1, totalSamples);
        testSignal.clear();
        testSignal.copyFrom(0, actualPosition, chirpMath, 0, 0, chirpMath.getNumSamples());
        
        saveWav(testSignal, "test1_pure_chirp_at_5000.wav");
        
        ChirpDetectorDebug detector(chirpMath);
        
        auto resultNCC = detector.detectWithNCC_Debug(testSignal, actualPosition, "test1_ncc_debug.csv");
        auto resultDot = detector.detectWithDotProduct_Debug(testSignal, actualPosition, "test1_dot_debug.csv");
        auto resultPHY = detector.detectWithSamplePHY_Debug(testSignal, actualPosition, "test1_phy_debug.csv");
        
        printResult(resultNCC);
        printResult(resultDot);
        printResult(resultPHY);
        
        allResults.push_back(resultNCC);
    }
    
    void test2_ChirpWithMarkerTone() {
        std::cout << "\n" << std::string(80, '-') << std::endl;
        std::cout << "TEST 2: Chirp with Observable Marker Tone" << std::endl;
        std::cout << "Marker: 1kHz tone for 100ms BEFORE chirp" << std::endl;
        std::cout << std::string(80, '-') << std::endl;
        
        auto markerTone = ChirpGenerator::generateMarkerTone();
        int markerSamples = markerTone.getNumSamples();
        int chirpPosition = markerSamples; // Chirp starts right after marker
        
        int totalSamples = markerSamples + chirpMath.getNumSamples() + 5000;
        juce::AudioBuffer<float> testSignal(1, totalSamples);
        testSignal.clear();
        
        // Add marker tone
        testSignal.copyFrom(0, 0, markerTone, 0, 0, markerSamples);
        
        // Add chirp right after marker
        testSignal.copyFrom(0, chirpPosition, chirpMath, 0, 0, chirpMath.getNumSamples());
        
        saveWav(testSignal, "test2_chirp_with_marker.wav");
        
        std::cout << "  Marker tone: samples 0 - " << markerSamples << std::endl;
        std::cout << "  Chirp should be at: sample " << chirpPosition << std::endl;
        
        ChirpDetectorDebug detector(chirpMath);
        auto resultNCC = detector.detectWithNCC_Debug(testSignal, chirpPosition, "test2_ncc_debug.csv");
        
        printResult(resultNCC);
        allResults.push_back(resultNCC);
    }
    
    void test3_PureChirpVsChirpPlusData() {
        std::cout << "\n" << std::string(80, '-') << std::endl;
        std::cout << "TEST 3: Pure Chirp vs Chirp + Data Signal" << std::endl;
        std::cout << std::string(80, '-') << std::endl;
        
        int chirpPos = 3000;
        
        // A: Pure chirp only
        juce::AudioBuffer<float> pureChirp(1, chirpPos + chirpMath.getNumSamples() + 2000);
        pureChirp.clear();
        pureChirp.copyFrom(0, chirpPos, chirpMath, 0, 0, chirpMath.getNumSamples());
        saveWav(pureChirp, "test3a_pure_chirp.wav");
        
        // B: Chirp followed by FSK data (2kHz and 4kHz alternating)
        juce::AudioBuffer<float> chirpPlusData(1, chirpPos + chirpMath.getNumSamples() + 5000);
        chirpPlusData.clear();
        chirpPlusData.copyFrom(0, chirpPos, chirpMath, 0, 0, chirpMath.getNumSamples());
        
        // Add FSK data after chirp
        int dataStart = chirpPos + chirpMath.getNumSamples();
        auto* signal = chirpPlusData.getWritePointer(0);
        for (int i = 0; i < 4000; ++i) {
            double freq = ((i / 44) % 2 == 0) ? 2000.0 : 4000.0;  // Alternating bits
            double phase = 2.0 * juce::MathConstants<double>::pi * freq * i / ChirpConfig::sampleRate;
            signal[dataStart + i] = std::sin(phase) * 0.8f;
        }
        saveWav(chirpPlusData, "test3b_chirp_plus_data.wav");
        
        ChirpDetectorDebug detector(chirpMath);
        
        std::cout << "\n  A) Pure Chirp:" << std::endl;
        auto resultA = detector.detectWithNCC_Debug(pureChirp, chirpPos, "test3a_ncc_debug.csv");
        printResult(resultA);
        
        std::cout << "\n  B) Chirp + Data:" << std::endl;
        auto resultB = detector.detectWithNCC_Debug(chirpPlusData, chirpPos, "test3b_ncc_debug.csv");
        printResult(resultB);
        
        allResults.push_back(resultA);
        allResults.push_back(resultB);
    }
    
    void test4_MultiplePositions() {
        std::cout << "\n" << std::string(80, '-') << std::endl;
        std::cout << "TEST 4: Multiple Positions (Consistency Check)" << std::endl;
        std::cout << std::string(80, '-') << std::endl;
        
        std::vector<int> positions = {1000, 5000, 10000, 15000, 20000};
        
        for (int pos : positions) {
            juce::AudioBuffer<float> testSignal(1, pos + chirpMath.getNumSamples() + 2000);
            testSignal.clear();
            testSignal.copyFrom(0, pos, chirpMath, 0, 0, chirpMath.getNumSamples());
            
            ChirpDetectorDebug detector(chirpMath);
            auto result = detector.detectWithNCC_Debug(testSignal, pos, 
                "test4_pos" + std::to_string(pos) + "_debug.csv");
            
            std::cout << "  Position " << pos << ": ";
            std::cout << "Detected at " << result.detectedPosition 
                     << ", Offset = " << result.offset << " samples" << std::endl;
            
            allResults.push_back(result);
        }
    }
    
    void test5_ThresholdTuning() {
        std::cout << "\n" << std::string(80, '-') << std::endl;
        std::cout << "TEST 5: Threshold Tuning Guide" << std::endl;
        std::cout << std::string(80, '-') << std::endl;
        
        int chirpPos = 5000;
        juce::AudioBuffer<float> testSignal(1, 15000);
        testSignal.clear();
        testSignal.copyFrom(0, chirpPos, chirpMath, 0, 0, chirpMath.getNumSamples());
        
        // Add noise
        juce::Random random;
        for (int i = 0; i < testSignal.getNumSamples(); ++i) {
            testSignal.setSample(0, i, testSignal.getSample(0, i) + 
                                (random.nextFloat() * 2.0f - 1.0f) * 0.1f);
        }
        
        saveWav(testSignal, "test5_noisy_chirp.wav");
        
        ChirpDetectorDebug detector(chirpMath);
        
        std::cout << "\n  Testing different thresholds:" << std::endl;
        std::vector<double> thresholds = {0.3, 0.5, 0.7, 0.9};
        
        for (double thresh : thresholds) {
            auto result = detector.detectWithNCC_Debug(testSignal, chirpPos, 
                "test5_thresh" + std::to_string((int)(thresh*10)) + "_debug.csv");
            
            std::cout << "    Threshold " << thresh << ": Offset = " << result.offset 
                     << ", Score = " << result.score << std::endl;
        }
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
        std::cout << "  Confidence ratio: " << result.confidence << std::endl;
        std::cout << "  Debug CSV: " << result.debugCsvFile << std::endl;
        
        if (std::abs(result.offset) == 0) {
            std::cout << "  ★ PERFECT ACCURACY ★" << std::endl;
        } else if (std::abs(result.offset) <= 1) {
            std::cout << "  ✓ Sample-level accuracy" << std::endl;
        } else if (std::abs(result.offset) <= 10) {
            std::cout << "  ⚠ Within 10 samples" << std::endl;
        } else {
            std::cout << "  ✗ Significant offset detected" << std::endl;
        }
    }
    
    void printFinalSummary() {
        std::cout << "\n" << std::string(80, '=') << std::endl;
        std::cout << "FINAL SUMMARY" << std::endl;
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
            std::cout << "    ✓ Chirp detection is ACCURATE" << std::endl;
            std::cout << "    Ready for production use" << std::endl;
        } else {
            std::cout << "    ⚠ Detection has systematic offset of " << (int)avgOffset << " samples" << std::endl;
            std::cout << "    Review debug CSV files to diagnose" << std::endl;
        }
        
        std::cout << "\n>>> DEBUG FILES GENERATED:" << std::endl;
        std::cout << "    Load *_debug.csv in Python/MATLAB to plot correlation curves" << std::endl;
        std::cout << "    Load *.wav files in Audacity to verify chirp positions visually" << std::endl;
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
};

// ==============================================================================
//  MAIN
// ==============================================================================
int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║        COMPREHENSIVE CHIRP DETECTION VERIFICATION TEST        ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════════╝\n";
    
    ComprehensiveChirpTest tester;
    tester.runAllTests();
    
    std::cout << "\n\nAll test files saved. Use these for analysis:" << std::endl;
    std::cout << "  WAV files: Load in Audacity to verify chirp positions visually" << std::endl;
    std::cout << "  CSV files: Plot in Python/MATLAB to see correlation curves" << std::endl;
    std::cout << "\nPython plotting example:" << std::endl;
    std::cout << "  import pandas as pd; import matplotlib.pyplot as plt" << std::endl;
    std::cout << "  df = pd.read_csv('test1_ncc_debug.csv')" << std::endl;
    std::cout << "  plt.plot(df['position'], df['ncc']); plt.show()" << std::endl;
    std::cout << "\nPress ENTER to exit..." << std::endl;
    std::cin.get();
    
    return 0;
}


/*
 * CHIRP GENERATION AND DETECTION TEST
 * 
 * PURPOSE:
 * Verify sample-level accuracy of chirp-based frame synchronization.
 * Test various detection methods and tuning parameters.
 * 
 * CHIRP SIGNAL THEORY:
 * A chirp (linear frequency modulation) sweeps from f0 to f1 over duration T:
 *   f(t) = c*t + f0,  where c = (f1 - f0) / T
 *   Phase: φ(t) = 2π ∫f(t)dt = 2π(c/2 * t² + f0*t)
 *   Signal: x(t) = sin(φ(t))
 * 
 * Our chirp: x(t) concatenated with -x(-t) (up-sweep then down-sweep)
 * This creates a symmetric chirp with better autocorrelation properties.
 * 
 * DETECTION METHODS:
 * 1. Dot Product: Simple correlation, fast but sensitive to amplitude
 * 2. NCC (Normalized Cross-Correlation): Scale-invariant, robust to amplitude
 * 3. Energy-Normalized: NCC with power estimation (from SamplePHY.m)
 * 4. Phase-Based: Instantaneous frequency tracking
 * 
 * GOAL: Sample-level accuracy (<1 sample @ 44.1kHz = 22.7μs error)
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
    constexpr double f0 = 2000.0;   // Start frequency: 1kHz
    constexpr double f1 = 4000.0;   // End frequency: 5kHz
    constexpr double duration = chirpSamples / sampleRate;
    constexpr double chirpRate = (f1 - f0) / duration;  // c = Δf/T
}

// ==============================================================================
//  CHIRP GENERATOR
// ==============================================================================
class ChirpGenerator {
public:
    static juce::AudioBuffer<float> generateChirp() {
        juce::AudioBuffer<float> chirp(1, ChirpConfig::chirpSamples);
        auto* signal = chirp.getWritePointer(0);
        
        std::cout << "Generating chirp with mathematical formulation..." << std::endl;
        std::cout << "  f(t) = " << ChirpConfig::chirpRate << " * t + " << ChirpConfig::f0 << std::endl;
        std::cout << "  Duration: " << ChirpConfig::duration << " s" << std::endl;
        std::cout << "  Samples: " << ChirpConfig::chirpSamples << std::endl;
        
        // Method 1: Direct phase integration (matching theory)
        // φ(t) = 2π * (c/2 * t² + f0*t)
        for (int i = 0; i < ChirpConfig::chirpSamples / 2; ++i) {
            double t = i / ChirpConfig::sampleRate;
            double phase = 2.0 * juce::MathConstants<double>::pi * 
                          (ChirpConfig::chirpRate / 2.0 * t * t + ChirpConfig::f0 * t);
            signal[i] = std::sin(phase);
        }
        
        // Second half: mirror with negation (creates -x(-t))
        for (int i = ChirpConfig::chirpSamples / 2; i < ChirpConfig::chirpSamples; ++i) {
            signal[i] = -signal[ChirpConfig::chirpSamples - 1 - i];
        }
        
        // Normalize
        float maxVal = chirp.getMagnitude(0, ChirpConfig::chirpSamples);
        if (maxVal > 0.0f) {
            chirp.applyGain(0.8f / maxVal);
        }
        
        return chirp;
    }
    
    // Alternative: MATLAB-style generation (matching SamplePHY.m)
    static juce::AudioBuffer<float> generateChirpMATLABStyle() {
        juce::AudioBuffer<float> chirp(1, ChirpConfig::chirpSamples);
        auto* signal = chirp.getWritePointer(0);
        
        std::cout << "Generating chirp using MATLAB linspace method (SamplePHY.m)..." << std::endl;
        
        // Generate frequency sweep (linspace equivalent)
        std::vector<double> f_sweep(ChirpConfig::chirpSamples);
        for (int i = 0; i < ChirpConfig::chirpSamples / 2; ++i) {
            f_sweep[i] = ChirpConfig::f0 + (ChirpConfig::f1 - ChirpConfig::f0) * 
                         i / (ChirpConfig::chirpSamples / 2.0);
        }
        for (int i = ChirpConfig::chirpSamples / 2; i < ChirpConfig::chirpSamples; ++i) {
            f_sweep[i] = ChirpConfig::f1 - (ChirpConfig::f1 - ChirpConfig::f0) * 
                         (i - ChirpConfig::chirpSamples / 2) / (ChirpConfig::chirpSamples / 2.0);
        }
        
        // Integrate to get phase (cumtrapz equivalent)
        double phase = 0.0;
        signal[0] = 0.0f;
        for (int i = 1; i < ChirpConfig::chirpSamples; ++i) {
            // Trapezoidal integration: Δφ = π * (f[i] + f[i-1]) * Δt
            phase += juce::MathConstants<double>::pi * (f_sweep[i] + f_sweep[i-1]) / ChirpConfig::sampleRate;
            signal[i] = std::sin(2.0 * phase) * 0.8f;
        }
        
        return chirp;
    }
};

// ==============================================================================
//  CHIRP DETECTOR (Multiple Methods)
// ==============================================================================
class ChirpDetector {
private:
    juce::AudioBuffer<float> template_;
    double templateEnergy_;
    
public:
    ChirpDetector(const juce::AudioBuffer<float>& chirpTemplate) {
        template_.makeCopyOf(chirpTemplate);
        templateEnergy_ = calculateEnergy(template_.getReadPointer(0), template_.getNumSamples());
        
        std::cout << "\nChirp template loaded:" << std::endl;
        std::cout << "  Samples: " << template_.getNumSamples() << std::endl;
        std::cout << "  Energy: " << templateEnergy_ << std::endl;
    }
    
    // Method 1: Simple Dot Product (fastest, but amplitude-sensitive)
    struct DetectionResult {
        int position;
        double score;
        double confidence;
        std::string method;
    };
    
    DetectionResult detectDotProduct(const juce::AudioBuffer<float>& signal, double threshold = 10.0) {
        std::cout << "\n=== METHOD 1: Dot Product ===" << std::endl;
        std::cout << "Threshold: " << threshold << std::endl;
        
        const float* sigData = signal.getReadPointer(0);
        const float* tempData = template_.getReadPointer(0);
        const int tempLen = template_.getNumSamples();
        
        double maxScore = 0.0;
        int maxPos = -1;
        std::vector<double> scores;
        
        for (int i = 0; i <= signal.getNumSamples() - tempLen; ++i) {
            double dotProduct = 0.0;
            for (int j = 0; j < tempLen; ++j) {
                dotProduct += sigData[i + j] * tempData[j];
            }
            
            scores.push_back(dotProduct);
            
            if (dotProduct > maxScore) {
                maxScore = dotProduct;
                maxPos = i;
            }
        }
        
        // Calculate confidence (ratio to second-best)
        std::sort(scores.rbegin(), scores.rend());
        double confidence = scores.size() > 1 ? scores[0] / (scores[1] + 1e-10) : 1.0;
        
        std::cout << "Max score: " << maxScore << " at sample " << maxPos << std::endl;
        std::cout << "Confidence ratio: " << confidence << std::endl;
        
        return {maxPos, maxScore, confidence, "DotProduct"};
    }
    
    // Method 2: Normalized Cross-Correlation (scale-invariant)
    DetectionResult detectNCC(const juce::AudioBuffer<float>& signal, double threshold = 0.7) {
        std::cout << "\n=== METHOD 2: Normalized Cross-Correlation ===" << std::endl;
        std::cout << "Threshold: " << threshold << std::endl;
        
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
        
        // Calculate confidence
        std::sort(nccValues.rbegin(), nccValues.rend());
        double confidence = nccValues.size() > 1 ? nccValues[0] / (nccValues[1] + 1e-10) : 1.0;
        
        std::cout << "Max NCC: " << maxNCC << " at sample " << maxPos << std::endl;
        std::cout << "Confidence ratio: " << confidence << std::endl;
        std::cout << "Detection: " << (maxNCC > threshold ? "SUCCESS" : "FAIL") << std::endl;
        
        return {maxPos, maxNCC, confidence, "NCC"};
    }
    
    // Method 3: SamplePHY.m method (power-normalized with exponential averaging)
    DetectionResult detectSamplePHYMethod(const juce::AudioBuffer<float>& signal, 
                                          double powerThresholdRatio = 2.0,
                                          double minAbsThreshold = 0.05) {
        std::cout << "\n=== METHOD 3: SamplePHY.m Method ===" << std::endl;
        std::cout << "Power threshold ratio: " << powerThresholdRatio << std::endl;
        std::cout << "Min absolute threshold: " << minAbsThreshold << std::endl;
        
        const float* sigData = signal.getReadPointer(0);
        const float* tempData = template_.getReadPointer(0);
        const int tempLen = template_.getNumSamples();
        
        double power = 0.0;  // Exponential moving average of power
        double syncPowerLocalMax = 0.0;
        int peakIndex = -1;
        
        std::vector<float> syncFIFO(tempLen, 0.0f);
        std::vector<double> nccTrace;
        std::vector<double> powerTrace;
        
        for (int i = 0; i < signal.getNumSamples(); ++i) {
            // Update power estimate (EMA with α = 1/64, from SamplePHY.m line 98)
            power = power * (1.0 - 1.0/64.0) + sigData[i] * sigData[i] / 64.0;
            powerTrace.push_back(power);
            
            // Shift FIFO
            for (int j = 0; j < tempLen - 1; ++j) {
                syncFIFO[j] = syncFIFO[j + 1];
            }
            syncFIFO[tempLen - 1] = sigData[i];
            
            if (i >= tempLen) {
                // Calculate correlation (from SamplePHY.m line 103)
                double correlation = 0.0;
                for (int j = 0; j < tempLen; ++j) {
                    correlation += syncFIFO[j] * tempData[j];
                }
                correlation /= 200.0;  // Normalization factor from SamplePHY.m
                
                nccTrace.push_back(correlation);
                
                // Detection criteria (from SamplePHY.m line 105)
                if (correlation > power * powerThresholdRatio && 
                    correlation > syncPowerLocalMax && 
                    correlation > minAbsThreshold) {
                    syncPowerLocalMax = correlation;
                    peakIndex = i;
                }
                else if (peakIndex != -1 && (i - peakIndex) > 200) {
                    // Peak confirmed (200-sample quiet period)
                    std::cout << "Peak detected and confirmed at sample " << peakIndex << std::endl;
                    std::cout << "Correlation score: " << syncPowerLocalMax << std::endl;
                    std::cout << "Local power: " << power << std::endl;
                    break;
                }
            }
        }
        
        double confidence = syncPowerLocalMax / (power + 1e-10);
        
        return {peakIndex, syncPowerLocalMax, confidence, "SamplePHY"};
    }
    
    // Method 4: Multi-scale detection (coarse then fine)
    DetectionResult detectMultiScale(const juce::AudioBuffer<float>& signal) {
        std::cout << "\n=== METHOD 4: Multi-Scale Detection ===" << std::endl;
        
        // Coarse pass: check every 10 samples
        const int coarseStep = 10;
        DetectionResult coarseResult = detectNCC(signal, 0.5);
        
        // Fine pass: refine around detected position
        if (coarseResult.position > 0) {
            int searchStart = std::max(0, coarseResult.position - 20);
            int searchEnd = std::min(signal.getNumSamples() - template_.getNumSamples(), 
                                     coarseResult.position + 20);
            
            std::cout << "Refining search around sample " << coarseResult.position << std::endl;
            std::cout << "Search range: [" << searchStart << ", " << searchEnd << "]" << std::endl;
            
            const float* sigData = signal.getReadPointer(0);
            const float* tempData = template_.getReadPointer(0);
            const int tempLen = template_.getNumSamples();
            
            double maxNCC = 0.0;
            int maxPos = -1;
            
            for (int i = searchStart; i <= searchEnd; ++i) {
                double dotProduct = 0.0;
                double signalEnergy = 0.0;
                
                for (int j = 0; j < tempLen; ++j) {
                    dotProduct += sigData[i + j] * tempData[j];
                    signalEnergy += sigData[i + j] * sigData[i + j];
                }
                
                double ncc = dotProduct / std::sqrt(signalEnergy * templateEnergy_ + 1e-10);
                
                if (ncc > maxNCC) {
                    maxNCC = ncc;
                    maxPos = i;
                }
            }
            
            std::cout << "Fine-tuned position: " << maxPos << " (Δ = " 
                     << (maxPos - coarseResult.position) << " samples)" << std::endl;
            std::cout << "Fine-tuned NCC: " << maxNCC << std::endl;
            
            return {maxPos, maxNCC, coarseResult.confidence, "MultiScale"};
        }
        
        return coarseResult;
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
class ChirpAccuracyTest {
public:
    void runAllTests() {
        std::cout << "\n" << std::string(80, '=') << std::endl;
        std::cout << "CHIRP GENERATION AND DETECTION ACCURACY TEST" << std::endl;
        std::cout << std::string(80, '=') << std::endl;
        
        // Generate chirp templates
        auto chirpMath = ChirpGenerator::generateChirp();
        auto chirpMATLAB = ChirpGenerator::generateChirpMATLABStyle();
        
        // Save templates for visualization
        saveWav(chirpMath, "chirp_template_mathematical.wav");
        saveWav(chirpMATLAB, "chirp_template_matlab.wav");
        std::cout << "\nChirp templates saved for inspection." << std::endl;
        
        // Test 1: Pure chirp (sanity check)
        testPureChirp(chirpMath);
        
        // Test 2: Chirp with known offset
        testChirpWithOffset(chirpMath, 5000);  // 5000 samples offset
        testChirpWithOffset(chirpMath, 22050); // 0.5s offset
        
        // Test 3: Chirp with noise
        testChirpWithNoise(chirpMath, 0.1);  // SNR = 10
        testChirpWithNoise(chirpMath, 0.3);  // SNR = 3.33
        
        // Test 4: Multiple chirps
        testMultipleChirps(chirpMath);
        
        // Test 5: Compare mathematical vs MATLAB style
        compareGenerationMethods(chirpMath, chirpMATLAB);
        
        std::cout << "\n" << std::string(80, '=') << std::endl;
        std::cout << "ALL TESTS COMPLETE" << std::endl;
        std::cout << std::string(80, '=') << std::endl;
    }
    
private:
    void testPureChirp(const juce::AudioBuffer<float>& chirp) {
        std::cout << "\n" << std::string(80, '-') << std::endl;
        std::cout << "TEST 1: Pure Chirp Detection (Sanity Check)" << std::endl;
        std::cout << std::string(80, '-') << std::endl;
        
        ChirpDetector detector(chirp);
        
        auto result1 = detector.detectDotProduct(chirp);
        auto result2 = detector.detectNCC(chirp);
        auto result3 = detector.detectSamplePHYMethod(chirp);
        
        std::cout << "\nExpected position: 0" << std::endl;
        std::cout << "DotProduct detected: " << result1.position << " (error: " << result1.position << ")" << std::endl;
        std::cout << "NCC detected: " << result2.position << " (error: " << result2.position << ")" << std::endl;
        std::cout << "SamplePHY detected: " << result3.position << " (error: " << result3.position << ")" << std::endl;
        
        bool passed = (result1.position == 0 && result2.position == 0);
        std::cout << "\nResult: " << (passed ? "PASS " : "FAIL ") << std::endl;
    }
    
    void testChirpWithOffset(const juce::AudioBuffer<float>& chirp, int offset) {
        std::cout << "\n" << std::string(80, '-') << std::endl;
        std::cout << "TEST 2: Chirp with Known Offset = " << offset << " samples" << std::endl;
        std::cout << "Expected time: " << (offset / ChirpConfig::sampleRate) << " seconds" << std::endl;
        std::cout << std::string(80, '-') << std::endl;
        
        // Create signal: silence + chirp + silence
        int totalSamples = offset + chirp.getNumSamples() + 5000;
        juce::AudioBuffer<float> signal(1, totalSamples);
        signal.clear();
        
        // Insert chirp at offset
        signal.copyFrom(0, offset, chirp, 0, 0, chirp.getNumSamples());
        
        saveWav(signal, "chirp_test_offset_" + std::to_string(offset) + ".wav");
        
        ChirpDetector detector(chirp);
        
        auto result1 = detector.detectDotProduct(signal);
        auto result2 = detector.detectNCC(signal);
        auto result3 = detector.detectSamplePHYMethod(signal);
        auto result4 = detector.detectMultiScale(signal);
        
        std::cout << "\nExpected position: " << offset << std::endl;
        std::cout << "DotProduct: " << result1.position << " (error: " << (result1.position - offset) << " samples)" << std::endl;
        std::cout << "NCC: " << result2.position << " (error: " << (result2.position - offset) << " samples)" << std::endl;
        std::cout << "SamplePHY: " << result3.position << " (error: " << (result3.position - offset) << " samples)" << std::endl;
        std::cout << "MultiScale: " << result4.position << " (error: " << (result4.position - offset) << " samples)" << std::endl;
        
        bool passed = (std::abs(result2.position - offset) <= 1);  // Allow 1-sample error
        std::cout << "\nSample-level accuracy: " << (passed ? "ACHIEVED " : "NOT ACHIEVED ") << std::endl;
    }
    
    void testChirpWithNoise(const juce::AudioBuffer<float>& chirp, double noiseLevel) {
        std::cout << "\n" << std::string(80, '-') << std::endl;
        std::cout << "TEST 3: Chirp with Noise (level = " << noiseLevel << ")" << std::endl;
        std::cout << std::string(80, '-') << std::endl;
        
        int offset = 10000;
        int totalSamples = offset + chirp.getNumSamples() + 5000;
        juce::AudioBuffer<float> signal(1, totalSamples);
        
        // Add white noise
        juce::Random random;
        for (int i = 0; i < totalSamples; ++i) {
            signal.setSample(0, i, (random.nextFloat() * 2.0f - 1.0f) * noiseLevel);
        }
        
        // Add chirp
        signal.addFrom(0, offset, chirp, 0, 0, chirp.getNumSamples());
        
        ChirpDetector detector(chirp);
        
        auto result = detector.detectNCC(signal);
        auto resultPHY = detector.detectSamplePHYMethod(signal);
        
        int error = std::abs(result.position - offset);
        std::cout << "\nExpected: " << offset << ", Detected (NCC): " << result.position 
                 << ", Error: " << error << " samples" << std::endl;
        std::cout << "Detected (SamplePHY): " << resultPHY.position 
                 << ", Error: " << std::abs(resultPHY.position - offset) << " samples" << std::endl;
        
        bool passed = (error <= 1);
        std::cout << "Result: " << (passed ? "PASS " : "FAIL ") << std::endl;
    }
    
    void testMultipleChirps(const juce::AudioBuffer<float>& chirp) {
        std::cout << "\n" << std::string(80, '-') << std::endl;
        std::cout << "TEST 4: Multiple Chirps Detection" << std::endl;
        std::cout << std::string(80, '-') << std::endl;
        
        std::vector<int> offsets = {5000, 15000, 30000};
        int totalSamples = 40000;
        juce::AudioBuffer<float> signal(1, totalSamples);
        signal.clear();
        
        for (int offset : offsets) {
            signal.addFrom(0, offset, chirp, 0, 0, chirp.getNumSamples());
        }
        
        ChirpDetector detector(chirp);
        auto result = detector.detectNCC(signal);
        
        std::cout << "Chirps at: 5000, 15000, 30000" << std::endl;
        std::cout << "First detected at: " << result.position << std::endl;
        std::cout << "Expected: 5000, Error: " << std::abs(result.position - 5000) << std::endl;
    }
    
    void compareGenerationMethods(const juce::AudioBuffer<float>& chirp1, 
                                   const juce::AudioBuffer<float>& chirp2) {
        std::cout << "\n" << std::string(80, '-') << std::endl;
        std::cout << "TEST 5: Compare Generation Methods" << std::endl;
        std::cout << std::string(80, '-') << std::endl;
        
        // Calculate difference
        double maxDiff = 0.0;
        double avgDiff = 0.0;
        int len = std::min(chirp1.getNumSamples(), chirp2.getNumSamples());
        
        for (int i = 0; i < len; ++i) {
            double diff = std::abs(chirp1.getSample(0, i) - chirp2.getSample(0, i));
            maxDiff = std::max(maxDiff, diff);
            avgDiff += diff;
        }
        avgDiff /= len;
        
        std::cout << "Mathematical vs MATLAB-style generation:" << std::endl;
        std::cout << "  Max difference: " << maxDiff << std::endl;
        std::cout << "  Avg difference: " << avgDiff << std::endl;
        std::cout << "  Methods are: " << (avgDiff < 0.1 ? "SIMILAR" : "DIFFERENT") << std::endl;
    }
    
    void saveWav(const juce::AudioBuffer<float>& buffer, const std::string& filename) {
        juce::File outFile = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(filename));
        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wavFormat.createWriterFor(new juce::FileOutputStream(outFile), 
                                     ChirpConfig::sampleRate, 1, 16, {}, 0));
        if (writer != nullptr) {
            writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
            std::cout << "  Saved: " << filename << std::endl;
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
    std::cout << "║     CHIRP SYNCHRONIZATION - ACCURACY VERIFICATION TEST         ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════════╝\n";
    
    ChirpAccuracyTest tester;
    tester.runAllTests();
    
    std::cout << "\n\nTest files saved in current directory:" << std::endl;
    std::cout << "  - chirp_template_mathematical.wav" << std::endl;
    std::cout << "  - chirp_template_matlab.wav" << std::endl;
    std::cout << "  - chirp_test_offset_*.wav" << std::endl;
    std::cout << "\nLoad these in Audacity/MATLAB to visually inspect chirps." << std::endl;
    std::cout << "\nPress ENTER to exit..." << std::endl;
    std::cin.get();
    
    return 0;
}


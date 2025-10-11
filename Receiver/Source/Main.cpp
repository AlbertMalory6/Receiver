/*
 * REAL-TIME CHIRP PLAY & RECORD TEST (Updated for latest JUCE)
 * 
 * Updated to use audioDeviceIOCallbackWithContext instead of deprecated audioDeviceIOCallback
 */

#include <JuceHeader.h>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <thread>

namespace ChirpConfig {
    constexpr double sampleRate = 44100.0;
    constexpr int chirpSamples = 440;
    constexpr double f0 = 1000.0;   // Start frequency: 1kHz
    constexpr double f1 = 5000.0;   // End frequency: 5kHz
    constexpr double duration = chirpSamples / sampleRate;
    constexpr double chirpRate = (f1 - f0) / duration;
    
    // Real-time specific settings
    constexpr int bufferSize = 512;
    constexpr int recordingLength = 10;  // seconds
    constexpr int maxRecordingSamples = static_cast<int>(sampleRate * recordingLength);
}

// ==============================================================================
//  CHIRP GENERATOR (Same as before)
// ==============================================================================
class ChirpGenerator {
public:
    static juce::AudioBuffer<float> generateChirp() {
        juce::AudioBuffer<float> chirp(1, ChirpConfig::chirpSamples);
        auto* signal = chirp.getWritePointer(0);
        
        // Direct phase integration method
        for (int i = 0; i < ChirpConfig::chirpSamples / 2; ++i) {
            double t = i / ChirpConfig::sampleRate;
            double phase = 2.0 * juce::MathConstants<double>::pi * 
                          (ChirpConfig::chirpRate / 2.0 * t * t + ChirpConfig::f0 * t);
            signal[i] = std::sin(phase);
        }
        
        // Second half: mirror with negation
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
};

// ==============================================================================
//  REAL-TIME CHIRP DETECTOR (Same as before)
// ==============================================================================
class RealTimeChirpDetector {
private:
    juce::AudioBuffer<float> template_;
    double templateEnergy_;
    
    // Real-time detection state
    std::vector<float> recordingBuffer_;
    std::vector<double> correlationTrace_;
    std::vector<double> powerTrace_;
    std::vector<int> detectionTimes_;
    
    double power_;
    double syncPowerLocalMax_;
    int peakIndex_;
    int samplesProcessed_;
    std::vector<float> syncFIFO_;
    
    // Detection parameters
    double powerThresholdRatio_;
    double minAbsThreshold_;
    int confirmationSamples_;
    
public:
    struct DetectionEvent {
        int sampleIndex;
        double correlationScore;
        double powerLevel;
        double confidence;
        std::chrono::high_resolution_clock::time_point timestamp;
    };
    
    std::vector<DetectionEvent> detections_;
    
    RealTimeChirpDetector(const juce::AudioBuffer<float>& chirpTemplate) :
        power_(0.0),
        syncPowerLocalMax_(0.0),
        peakIndex_(-1),
        samplesProcessed_(0),
        powerThresholdRatio_(2.0),
        minAbsThreshold_(0.05),
        confirmationSamples_(200)
    {
        template_.makeCopyOf(chirpTemplate);
        templateEnergy_ = calculateEnergy(template_.getReadPointer(0), template_.getNumSamples());
        
        syncFIFO_.resize(template_.getNumSamples(), 0.0f);
        recordingBuffer_.reserve(ChirpConfig::maxRecordingSamples);
        correlationTrace_.reserve(ChirpConfig::maxRecordingSamples);
        powerTrace_.reserve(ChirpConfig::maxRecordingSamples);
        
        std::cout << "Real-time chirp detector initialized:" << std::endl;
        std::cout << "  Template samples: " << template_.getNumSamples() << std::endl;
        std::cout << "  Template energy: " << templateEnergy_ << std::endl;
        std::cout << "  Power threshold ratio: " << powerThresholdRatio_ << std::endl;
        std::cout << "  Min absolute threshold: " << minAbsThreshold_ << std::endl;
    }
    
    void reset() {
        power_ = 0.0;
        syncPowerLocalMax_ = 0.0;
        peakIndex_ = -1;
        samplesProcessed_ = 0;
        std::fill(syncFIFO_.begin(), syncFIFO_.end(), 0.0f);
        recordingBuffer_.clear();
        correlationTrace_.clear();
        powerTrace_.clear();
        detections_.clear();
    }
    
    void processBuffer(const float* audioData, int numSamples) {
        const float* tempData = template_.getReadPointer(0);
        const int tempLen = template_.getNumSamples();
        
        for (int i = 0; i < numSamples; ++i) {
            float sample = audioData[i];
            
            // Store in recording buffer
            if (recordingBuffer_.size() < ChirpConfig::maxRecordingSamples) {
                recordingBuffer_.push_back(sample);
            }
            
            // Update power estimate (EMA with α = 1/64)
            power_ = power_ * (1.0 - 1.0/64.0) + sample * sample / 64.0;
            powerTrace_.push_back(power_);
            
            // Shift FIFO
            for (int j = 0; j < tempLen - 1; ++j) {
                syncFIFO_[j] = syncFIFO_[j + 1];
            }
            syncFIFO_[tempLen - 1] = sample;
            
            samplesProcessed_++;
            
            if (samplesProcessed_ >= tempLen) {
                // Calculate correlation
                double correlation = 0.0;
                for (int j = 0; j < tempLen; ++j) {
                    correlation += syncFIFO_[j] * tempData[j];
                }
                correlation /= 200.0;  // Normalization factor
                
                correlationTrace_.push_back(correlation);
                
                // Detection logic
                if (correlation > power_ * powerThresholdRatio_ && 
                    correlation > syncPowerLocalMax_ && 
                    correlation > minAbsThreshold_) {
                    
                    syncPowerLocalMax_ = correlation;
                    peakIndex_ = samplesProcessed_;
                }
                else if (peakIndex_ != -1 && (samplesProcessed_ - peakIndex_) > confirmationSamples_) {
                    // Peak confirmed - create detection event
                    DetectionEvent event;
                    event.sampleIndex = peakIndex_;
                    event.correlationScore = syncPowerLocalMax_;
                    event.powerLevel = power_;
                    event.confidence = syncPowerLocalMax_ / (power_ + 1e-10);
                    event.timestamp = std::chrono::high_resolution_clock::now();
                    
                    detections_.push_back(event);
                    
                    std::cout << "CHIRP DETECTED at sample " << peakIndex_ 
                             << " (correlation: " << syncPowerLocalMax_ 
                             << ", confidence: " << event.confidence << ")" << std::endl;
                    
                    // Reset for next detection
                    syncPowerLocalMax_ = 0.0;
                    peakIndex_ = -1;
                }
            }
        }
    }
    
    juce::AudioBuffer<float> getRecordingBuffer() const {
        juce::AudioBuffer<float> buffer(1, static_cast<int>(recordingBuffer_.size()));
        buffer.copyFrom(0, 0, recordingBuffer_.data(), static_cast<int>(recordingBuffer_.size()));
        return buffer;
    }
    
    void saveAnalysisData(const std::string& filename) {
        std::ofstream file(filename);
        file << "Sample,Correlation,Power\n";
        
        size_t minSize = std::min(correlationTrace_.size(), powerTrace_.size());
        for (size_t i = 0; i < minSize; ++i) {
            file << i << "," << correlationTrace_[i] << "," << powerTrace_[i] << "\n";
        }
        
        std::cout << "Analysis data saved to " << filename << std::endl;
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
//  REAL-TIME AUDIO MANAGER (UPDATED FOR LATEST JUCE)
// ==============================================================================
class RealTimeAudioManager : public juce::AudioIODeviceCallback {
private:
    std::unique_ptr<juce::AudioDeviceManager> deviceManager_;
    std::unique_ptr<RealTimeChirpDetector> detector_;
    
    // Playback state
    juce::AudioBuffer<float> chirpTemplate_;
    std::vector<int> scheduledChirpTimes_;  // Sample times when chirps should play
    std::vector<int> playedChirpTimes_;     // Actual sample times when chirps were played
    int currentSample_;
    int nextChirpIndex_;
    
    // Test configuration
    bool isRecording_;
    bool isPlaying_;
    double playbackGain_;
    
    // Timing measurement
    std::chrono::high_resolution_clock::time_point testStartTime_;
    
public:
    RealTimeAudioManager() : 
        currentSample_(0),
        nextChirpIndex_(0),
        isRecording_(false),
        isPlaying_(false),
        playbackGain_(0.5)
    {
        deviceManager_ = std::make_unique<juce::AudioDeviceManager>();
        chirpTemplate_ = ChirpGenerator::generateChirp();
        detector_ = std::make_unique<RealTimeChirpDetector>(chirpTemplate_);
    }
    
    bool initialize() {
        std::cout << "\nInitializing audio devices..." << std::endl;
        
        // Initialize audio device
        juce::String error = deviceManager_->initialiseWithDefaultDevices(1, 1);  // 1 input, 1 output
        if (error.isNotEmpty()) {
            std::cout << "Audio initialization error: " << error << std::endl;
            return false;
        }
        
        // Configure audio settings
        auto* device = deviceManager_->getCurrentAudioDevice();
        if (device != nullptr) {
            std::cout << "Audio device: " << device->getName() << std::endl;
            std::cout << "Sample rate: " << device->getCurrentSampleRate() << " Hz" << std::endl;
            std::cout << "Buffer size: " << device->getCurrentBufferSizeSamples() << " samples" << std::endl;
            std::cout << "Input channels: " << device->getActiveInputChannels().toInteger() << std::endl;
            std::cout << "Output channels: " << device->getActiveOutputChannels().toInteger() << std::endl;
        }
        
        deviceManager_->addAudioCallback(this);
        return true;
    }
    
    void shutdown() {
        deviceManager_->removeAudioCallback(this);
        deviceManager_->closeAudioDevice();
    }
    
    // Schedule chirps at specific times (in samples)
    void scheduleChirps(const std::vector<double>& timesInSeconds) {
        scheduledChirpTimes_.clear();
        playedChirpTimes_.clear();
        
        for (double timeSeconds : timesInSeconds) {
            int sampleTime = static_cast<int>(timeSeconds * ChirpConfig::sampleRate);
            scheduledChirpTimes_.push_back(sampleTime);
        }
        
        std::cout << "\nScheduled " << scheduledChirpTimes_.size() << " chirps:" << std::endl;
        for (size_t i = 0; i < scheduledChirpTimes_.size(); ++i) {
            std::cout << "  Chirp " << (i+1) << " at " << (scheduledChirpTimes_[i] / ChirpConfig::sampleRate) 
                     << " seconds (sample " << scheduledChirpTimes_[i] << ")" << std::endl;
        }
        
        nextChirpIndex_ = 0;
    }
    
    void startTest() {
        detector_->reset();
        currentSample_ = 0;
        nextChirpIndex_ = 0;
        isRecording_ = true;
        isPlaying_ = true;
        testStartTime_ = std::chrono::high_resolution_clock::now();
        
        std::cout << "\n" << std::string(60, '=') << std::endl;
        std::cout << "REAL-TIME TEST STARTED" << std::endl;
        std::cout << std::string(60, '=') << std::endl;
    }
    
    void stopTest() {
        isRecording_ = false;
        isPlaying_ = false;
        
        std::cout << "\n" << std::string(60, '=') << std::endl;
        std::cout << "REAL-TIME TEST STOPPED" << std::endl;
        std::cout << std::string(60, '=') << std::endl;
        
        analyzeResults();
    }
    
    void setPlaybackGain(double gain) {
        playbackGain_ = juce::jlimit(0.0, 1.0, gain);
        std::cout << "Playback gain set to " << playbackGain_ << std::endl;
    }
    
    // UPDATED: AudioIODeviceCallback implementation with new signature
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                         int numInputChannels,
                                         float* const* outputChannelData,
                                         int numOutputChannels,
                                         int numSamples,
                                         const juce::AudioIODeviceCallbackContext& context) override {
        
        // Clear output
        for (int channel = 0; channel < numOutputChannels; ++channel) {
            juce::FloatVectorOperations::clear(outputChannelData[channel], numSamples);
        }
        
        if (!isRecording_ && !isPlaying_) return;
        
        // Process input (recording and detection)
        if (isRecording_ && numInputChannels > 0 && inputChannelData[0] != nullptr) {
            detector_->processBuffer(inputChannelData[0], numSamples);
        }
        
        // Process output (chirp playback)
        if (isPlaying_ && numOutputChannels > 0) {
            for (int sample = 0; sample < numSamples; ++sample) {
                int globalSample = currentSample_ + sample;
                
                // Check if we should start playing a chirp
                if (nextChirpIndex_ < scheduledChirpTimes_.size() && 
                    globalSample >= scheduledChirpTimes_[nextChirpIndex_]) {
                    
                    playedChirpTimes_.push_back(globalSample);
                    std::cout << "Playing chirp " << (nextChirpIndex_ + 1) 
                             << " at sample " << globalSample << std::endl;
                    nextChirpIndex_++;
                }
                
                // Generate chirp output
                float outputSample = 0.0f;
                for (size_t i = 0; i < playedChirpTimes_.size(); ++i) {
                    int chirpStartSample = playedChirpTimes_[i];
                    int chirpPosition = globalSample - chirpStartSample;
                    
                    if (chirpPosition >= 0 && chirpPosition < chirpTemplate_.getNumSamples()) {
                        outputSample += chirpTemplate_.getSample(0, chirpPosition) * playbackGain_;
                    }
                }
                
                // Apply to all output channels
                for (int channel = 0; channel < numOutputChannels; ++channel) {
                    outputChannelData[channel][sample] = outputSample;
                }
            }
        }
        
        currentSample_ += numSamples;
    }
    
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override {
        std::cout << "Audio device starting: " << device->getName() << std::endl;
    }
    
    void audioDeviceStopped() override {
        std::cout << "Audio device stopped" << std::endl;
    }
    
    void audioDeviceError(const juce::String& errorMessage) override {
        std::cout << "Audio device error: " << errorMessage << std::endl;
    }
    
private:
    void analyzeResults() {
        const auto& detections = detector_->detections_;
        
        std::cout << "\n" << std::string(60, '-') << std::endl;
        std::cout << "RESULTS ANALYSIS" << std::endl;
        std::cout << std::string(60, '-') << std::endl;
        
        std::cout << "Chirps played: " << playedChirpTimes_.size() << std::endl;
        std::cout << "Chirps detected: " << detections.size() << std::endl;
        
        if (!playedChirpTimes_.empty()) {
            std::cout << "\nTiming Analysis:" << std::endl;
            std::cout << "Expected\tDetected\tError(samples)\tError(ms)\tConfidence" << std::endl;
            std::cout << std::string(70, '-') << std::endl;
            
            for (size_t i = 0; i < std::min(playedChirpTimes_.size(), detections.size()); ++i) {
                int expectedSample = playedChirpTimes_[i];
                int detectedSample = detections[i].sampleIndex;
                int errorSamples = detectedSample - expectedSample;
                double errorMs = errorSamples * 1000.0 / ChirpConfig::sampleRate;
                
                std::cout << expectedSample << "\t\t" << detectedSample << "\t\t" 
                         << errorSamples << "\t\t" << std::fixed << std::setprecision(2) 
                         << errorMs << "\t\t" << detections[i].confidence << std::endl;
            }
            
            // Calculate statistics
            if (detections.size() >= playedChirpTimes_.size()) {
                std::vector<int> errors;
                for (size_t i = 0; i < playedChirpTimes_.size(); ++i) {
                    errors.push_back(std::abs(detections[i].sampleIndex - playedChirpTimes_[i]));
                }
                
                int maxError = *std::max_element(errors.begin(), errors.end());
                double avgError = std::accumulate(errors.begin(), errors.end(), 0.0) / errors.size();
                
                std::cout << "\nAccuracy Statistics:" << std::endl;
                std::cout << "  Max error: " << maxError << " samples (" 
                         << (maxError * 1000.0 / ChirpConfig::sampleRate) << " ms)" << std::endl;
                std::cout << "  Avg error: " << std::fixed << std::setprecision(2) << avgError 
                         << " samples (" << (avgError * 1000.0 / ChirpConfig::sampleRate) << " ms)" << std::endl;
                std::cout << "  Sample-level accuracy: " << (maxError <= 1 ? "ACHIEVED " : "NOT ACHIEVED ") << std::endl;
            }
        }
        
        // Save results
        saveResultsToFile();
    }
    
    void saveResultsToFile() {
        // Save recording
        auto recording = detector_->getRecordingBuffer();
        saveWav(recording, "realtime_recording.wav");
        
        // Save analysis data
        detector_->saveAnalysisData("realtime_analysis.csv");
        
        // Save timing results
        std::ofstream timingFile("realtime_timing_results.txt");
        timingFile << "Real-Time Chirp Detection Results\n";
        timingFile << "================================\n\n";
        timingFile << "Test Configuration:\n";
        timingFile << "  Sample Rate: " << ChirpConfig::sampleRate << " Hz\n";
        timingFile << "  Chirp Duration: " << ChirpConfig::duration << " s\n";
        timingFile << "  Playback Gain: " << playbackGain_ << "\n\n";
        
        timingFile << "Timing Results:\n";
        timingFile << "Expected(sample)\tDetected(sample)\tError(samples)\tError(ms)\tConfidence\n";
        
        const auto& detections = detector_->detections_;
        for (size_t i = 0; i < std::min(playedChirpTimes_.size(), detections.size()); ++i) {
            int expectedSample = playedChirpTimes_[i];
            int detectedSample = detections[i].sampleIndex;
            int errorSamples = detectedSample - expectedSample;
            double errorMs = errorSamples * 1000.0 / ChirpConfig::sampleRate;
            
            timingFile << expectedSample << "\t\t" << detectedSample << "\t\t" 
                      << errorSamples << "\t\t" << std::fixed << std::setprecision(2) 
                      << errorMs << "\t\t" << detections[i].confidence << "\n";
        }
        
        std::cout << "\nResults saved:" << std::endl;
        std::cout << "  - realtime_recording.wav (audio recording)" << std::endl;
        std::cout << "  - realtime_analysis.csv (correlation/power trace)" << std::endl;
        std::cout << "  - realtime_timing_results.txt (timing analysis)" << std::endl;
    }
    
    void saveWav(const juce::AudioBuffer<float>& buffer, const std::string& filename) {
        juce::File outFile = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(filename));
        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wavFormat.createWriterFor(new juce::FileOutputStream(outFile), 
                                     ChirpConfig::sampleRate, buffer.getNumChannels(), 16, {}, 0));
        if (writer != nullptr) {
            writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
        }
    }
};

// ==============================================================================
//  REAL-TIME TEST SUITE (Rest of the code remains the same)
// ==============================================================================
class RealTimeTestSuite {
private:
    std::unique_ptr<RealTimeAudioManager> audioManager_;
    
public:
    RealTimeTestSuite() {
        audioManager_ = std::make_unique<RealTimeAudioManager>();
    }
    
    bool initialize() {
        return audioManager_->initialize();
    }
    
    void shutdown() {
        audioManager_->shutdown();
    }
    
    void runBasicLatencyTest() {
        std::cout << "\n" << std::string(80, '=') << std::endl;
        std::cout << "TEST 1: Basic Latency Measurement" << std::endl;
        std::cout << std::string(80, '=') << std::endl;
        std::cout << "This test measures the basic speaker-to-microphone latency." << std::endl;
        std::cout << "Make sure speakers and microphone are set up properly." << std::endl;
        std::cout << "Press ENTER when ready...";
        std::cin.get();
        
        // Single chirp after 1 second
        audioManager_->scheduleChirps({1.0});
        audioManager_->setPlaybackGain(0.8);
        audioManager_->startTest();
        
        std::this_thread::sleep_for(std::chrono::seconds(3));
        audioManager_->stopTest();
    }
    
    void runMultipleChirpTest() {
        std::cout << "\n" << std::string(80, '=') << std::endl;
        std::cout << "TEST 2: Multiple Chirp Timing Test" << std::endl;
        std::cout << std::string(80, '=') << std::endl;
        std::cout << "Testing detection accuracy with multiple chirps at known intervals." << std::endl;
        std::cout << "Press ENTER when ready...";
        std::cin.get();
        
        // Multiple chirps: 1s, 2.5s, 4s, 5.5s, 7s
        audioManager_->scheduleChirps({1.0, 2.5, 4.0, 5.5, 7.0});
        audioManager_->setPlaybackGain(0.7);
        audioManager_->startTest();
        
        std::this_thread::sleep_for(std::chrono::seconds(9));
        audioManager_->stopTest();
    }
    
    void runVolumeVariationTest() {
        std::cout << "\n" << std::string(80, '=') << std::endl;
        std::cout << "TEST 3: Volume Variation Test" << std::endl;
        std::cout << std::string(80, '=') << std::endl;
        std::cout << "Testing detection at different volume levels." << std::endl;
        
        std::vector<double> volumes = {0.2, 0.4, 0.6, 0.8};
        
        for (size_t i = 0; i < volumes.size(); ++i) {
            std::cout << "\nTesting volume level " << volumes[i] << std::endl;
            std::cout << "Press ENTER when ready...";
            std::cin.get();
            
            audioManager_->scheduleChirps({1.0, 3.0});
            audioManager_->setPlaybackGain(volumes[i]);
            audioManager_->startTest();
            
            std::this_thread::sleep_for(std::chrono::seconds(5));
            audioManager_->stopTest();
        }
    }
    
    void runBackgroundNoiseTest() {
        std::cout << "\n" << std::string(80, '=') << std::endl;
        std::cout << "TEST 4: Background Noise Resilience Test" << std::endl;
        std::cout << std::string(80, '=') << std::endl;
        std::cout << "Testing detection with background noise present." << std::endl;
        std::cout << "Please introduce some background noise (music, talking, etc.)" << std::endl;
        std::cout << "Press ENTER when ready...";
        std::cin.get();
        
        audioManager_->scheduleChirps({1.0, 3.0, 5.0});
        audioManager_->setPlaybackGain(0.6);
        audioManager_->startTest();
        
        std::this_thread::sleep_for(std::chrono::seconds(7));
        audioManager_->stopTest();
    }
    
    void runInteractiveTest() {
        std::cout << "\n" << std::string(80, '=') << std::endl;
        std::cout << "INTERACTIVE TEST MODE" << std::endl;
        std::cout << std::string(80, '=') << std::endl;
        std::cout << "Commands:" << std::endl;
        std::cout << "  p - Play single chirp now" << std::endl;
        std::cout << "  r - Start/stop recording" << std::endl;
        std::cout << "  v <value> - Set volume (0.0-1.0)" << std::endl;
        std::cout << "  s - Show detection statistics" << std::endl;
        std::cout << "  q - Quit" << std::endl;
        
        audioManager_->startTest();
        
        std::string command;
        while (true) {
            std::cout << "\n> ";
            std::cin >> command;
            
            if (command == "q") break;
            else if (command == "p") {
                double currentTime = std::chrono::duration<double>(
                    std::chrono::high_resolution_clock::now().time_since_epoch()).count();
                audioManager_->scheduleChirps({0.1});  // Play in 100ms
            }
            else if (command == "v") {
                double volume;
                std::cin >> volume;
                audioManager_->setPlaybackGain(volume);
            }
            else if (command == "s") {
                std::cout << "Current statistics will be shown when test stops." << std::endl;
            }
        }
        
        audioManager_->stopTest();
    }
    
    void runAllTests() {
        if (!initialize()) {
            std::cout << "Failed to initialize audio system!" << std::endl;
            return;
        }
        
        std::cout << "\n";
        std::cout << "╔════════════════════════════════════════════════════════════════╗\n";
        std::cout << "║           REAL-TIME CHIRP DETECTION TEST SUITE                 ║\n";
        std::cout << "╚════════════════════════════════════════════════════════════════╝\n";
        
        std::cout << "\nThis test suite will play chirps through speakers and detect them" << std::endl;
        std::cout << "through the microphone to measure real-world accuracy." << std::endl;
        std::cout << "\nIMPORTANT SETUP:" << std::endl;
        std::cout << "1. Make sure speakers and microphone are working" << std::endl;
        std::cout << "2. Position microphone to clearly hear speakers" << std::endl;
        std::cout << "3. Adjust system volume to comfortable level" << std::endl;
        std::cout << "4. Minimize background noise when possible" << std::endl;
        
        while (true) {
            std::cout << "\n" << std::string(60, '-') << std::endl;
            std::cout << "Select test to run:" << std::endl;
            std::cout << "1. Basic Latency Test" << std::endl;
            std::cout << "2. Multiple Chirp Timing Test" << std::endl;
            std::cout << "3. Volume Variation Test" << std::endl;
            std::cout << "4. Background Noise Test" << std::endl;
            std::cout << "5. Interactive Test Mode" << std::endl;
            std::cout << "0. Exit" << std::endl;
            std::cout << "\nChoice: ";
            
            int choice;
            std::cin >> choice;
            std::cin.ignore(); // Clear newline
            
            switch (choice) {
                case 1: runBasicLatencyTest(); break;
                case 2: runMultipleChirpTest(); break;
                case 3: runVolumeVariationTest(); break;
                case 4: runBackgroundNoiseTest(); break;
                case 5: runInteractiveTest(); break;
                case 0: 
                    shutdown();
                    return;
                default:
                    std::cout << "Invalid choice!" << std::endl;
            }
        }
    }
};

// ==============================================================================
//  MAIN
// ==============================================================================
int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    
    RealTimeTestSuite testSuite;
    testSuite.runAllTests();
    
    std::cout << "\nAll tests completed. Check generated files for detailed analysis." << std::endl;
    
    return 0;
}
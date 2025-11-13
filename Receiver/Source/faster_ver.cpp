/*
 * =========================================================================
 * ASK NRZ LINK WITH OFFLINE DEMODULATION + ACK
 * =========================================================================
 * - Frame format: [1 TYPE][9 ID][100 DATA][8 CRC] = 118 bits
 * - Line coding: NRZ baseband (2 kHz tone)
 * - Preamble: 440-sample chirp (1 kHz → 4 kHz → 1 kHz)
 * - Demodulation: offline, dot-product chirp detection (same as offline_reference)
 * - MAC: single coordinator class that drives AudioPlayer/AudioRecorder
 *
 * The goal of this refactor is to keep the playback/recording logic identical to
 * the proven AudioPlayer/AudioRecorder examples, while exposing a lightweight
 * MAC layer (MacProtocol) to orchestrate transmissions, ACKs, and frame storage.
 *
 * Files are saved under: D:\fourth_year\cs120\debug_pic\ASK\
 * =========================================================================
 */

 #include <JuceHeader.h>
#include <algorithm>
#include <atomic>
#include <chrono>
 #include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
 #include <vector>
 
 namespace ASK {
    constexpr double sampleRate = 44100.0;
     constexpr int preambleSamples = 440;
    constexpr double chirp_f_start = 1000.0;
    constexpr double chirp_f_end = 4000.0;
    constexpr double baseFreq = 2000.0;
    constexpr int samplesPerBit = 3;  // ~14.7 kbps raw

    constexpr int typeBitsPerFrame = 1;
    constexpr int idBitsPerFrame = 9;
     constexpr int dataBitsPerFrame = 100;
     constexpr int crcBitsPerFrame = 8;
    constexpr int bitsPerFrame = typeBitsPerFrame + idBitsPerFrame + dataBitsPerFrame + crcBitsPerFrame;

    constexpr int numFrames = 500;
    constexpr bool FRAME_TYPE_DATA = false;
    constexpr bool FRAME_TYPE_ACK = true;

    constexpr int TIMEOUT_MS = 5000;
    constexpr int MAX_RESEND = 5;

    constexpr uint8_t crcPolynomial = 0xD5;  // x^8 + x^7 + x^5 + x^2 + x + 1

    const std::string inputPath = "INPUT.bin";
    const std::string outputPath = "D\\fourth_year\\cs120\\debug_pic\\ASK\\";
 }
 
 // ==============================================================================
// CRC8 GENERATOR
 // ==============================================================================
 class CRC8Generator {
 public:
    explicit CRC8Generator(uint8_t poly) : polynomial_(poly) {}
 
    std::vector<bool> generate(const std::vector<bool>& data) const {
         std::vector<bool> result = data;
         uint8_t crc = 0;
         for (bool bit : data) {
            bool feedback = ((crc >> 7) & 1) ^ (bit ? 1 : 0);
             crc <<= 1;
             if (feedback) {
                crc ^= polynomial_;
             }
         }
         for (int i = 7; i >= 0; --i) {
             result.push_back((crc >> i) & 1);
         }
         return result;
     }
 
    bool check(const std::vector<bool>& dataWithCRC) const {
         if (dataWithCRC.size() < 8) return false;
         std::vector<bool> data(dataWithCRC.begin(), dataWithCRC.end() - 8);
         std::vector<bool> computed = generate(data);
         for (size_t i = 0; i < 8; ++i) {
             if (computed[data.size() + i] != dataWithCRC[data.size() + i]) {
                 return false;
             }
         }
         return true;
     }

private:
    uint8_t polynomial_;
 };
 
 // ==============================================================================
// SIGNAL GENERATOR UTILITIES
 // ==============================================================================
 class SignalGenerator {
 public:
     static juce::AudioBuffer<float> generatePreamble() {
        juce::AudioBuffer<float> buffer(1, ASK::preambleSamples);
        auto* samples = buffer.getWritePointer(0);
 
        std::vector<double> sweep;
        sweep.reserve(ASK::preambleSamples);
         for (int i = 0; i < ASK::preambleSamples / 2; ++i) {
             double freq = juce::jmap((double)i, 0.0, (double)(ASK::preambleSamples / 2 - 1),
                 ASK::chirp_f_start, ASK::chirp_f_end);
            sweep.push_back(freq);
         }
         for (int i = ASK::preambleSamples / 2; i < ASK::preambleSamples; ++i) {
             double freq = juce::jmap((double)i, (double)(ASK::preambleSamples / 2),
                 (double)(ASK::preambleSamples - 1),
                 ASK::chirp_f_end, ASK::chirp_f_start);
            sweep.push_back(freq);
         }
 
        double phase = 0.0;
         for (int i = 0; i < ASK::preambleSamples; ++i) {
            double phaseIncrement = 2.0 * juce::MathConstants<double>::pi * sweep[i] / ASK::sampleRate;
            phase += phaseIncrement;
            samples[i] = std::sin(phase);
        }
        return buffer;
    }

    static juce::AudioBuffer<float> generateNRZBit(bool bitValue, int samplesPerBit) {
        juce::AudioBuffer<float> buffer(1, samplesPerBit);
        auto* samples = buffer.getWritePointer(0);
 
         double phase = 0.0;
        double phaseIncrement = 2.0 * juce::MathConstants<double>::pi * ASK::baseFreq / ASK::sampleRate;
        const float polarity = bitValue ? 1.0f : -1.0f;
 
        for (int i = 0; i < samplesPerBit; ++i) {
            samples[i] = polarity * std::sin(phase);
             phase += phaseIncrement;
             if (phase > 2.0 * juce::MathConstants<double>::pi) {
                 phase -= 2.0 * juce::MathConstants<double>::pi;
             }
         }
        return buffer;
    }
};

// ==============================================================================
// FRAME STRUCTURE
// ==============================================================================
struct Frame {
    bool type = ASK::FRAME_TYPE_DATA;
    int id = 0;
    std::vector<bool> data = std::vector<bool>(ASK::dataBitsPerFrame, false);
    bool crcValid = false;
    int sampleIndex = -1;  // Preamble start within analysed buffer
 };
 
 // ==============================================================================
// MODULATOR
 // ==============================================================================
class Modulator {
 public:
    static juce::AudioBuffer<float> modulateFrame(const Frame& frame) {
        CRC8Generator crcGen(ASK::crcPolynomial);

        std::vector<bool> bits;
        bits.reserve(ASK::bitsPerFrame);
        bits.push_back(frame.type);
        for (int i = ASK::idBitsPerFrame - 1; i >= 0; --i) {
            bits.push_back((frame.id >> i) & 1);
        }
        for (int i = 0; i < ASK::dataBitsPerFrame; ++i) {
            bits.push_back(i < (int)frame.data.size() ? frame.data[i] : false);
        }

        std::vector<bool> frameWithCRC = crcGen.generate(bits);

        auto preamble = SignalGenerator::generatePreamble();
        juce::AudioBuffer<float> result(1, ASK::preambleSamples + (int)frameWithCRC.size() * ASK::samplesPerBit);
        result.clear();

        // Write preamble
        result.copyFrom(0, 0, preamble, 0, 0, preamble.getNumSamples());

        // Write NRZ payload
        int writePos = preamble.getNumSamples();
        for (bool bit : frameWithCRC) {
            auto bitBuffer = SignalGenerator::generateNRZBit(bit, ASK::samplesPerBit);
            result.copyFrom(0, writePos, bitBuffer, 0, 0, bitBuffer.getNumSamples());
            writePos += bitBuffer.getNumSamples();
        }

        return result;
     }
 };
 
 // ==============================================================================
// FRAME DEMODULATOR (offline, dot-product detection)
 // ==============================================================================
class FrameDemodulator {
 public:
    FrameDemodulator() : crcGen_(ASK::crcPolynomial) {
        preambleTemplate_ = SignalGenerator::generatePreamble();
    }

    const juce::AudioBuffer<float>& getPreambleTemplate() const {
        return preambleTemplate_;
    }

    // Fast ACK detection: quickly find and discard ACK frames (for receiver mode)
    // Returns vector of sample positions where ACK frames were detected
    std::vector<int> fastAckDetect(const std::vector<float>& buffer) const {
        std::vector<int> ackPositions;
        if (buffer.empty()) return ackPositions;
        
        const float* sigData = buffer.data();
        const float* preData = preambleTemplate_.getReadPointer(0);
        const int sigLen = (int)buffer.size();
        const int preLen = preambleTemplate_.getNumSamples();
        const int samplesToCheck = ASK::samplesPerBit;  // Only need TYPE bit
        
        std::vector<float> syncFIFO((size_t)preLen, 0.0f);
        double power = 0.0;
        double syncPowerLocalMax = 0.0;
        int startIndex = 0;
        
        for (int i = 0; i < sigLen; ++i) {
            power = power * (1.0 - 1.0 / 64.0) + (sigData[i] * sigData[i]) / 64.0;
            std::rotate(syncFIFO.begin(), syncFIFO.begin() + 1, syncFIFO.end());
            syncFIFO.back() = (i < sigLen ? sigData[i] : 0.0f);
            
            double dotProduct = 0.0;
            for (int j = 0; j < preLen; ++j) {
                dotProduct += (double)syncFIFO[j] * (double)preData[j];
            }
            double syncPower = dotProduct / 200.0;
            
            if (syncPower > 1.0 && syncPower > syncPowerLocalMax && syncPower > power / 2.0) {
                syncPowerLocalMax = syncPower;
                startIndex = i;
            }
            else if ((i - startIndex > 200) && startIndex != 0) {
                if (startIndex + preLen + samplesToCheck <= sigLen) {
                    int pos = startIndex - preLen + 1;
                    int frameStart = pos + preLen;
                    
                    // Check only TYPE bit (first bit after preamble)
                    int sampleStart = ASK::samplesPerBit / 4;
                    int sampleEnd = ASK::samplesPerBit - ASK::samplesPerBit / 4;
                    double sum = 0.0;
                    for (int j = sampleStart; j < sampleEnd && j < samplesToCheck; ++j) {
                        sum += sigData[frameStart + j];
                    }
                    bool isAck = (sum > 0.0);  // ACK type bit is 1
                    
                    if (isAck) {
                        ackPositions.push_back(pos);
                    }
                }
                syncPowerLocalMax = 0.0;
                startIndex = 0;
                std::fill(syncFIFO.begin(), syncFIFO.end(), 0.0f);
            }
        }
        
        return ackPositions;
    }

    // Fast ACK check: only demodulate first 10 bits (TYPE + ID) for sender mode
    bool fastAckCheck(const std::vector<float>& buffer, int expectedFrameId, int& detectedSamplePos) const {
        if (buffer.empty()) return false;
        
        const float* sigData = buffer.data();
        const float* preData = preambleTemplate_.getReadPointer(0);
        const int sigLen = (int)buffer.size();
        const int preLen = preambleTemplate_.getNumSamples();
        const int bitsToCheck = 1 + ASK::idBitsPerFrame;  // TYPE + ID = 10 bits
        const int samplesToCheck = bitsToCheck * ASK::samplesPerBit;
        
        // Find preamble using same logic
        std::vector<int> preamblePositions;
        std::vector<float> syncFIFO((size_t)preLen, 0.0f);
        double power = 0.0;
        double syncPowerLocalMax = 0.0;
        int startIndex = 0;
        
        for (int i = 0; i < sigLen; ++i) {
            power = power * (1.0 - 1.0 / 64.0) + (sigData[i] * sigData[i]) / 64.0;
            std::rotate(syncFIFO.begin(), syncFIFO.begin() + 1, syncFIFO.end());
            syncFIFO.back() = (i < sigLen ? sigData[i] : 0.0f);
            
            double dotProduct = 0.0;
            for (int j = 0; j < preLen; ++j) {
                dotProduct += (double)syncFIFO[j] * (double)preData[j];
            }
            double syncPower = dotProduct / 200.0;
            
            if (syncPower > 1.0 && syncPower > syncPowerLocalMax && syncPower > power / 2.0) {
                syncPowerLocalMax = syncPower;
                startIndex = i;
            }
            else if ((i - startIndex > 200) && startIndex != 0) {
                if (startIndex + preLen + samplesToCheck <= sigLen) {
                    preamblePositions.push_back(startIndex - preLen + 1);
                }
                syncPowerLocalMax = 0.0;
                startIndex = 0;
                std::fill(syncFIFO.begin(), syncFIFO.end(), 0.0f);
            }
        }
        
        // Check each detected preamble for ACK with matching ID
        for (int pos : preamblePositions) {
            int frameStart = pos + preLen;
            if (frameStart + samplesToCheck > sigLen) continue;
            
            // Demodulate only first 10 bits
            std::vector<bool> bits(bitsToCheck);
            for (int bitIdx = 0; bitIdx < bitsToCheck; ++bitIdx) {
                int bitStart = bitIdx * ASK::samplesPerBit;
                int bitEnd = (bitIdx + 1) * ASK::samplesPerBit;
                int sampleStart = bitStart + ASK::samplesPerBit / 4;
                int sampleEnd = bitEnd - ASK::samplesPerBit / 4;
                double sum = 0.0;
                for (int j = sampleStart; j < sampleEnd && j < samplesToCheck; ++j) {
                    sum += sigData[frameStart + j];
                }
                bits[bitIdx] = (sum > 0.0);
            }
            
            // Check TYPE and ID
            bool type = bits[0];
            int id = 0;
            for (int j = 0; j < ASK::idBitsPerFrame; ++j) {
                id = (id << 1) | (bits[1 + j] ? 1 : 0);
            }
            
            if (type == ASK::FRAME_TYPE_ACK && id == expectedFrameId) {
                detectedSamplePos = pos;
                return true;
            }
        }
        
        return false;
    }

    std::vector<Frame> demodulateBuffer(const std::vector<float>& buffer) const {
        std::vector<Frame> frames;
        if (buffer.empty()) return frames;

        const float* sigData = buffer.data();
        const float* preData = preambleTemplate_.getReadPointer(0);
        const int sigLen = (int)buffer.size();
        const int preLen = preambleTemplate_.getNumSamples();
        const int frameSamples = ASK::bitsPerFrame * ASK::samplesPerBit;

        std::vector<int> preamblePositions;
        std::vector<float> syncFIFO((size_t)preLen, 0.0f);
        double power = 0.0;
        double syncPowerLocalMax = 0.0;
        int startIndex = 0;

        for (int i = 0; i < sigLen; ++i) {
            power = power * (1.0 - 1.0 / 64.0) + (sigData[i] * sigData[i]) / 64.0;
            std::rotate(syncFIFO.begin(), syncFIFO.begin() + 1, syncFIFO.end());
            syncFIFO.back() = (i < sigLen ? sigData[i] : 0.0f);

            double dotProduct = 0.0;
            for (int j = 0; j < preLen; ++j) {
                dotProduct += (double)syncFIFO[j] * (double)preData[j];
            }
            double syncPower = dotProduct / 200.0;

            if (syncPower > 1.0 && syncPower > syncPowerLocalMax && syncPower > power / 2.0) {
                syncPowerLocalMax = syncPower;
                startIndex = i;
            }
            else if ((i - startIndex > 200) && startIndex != 0) {
                if (startIndex + preLen + frameSamples <= sigLen) {
                    bool farEnough = preamblePositions.empty() ||
                        (std::abs(startIndex - preamblePositions.back()) >= preLen);
                    if (farEnough) {
                        preamblePositions.push_back(startIndex - preLen + 1);
                    }
                }
                syncPowerLocalMax = 0.0;
                startIndex = 0;
                std::fill(syncFIFO.begin(), syncFIFO.end(), 0.0f);
            }
        }

        for (int pos : preamblePositions) {
            int frameStart = pos + preLen;
            if (frameStart + frameSamples > sigLen) continue;

            std::vector<bool> bits = demodulateFrame(sigData + frameStart, frameSamples);
            Frame frame;
            frame.sampleIndex = pos;
            frame.type = bits[0];

            frame.id = 0;
            for (int j = 0; j < ASK::idBitsPerFrame; ++j) {
                frame.id = (frame.id << 1) | (bits[ASK::typeBitsPerFrame + j] ? 1 : 0);
            }

            int dataStart = ASK::typeBitsPerFrame + ASK::idBitsPerFrame;
            for (int j = 0; j < ASK::dataBitsPerFrame && dataStart + j < (int)bits.size(); ++j) {
                frame.data[j] = bits[dataStart + j];
            }

            frame.crcValid = crcGen_.check(bits);

            frames.push_back(frame);
        }

        std::sort(frames.begin(), frames.end(), [](const Frame& a, const Frame& b) {
            return a.sampleIndex < b.sampleIndex;
            });
        return frames;
    }

private:
    std::vector<bool> demodulateFrame(const float* frameData, int frameSamples) const {
        std::vector<bool> bits(ASK::bitsPerFrame);
        for (int bitIdx = 0; bitIdx < ASK::bitsPerFrame; ++bitIdx) {
            int bitStart = bitIdx * ASK::samplesPerBit;
            int bitEnd = (bitIdx + 1) * ASK::samplesPerBit;
            int sampleStart = bitStart + ASK::samplesPerBit / 4;
            int sampleEnd = bitEnd - ASK::samplesPerBit / 4;
            double sum = 0.0;
            for (int j = sampleStart; j < sampleEnd && j < frameSamples; ++j) {
                sum += frameData[j];
            }
            bits[bitIdx] = (sum > 0.0);
        }
        return bits;
    }

    juce::AudioBuffer<float> preambleTemplate_;
    CRC8Generator crcGen_;
 };
 
 // ==============================================================================
// AUDIO RECORDER (AudioRecordingDemo pattern with optional sample callback)
 // ==============================================================================
 class AudioRecorder : public juce::AudioIODeviceCallback {
 public:
     AudioRecorder() {
        backgroundThread_.startThread();
     }
 
     ~AudioRecorder() override {
         stop();
        backgroundThread_.stopThread(1000);
     }
 
     void startRecording(const juce::File& file, double sampleRateToUse) {
         stop();
 
        sampleRate_ = sampleRateToUse;
        if (sampleRate_ <= 0) return;
 
             file.deleteFile();
             if (std::unique_ptr<juce::OutputStream> fileStream{ file.createOutputStream() }) {
                 juce::WavAudioFormat wavFormat;
                 using Opts = juce::AudioFormatWriterOptions;
                 if (auto writer = wavFormat.createWriterFor(fileStream,
                Opts{}
                .withSampleRate(sampleRate_)
                     .withNumChannels(1)
                     .withBitsPerSample(16))) {
                threadedWriter_.reset(new juce::AudioFormatWriter::ThreadedWriter(writer.release(), backgroundThread_, 32768));
                const juce::ScopedLock sl(writerLock_);
                activeWriter_ = threadedWriter_.get();
                nextSampleNum_ = 0;
                std::cout << "Recording to: " << file.getFullPathName() << std::endl;
             }
         }
     }
 
     void stop() {
        const juce::ScopedLock sl(writerLock_);
        activeWriter_ = nullptr;
        threadedWriter_.reset();
     }
 
     bool isRecording() const {
        return activeWriter_.load() != nullptr;
    }

    void setSampleCallback(std::function<void(const float*, int)> cb) {
        const juce::ScopedLock sl(callbackLock_);
        sampleCallback_ = std::move(cb);
     }
 
     void audioDeviceAboutToStart(juce::AudioIODevice* device) override {
        if (device != nullptr && sampleRate_ == 0) {
            sampleRate_ = device->getCurrentSampleRate();
        }
    }

    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
        int numInputChannels,
        float* const* outputChannelData,
        int numOutputChannels,
         int numSamples,
         const juce::AudioIODeviceCallbackContext& context) override {
         juce::ignoreUnused(context);
 
        {
            const juce::ScopedLock sl(writerLock_);
            if (activeWriter_.load() != nullptr && numInputChannels > 0) {
                activeWriter_.load()->write(inputChannelData, numSamples);
                nextSampleNum_ += numSamples;
            }
        }

        std::function<void(const float*, int)> callbackCopy;
        {
            const juce::ScopedLock sl(callbackLock_);
            callbackCopy = sampleCallback_;
        }
        if (callbackCopy && numInputChannels > 0 && inputChannelData[0] != nullptr) {
            callbackCopy(inputChannelData[0], numSamples);
        }

         for (int i = 0; i < numOutputChannels; ++i) {
             if (outputChannelData[i] != nullptr) {
                 juce::FloatVectorOperations::clear(outputChannelData[i], numSamples);
             }
         }
     }
 
 private:
    juce::TimeSliceThread backgroundThread_{ "Audio Recorder Thread" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedWriter_;
    double sampleRate_ = 0.0;
    int nextSampleNum_ = 0;
    juce::CriticalSection writerLock_;
    std::atomic<juce::AudioFormatWriter::ThreadedWriter*> activeWriter_{ nullptr };

    juce::CriticalSection callbackLock_;
    std::function<void(const float*, int)> sampleCallback_;
 };
 
 // ==============================================================================
// AUDIO PLAYER (playback only)
 // ==============================================================================
 class AudioPlayer : public juce::AudioIODeviceCallback {
 public:
    explicit AudioPlayer(juce::AudioBuffer<float> bufferToPlay)
        : sourceBuffer_(std::move(bufferToPlay)), samplesPlayed_(0) {
     }
 
     void audioDeviceAboutToStart(juce::AudioIODevice*) override {
        samplesPlayed_ = 0;
     }
 
     void audioDeviceStopped() override {}
 
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
        int numInputChannels,
        float* const* outputChannelData,
        int numOutputChannels,
         int numSamples,
         const juce::AudioIODeviceCallbackContext& context) override {
         juce::ignoreUnused(inputChannelData, numInputChannels, context);
 
        int samplesRemaining = sourceBuffer_.getNumSamples() - samplesPlayed_;
         int samplesToPlay = std::min(numSamples, samplesRemaining);
 
         if (samplesToPlay > 0) {
            const float* src = sourceBuffer_.getReadPointer(0, samplesPlayed_);
             for (int i = 0; i < numOutputChannels; ++i) {
                 if (outputChannelData[i] != nullptr) {
                     std::memcpy(outputChannelData[i], src, sizeof(float) * samplesToPlay);
                     if (samplesToPlay < numSamples) {
                         juce::FloatVectorOperations::clear(outputChannelData[i] + samplesToPlay,
                             numSamples - samplesToPlay);
                     }
                 }
             }
            samplesPlayed_ += samplesToPlay;
         }
         else {
             for (int i = 0; i < numOutputChannels; ++i) {
                 if (outputChannelData[i] != nullptr) {
                     juce::FloatVectorOperations::clear(outputChannelData[i], numSamples);
                 }
             }
         }
     }
 
 private:
    juce::AudioBuffer<float> sourceBuffer_;
    int samplesPlayed_ = 0;
 };
 
 // ==============================================================================
// MAC PROTOCOL COORDINATOR
 // ==============================================================================
class MacProtocol {
public:
    enum class Mode { Sender, Receiver };

    MacProtocol(juce::AudioDeviceManager& deviceManager, AudioRecorder& recorder, Mode mode)
        : deviceManager_(deviceManager), recorder_(recorder), mode_(mode) {
    }

    ~MacProtocol() {
        stop();
    }

    void setVerbose(bool verbose) { verbose_ = verbose; }

    void start() {
        if (running_) return;
        recorder_.setSampleCallback([this](const float* samples, int numSamples) {
            onSamples(samples, numSamples);
            });
        running_ = true;
        macThread_ = std::thread(&MacProtocol::runLoop, this);
    }

    void stop() {
        if (!running_) {
            recorder_.setSampleCallback(nullptr);
             return;
        }
        running_ = false;
        if (macThread_.joinable()) {
            macThread_.join();
        }
        recorder_.setSampleCallback(nullptr);
    }

    void enqueueDataFrame(int frameId, const std::vector<bool>& data) {
        Frame frame;
        frame.type = ASK::FRAME_TYPE_DATA;
        frame.id = frameId;
        frame.data = data;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            dataQueue_.push(frame);
        }
        totalFramesEnqueued_++;
    }

    int getAckedFrameCount() const {
        return ackedFrames_.load();
    }

    std::map<int, std::vector<bool>> getReceivedFrames() const {

        std::lock_guard<std::mutex> lock(receivedMutex_);
        return receivedData_;
    }

private:
    enum class State { Idle, TxPending, WaitAck };

    void runLoop() {
        std::cout << "[MAC] Coordinator started" << std::endl;
        while (running_) {
            processIncomingFrames();
            handleTransmissions();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));  // Tight loop for speed
        }
        std::cout << "[MAC] Coordinator stopped" << std::endl;
    }

    void onSamples(const float* samples, int numSamples) {
        std::lock_guard<std::mutex> lock(sampleMutex_);
        sampleBuffer_.insert(sampleBuffer_.end(), samples, samples + numSamples);
        totalSamplesCaptured_ += numSamples;
        if (sampleBuffer_.size() > maxBufferSamples_) {
            size_t removeCount = sampleBuffer_.size() - maxBufferSamples_;
            sampleBuffer_.erase(sampleBuffer_.begin(), sampleBuffer_.begin() + removeCount);
            bufferStartSample_ += (int64_t)removeCount;
        }
    }

    void processIncomingFrames() {
        std::vector<float> bufferCopy;
        int64_t bufferStart = 0;
        int64_t currentSamplePos = 0;
        {
            std::lock_guard<std::mutex> lock(sampleMutex_);
            if (sampleBuffer_.size() < minimumFrameSamples_) return;
            bufferCopy = sampleBuffer_;
            bufferStart = bufferStartSample_;
            currentSamplePos = bufferStart + (int64_t)sampleBuffer_.size();
        }

        // if (verbose_) {
        //     std::cout << "[DEBUG] processIncomingFrames @sample=" << currentSamplePos << std::endl;
        // }

        if (mode_ == Mode::Sender && state_ == State::WaitAck) {
            // SENDER MODE: Fast ACK check - only demodulate first 10 bits
            int detectedPos = 0;
            if (demodulator_.fastAckCheck(bufferCopy, currentFrame_.id, detectedPos)) {
                int64_t absoluteSample = bufferStart + detectedPos;
                if (absoluteSample > lastProcessedSample_) {
                    lastProcessedSample_ = absoluteSample;
                    if (verbose_) {
                        std::cout << "[MAC] ACK received for frame " << currentFrame_.id 
                                  << " (fast check @ " << absoluteSample << ", no CRC)" << std::endl;
                    }
                    state_ = State::Idle;
                    resendCount_ = 0;
                    ackedFrames_++;
                }
            }
            trimBuffer();  // Trim after fast check
            return;  // Don't do full demodulation in sender mode
        }

        // RECEIVER MODE: Fast check to discard own ACK frames before full demodulation
        if (mode_ == Mode::Receiver) {
            std::vector<int> ackPositions = demodulator_.fastAckDetect(bufferCopy);
            for (int pos : ackPositions) {
                int64_t absoluteSample = bufferStart + pos;
                if (absoluteSample > lastProcessedSample_) {
                    lastProcessedSample_ = absoluteSample;
                    if (verbose_) {
                        std::cout << "[MAC] Own ACK frame discarded @ " << absoluteSample 
                                  << " (fast check, skipped full demodulation)" << std::endl;
                    }
                }
            }
        }

        // Full demodulation for DATA frames (receiver) or all frames (sender not waiting)
        auto frames = demodulator_.demodulateBuffer(bufferCopy);
        for (const Frame& frame : frames) {
            int64_t absoluteSample = bufferStart + frame.sampleIndex;
            if (absoluteSample <= lastProcessedSample_) {
                continue;  // Already processed this frame
            }
            lastProcessedSample_ = absoluteSample;

            // Verbose output for new frames only
            if (verbose_) {
                std::string typeBits(1, frame.type ? '1' : '0');
                std::string idBits;
                for (int i = ASK::idBitsPerFrame - 1; i >= 0; --i) {
                    idBits += ((frame.id >> i) & 1) ? '1' : '0';
                }
                std::string dataBits;
                for (int i = 0; i < ASK::dataBitsPerFrame && i < (int)frame.data.size(); ++i) {
                    dataBits += frame.data[i] ? '1' : '0';
                }
                std::cout << "[RX]                Frame @" << absoluteSample
                    << " TYPE=" << (frame.type ? "ACK" : "DATA")
                    << " ID=" << frame.id
                    << " CRC=" << (frame.crcValid ? "OK" : "FAIL")<<"\n" << std::endl;
                //std::cout << "      TYPE bits: " << typeBits << std::endl;
                //std::cout << "      ID   bits: " << idBits << std::endl;
                //std::cout << "      DATA bits: " << dataBits << std::endl;
            }

            if (mode_ == Mode::Sender) {
                // Sender mode but not waiting for ACK - ignore all frames
                continue;
            }
            else {
                // RECEIVER MODE: Skip ACK frames (already discarded by fast check)
                if (frame.type == ASK::FRAME_TYPE_ACK) {
                    continue;  // ACK frames already handled by fastAckDetect
                }
                
                // RECEIVER MODE: Only process DATA frames with consecutive IDs
                if (frame.type == ASK::FRAME_TYPE_DATA) {
                    // Check CRC for DATA frames
                    if (!frame.crcValid) {
                        if (verbose_) {
                            std::cout << "[MAC] DATA frame ID=" << frame.id << " CRC FAIL, dumping" << std::endl;
                        }
                        continue;
                    }

                    // Check if ID is consecutive to previously received ID
                    bool isConsecutive = (expectedNextId_ == 0) || (frame.id == expectedNextId_);

                    if (isConsecutive) {
                        // Store frame
                        {
                            std::lock_guard<std::mutex> lock(receivedMutex_);
                            receivedData_[frame.id] = frame.data;
                        }
                        expectedNextId_ = frame.id + 1;

                        if (verbose_) {
                            std::cout << "[MAC] DATA frame " << frame.id << " stored (consecutive), sending ACK" << std::endl;
                        }

                        // Send ACK immediately
                        Frame ack;
                        ack.type = ASK::FRAME_TYPE_ACK;
                        ack.id = frame.id;  // Echo the ID
                        ack.data.assign(ASK::dataBitsPerFrame, false);  // No data in ACK
                        std::lock_guard<std::mutex> lock(queueMutex_);
                        ackQueue_.push(ack);

                        // if (verbose_) {
                        //     std::cout << "[DEBUG] ACK queued for frame " << frame.id << " @sample=" << currentSamplePos << std::endl;
                        // }
                    }
                    else {
                        if (verbose_) {
                            std::cout << "[MAC] DATA frame ID=" << frame.id
                                << " dumped (expected " << expectedNextId_ << ")" << std::endl;
                        }
                    }
                }
                // Ignore ACK frames in receiver mode (we send them, don't process them)
            }
        }

        trimBuffer();  // Trim after full processing
    }

    void handleTransmissions() {
        // if (verbose_) {
        //     int64_t currentPos = 0;
        //     {
        //         std::lock_guard<std::mutex> lock(sampleMutex_);
        //         currentPos = bufferStartSample_ + (int64_t)sampleBuffer_.size();
        //     }
        //     std::cout << "[DEBUG] handleTransmissions @sample=" << currentPos << std::endl;
        // }

        // Check if current transmission is finished
        if (isTransmitting_) {
            auto txElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - transmissionStartTime_).count();
            if (txElapsed >= transmissionDurationMs_) {
                isTransmitting_ = false;
                // if (verbose_) {
                //     int64_t currentPos = 0;
                //     {
                //         std::lock_guard<std::mutex> lock(sampleMutex_);
                //         currentPos = bufferStartSample_ + (int64_t)sampleBuffer_.size();
                //     }
                //     std::cout << "[DEBUG] Transmission finished @sample=" << currentPos << std::endl;
                // }
            }
        }

        // ACK frames have priority
        Frame ackFrame;
        bool hasAckFrame = false;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (!ackQueue_.empty()) {
                ackFrame = ackQueue_.front();
                ackQueue_.pop();
                hasAckFrame = true;
            }
        }
        if (hasAckFrame && !isTransmitting_) {
            // if (verbose_) {
            //     int64_t currentPos = 0;
            //     {
            //         std::lock_guard<std::mutex> lock(sampleMutex_);
            //         currentPos = bufferStartSample_ + (int64_t)sampleBuffer_.size();
            //     }
            //     std::cout << "[DEBUG] Dequeuing ACK for frame " << ackFrame.id << " @sample=" << currentPos << std::endl;
            // }
            playFrame(ackFrame);
         return;
     }

        switch (state_) {
        case State::Idle: {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (!dataQueue_.empty()) {
                currentFrame_ = dataQueue_.front();
                dataQueue_.pop();
                resendCount_ = 0;
                state_ = State::TxPending;
            }
            break;
        }
        case State::TxPending: {
            if (!isTransmitting_) {
                resendCount_++;
                if (verbose_) {
                    std::cout << "[MAC] Sending DATA frame " << currentFrame_.id
                        << " (attempt " << resendCount_ << ")" << std::endl;
                }
                playFrame(currentFrame_);
                timeoutStart_ = std::chrono::steady_clock::now();
                state_ = State::WaitAck;
            }
            break;
        }
        case State::WaitAck: {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - timeoutStart_).count();
            if (elapsed > ASK::TIMEOUT_MS) {
                if (resendCount_ < ASK::MAX_RESEND) {
                    if (verbose_) {
                        std::cout << "[MAC] Timeout, retrying frame " << currentFrame_.id << std::endl;
                    }
                    state_ = State::TxPending;
                }
                else {
                    std::cerr << "[MAC] ERROR: Frame " << currentFrame_.id
                        << " failed after max retries" << std::endl;
                    state_ = State::Idle;
                }
            }
            break;
        }
        }
    }

    void playFrame(const Frame& frame) {
        // Get current sample position before sending
        int64_t txSamplePos = 0;
        {
            std::lock_guard<std::mutex> lock(sampleMutex_);
            txSamplePos = bufferStartSample_ + (int64_t)sampleBuffer_.size();
        }

        juce::AudioBuffer<float> audio = Modulator::modulateFrame(frame);
        const int numSamples = audio.getNumSamples();
        if (verbose_) {
            std::cout  << "[TX] Frame ID=" << frame.id
                << " " << (frame.type ? "ACK" : "DATA")
                << "     @sample=" << txSamplePos <<"\n"<< std::endl;
            //std::cout << "     Bits: " << frameToBitString(frame) << std::endl;
        }

        // Start non-blocking transmission
        AudioPlayer* player = new AudioPlayer(std::move(audio));
        deviceManager_.addAudioCallback(player);

        // Record transmission start
        isTransmitting_ = true;
        transmissionStartTime_ = std::chrono::steady_clock::now();
        transmissionDurationMs_ = static_cast<int>((double)numSamples / ASK::sampleRate * 1000.0) + 5;  // +margin

        // Schedule deletion after transmission
        std::thread([this, player]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(transmissionDurationMs_ + 5));
            deviceManager_.removeAudioCallback(player);
            delete player;
        }).detach();
    }

    std::string frameToBitString(const Frame& frame) const {
        CRC8Generator crcGen(ASK::crcPolynomial);
        std::vector<bool> bits;
        bits.reserve(ASK::bitsPerFrame);
        bits.push_back(frame.type);
        for (int i = ASK::idBitsPerFrame - 1; i >= 0; --i) {
            bits.push_back((frame.id >> i) & 1);
        }
        for (int i = 0; i < ASK::dataBitsPerFrame; ++i) {
            bits.push_back(i < (int)frame.data.size() ? frame.data[i] : false);
        }
        std::vector<bool> full = crcGen.generate(bits);
        std::string out;
        out.reserve(full.size());
        for (bool b : full) {
            out += (b ? '1' : '0');
        }
        return out;
    }

    void trimBuffer() {
        std::lock_guard<std::mutex> lock(sampleMutex_);
        if (lastProcessedSample_ < 0) return;

        int64_t trimTo = lastProcessedSample_ - bufferStartSample_ + ASK::preambleSamples;  // Keep margin for next preamble
        if (trimTo < 0) trimTo = 0;
        if (trimTo >= (int64_t)sampleBuffer_.size()) {
            sampleBuffer_.clear();
            bufferStartSample_ += (int64_t)sampleBuffer_.size();
            return;
        }

        sampleBuffer_.erase(sampleBuffer_.begin(), sampleBuffer_.begin() + (size_t)trimTo);
        bufferStartSample_ += trimTo;
    }

    juce::AudioDeviceManager& deviceManager_;
    AudioRecorder& recorder_;
    FrameDemodulator demodulator_;
    Mode mode_;

    std::atomic<bool> running_{ false };
    std::thread macThread_;

    std::mutex sampleMutex_;
    std::vector<float> sampleBuffer_;
    int64_t totalSamplesCaptured_ = 0;
    int64_t bufferStartSample_ = 0;
    int64_t lastProcessedSample_ = -1;
    const size_t maxBufferSamples_ = static_cast<size_t>(ASK::sampleRate * 4);  // keep last 4 s
    const size_t minimumFrameSamples_ = ASK::preambleSamples + ASK::bitsPerFrame * ASK::samplesPerBit;

    std::mutex queueMutex_;
    std::queue<Frame> dataQueue_;
    std::queue<Frame> ackQueue_;

    State state_ = State::Idle;
    Frame currentFrame_;
    int resendCount_ = 0;
    std::chrono::steady_clock::time_point timeoutStart_;
    std::atomic<int> ackedFrames_{ 0 };
    int totalFramesEnqueued_ = 0;

    mutable     std::mutex receivedMutex_;
    std::map<int, std::vector<bool>> receivedData_;
    int expectedNextId_ = 0;  // For receiver mode: tracks expected consecutive ID

    // Transmission tracking for non-blocking sends
    std::atomic<bool> isTransmitting_{ false };
    std::chrono::steady_clock::time_point transmissionStartTime_;
    int transmissionDurationMs_ = 0;

    bool verbose_ = false;
};

// ==============================================================================
// FILE I/O UTILITIES
// ==============================================================================
class FileIO {
public:
    static std::vector<bool> readInputFile(const std::string& filename) {
        std::vector<bool> bits;
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "ERROR: Unable to open " << filename << std::endl;
            return bits;
        }
        std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)),
            (std::istreambuf_iterator<char>()));
        for (unsigned char byte : bytes) {
            for (int bit = 7; bit >= 0; --bit) {
                bits.push_back((byte >> bit) & 1);
            }
        }
        int maxBits = ASK::numFrames * ASK::dataBitsPerFrame;
        if ((int)bits.size() > maxBits) {
            bits.resize(maxBits);
            std::cout << "WARNING: Input truncated to " << maxBits << " bits" << std::endl;
        }
        std::cout << "Read " << bits.size() << " bits from " << filename << std::endl;
        return bits;
    }

    static void writeOutputFile(const std::string& filename,
        const std::map<int, std::vector<bool>>& frames) {
        std::ofstream out(filename, std::ios::binary);
        if (!out.is_open()) {
            std::cerr << "ERROR: Unable to write to " << filename << std::endl;
             return;
         }
         for (int i = 1; i <= ASK::numFrames; ++i) {
            auto it = frames.find(i);
            if (it == frames.end()) continue;
            for (bool b : it->second) {
                out << (b ? '1' : '0');
            }
        }
        out.close();
        std::cout << "Saved decoded output to " << filename << std::endl;
     }
 };
 
 // ==============================================================================
// HELPER FUNCTIONS
 // ==============================================================================
std::vector<std::vector<bool>> splitIntoFrames(const std::vector<bool>& inputBits) {
    std::vector<std::vector<bool>> frames(ASK::numFrames, std::vector<bool>(ASK::dataBitsPerFrame, false));
    for (int i = 0; i < ASK::numFrames; ++i) {
        int start = i * ASK::dataBitsPerFrame;
        for (int j = 0; j < ASK::dataBitsPerFrame && start + j < (int)inputBits.size(); ++j) {
            frames[i][j] = inputBits[start + j];
        }
    }
    return frames;
}

void runSenderMode(juce::AudioDeviceManager& deviceManager,
    const std::vector<std::vector<bool>>& frames,
    const juce::File& outputDir) {
    std::cout << "\n[SENDER MODE]\nVerbose output? (y/n): ";
    char resp;
    std::cin >> resp;
    std::cin.ignore();
    bool verbose = (resp == 'y' || resp == 'Y');

    std::cout << "Press ENTER to start transmission..." << std::endl;
             std::cin.get();

    AudioRecorder recorder;
    juce::File recordFile = outputDir.getChildFile("sender_record.wav");
    recorder.startRecording(recordFile, ASK::sampleRate);
    deviceManager.addAudioCallback(&recorder);

    MacProtocol mac(deviceManager, recorder, MacProtocol::Mode::Sender);
    mac.setVerbose(verbose);
    mac.start();

    for (int i = 0; i < (int)frames.size(); ++i) {
        mac.enqueueDataFrame(i + 1, frames[i]);
    }

    std::cout << "Transmitting... Press ENTER to interrupt." << std::endl;
    std::atomic<bool> interrupted{ false };
    std::thread inputThread([&interrupted]() {
         std::cin.get();
        interrupted = true;
        });

    const int totalFrames = (int)frames.size();
    while (!interrupted && mac.getAckedFrameCount() < totalFrames) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    if (interrupted) {
        std::cout << "\n[INTERRUPTED] Stopping transmission..." << std::endl;
    }

    mac.stop();

    if (inputThread.joinable()) {
        if (interrupted) inputThread.join();
        else inputThread.detach();
    }

    recorder.stop();
    deviceManager.removeAudioCallback(&recorder);

    std::cout << "Frames acknowledged: " << mac.getAckedFrameCount()
        << " / " << totalFrames << std::endl;
}

void runReceiverMode(juce::AudioDeviceManager& deviceManager, const juce::File& outputDir) {
    std::cout << "\n[RECEIVER MODE]\nVerbose output? (y/n): ";
    char resp;
    std::cin >> resp;
    std::cin.ignore();
    bool verbose = (resp == 'y' || resp == 'Y');

    std::cout << "Press ENTER to start receiving..." << std::endl;
     std::cin.get();
 
    AudioRecorder recorder;
    juce::File recordFile = outputDir.getChildFile("receiver_record.wav");
    recorder.startRecording(recordFile, ASK::sampleRate);
     deviceManager.addAudioCallback(&recorder);

    MacProtocol mac(deviceManager, recorder, MacProtocol::Mode::Receiver);
    mac.setVerbose(verbose);
    mac.start();

    std::cout << "Receiving... Press ENTER to stop." << std::endl;
     std::cin.get();

    mac.stop();
    auto received = mac.getReceivedFrames();
 
     recorder.stop();
     deviceManager.removeAudioCallback(&recorder);

    FileIO::writeOutputFile(outputDir.getChildFile("OUTPUT.txt").getFullPathName().toStdString(), received);
    std::cout << "Frames received: " << received.size() << std::endl;
}

// ==============================================================================
// MAIN
// ==============================================================================
int main(int argc, char* argv[]) {
    juce::ignoreUnused(argc, argv);
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::cout << "\n╔════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║      ASK NRZ DEMOD WITH OFFLINE CHIRP DETECTION + ACK        ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════════╝\n";
    std::cout << "Sample rate: " << ASK::sampleRate << " Hz" << std::endl;
    std::cout << "Samples per bit: " << ASK::samplesPerBit
        << " (raw rate ~" << (int)(ASK::sampleRate / ASK::samplesPerBit) << " bps)" << std::endl;

    juce::File outputDir(ASK::outputPath);
    if (!outputDir.exists()) {
        auto result = outputDir.createDirectory();
        if (result.failed()) {
            std::cerr << "ERROR: Unable to create output directory: " << ASK::outputPath << std::endl;
         return 1;
     }
    }

    juce::AudioDeviceManager deviceManager;
    auto initResult = deviceManager.initialiseWithDefaultDevices(1, 2);
    if (initResult.isNotEmpty()) {
        std::cerr << "WARNING: " << initResult << std::endl;
    }

    std::vector<bool> inputBits = FileIO::readInputFile(ASK::inputPath);
    if (inputBits.empty()) {
        std::cerr << "ERROR: No input bits available" << std::endl;
         return 1;
     }
    auto frames = splitIntoFrames(inputBits);

    bool running = true;
    while (running) {
     std::cout << "\n" << std::string(60, '=') << std::endl;
        std::cout << "Select mode:" << std::endl;
        std::cout << "  1. Sender (with ACK)" << std::endl;
        std::cout << "  2. Receiver (with ACK)" << std::endl;
        std::cout << "  0. Exit" << std::endl;
        std::cout << "Enter choice: ";

        int choice = 0;
        std::cin >> choice;
        std::cin.ignore();

        switch (choice) {
        case 0:
            running = false;
            break;
        case 1:
            runSenderMode(deviceManager, frames, outputDir);
            break;
        case 2:
            runReceiverMode(deviceManager, outputDir);
            break;
        default:
            std::cout << "Invalid choice." << std::endl;
            break;
        }
    }

    std::cout << "\nProgram terminated." << std::endl;
     return 0;
 }
 
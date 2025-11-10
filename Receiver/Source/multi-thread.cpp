/*
 * ============================================================================
 * ASK MODULATION/DEMODULATION WITH FULL ACK PROTOCOL
 * ============================================================================
 *
 * ARCHITECTURE:
 * -------------
 * 1. **MAC Thread**: Manages ACK protocol state machine
 *    - Idle → TxPending → WaitACK → (timeout/ACK) → Idle
 *    - Handles retransmissions and timeout logic
 * 
 * 2. **Audio Callback Thread** (real-time):
 *    - Records incoming audio → feeds to real-time frame detector
 *    - Plays outgoing frames from TX queue
 *    - Detects frames in real-time, queues to RX buffer
 * 
 * 3. **Frame Detector**: Real-time chirp detection and demodulation
 *    - Sliding window correlation for preamble detection
 *    - Immediate demodulation when frame detected
 * 
 * 4. **Frame Queues**:
 *    - TX Queue: Frames waiting to be transmitted
 *    - RX Queue: Frames received and decoded
 * 
 * ACK PROTOCOL STATE MACHINE:
 * ----------------------------
 * State: Idle
 *   - Wait for upper layer to set TxPending flag
 *   - Process incoming frames from RX queue
 *   - If DATA frame received: send ACK immediately
 * 
 * State: TxPending
 *   - Queue DATA frame for transmission
 *   - Start TIMEOUT timer (500ms)
 *   - Increment ReSend counter
 *   - Transition to WaitACK
 * 
 * State: WaitACK
 *   - Monitor RX queue for ACK frame with matching ID
 *   - If ACK received: clear timeout, return to Idle
 *   - If timeout expires:
 *     * ReSend < MAX_RESEND: retransmit (→ TxPending)
 *     * ReSend >= MAX_RESEND: report error, abort
 *   - If DATA frame received: send ACK (collision handling)
 * 
 * FRAME STRUCTURE (118 bits):
 * ---------------------------
 * [1-bit TYPE][9-bit ID][100-bit DATA][8-bit CRC]
 * - TYPE: 0=DATA, 1=ACK
 * - ID: 1-500 for DATA, echo ID for ACK
 * - DATA: 100 bits payload (zeros for ACK frames)
 * - CRC8: Error detection
 *
 * CRITICAL BUG FIXES:
 * -------------------
 * ✅ bitsPerFrame = 118 (was 108, math error fixed)
 * ✅ idBitsPerFrame = 9 (was 8, now supports 512 frames)
 * ✅ CRC validation checks all bits including CRC
 * ✅ Removed Manchester/NCC clutter
 *
 * FILES: D:\fourth_year\cs120\debug_pic\ASK\
 * ============================================================================
 */

 #include <JuceHeader.h>
 #include <iostream>
 #include <fstream>
 #include <cmath>
 #include <iomanip>
 #include <vector>
 #include <random>
 #include <algorithm>
#include <queue>
#include <mutex>
#include <chrono>
#include <map>
#include <atomic>
 
 namespace ASK {
    constexpr double sampleRate = 44100.0;
     constexpr int preambleSamples = 440;
 
    // Chirp parameters
    constexpr double chirp_f_start = 1000.0;
    constexpr double chirp_f_end = 4000.0;

    // NRZ line coding
    constexpr double baseFreq = 2000.0;
    constexpr int samplesPerBit = 3;          // 4,410 bps (tune: 3-15)
    
    // Frame structure: [1 TYPE][9 ID][100 DATA][8 CRC] = 118 bits
    constexpr int typeBitsPerFrame = 1;
    constexpr int idBitsPerFrame = 9;
     constexpr int dataBitsPerFrame = 100;
     constexpr int crcBitsPerFrame = 8;
    constexpr int bitsPerFrame = typeBitsPerFrame + idBitsPerFrame + dataBitsPerFrame + crcBitsPerFrame;
    constexpr int numFrames = 500;
    
    // ACK Protocol
    constexpr int TIMEOUT_MS = 5000;
    constexpr int MAX_RESEND = 5;
    
    // Frame types
    constexpr bool FRAME_TYPE_DATA = false;
    constexpr bool FRAME_TYPE_ACK = true;

     constexpr uint8_t crcPolynomial = 0xD5;
 
    const std::string inputPath = "INPUT.bin";
     const std::string outputPath = "D:\\fourth_year\\cs120\\debug_pic\\ASK\\";
 }
 
 // ==============================================================================
 //  CRC GENERATOR
 // ==============================================================================
 class CRC8Generator {
 private:
     uint8_t polynomial;
 public:
     CRC8Generator(uint8_t poly) : polynomial(poly) {}
 
     std::vector<bool> generate(const std::vector<bool>& data) {
         std::vector<bool> result = data;
         uint8_t crc = 0;
         for (bool bit : data) {
             bool feedback = (crc >> 7) ^ bit;
             crc <<= 1;
            if (feedback) crc ^= polynomial;
             }
         for (int i = 7; i >= 0; --i) {
             result.push_back((crc >> i) & 1);
         }
         return result;
     }
 
     bool check(const std::vector<bool>& dataWithCRC) {
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
 };
 
 // ==============================================================================
 //  SIGNAL GENERATOR
 // ==============================================================================
 class SignalGenerator {
 public:
     static juce::AudioBuffer<float> generatePreamble() {
         juce::AudioBuffer<float> preamble(1, ASK::preambleSamples);
         auto* signal = preamble.getWritePointer(0);
 
         std::vector<double> freqSweep;
         for (int i = 0; i < ASK::preambleSamples / 2; ++i) {
             double freq = juce::jmap((double)i, 0.0, (double)(ASK::preambleSamples / 2 - 1),
                 ASK::chirp_f_start, ASK::chirp_f_end);
             freqSweep.push_back(freq);
         }
         for (int i = ASK::preambleSamples / 2; i < ASK::preambleSamples; ++i) {
             double freq = juce::jmap((double)i, (double)(ASK::preambleSamples / 2),
                (double)(ASK::preambleSamples - 1), ASK::chirp_f_end, ASK::chirp_f_start);
             freqSweep.push_back(freq);
         }
 
         double currentPhase = 0.0;
         for (int i = 0; i < ASK::preambleSamples; ++i) {
             double phaseIncrement = 2.0 * juce::MathConstants<double>::pi * freqSweep[i] / ASK::sampleRate;
             currentPhase += phaseIncrement;
             signal[i] = std::sin(currentPhase);
         }
         return preamble;
     }
 
    static juce::AudioBuffer<float> generateNRZBit(bool bitValue, int samplesPerBit) {
        juce::AudioBuffer<float> tone(1, samplesPerBit);
        auto* signal = tone.getWritePointer(0);
 
         double phase = 0.0;
        double phaseIncrement = 2.0 * juce::MathConstants<double>::pi * ASK::baseFreq / ASK::sampleRate;
        float polarity = bitValue ? 1.0f : -1.0f;
 
        for (int i = 0; i < samplesPerBit; ++i) {
            signal[i] = polarity * std::sin(phase);
             phase += phaseIncrement;
             if (phase > 2.0 * juce::MathConstants<double>::pi) {
                 phase -= 2.0 * juce::MathConstants<double>::pi;
             }
         }
        return tone;
     }
 };
 
 // ==============================================================================
//  FRAME STRUCTURE
 // ==============================================================================
struct Frame {
    bool type;                          // 0=DATA, 1=ACK
    int id;                             // Frame ID (1-500)
    std::vector<bool> data;             // 100 bits
    bool crcValid;
    
    Frame() : type(false), id(0), crcValid(false) {
        data.resize(ASK::dataBitsPerFrame, false);
     }
 };
 
 // ==============================================================================
 //  MODULATOR
 // ==============================================================================
 class Modulator {
 public:
    static juce::AudioBuffer<float> modulateFrame(const Frame& frame) {
        std::vector<bool> frameBits;
        
        // TYPE bit
        frameBits.push_back(frame.type);
        
        // ID bits (9 bits)
        for (int i = ASK::idBitsPerFrame - 1; i >= 0; --i) {
            frameBits.push_back((frame.id >> i) & 1);
        }
        
        // DATA bits
             for (int i = 0; i < ASK::dataBitsPerFrame; ++i) {
            frameBits.push_back(i < (int)frame.data.size() ? frame.data[i] : false);
             }
 
             // Add CRC
        CRC8Generator crcGen(ASK::crcPolynomial);
        std::vector<bool> frameWithCRC = crcGen.generate(frameBits);
        
        // Generate audio signal
        auto preamble = SignalGenerator::generatePreamble();
        std::vector<float> signal;
        
             for (int i = 0; i < preamble.getNumSamples(); ++i) {
            signal.push_back(preamble.getSample(0, i));
        }
        
        for (bool bit : frameWithCRC) {
            auto bitSignal = SignalGenerator::generateNRZBit(bit, ASK::samplesPerBit);
            for (int j = 0; j < ASK::samplesPerBit; ++j) {
                signal.push_back(bitSignal.getSample(0, j));
            }
        }
        
        juce::AudioBuffer<float> result(1, (int)signal.size());
        for (size_t i = 0; i < signal.size(); ++i) {
            result.setSample(0, i, signal[i]);
        }
         return result;
     }
 };
 
 // ==============================================================================
//  REAL-TIME FRAME DETECTOR
 // ==============================================================================
class RealtimeFrameDetector {
private:
    juce::AudioBuffer<float> preambleTemplate_;
    CRC8Generator crcGen_;
    
    // State machine for detection
    enum State { DetectPreamble, CollectFrame };
    State state_ = DetectPreamble;
    
    std::vector<float> syncFIFO_;
    std::vector<float> frameFIFO_;
    double power_ = 0.0;
    double syncPowerLocalMax_ = 0.0;
    int startIndex_ = 0;
    int sampleCounter_ = 0;
    int frameSamples_ = ASK::bitsPerFrame * ASK::samplesPerBit;
    
    std::deque<bool> bitValue;
    
    std::vector<bool> demodulateFrameNRZ(const float* frameData, int frameSamples) {
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

public:
    RealtimeFrameDetector() : crcGen_(ASK::crcPolynomial) {
        preambleTemplate_ = SignalGenerator::generatePreamble();
        syncFIFO_.resize(ASK::preambleSamples, 0.0f);
    }
    
    // Process incoming samples, returns detected frame if any
    std::unique_ptr<Frame> processSamples(const float* samples, int numSamples) {
        const float* tempData = preambleTemplate_.getReadPointer(0);
        const int tempLen = preambleTemplate_.getNumSamples();
        
        for (int i = 0; i < numSamples; ++i) {
            float sample = samples[i];
            power_ = power_ * (1.0 - 1.0 / 64.0) + (sample * sample) / 64.0;
            
            if (state_ == DetectPreamble) {
                // Sliding window for preamble detection
                std::rotate(syncFIFO_.begin(), syncFIFO_.begin() + 1, syncFIFO_.end());
                syncFIFO_.back() = sample;
                
                double dotProduct = 0.0;
                for (int j = 0; j < tempLen; ++j) {
                    dotProduct += syncFIFO_[j] * tempData[j];
                }
                double syncPower = dotProduct / 200.0;
                
                if (syncPower > 1.0 && syncPower > syncPowerLocalMax_ && syncPower > power_ / 3) {
                    syncPowerLocalMax_ = syncPower;
                    startIndex_ = sampleCounter_;
                }
                else if ((sampleCounter_ - startIndex_ > 200) && (startIndex_ != 0)) {
                    // Preamble detected! Start collecting frame
                    state_ = CollectFrame;
                    frameFIFO_.clear();
                    std::fill(syncFIFO_.begin(), syncFIFO_.end(), 0.0f);
                    syncPowerLocalMax_ = 0.0;
                }
            }
            else if (state_ == CollectFrame) {
                frameFIFO_.push_back(sample);
                
                if ((int)frameFIFO_.size() >= frameSamples_) {
                    // Demodulate frame
                    std::vector<bool> bits = demodulateFrameNRZ(frameFIFO_.data(), frameSamples_);
                    
                    auto frame = std::make_unique<Frame>();
                    frame->type = bits[0];
                    frame->id = 0;
                    for (int j = 0; j < ASK::idBitsPerFrame; ++j) {
                        frame->id = (frame->id << 1) | (bits[ASK::typeBitsPerFrame + j] ? 1 : 0);
                    }
                    
                    int dataStart = ASK::typeBitsPerFrame + ASK::idBitsPerFrame;
                    for (int j = 0; j < ASK::dataBitsPerFrame; ++j) {
                        frame->data[j] = bits[dataStart + j];
                    }
                    
                    frame->crcValid = crcGen_.check(bits);
                    
                    // Reset state
                    state_ = DetectPreamble;
                    startIndex_ = 0;
                    frameFIFO_.clear();
                    
                    return frame;
                }
            }
            
            sampleCounter_++;
        }
        
        return nullptr;
    }
    
    void reset() {
        state_ = DetectPreamble;
        std::fill(syncFIFO_.begin(), syncFIFO_.end(), 0.0f);
        frameFIFO_.clear();
        power_ = 0.0;
        syncPowerLocalMax_ = 0.0;
        startIndex_ = 0;
        sampleCounter_ = 0;
    }
 };
 
 // ==============================================================================
//  TX AUDIO HANDLER (Playback only)
 // ==============================================================================
class TxAudioHandler : public juce::AudioIODeviceCallback {
private:
    std::mutex txMutex_;
    std::queue<juce::AudioBuffer<float>> txQueue_;   // Frames waiting to be sent
    juce::AudioBuffer<float> currentFrame_;
    int sampleIndex_ = 0;
    bool transmitting_ = false;
    std::atomic<bool> verbose_{false};

 public:
    TxAudioHandler() = default;

    void setVerbose(bool v) { verbose_ = v; }

    void queueFrame(const juce::AudioBuffer<float>& frame) {
        std::lock_guard<std::mutex> lock(txMutex_);
        txQueue_.push(frame);
     }
 
     void audioDeviceAboutToStart(juce::AudioIODevice*) override {
        sampleIndex_ = 0;
        transmitting_ = false;
        currentFrame_.setSize(0, 0);
     }
 
     void audioDeviceStopped() override {}
 
     void audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
         float* const* outputChannelData, int numOutputChannels,
        int numSamples, const juce::AudioIODeviceCallbackContext& ctx) override {
        juce::ignoreUnused(inputChannelData, numInputChannels, ctx);

        for (int ch = 0; ch < numOutputChannels; ++ch) {
            if (outputChannelData[ch] == nullptr) continue;

            int samplesWritten = 0;

            while (samplesWritten < numSamples) {
                if (!transmitting_) {
                    std::lock_guard<std::mutex> lock(txMutex_);
                    if (txQueue_.empty()) {
                        juce::FloatVectorOperations::clear(outputChannelData[ch] + samplesWritten,
                            numSamples - samplesWritten);
                        break;
                    }

                    currentFrame_ = txQueue_.front();
                    txQueue_.pop();
                    sampleIndex_ = 0;
                    transmitting_ = true;

                    if (verbose_) {
                        std::cout << "[TX] Starting frame (" << currentFrame_.getNumSamples()
                                  << " samples)" << std::endl;
                    }
                }

                int samplesRemaining = currentFrame_.getNumSamples() - sampleIndex_;
                int samplesToCopy = std::min(samplesRemaining, numSamples - samplesWritten);

                const float* src = currentFrame_.getReadPointer(0, sampleIndex_);
                std::memcpy(outputChannelData[ch] + samplesWritten, src, sizeof(float) * samplesToCopy);

                sampleIndex_ += samplesToCopy;
                samplesWritten += samplesToCopy;

                if (sampleIndex_ >= currentFrame_.getNumSamples()) {
                    transmitting_ = false;
                }
            }
        }
    }
 };
 
 // ==============================================================================
//  RX AUDIO HANDLER (Recording + Frame detection)
 // ==============================================================================
class RxAudioHandler : public juce::AudioIODeviceCallback {
 private:
    RealtimeFrameDetector detector_;
    std::mutex rxMutex_;
    std::queue<Frame> rxQueue_;
    std::atomic<bool> verbose_{false};
 
 public:
    RxAudioHandler() = default;

    void setVerbose(bool v) { verbose_ = v; }

    std::unique_ptr<Frame> getReceivedFrame() {
        std::lock_guard<std::mutex> lock(rxMutex_);
        if (rxQueue_.empty()) return nullptr;
        auto frame = std::make_unique<Frame>(rxQueue_.front());
        rxQueue_.pop();
        return frame;
    }

    void audioDeviceAboutToStart(juce::AudioIODevice*) override {
        detector_.reset();
    }

    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
        float* const* outputChannelData, int numOutputChannels,
        int numSamples, const juce::AudioIODeviceCallbackContext& ctx) override {
        juce::ignoreUnused(ctx);

        // Process input channels for frame detection
        if (numInputChannels > 0 && inputChannelData[0] != nullptr) {
            auto detectedFrame = detector_.processSamples(inputChannelData[0], numSamples);
            if (detectedFrame) {
                if (verbose_) {
                    std::cout << "[RX] Frame detected: ID=" << detectedFrame->id
                              << " Type=" << (detectedFrame->type ? "ACK" : "DATA")
                              << " CRC=" << (detectedFrame->crcValid ? "OK" : "FAIL") << std::endl;
                }
                std::lock_guard<std::mutex> lock(rxMutex_);
                rxQueue_.push(*detectedFrame);
            }
        }

        // Always output silence on playback channels for pure RX handler
        for (int ch = 0; ch < numOutputChannels; ++ch) {
            if (outputChannelData[ch] != nullptr) {
                juce::FloatVectorOperations::clear(outputChannelData[ch], numSamples);
            }
        }
     }
 };
 
 // ==============================================================================
//  ACK PROTOCOL MANAGER (MAC Layer)
 // ==============================================================================
class ACKProtocolManager {
public:
    enum State { Idle, TxPending, WaitACK };
    
private:
    State state_ = Idle;
    TxAudioHandler* txHandler_;
    RxAudioHandler* rxHandler_;
    
    int currentFrameId_ = 0;
    Frame currentFrame_;
    int resendCount_ = 0;
    std::chrono::steady_clock::time_point timeoutStart_;
    
    std::mutex stateMutex_;
    std::atomic<bool> running_{false};
    std::thread macThread_;
    
    std::map<int, std::vector<bool>> receivedDataFrames_;  // Store received DATA
    bool verbose_ = false;

public:
    ACKProtocolManager(TxAudioHandler* txHandler, RxAudioHandler* rxHandler)
        : txHandler_(txHandler), rxHandler_(rxHandler) {}
    
    ~ACKProtocolManager() {
        stop();
    }
    
    void setVerbose(bool v) { verbose_ = v; }
    
    void start() {
        running_ = true;
        macThread_ = std::thread(&ACKProtocolManager::macThreadFunc, this);
    }
    
    void stop() {
        running_ = false;
        if (macThread_.joinable()) {
            macThread_.join();
        }
    }
    
    // Upper layer calls this to send a DATA frame
    void sendDataFrame(int frameId, const std::vector<bool>& data) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (state_ != Idle) {
            std::cerr << "[MAC] ERROR: Tried to send while not idle!" << std::endl;
            return;
        }
        
        currentFrameId_ = frameId;
        currentFrame_.type = ASK::FRAME_TYPE_DATA;
        currentFrame_.id = frameId;
        currentFrame_.data = data;
        resendCount_ = 0;
        state_ = TxPending;
    }
    
    State getState() const {
        return state_;
    }
    
    std::map<int, std::vector<bool>> getReceivedFrames() const {
        return receivedDataFrames_;
    }

 private:
    void macThreadFunc() {
        std::cout << "[MAC] Thread started" << std::endl;
        
        while (running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            
            std::lock_guard<std::mutex> lock(stateMutex_);
            
            // Check for incoming frames
            while (auto frame = rxHandler_->getReceivedFrame()) {
                if (frame->crcValid) {
                    if (frame->type == ASK::FRAME_TYPE_DATA) {
                        // Received DATA frame → send ACK
                        handleIncomingDataFrame(*frame);
                    }
                    else if (frame->type == ASK::FRAME_TYPE_ACK) {
                        // Received ACK frame
                        handleIncomingAckFrame(*frame);
                    }
                }
            }
            
            // State machine logic
            if (state_ == TxPending) {
                // Send DATA frame
                resendCount_++;
                if (verbose_) {
                    std::cout << "[MAC] Sending DATA frame " << currentFrameId_
                        << " (attempt " << resendCount_ << ")" << std::endl;
                }
                
                auto audioFrame = Modulator::modulateFrame(currentFrame_);
                txHandler_->queueFrame(audioFrame);
                
                timeoutStart_ = std::chrono::steady_clock::now();
                state_ = WaitACK;
            }
            else if (state_ == WaitACK) {
                // Check timeout
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - timeoutStart_).count();
                
                if (elapsed > ASK::TIMEOUT_MS) {
                    if (resendCount_ < ASK::MAX_RESEND) {
                        if (verbose_) {
                            std::cout << "[MAC] Timeout! Retransmitting frame " << currentFrameId_ << std::endl;
                        }
                        state_ = TxPending;
                    }
                    else {
                        std::cerr << "[MAC] ERROR: Max retransmissions reached for frame "
                            << currentFrameId_ << std::endl;
                        state_ = Idle;
                    }
                }
            }
        }
        
        std::cout << "[MAC] Thread stopped" << std::endl;
    }
    
    void handleIncomingDataFrame(const Frame& frame) {
        if (verbose_) {
            std::cout << "[MAC] Received DATA frame " << frame.id << ", sending ACK" << std::endl;
        }
        
        // Store data
        receivedDataFrames_[frame.id] = frame.data;
        
        // Send ACK immediately
        Frame ackFrame;
        ackFrame.type = ASK::FRAME_TYPE_ACK;
        ackFrame.id = frame.id;  // Echo the ID
        ackFrame.data.resize(ASK::dataBitsPerFrame, false);  // ACK has no payload
        
        auto audioFrame = Modulator::modulateFrame(ackFrame);
        txHandler_->queueFrame(audioFrame);
    }
    
    void handleIncomingAckFrame(const Frame& frame) {
        if (state_ == WaitACK && frame.id == currentFrameId_) {
            if (verbose_) {
                std::cout << "[MAC] Received ACK for frame " << frame.id << std::endl;
            }
            state_ = Idle;
        }
    }
};

// ==============================================================================
//  FILE I/O
// ==============================================================================
class FileIO {
public:
    static std::vector<bool> readInputFile(const std::string& filename) {
        std::vector<bool> data;
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "ERROR: Could not open: " << filename << std::endl;
            return data;
        }

        std::vector<unsigned char> bytes(
            (std::istreambuf_iterator<char>(file)),
            (std::istreambuf_iterator<char>())
        );
        file.close();

        for (unsigned char byte : bytes) {
            for (int bit = 7; bit >= 0; --bit) {
                data.push_back((byte >> bit) & 1);
            }
        }

        int maxDataSize = ASK::numFrames * ASK::dataBitsPerFrame;
        if ((int)data.size() > maxDataSize) {
            data.resize(maxDataSize);
        }

        std::cout << "Read " << data.size() << " bits from " << filename << std::endl;
        return data;
    }
    
    static void writeOutputFile(const std::string& filename, const std::map<int, std::vector<bool>>& frames) {
        std::ofstream file(filename);
        if (!file.is_open()) {
            std::cerr << "ERROR: Could not write to: " << filename << std::endl;
             return;
         }
 
         for (int i = 1; i <= ASK::numFrames; ++i) {
            if (frames.find(i) != frames.end()) {
                for (bool b : frames.at(i)) {
                    file << (b ? '1' : '0');
                }
            }
        }
        file.close();
        std::cout << "✓ Output saved to: " << filename << std::endl;
     }
 };
 
 // ==============================================================================
 //  MAIN APPLICATION
 // ==============================================================================
 int main(int argc, char* argv[]) {
     juce::ScopedJuceInitialiser_GUI juceInit;
 
     std::cout << "\n╔════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║         ASK MODULATION WITH FULL ACK PROTOCOL                 ║\n";
     std::cout << "╚════════════════════════════════════════════════════════════════╝\n";
    std::cout << "\nFrame: [1 TYPE][9 ID][100 DATA][8 CRC] = 118 bits" << std::endl;
    std::cout << "Speed: " << (int)(ASK::sampleRate / ASK::samplesPerBit) << " bps" << std::endl;
    std::cout << "ACK Protocol: TIMEOUT=" << ASK::TIMEOUT_MS << "ms, MAX_RESEND=" << ASK::MAX_RESEND << std::endl;
 
     juce::File outputDir(ASK::outputPath);
    if (!outputDir.exists()) outputDir.createDirectory();

    juce::AudioDeviceManager deviceManager;
    auto result = deviceManager.initialiseWithDefaultDevices(1, 2);
    if (result.isNotEmpty()) std::cerr << "WARNING: " << result << std::endl;

    std::cout << "\nReading input data..." << std::endl;
    std::vector<bool> inputData = FileIO::readInputFile(ASK::inputPath);
    if (inputData.empty()) {
        std::cerr << "ERROR: No input data!" << std::endl;
             return 1;
         }

    // Split input into frames
    std::vector<std::vector<bool>> dataFrames;
    for (int i = 0; i < ASK::numFrames; ++i) {
        std::vector<bool> frameData(ASK::dataBitsPerFrame, false);
        int dataStart = i * ASK::dataBitsPerFrame;
        for (int j = 0; j < ASK::dataBitsPerFrame && (dataStart + j) < (int)inputData.size(); ++j) {
            frameData[j] = inputData[dataStart + j];
        }
        dataFrames.push_back(frameData);
    }

    bool continueRunning = true;
    while (continueRunning) {
        std::cout << "\n" << std::string(60, '=') << std::endl;
        std::cout << "SELECT MODE:" << std::endl;
        std::cout << "  1. Transmit with ACK protocol (sender mode)" << std::endl;
        std::cout << "  2. Receive with ACK protocol (receiver mode)" << std::endl;
        std::cout << "  3. Test mode (loopback with ACK)" << std::endl;
        std::cout << "  0. Exit" << std::endl;
        std::cout << "Enter choice: ";

        int choice;
        std::cin >> choice;
        std::cin.ignore();

        if (choice == 0) {
            continueRunning = false;
            continue;
        }

        switch (choice) {
        case 1: { // SENDER MODE
            std::cout << "\n[SENDER MODE WITH ACK PROTOCOL]" << std::endl;
            std::cout << "Will transmit " << ASK::numFrames << " frames with ACK" << std::endl;
            std::cout << "Verbose output? (y/n): ";
            char verboseResp;
            std::cin >> verboseResp;
            std::cin.ignore();
            bool verbose = (verboseResp == 'y' || verboseResp == 'Y');
            
            std::cout << "Press ENTER to start..." << std::endl;
         std::cin.get();

            TxAudioHandler txHandler;
            RxAudioHandler rxHandler;
            txHandler.setVerbose(verbose);
            rxHandler.setVerbose(verbose);
            ACKProtocolManager ackManager(&txHandler, &rxHandler);
            ackManager.setVerbose(verbose);
            
            deviceManager.addAudioCallback(&rxHandler);
            deviceManager.addAudioCallback(&txHandler);
            ackManager.start();
            
            // Send all frames with ACK
            int successCount = 0;
            auto startTime = std::chrono::steady_clock::now();
            
            for (int i = 0; i < ASK::numFrames; ++i) {
                ackManager.sendDataFrame(i + 1, dataFrames[i]);
                
                // Wait for ACK (with timeout)
                auto waitStart = std::chrono::steady_clock::now();
                while (ackManager.getState() != ACKProtocolManager::Idle) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::steady_clock::now() - waitStart).count();
                    if (elapsed > 10) {
                        std::cerr << "Frame " << (i + 1) << " failed after 10 seconds" << std::endl;
                        break;
                    }
                }
                
                if (ackManager.getState() == ACKProtocolManager::Idle) {
                    successCount++;
                    if (!verbose) {
                        std::cout << "Frame " << (i + 1) << " / " << ASK::numFrames
                            << " acknowledged" << std::endl;
                    }
                }

                std::cin.get();
            }
            
            auto endTime = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime).count();
            
            ackManager.stop();
            deviceManager.removeAudioCallback(&txHandler);
            deviceManager.removeAudioCallback(&rxHandler);
            
            std::cout << "\n[TRANSMISSION COMPLETE]" << std::endl;
            std::cout << "  Frames sent successfully: " << successCount << " / " << ASK::numFrames << std::endl;
            std::cout << "  Total time: " << duration << " seconds" << std::endl;
            std::cout << "  Success rate: " << (100.0 * successCount / ASK::numFrames) << "%" << std::endl;
            break;
        }

        case 2: { // RECEIVER MODE
            std::cout << "\n[RECEIVER MODE WITH ACK PROTOCOL]" << std::endl;
            std::cout << "Listening for incoming frames..." << std::endl;
            std::cout << "Verbose output? (y/n): ";
            char verboseResp;
            std::cin >> verboseResp;
            std::cin.ignore();
            bool verbose = (verboseResp == 'y' || verboseResp == 'Y');
            
     std::cout << "Press ENTER to start..." << std::endl;
     std::cin.get();
 
            TxAudioHandler txHandler;
            RxAudioHandler rxHandler;
            txHandler.setVerbose(verbose);
            rxHandler.setVerbose(verbose);
            ACKProtocolManager ackManager(&txHandler, &rxHandler);
            ackManager.setVerbose(verbose);
            
            deviceManager.addAudioCallback(&rxHandler);
            deviceManager.addAudioCallback(&txHandler);
            ackManager.start();
            
            std::cout << "Receiving... Press ENTER to stop." << std::endl;
     std::cin.get();
 
            auto receivedFrames = ackManager.getReceivedFrames();
            
            ackManager.stop();
            deviceManager.removeAudioCallback(&txHandler);
            deviceManager.removeAudioCallback(&rxHandler);
            
            std::cout << "\n[RECEPTION COMPLETE]" << std::endl;
            std::cout << "  Frames received: " << receivedFrames.size() << " / " << ASK::numFrames << std::endl;
            std::cout << "  Success rate: " << (100.0 * receivedFrames.size() / ASK::numFrames) << "%" << std::endl;
            
            // Save output
            FileIO::writeOutputFile(ASK::outputPath + "OUTPUT.txt", receivedFrames);
            break;
        }

        case 3: { // TEST MODE (Loopback)
            std::cout << "\n[TEST MODE - LOOPBACK WITH ACK]" << std::endl;
            std::cout << "Testing ACK protocol on same computer" << std::endl;
            std::cout << "Press ENTER to start..." << std::endl;
         std::cin.get();

            TxAudioHandler txHandler;
            RxAudioHandler rxHandler;
            txHandler.setVerbose(true);
            rxHandler.setVerbose(true);
            ACKProtocolManager ackManager(&txHandler, &rxHandler);
            ackManager.setVerbose(true);
            
            deviceManager.addAudioCallback(&rxHandler);
            deviceManager.addAudioCallback(&txHandler);
            ackManager.start();
            
            // Send first 10 frames as test
            for (int i = 0; i < 10 && i < ASK::numFrames; ++i) {
                ackManager.sendDataFrame(i + 1, dataFrames[i]);
                
                auto waitStart = std::chrono::steady_clock::now();
                while (ackManager.getState() != ACKProtocolManager::Idle) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::steady_clock::now() - waitStart).count();
                    if (elapsed > 5) break;
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            std::this_thread::sleep_for(std::chrono::seconds(2));
            
            auto receivedFrames = ackManager.getReceivedFrames();
            
            ackManager.stop();
            deviceManager.removeAudioCallback(&txHandler);
            deviceManager.removeAudioCallback(&rxHandler);
            
            std::cout << "\n[TEST RESULTS]" << std::endl;
            std::cout << "  Frames received back: " << receivedFrames.size() << " / 10" << std::endl;
            break;
        }

        default:
            std::cout << "Invalid choice" << std::endl;
            break;
        }

        if (continueRunning && choice >= 1 && choice <= 3) {
            std::cout << "\nContinue? (y/n): ";
            char response;
            std::cin >> response;
            std::cin.ignore();
            continueRunning = (response == 'y' || response == 'Y');
        }
    }

    std::cout << "\n✓ Program terminated" << std::endl;
    return 0;
}
 
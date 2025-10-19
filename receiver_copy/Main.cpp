/*
 * FSK/OFDM RECEIVER WITH CHIRP DETECTION
 * 
 * Features:
 * - Chirp detection using NCC or Dot Product (configurable)
 * - FSK or OFDM demodulation (configurable)
 * - Handles 0.5s silence before preamble
 * - Real-time recording and offline processing
 */

#include <JuceHeader.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <complex>
#include <algorithm>

// ==============================================================================
//  CONFIGURATION
// ==============================================================================
#define USE_NCC_DETECTION 1  // 1 = NCC, 0 = Dot Product
#define USE_OFDM 1           // 1 = OFDM, 0 = FSK (must match sender!)

namespace Config {
    constexpr double sampleRate = 44100.0;
    constexpr int preambleSamples = 440;
    
#if USE_OFDM
    // OFDM Parameters (must match sender)
    constexpr int numSubcarriers = 32;
    constexpr int fftSize = 64;
    constexpr int cyclicPrefixLen = 16;
    constexpr int symbolSamples = fftSize + cyclicPrefixLen;
    constexpr int bitsPerSymbol = numSubcarriers;
    constexpr double baseFreq = 2000.0;
    constexpr int payloadBits = 10000;
    constexpr int crcBits = 8;
    constexpr int totalBits = payloadBits + crcBits;
    constexpr int numSymbols = (totalBits + bitsPerSymbol - 1) / bitsPerSymbol;
    constexpr int totalDataSamples = numSymbols * symbolSamples;
#else
    // FSK Parameters
    constexpr double f0 = 2000.0;
    constexpr double f1 = 4000.0;
    constexpr double bitRate = 1000.0;
    constexpr int samplesPerBit = static_cast<int>(sampleRate / bitRate);
    constexpr int payloadBits = 10000;
    constexpr int crcBits = 8;
    constexpr int totalFrameBits = payloadBits + crcBits;
    constexpr int totalDataSamples = totalFrameBits * samplesPerBit;
    constexpr double chirpF0 = f0 - 1000.0;
    constexpr double chirpF1 = f1 + 1000.0;
#endif
}

// ==============================================================================
//  CRC-8
// ==============================================================================
uint8_t calculateCRC8(const std::vector<bool>& data) {
    const uint8_t polynomial = 0xD7;
    uint8_t crc = 0;
    for (bool bit : data) {
        crc ^= (bit ? 0x80 : 0x00);
        for (int i = 0; i < 8; ++i) {
            if (crc & 0x80) crc = (crc << 1) ^ polynomial;
            else crc <<= 1;
        }
    }
    return crc;
}

// ==============================================================================
//  CHIRP GENERATOR
// ==============================================================================
class ChirpGenerator {
public:
    static juce::AudioBuffer<float> generateChirp() {
        juce::AudioBuffer<float> chirp(1, Config::preambleSamples);
        auto* signal = chirp.getWritePointer(0);
        
        double currentPhase = 0.0;
        for (int i = 0; i < Config::preambleSamples; ++i) {
            double freq;
            
#if USE_OFDM
            if (i < Config::preambleSamples / 2) {
                freq = juce::jmap((double)i, 0.0, (double)Config::preambleSamples / 2.0,
                                 Config::baseFreq - 1000.0, Config::baseFreq + 3000.0);
            } else {
                freq = juce::jmap((double)i, (double)Config::preambleSamples / 2.0,
                                 (double)Config::preambleSamples, Config::baseFreq + 3000.0, Config::baseFreq - 1000.0);
            }
#else
            if (i < Config::preambleSamples / 2) {
                freq = juce::jmap((double)i, 0.0, (double)Config::preambleSamples / 2.0,
                                 Config::chirpF0, Config::chirpF1);
            } else {
                freq = juce::jmap((double)i, (double)Config::preambleSamples / 2.0,
                                 (double)Config::preambleSamples, Config::chirpF1, Config::chirpF0);
            }
#endif
            
            double phaseIncrement = 2.0 * juce::MathConstants<double>::pi * freq / Config::sampleRate;
            currentPhase += phaseIncrement;
            signal[i] = std::sin(currentPhase) * 0.5;
        }
        
        return chirp;
    }
};

// ==============================================================================
//  CHIRP DETECTOR
// ==============================================================================
class ChirpDetector {
private:
    juce::AudioBuffer<float> template_;
    double templateEnergy_;
    
public:
    ChirpDetector() {
        template_ = ChirpGenerator::generateChirp();
        templateEnergy_ = calculateEnergy(template_.getReadPointer(0), template_.getNumSamples());
        std::cout << "Chirp template: " << template_.getNumSamples() << " samples, energy=" << templateEnergy_ << std::endl;
    }
    
    int detectChirp(const juce::AudioBuffer<float>& signal) {
        const float* sigData = signal.getReadPointer(0);
        const float* tempData = template_.getReadPointer(0);
        const int tempLen = template_.getNumSamples();
        
        std::cout << "\nSearching for chirp in " << signal.getNumSamples() << " samples..." << std::endl;
        
#if USE_NCC_DETECTION
        std::cout << "Using NCC detection" << std::endl;
        return detectWithNCC(sigData, signal.getNumSamples(), tempData, tempLen);
#else
        std::cout << "Using Dot Product detection" << std::endl;
        return detectWithDotProduct(sigData, signal.getNumSamples(), tempData, tempLen);
#endif
    }
    
private:
    int detectWithNCC(const float* sigData, int sigLen, const float* tempData, int tempLen) {
        double maxNCC = 0.0;
        int maxPos = -1;
        
        for (int i = 0; i <= sigLen - tempLen; ++i) {
            double dotProduct = 0.0, signalEnergy = 0.0;
            
            for (int j = 0; j < tempLen; ++j) {
                dotProduct += sigData[i + j] * tempData[j];
                signalEnergy += sigData[i + j] * sigData[i + j];
            }
            
            if (signalEnergy < 1e-10 || templateEnergy_ < 1e-10) continue;
            
            double ncc = dotProduct / std::sqrt(signalEnergy * templateEnergy_);
            
            if (ncc > maxNCC) {
                maxNCC = ncc;
                maxPos = i;
            }
        }
        
        std::cout << "Max NCC=" << maxNCC << " at sample " << maxPos << std::endl;
        return maxPos;
    }
    
    int detectWithDotProduct(const float* sigData, int sigLen, const float* tempData, int tempLen) {
        double maxDot = -1e10;
        int maxPos = -1;
        
        for (int i = 0; i <= sigLen - tempLen; ++i) {
            double dotProduct = 0.0;
            for (int j = 0; j < tempLen; ++j) {
                dotProduct += sigData[i + j] * tempData[j];
            }
            
            if (dotProduct > maxDot) {
                maxDot = dotProduct;
                maxPos = i;
            }
        }
        
        std::cout << "Max Dot=" << maxDot << " at sample " << maxPos << std::endl;
        return maxPos;
    }
    
    double calculateEnergy(const float* buffer, int numSamples) {
        double energy = 0.0;
        for (int i = 0; i < numSamples; ++i) {
            energy += buffer[i] * buffer[i];
        }
        return energy;
    }
};

// ==============================================================================
//  DEMODULATORS
// ==============================================================================
#if !USE_OFDM
// FSK Matched Filter Demodulator
class FSKDemodulator {
private:
    juce::AudioBuffer<float> reference_f0, reference_f1;
    
    void generateReferences() {
        reference_f0.setSize(1, Config::samplesPerBit);
        reference_f1.setSize(1, Config::samplesPerBit);
        
        auto* ref0 = reference_f0.getWritePointer(0);
        auto* ref1 = reference_f1.getWritePointer(0);
        
        for (int i = 0; i < Config::samplesPerBit; ++i) {
            double phase0 = 2.0 * juce::MathConstants<double>::pi * Config::f0 * i / Config::sampleRate;
            double phase1 = 2.0 * juce::MathConstants<double>::pi * Config::f1 * i / Config::sampleRate;
            ref0[i] = std::sin(phase0);
            ref1[i] = std::sin(phase1);
        }
    }
    
public:
    FSKDemodulator() { generateReferences(); }
    
    std::vector<bool> demodulate(const juce::AudioBuffer<float>& frameData) {
        std::vector<bool> bits;
        bits.reserve(Config::totalFrameBits);
        
        const float* data = frameData.getReadPointer(0);
        const float* ref0 = reference_f0.getReadPointer(0);
        const float* ref1 = reference_f1.getReadPointer(0);
        
        for (int i = 0; i < Config::totalFrameBits; ++i) {
            const float* bitSamples = data + (i * Config::samplesPerBit);
            
            double corr_f0 = 0.0, corr_f1 = 0.0;
            for (int j = 0; j < Config::samplesPerBit; ++j) {
                corr_f0 += bitSamples[j] * ref0[j];
                corr_f1 += bitSamples[j] * ref1[j];
            }
            
            bits.push_back(corr_f1 > corr_f0);
            
            if ((i + 1) % 1000 == 0)
                std::cout << "  Demodulated " << (i + 1) << "/" << Config::totalFrameBits << " bits" << std::endl;
        }
        
        return bits;
    }
};
#else
// OFDM Demodulator
class OFDMDemodulator {
public:
    std::vector<bool> demodulate(const juce::AudioBuffer<float>& frameData) {
        std::vector<bool> bits;
        bits.reserve(Config::totalBits);
        
        const float* data = frameData.getReadPointer(0);
        
        std::cout << "Demodulating " << Config::numSymbols << " OFDM symbols..." << std::endl;
        
        for (int symIdx = 0; symIdx < Config::numSymbols; ++symIdx) {
            int symbolStart = symIdx * Config::symbolSamples;
            
            // Skip cyclic prefix, extract FFT window
            std::vector<std::complex<double>> timeDomain(Config::fftSize);
            for (int i = 0; i < Config::fftSize; ++i) {
                timeDomain[i] = {data[symbolStart + Config::cyclicPrefixLen + i], 0.0};
            }
            
            // FFT
            std::vector<std::complex<double>> freqDomain = simpleFFT(timeDomain);
            
            // Demodulate each subcarrier (BPSK)
            for (int k = 0; k < Config::numSubcarriers && bits.size() < Config::totalBits; ++k) {
                // Skip DC (k=0), start from k=1
                double realPart = freqDomain[k + 1].real();
                bits.push_back(realPart > 0.0);  // BPSK: +1 → 1, -1 → 0
            }
            
            if ((symIdx + 1) % 50 == 0)
                std::cout << "  Symbol " << (symIdx + 1) << "/" << Config::numSymbols << std::endl;
        }
        
        // Truncate to exact size
        if (bits.size() > Config::totalBits)
            bits.resize(Config::totalBits);
        
        return bits;
    }
    
private:
    std::vector<std::complex<double>> simpleFFT(const std::vector<std::complex<double>>& time) {
        int N = time.size();
        std::vector<std::complex<double>> freq(N);
        
        for (int k = 0; k < N; ++k) {
            std::complex<double> sum = {0.0, 0.0};
            for (int n = 0; n < N; ++n) {
                double angle = -2.0 * juce::MathConstants<double>::pi * k * n / N;
                std::complex<double> twiddle = {std::cos(angle), std::sin(angle)};
                sum += time[n] * twiddle;
            }
            freq[k] = sum;
        }
        
        return freq;
    }
};
#endif

// ==============================================================================
//  AUDIO RECORDER
// ==============================================================================
class AudioRecorder : public juce::AudioIODeviceCallback {
public:
    AudioRecorder(int durationSeconds) {
        int requiredSamples = static_cast<int>(Config::sampleRate * durationSeconds);
        recordedAudio.setSize(1, requiredSamples);
        recordedAudio.clear();
        samplesRecorded = 0;
    }
    
    void audioDeviceAboutToStart(juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}
    
    void audioDeviceIOCallbackWithContext(const float* const* input, int numIn,
                                         float* const*, int, int numSamples,
                                         const juce::AudioIODeviceCallbackContext&) override {
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
    juce::AudioBuffer<float> recordedAudio;
    int samplesRecorded;
};

// ==============================================================================
//  MAIN
// ==============================================================================
int main(int argc, char* argv[]) {
    juce::ScopedJuceInitialiser_GUI juceInit;
    
    std::cout << "\n╔════════════════════════════════════════════════════════════════╗\n";
#if USE_OFDM
    std::cout << "║              OFDM RECEIVER WITH CHIRP DETECTION                ║\n";
#else
    std::cout << "║              FSK RECEIVER WITH CHIRP DETECTION                 ║\n";
#endif
    std::cout << "╚════════════════════════════════════════════════════════════════╝\n";
    
    std::cout << "\nConfiguration:" << std::endl;
    std::cout << "  Sample Rate: " << Config::sampleRate << " Hz" << std::endl;
    std::cout << "  Detection: " << (USE_NCC_DETECTION ? "NCC" : "Dot Product") << std::endl;
    
#if USE_OFDM
    std::cout << "  Modulation: OFDM (" << Config::numSubcarriers << " subcarriers)" << std::endl;
    std::cout << "  Expected symbols: " << Config::numSymbols << std::endl;
#else
    std::cout << "  Modulation: FSK (f0=" << Config::f0 << ", f1=" << Config::f1 << ")" << std::endl;
    std::cout << "  Expected payload: " << Config::payloadBits << " bits" << std::endl;
#endif
    
    // Setup audio
    juce::AudioDeviceManager deviceManager;
    deviceManager.initialiseWithDefaultDevices(1, 0);
    
    AudioRecorder recorder(25);  // 25s capacity
    
    std::cout << "\n>>> READY TO RECORD <<<" << std::endl;
    std::cout << "Press ENTER to start recording..." << std::endl;
    std::cin.get();
    
    deviceManager.addAudioCallback(&recorder);
    std::cout << "\n🔴 RECORDING... (start Sender now)" << std::endl;
    std::cout << "Press ENTER when transmission complete..." << std::endl;
    std::cin.get();
    
    deviceManager.removeAudioCallback(&recorder);
    std::cout << "\n✓ Recorded " << recorder.getSamplesRecorded() 
              << " samples (" << (recorder.getSamplesRecorded() / Config::sampleRate) << "s)" << std::endl;
    
    // Save recording
    juce::File outFile = juce::File::getCurrentWorkingDirectory().getChildFile("debug_recording.wav");
    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wavFormat.createWriterFor(new juce::FileOutputStream(outFile), 
                                 Config::sampleRate, 1, 16, {}, 0));
    if (writer != nullptr) {
        writer->writeFromAudioSampleBuffer(recorder.getRecording(), 0, recorder.getSamplesRecorded());
        std::cout << "Saved: debug_recording.wav" << std::endl;
    }
    
    // STEP 1: Detect chirp
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "STEP 1: CHIRP DETECTION" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    
    ChirpDetector detector;
    int chirpPosition = detector.detectChirp(recorder.getRecording());
    
    if (chirpPosition < 0) {
        std::cerr << "\n✗ Chirp not detected!" << std::endl;
        return 1;
    }
    
    std::cout << "\n✓ Chirp at sample " << chirpPosition << " (" << (chirpPosition / Config::sampleRate) << "s)" << std::endl;
    
    // STEP 2: Extract data
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "STEP 2: EXTRACT DATA FRAME" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    
    int frameStart = chirpPosition + Config::preambleSamples;
    
    if (frameStart + Config::totalDataSamples > recorder.getSamplesRecorded()) {
        std::cerr << "✗ Insufficient samples!" << std::endl;
        return 1;
    }
    
    juce::AudioBuffer<float> frameData(1, Config::totalDataSamples);
    frameData.copyFrom(0, 0, recorder.getRecording(), 0, frameStart, Config::totalDataSamples);
    std::cout << "✓ Extracted " << Config::totalDataSamples << " samples" << std::endl;
    
    // STEP 3: Demodulate
    std::cout << "\n" << std::string(60, '=') << std::endl;
#if USE_OFDM
    std::cout << "STEP 3: OFDM DEMODULATION" << std::endl;
#else
    std::cout << "STEP 3: FSK MATCHED FILTER DEMODULATION" << std::endl;
#endif
    std::cout << std::string(60, '=') << std::endl;
    
#if USE_OFDM
    OFDMDemodulator demodulator;
#else
    FSKDemodulator demodulator;
#endif
    
    std::vector<bool> receivedBits = demodulator.demodulate(frameData);
    std::cout << "✓ Demodulated " << receivedBits.size() << " bits" << std::endl;
    
    // STEP 4: CRC
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "STEP 4: CRC VERIFICATION" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    
    std::vector<bool> payload(receivedBits.begin(), receivedBits.begin() + Config::payloadBits);
    std::vector<bool> receivedCRC(receivedBits.begin() + Config::payloadBits, receivedBits.end());
    
    uint8_t calculatedCRC = calculateCRC8(payload);
    uint8_t receivedCRCValue = 0;
    for (int i = 0; i < 8; ++i) {
        if (receivedCRC[i]) receivedCRCValue |= (0x80 >> i);
    }
    
    std::cout << "Calculated CRC: 0x" << std::hex << (int)calculatedCRC << std::dec << std::endl;
    std::cout << "Received CRC:   0x" << std::hex << (int)receivedCRCValue << std::dec << std::endl;
    std::cout << (calculatedCRC == receivedCRCValue ? "✓ CRC PASS" : "✗ CRC FAIL") << std::endl;
    
    // STEP 5: Save
    std::ofstream outputFile("OUTPUT.txt");
    for (bool bit : payload) outputFile << (bit ? '1' : '0');
    outputFile.close();
    
    std::cout << "\n✓ Saved " << payload.size() << " bits to OUTPUT.txt" << std::endl;
    
    // BER calculation
    std::ifstream inputFile("INPUT.txt");
    if (inputFile.is_open()) {
        std::vector<bool> originalBits;
        char ch;
        while (inputFile.get(ch) && originalBits.size() < payload.size()) {
            if (ch == '0' || ch == '1') originalBits.push_back(ch == '1');
        }
        inputFile.close();
        
        if (originalBits.size() == payload.size()) {
            int errors = 0;
            for (size_t i = 0; i < payload.size(); ++i) {
                if (payload[i] != originalBits[i]) errors++;
            }
            std::cout << "\n📊 BER: " << errors << "/" << payload.size() 
                      << " = " << (100.0 * errors / payload.size()) << "%" << std::endl;
        }
    }
    
    std::cout << "\nPress ENTER to exit..." << std::endl;
    std::cin.get();
    
    return 0;
}

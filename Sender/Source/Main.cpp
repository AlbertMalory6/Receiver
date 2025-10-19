/*
 * FSK/OFDM SENDER WITH CHIRP PREAMBLE
 * - Configurable modulation: FSK or OFDM
 * - 0.5s silence before preamble
 * - Reads from INPUT.txt, adds CRC-8
 */

#include <JuceHeader.h>
#include <fstream>
#include <iostream>
#include <complex>
#include <vector>

// ==============================================================================
//  CONFIGURATION
// ==============================================================================
#define USE_OFDM 0  // 1 = OFDM, 0 = FSK

namespace Config {
    constexpr double sampleRate = 44100.0;
    constexpr int preambleSamples = 440;
    constexpr int silenceSamples = static_cast<int>(sampleRate * 0.5);  // 0.5s silence
    
#if USE_OFDM
    // OFDM Parameters
    constexpr int numSubcarriers = 32;          // Number of subcarriers
    constexpr int fftSize = 64;                 // FFT size (must be power of 2, >= numSubcarriers)
    constexpr int cyclicPrefixLen = 16;         // Cyclic prefix length (combat ISI)
    constexpr int symbolSamples = fftSize + cyclicPrefixLen;  // Total samples per OFDM symbol
    constexpr int bitsPerSymbol = numSubcarriers;  // QPSK on each subcarrier = 2 bits, but using BPSK = 1 bit
    constexpr double symbolRate = sampleRate / symbolSamples;  // ~552 symbols/sec
    constexpr double dataRate = symbolRate * bitsPerSymbol;    // ~17,664 bps
    constexpr double subcarrierSpacing = sampleRate / fftSize; // ~689 Hz
    constexpr double baseFreq = 2000.0;         // Start frequency for subcarriers
#else
    // FSK Parameters
    constexpr double f0 = 2000.0;
    constexpr double f1 = 4000.0;
    constexpr double bitRate = 1000.0;
    constexpr int samplesPerBit = static_cast<int>(sampleRate / bitRate);
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
//  SIGNAL GENERATOR
// ==============================================================================
class SignalSource : public juce::AudioSource {
public:
    SignalSource(const std::vector<bool>& bitsToSend) {
        generateFullSignal(bitsToSend);
    }
    
    bool isFinished() const { return isPlaybackFinished; }
    
    void prepareToPlay(int, double) override {
        position = 0;
        isPlaybackFinished = false;
    }
    
    void releaseResources() override {}
    
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override {
        if (isPlaybackFinished) { bufferToFill.clearActiveBufferRegion(); return; }
        
        auto remainingSamples = signalBuffer.getNumSamples() - position;
        auto samplesThisTime = juce::jmin(bufferToFill.numSamples, remainingSamples);
        
        if (samplesThisTime > 0) {
            for (int chan = 0; chan < bufferToFill.buffer->getNumChannels(); ++chan)
                bufferToFill.buffer->copyFrom(chan, bufferToFill.startSample, 
                                             signalBuffer, 0, position, samplesThisTime);
            position += samplesThisTime;
        }
        
        if (samplesThisTime < bufferToFill.numSamples) {
            bufferToFill.buffer->clear(bufferToFill.startSample + samplesThisTime, 
                                      bufferToFill.numSamples - samplesThisTime);
            isPlaybackFinished = true;
        }
    }
    
private:
    void generateFullSignal(const std::vector<bool>& bits) {
        // Add CRC
        auto payloadWithCrc = bits;
        uint8_t crc = calculateCRC8(bits);
        for (int i = 7; i >= 0; --i)
            payloadWithCrc.push_back((crc >> i) & 1);
        
#if USE_OFDM
        generateOFDMSignal(payloadWithCrc);
#else
        generateFSKSignal(payloadWithCrc);
#endif
    }
    
    void generateFSKSignal(const std::vector<bool>& bits) {
        const int totalDataSamples = bits.size() * Config::samplesPerBit;
        const int totalSamples = Config::silenceSamples + Config::preambleSamples + totalDataSamples;
        
        signalBuffer.setSize(1, totalSamples);
        signalBuffer.clear();
        auto* signal = signalBuffer.getWritePointer(0);
        
        // 1. Silence (0.5s)
        // Already cleared
        
        // 2. Preamble (Chirp)
        double currentPhase = 0.0;
        for (int i = 0; i < Config::preambleSamples; ++i) {
            double freq;
            if (i < Config::preambleSamples / 2)
                freq = juce::jmap((double)i, 0.0, (double)Config::preambleSamples / 2.0, 
                                 Config::f0 - 1000.0, Config::f1 + 1000.0);
            else
                freq = juce::jmap((double)i, (double)Config::preambleSamples / 2.0, 
                                 (double)Config::preambleSamples, Config::f1 + 1000.0, Config::f0 - 1000.0);
            
            double phaseIncrement = 2.0 * juce::MathConstants<double>::pi * freq / Config::sampleRate;
            currentPhase += phaseIncrement;
            signal[Config::silenceSamples + i] = std::sin(currentPhase) * 0.5;
        }
        
        // 3. FSK Data
        int sampleIndex = Config::silenceSamples + Config::preambleSamples;
        for (bool bit : bits) {
            double freq = bit ? Config::f1 : Config::f0;
            double phaseIncrement = 2.0 * juce::MathConstants<double>::pi * freq / Config::sampleRate;
            for (int i = 0; i < Config::samplesPerBit; ++i) {
                signal[sampleIndex++] = std::sin(currentPhase);
                currentPhase += phaseIncrement;
            }
        }
        
        signalBuffer.applyGain(0.9f / std::max(0.01f, signalBuffer.getMagnitude(0, signalBuffer.getNumSamples())));
    }
    
    void generateOFDMSignal(const std::vector<bool>& bits) {
        // Calculate number of OFDM symbols needed
        int numSymbols = (bits.size() + Config::bitsPerSymbol - 1) / Config::bitsPerSymbol;
        
        const int totalDataSamples = numSymbols * Config::symbolSamples;
        const int totalSamples = Config::silenceSamples + Config::preambleSamples + totalDataSamples;
        
        signalBuffer.setSize(1, totalSamples);
        signalBuffer.clear();
        auto* signal = signalBuffer.getWritePointer(0);
        
        // 1. Silence (0.5s)
        // Already cleared
        
        // 2. Preamble (Chirp) - same as FSK
        double currentPhase = 0.0;
        for (int i = 0; i < Config::preambleSamples; ++i) {
            double freq;
            if (i < Config::preambleSamples / 2)
                freq = juce::jmap((double)i, 0.0, (double)Config::preambleSamples / 2.0, 
                                 Config::baseFreq - 1000.0, Config::baseFreq + 3000.0);
            else
                freq = juce::jmap((double)i, (double)Config::preambleSamples / 2.0, 
                                 (double)Config::preambleSamples, Config::baseFreq + 3000.0, Config::baseFreq - 1000.0);
            
            double phaseIncrement = 2.0 * juce::MathConstants<double>::pi * freq / Config::sampleRate;
            currentPhase += phaseIncrement;
            signal[Config::silenceSamples + i] = std::sin(currentPhase) * 0.5;
        }
        
        // 3. OFDM Data
        std::cout << "Generating OFDM signal: " << numSymbols << " symbols, " 
                  << Config::numSubcarriers << " subcarriers" << std::endl;
        
        int sampleIndex = Config::silenceSamples + Config::preambleSamples;
        
        for (int symIdx = 0; symIdx < numSymbols; ++symIdx) {
            // Get bits for this symbol (BPSK: 1 bit per subcarrier)
            std::vector<std::complex<double>> subcarrierData(Config::fftSize, {0.0, 0.0});
            
            for (int k = 0; k < Config::numSubcarriers && (symIdx * Config::numSubcarriers + k) < bits.size(); ++k) {
                bool bit = bits[symIdx * Config::numSubcarriers + k];
                // BPSK: 0 → -1, 1 → +1
                subcarrierData[k + 1] = {bit ? 1.0 : -1.0, 0.0};  // Skip DC (k=0)
            }
            
            // IFFT to generate time-domain OFDM symbol
            std::vector<std::complex<double>> timeDomain = simpleIFFT(subcarrierData);
            
            // Add cyclic prefix
            for (int i = 0; i < Config::cyclicPrefixLen; ++i) {
                int srcIdx = Config::fftSize - Config::cyclicPrefixLen + i;
                signal[sampleIndex++] = timeDomain[srcIdx].real();
            }
            
            // Add main symbol
            for (int i = 0; i < Config::fftSize; ++i) {
                signal[sampleIndex++] = timeDomain[i].real();
            }
        }
        
        signalBuffer.applyGain(0.9f / std::max(0.01f, signalBuffer.getMagnitude(0, signalBuffer.getNumSamples())));
    }
    
    // Simple IFFT (DFT-based, not optimized)
    std::vector<std::complex<double>> simpleIFFT(const std::vector<std::complex<double>>& freq) {
        int N = freq.size();
        std::vector<std::complex<double>> time(N);
        
        for (int n = 0; n < N; ++n) {
            std::complex<double> sum = {0.0, 0.0};
            for (int k = 0; k < N; ++k) {
                double angle = 2.0 * juce::MathConstants<double>::pi * k * n / N;
                std::complex<double> twiddle = {std::cos(angle), std::sin(angle)};
                sum += freq[k] * twiddle;
            }
            time[n] = sum / std::complex<double>(N, 0.0);
        }
        
        return time;
    }
    
    juce::AudioBuffer<float> signalBuffer;
    int position = 0;
    bool isPlaybackFinished = true;
};

// ==============================================================================
//  MAIN
// ==============================================================================
int main(int argc, char* argv[]) {
    juce::ScopedJuceInitialiser_GUI juceInit;
    
    std::cout << "\n╔════════════════════════════════════════════════════════════════╗\n";
#if USE_OFDM
    std::cout << "║                    OFDM SENDER                                 ║\n";
#else
    std::cout << "║                    FSK SENDER                                  ║\n";
#endif
    std::cout << "╚════════════════════════════════════════════════════════════════╝\n";
    
    std::cout << "\nConfiguration:" << std::endl;
    std::cout << "  Sample Rate: " << Config::sampleRate << " Hz" << std::endl;
    std::cout << "  Silence before preamble: 0.5 seconds" << std::endl;
    std::cout << "  Preamble: " << Config::preambleSamples << " samples (chirp)" << std::endl;
    
#if USE_OFDM
    std::cout << "  Modulation: OFDM" << std::endl;
    std::cout << "  Subcarriers: " << Config::numSubcarriers << std::endl;
    std::cout << "  FFT Size: " << Config::fftSize << std::endl;
    std::cout << "  Cyclic Prefix: " << Config::cyclicPrefixLen << " samples" << std::endl;
    std::cout << "  Symbol Duration: " << Config::symbolSamples << " samples" << std::endl;
    std::cout << "  Data Rate: ~" << (int)Config::dataRate << " bps" << std::endl;
#else
    std::cout << "  Modulation: FSK" << std::endl;
    std::cout << "  Frequencies: f0=" << Config::f0 << " Hz, f1=" << Config::f1 << " Hz" << std::endl;
    std::cout << "  Bit Rate: " << Config::bitRate << " bps" << std::endl;
#endif
    
    // Read INPUT.txt
    std::vector<bool> bitsToSend;
    std::ifstream inputFile("INPUT.txt");
    
    if (!inputFile.is_open()) {
        std::cerr << "\n✗ ERROR: Could not open INPUT.txt" << std::endl;
        return 1;
    }
    
    char bitChar;
    while (inputFile.get(bitChar)) {
        if (bitChar == '0') bitsToSend.push_back(false);
        else if (bitChar == '1') bitsToSend.push_back(true);
    }
    inputFile.close();
    
    std::cout << "\n✓ Read " << bitsToSend.size() << " bits from INPUT.txt" << std::endl;
    std::cout << "  Payload: " << bitsToSend.size() << " bits" << std::endl;
    std::cout << "  With CRC-8: " << (bitsToSend.size() + 8) << " bits" << std::endl;
    
#if USE_OFDM
    int numSymbols = (bitsToSend.size() + 8 + Config::bitsPerSymbol - 1) / Config::bitsPerSymbol;
    double duration = (Config::silenceSamples + Config::preambleSamples + numSymbols * Config::symbolSamples) / Config::sampleRate;
#else
    double duration = (Config::silenceSamples + Config::preambleSamples + (bitsToSend.size() + 8) * Config::samplesPerBit) / Config::sampleRate;
#endif
    
    std::cout << "  Estimated transmission time: " << duration << " seconds" << std::endl;
    
    // Setup audio
    juce::AudioDeviceManager deviceManager;
    deviceManager.initialiseWithDefaultDevices(0, 2);
    
    SignalSource source(bitsToSend);
    juce::AudioSourcePlayer audioPlayer;
    audioPlayer.setSource(&source);
    
    std::cout << "\nPress ENTER to start transmission..." << std::endl;
    std::cin.get();
    
    std::cout << "\n🔴 TRANSMITTING..." << std::endl;
    deviceManager.addAudioCallback(&audioPlayer);
    
    while (!source.isFinished()) {
        juce::Thread::sleep(100);
    }
    
    deviceManager.removeAudioCallback(&audioPlayer);
    std::cout << "\n✓ Transmission complete!" << std::endl;
    
    return 0;
}

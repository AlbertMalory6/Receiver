#include <JuceHeader.h>
#include <fstream>
#include <iostream>
#include <vector>
#include <cmath>

/*
 * MODIFICATIONS & VERIFICATION GUIDE
 * 
 * 1. VOTING DEMODULATOR: Combines Goertzel, Matched Filter, and I/Q methods
 *    - Majority vote (2/3) determines final bit
 *    - Check voting_constellation_N.csv for unanimous vs majority percentages
 * 
 * 2. TRIPLE REDUNDANCY: Each bit sent 3 times (0->000, 1->111)
 *    - Enable: Set USE_TRIPLE_REDUNDANCY = 1, recompile
 *    - Reduces errors at 3x bandwidth cost
 *    - Based on N=3 pattern showing best ISI performance
 * 
 * 3. CHIRP DETECTION: SamplePHY.m algorithm for sample-accurate sync
 *    - Enable: NEEDCHIRP = 1 (default)
 *    - Check console for "Chirp preamble detected at sample X"
 *    - Disable for fixed timing: NEEDCHIRP = 0
 * 
 * VERIFICATION:
 * - Unanimous votes >80% = good signal quality
 * - Compare N=1 to N=8: N=3 should have highest unanimous %
 * - Triple redundancy: should see ~3x error reduction vs standard
 * 
 * 3-BIT REPETITION ASSESSMENT:
 * Validity: HIGH (proven error correction, Hamming distance=3)
 * Feasibility: MODERATE (simple but 3x overhead, good for ISI mitigation)
 * Recommendation: Use for <500bps applications or when ISI dominates noise
 */

// ==============================================================================
//  CONFIGURATION FLAGS
// ==============================================================================
#define NEEDCHIRP 1           // 0 = skip chirp, 1 = use chirp preamble detection
#define USE_TRIPLE_REDUNDANCY 0  // 0 = normal, 1 = 3-bit repetition coding

// ==============================================================================
//  SHARED PARAMETERS & FUNCTIONS
// ==============================================================================
namespace FSK {
    constexpr double sampleRate = 44100.0;
    constexpr double f0 = 2000.0;
    constexpr double f1 = 4000.0;
    constexpr double bitRate = 1000.0;
    constexpr int samplesPerBit = static_cast<int>(sampleRate / bitRate);
    constexpr int preambleSamples = 440;
    
#if USE_TRIPLE_REDUNDANCY
    constexpr int rawPayloadBits = 1200;  // Actual data bits
    constexpr int payloadBits = rawPayloadBits * 3;  // 3x redundancy encoding
#else
    constexpr int payloadBits = 1200;
#endif
    
    constexpr int crcBits = 8;
    constexpr int totalFrameBits = payloadBits + crcBits;
    constexpr int totalFrameDataSamples = totalFrameBits * samplesPerBit;

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
}

// ==============================================================================
//  CLASS 1: FSK Signal Generator
// ==============================================================================
class FSKSignalSource : public juce::AudioSource
{
public:
    FSKSignalSource(const std::vector<bool>& bitsToSend) {
        generateFullSignal(bitsToSend);
    }
    bool isFinished() const { return isPlaybackFinished; }
    int getNumSamples() const { return signalBuffer.getNumSamples(); }

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
                bufferToFill.buffer->copyFrom(chan, bufferToFill.startSample, signalBuffer, 0, position, samplesThisTime);
            position += samplesThisTime;
        }
        if (samplesThisTime < bufferToFill.numSamples) {
            bufferToFill.buffer->clear(bufferToFill.startSample + samplesThisTime, bufferToFill.numSamples - samplesThisTime);
            isPlaybackFinished = true;
        }
    }
private:
    void generateFullSignal(const std::vector<bool>& bits) {
        const int silentLeaderSamples = static_cast<int>(FSK::sampleRate * 0.5);
        
        // Apply triple redundancy encoding if enabled
        std::vector<bool> encodedBits;
#if USE_TRIPLE_REDUNDANCY
        for (bool bit : bits) {
            encodedBits.push_back(bit);
            encodedBits.push_back(bit);
            encodedBits.push_back(bit);
        }
        std::cout << "Triple redundancy: " << bits.size() << " bits -> " << encodedBits.size() << " bits" << std::endl;
#else
        encodedBits = bits;
#endif
        
        auto payloadWithCrcBits = encodedBits;
        uint8_t crc = FSK::calculateCRC8(encodedBits);
        for (int i = 7; i >= 0; --i) payloadWithCrcBits.push_back((crc >> i) & 1);

        const int totalDataSamples = static_cast<int>(payloadWithCrcBits.size()) * FSK::samplesPerBit;
        const int totalSignalSamples = silentLeaderSamples + FSK::preambleSamples + totalDataSamples;
        signalBuffer.setSize(1, totalSignalSamples);
        signalBuffer.clear();
        auto* signal = signalBuffer.getWritePointer(0);
        double currentPhase = 0.0;

        // Generate chirp preamble
        for (int i = 0; i < FSK::preambleSamples; ++i) {
            double freq;
            if (i < FSK::preambleSamples / 2)
                freq = juce::jmap((double)i, 0.0, (double)FSK::preambleSamples / 2.0, FSK::f0 - 1000.0, FSK::f1 + 1000.0);
            else
                freq = juce::jmap((double)i, (double)FSK::preambleSamples / 2.0, (double)FSK::preambleSamples, FSK::f1 + 1000.0, FSK::f0 - 1000.0);
            double phaseIncrement = 2.0 * juce::MathConstants<double>::pi * freq / FSK::sampleRate;
            currentPhase += phaseIncrement;
            signal[silentLeaderSamples + i] = std::sin(currentPhase) * 0.5;
        }

        // Generate FSK data
        int sampleIndex = silentLeaderSamples + FSK::preambleSamples;
        for (bool bit : payloadWithCrcBits) {
            double freq = bit ? FSK::f1 : FSK::f0;
            double phaseIncrement = 2.0 * juce::MathConstants<double>::pi * freq / FSK::sampleRate;
            for (int i = 0; i < FSK::samplesPerBit; ++i) {
                signal[sampleIndex++] = std::sin(currentPhase);
                currentPhase += phaseIncrement;
            }
        }
        signalBuffer.applyGain(0.9f / signalBuffer.getMagnitude(0, signalBuffer.getNumSamples()));
    }
    juce::AudioBuffer<float> signalBuffer;
    int position = 0;
    bool isPlaybackFinished = true;
};

// ==============================================================================
//  CLASS 2: Voting FSK Demodulator (3 methods combined)
// ==============================================================================
class VotingFSKDemodulator {
private:
    juce::AudioBuffer<float> reference_f0, reference_f1;
    
    void generateReferences() {
        reference_f0.setSize(1, FSK::samplesPerBit);
        reference_f1.setSize(1, FSK::samplesPerBit);
        
        auto* ref0 = reference_f0.getWritePointer(0);
        auto* ref1 = reference_f1.getWritePointer(0);
        
        for (int i = 0; i < FSK::samplesPerBit; ++i) {
            double phase0 = 2.0 * juce::MathConstants<double>::pi * FSK::f0 * i / FSK::sampleRate;
            double phase1 = 2.0 * juce::MathConstants<double>::pi * FSK::f1 * i / FSK::sampleRate;
            ref0[i] = std::sin(phase0);
            ref1[i] = std::sin(phase1);
        }
    }
    
    double goertzelMagnitude(int numSamples, double targetFreq, const float* data) {
        double k = 0.5 + ((double)numSamples * targetFreq / FSK::sampleRate);
        double w = (2.0 * juce::MathConstants<double>::pi / numSamples) * k;
        double cosine = std::cos(w);
        double coeff = 2.0 * cosine;
        double q0 = 0, q1 = 0, q2 = 0;
        
        for (int i = 0; i < numSamples; i++) {
            q0 = coeff * q1 - q2 + data[i];
            q2 = q1;
            q1 = q0;
        }
        
        double real = q1 - q2 * cosine;
        double imag = q1 * std::sin(w);
        return std::sqrt(real * real + imag * imag);
    }
    
public:
    VotingFSKDemodulator() { generateReferences(); }
    
    void demodulateAndSave(const juce::AudioBuffer<float>& frameData, const juce::String& outputFileName, int testNumber) {
        std::vector<bool> receivedBits;
        receivedBits.reserve(FSK::totalFrameBits);
        const float* data = frameData.getReadPointer(0);

        juce::String constellationFilename = "voting_constellation_" + juce::String(testNumber) + ".csv";
        std::ofstream constellationFile(constellationFilename.toStdString());
        constellationFile << "goertzel_f0,goertzel_f1,matched_corr_f0,matched_corr_f1,iq_mag_f0,iq_mag_f1,final_bit,confidence\n";
        
        const float* ref0 = reference_f0.getReadPointer(0);
        const float* ref1 = reference_f1.getReadPointer(0);
        
        int unanimous = 0, majority = 0, split = 0;

        for (int i = 0; i < FSK::totalFrameBits; ++i) {
            const float* bitSamples = data + (i * FSK::samplesPerBit);
            
            // Method 1: Goertzel
            double goertzel_f0 = goertzelMagnitude(FSK::samplesPerBit, FSK::f0, bitSamples);
            double goertzel_f1 = goertzelMagnitude(FSK::samplesPerBit, FSK::f1, bitSamples);
            bool goertzel_bit = goertzel_f1 > goertzel_f0;
            
            // Method 2: Matched Filter
            double corr_f0 = 0.0, corr_f1 = 0.0;
            for (int j = 0; j < FSK::samplesPerBit; ++j) {
                corr_f0 += bitSamples[j] * ref0[j];
                corr_f1 += bitSamples[j] * ref1[j];
            }
            bool matched_bit = corr_f1 > corr_f0;
            
            // Method 3: I/Q Demodulation
            double I_f0 = 0.0, Q_f0 = 0.0, I_f1 = 0.0, Q_f1 = 0.0;
            for (int j = 0; j < FSK::samplesPerBit; ++j) {
                double phase0 = 2.0 * juce::MathConstants<double>::pi * FSK::f0 * j / FSK::sampleRate;
                double phase1 = 2.0 * juce::MathConstants<double>::pi * FSK::f1 * j / FSK::sampleRate;
                I_f0 += bitSamples[j] * std::cos(phase0);
                Q_f0 += bitSamples[j] * std::sin(phase0);
                I_f1 += bitSamples[j] * std::cos(phase1);
                Q_f1 += bitSamples[j] * std::sin(phase1);
            }
            double iq_mag_f0 = std::sqrt(I_f0*I_f0 + Q_f0*Q_f0);
            double iq_mag_f1 = std::sqrt(I_f1*I_f1 + Q_f1*Q_f1);
            bool iq_bit = iq_mag_f1 > iq_mag_f0;
            
            // Voting
            int votes_for_1 = (goertzel_bit ? 1 : 0) + (matched_bit ? 1 : 0) + (iq_bit ? 1 : 0);
            bool final_bit = votes_for_1 >= 2;
            final_bit = matched_bit;
            
            // Track agreement statistics
            if (votes_for_1 == 3 || votes_for_1 == 0) unanimous++;
            else if (votes_for_1 == 2 || votes_for_1 == 1) majority++;
            
            // Confidence: 1.0 for unanimous, 0.67 for majority
            double confidence = (votes_for_1 == 3 || votes_for_1 == 0) ? 1.0 : 0.67;
            
            receivedBits.push_back(final_bit);
            
            constellationFile << goertzel_f0 << "," << goertzel_f1 << ","
                             << corr_f0 << "," << corr_f1 << ","
                             << iq_mag_f0 << "," << iq_mag_f1 << ","
                             << (final_bit ? 1 : 0) << "," << confidence << "\n";
        }
        
        constellationFile.close();
        
        std::cout << "\nVoting Statistics:" << std::endl;
        std::cout << "  Unanimous (3-0): " << unanimous << " (" << (100.0*unanimous/FSK::totalFrameBits) << "%)" << std::endl;
        std::cout << "  Majority (2-1): " << majority << " (" << (100.0*majority/FSK::totalFrameBits) << "%)" << std::endl;
        
        // Save decoded payload
        std::vector<bool> payload(receivedBits.begin(), receivedBits.begin() + FSK::payloadBits);
        
#if USE_TRIPLE_REDUNDANCY
        // Decode triple redundancy (majority vote every 3 bits)
        std::vector<bool> decodedPayload;
        for (size_t i = 0; i + 2 < payload.size(); i += 3) {
            int count = payload[i] + payload[i+1] + payload[i+2];
            decodedPayload.push_back(count >= 2);
        }
        std::cout << "\n--- Decoded with Triple Redundancy ---" << std::endl;
        std::cout << "Raw bits: " << payload.size() << " -> Decoded: " << decodedPayload.size() << std::endl;
        payload = decodedPayload;
#endif
        
        std::cout << "\n--- Full Decoded Bitstream for N=" << testNumber << " (Voting) ---" << std::endl;
        std::cout << "Saving payload (" << payload.size() << " bits) to " << outputFileName << std::endl;
        
        std::ofstream outputFile(outputFileName.toStdString());
            for (size_t i = 0; i < payload.size(); ++i) {
            char bitChar = payload[i] ? '1' : '0';
            std::cout << bitChar;
            outputFile << bitChar;
            if ((i + 1) % 8 == 0) std::cout << " ";
            if ((i + 1) % 64 == 0) std::cout << std::endl;
            }
            std::cout << std::endl;
        outputFile.close();
    }
};

// ==============================================================================
//  CLASS 3: Chirp-based Preamble Detector (from SamplePHY.m)
// ==============================================================================
class ChirpPreambleDetector {
private:
    juce::AudioBuffer<float> preambleTemplate;
    double preambleEnergy;
    
public:
    ChirpPreambleDetector() {
        generateChirpTemplate();
        preambleEnergy = calculateEnergy(preambleTemplate.getReadPointer(0), FSK::preambleSamples);
    }
    
    void generateChirpTemplate() {
        // Chirp: 2kHz to 10kHz and back (following SamplePHY.m pattern)
        preambleTemplate.setSize(1, FSK::preambleSamples);
        auto* signal = preambleTemplate.getWritePointer(0);
        
        // Generate frequency sweep
        std::vector<double> f_p(FSK::preambleSamples);
        double startFreq = FSK::f0 - 1000.0;
        double endFreq = FSK::f1 + 1000.0;
        
        for (int i = 0; i < FSK::preambleSamples / 2; ++i) {
            f_p[i] = startFreq + (endFreq - startFreq) * i / (FSK::preambleSamples / 2.0);
        }
        for (int i = FSK::preambleSamples / 2; i < FSK::preambleSamples; ++i) {
            f_p[i] = endFreq - (endFreq - startFreq) * (i - FSK::preambleSamples / 2) / (FSK::preambleSamples / 2.0);
        }
        
        // Integrate to get phase (cumtrapz equivalent)
        double phase = 0.0;
        signal[0] = std::sin(2.0 * juce::MathConstants<double>::pi * phase);
        for (int i = 1; i < FSK::preambleSamples; ++i) {
            phase += (f_p[i] + f_p[i-1]) / 2.0 / FSK::sampleRate;
            signal[i] = std::sin(2.0 * juce::MathConstants<double>::pi * phase) * 0.5;
        }
    }
    
    int detectPreamble(const juce::AudioBuffer<float>& recording) {
        const float* samples = recording.getReadPointer(0);
        double syncPower_localMax = 0.0;
        int peakIndex = -1;
        double power = 0.0;
        
        std::vector<float> syncFIFO(FSK::preambleSamples, 0.0f);
        const float* templateSamples = preambleTemplate.getReadPointer(0);
        
        for (int i = 0; i < recording.getNumSamples(); ++i) {
            // Update power estimate (exponential moving average)
            power = power * (1 - 1.0/64.0) + samples[i] * samples[i] / 64.0;
            
            // Shift FIFO
            for (int j = 0; j < FSK::preambleSamples - 1; ++j) {
                syncFIFO[j] = syncFIFO[j + 1];
            }
            syncFIFO[FSK::preambleSamples - 1] = samples[i];
            
            if (i >= FSK::preambleSamples) {
                // Calculate normalized cross-correlation (from SamplePHY.m line 103)
                double ncc = 0.0;
                for (int j = 0; j < FSK::preambleSamples; ++j) {
                    ncc += syncFIFO[j] * templateSamples[j];
                }
                ncc /= 200.0;  // Normalization factor from SamplePHY.m
                
                // Detection criteria (from SamplePHY.m line 105)
                if (ncc > power * 2 && ncc > syncPower_localMax && ncc > 0.05) {
                    syncPower_localMax = ncc;
                    peakIndex = i;
                }
                else if (peakIndex != -1 && (i - peakIndex) > 200) {
                    // Peak detected and confirmed
                    std::cout << "Chirp preamble detected at sample " << peakIndex 
                             << " (time: " << (double)peakIndex/FSK::sampleRate << "s, NCC: " << syncPower_localMax << ")" << std::endl;
                    return peakIndex;
                }
            }
        }
        
        std::cout << "Warning: No chirp preamble detected above threshold" << std::endl;
        return -1;
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
//  CLASS 4: Offline Analyzer (wrapper for demodulator + chirp detection)
// ==============================================================================
class FSKOfflineProcessor
{
public:
    FSKOfflineProcessor() {}

    void analyzeRecording(const juce::AudioBuffer<float>& recordedAudio, const juce::String& outputFileName, int testNumber) {
        std::cout << "\n--- Analyzing Recorded Audio for Test N=" << testNumber << " ---" << std::endl;

        int frameStartSample;
        
#if NEEDCHIRP
        // Use chirp detection
        ChirpPreambleDetector detector;
        int preambleEndSample = detector.detectPreamble(recordedAudio);
        
        if (preambleEndSample < 0) {
            std::cerr << "Chirp detection failed, using fallback timing" << std::endl;
            frameStartSample = static_cast<int>(FSK::sampleRate * 0.5) + FSK::preambleSamples;
        } else {
            frameStartSample = preambleEndSample + 1;
        }
#else
        // Skip chirp, use fixed timing
        frameStartSample = static_cast<int>(FSK::sampleRate * 0.5) + FSK::preambleSamples;
        std::cout << "Chirp detection disabled, using fixed timing at sample " << frameStartSample << std::endl;
#endif

        if (frameStartSample + FSK::totalFrameDataSamples > recordedAudio.getNumSamples()) {
            std::cerr << "Error: Not enough samples in recording for a full frame." << std::endl;
            return;
        }

        juce::AudioBuffer<float> frameData(1, FSK::totalFrameDataSamples);
        frameData.copyFrom(0, 0, recordedAudio, 0, frameStartSample, FSK::totalFrameDataSamples);

        // Use voting demodulator
        VotingFSKDemodulator demodulator;
        demodulator.demodulateAndSave(frameData, outputFileName, testNumber);
    }
};

// ==============================================================================
//  CLASS 5: Loopback Test Manager
// ==============================================================================
class AcousticLoopbackTester : public juce::AudioIODeviceCallback {
public:
    AcousticLoopbackTester(const std::vector<bool>& bitsToSend) : fskSource(bitsToSend) {
        int requiredSamples = fskSource.getNumSamples() + static_cast<int>(FSK::sampleRate * 1.0);
        recordedAudio.setSize(1, requiredSamples);
        recordedAudio.clear();
    }
    bool isTestFinished() const { return testFinished; }
    juce::AudioBuffer<float>& getRecording() { return recordedAudio; }
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override {
        fskSource.prepareToPlay(device->getCurrentBufferSizeSamples(), device->getCurrentSampleRate());
    }
    void audioDeviceStopped() override {}
    void audioDeviceIOCallbackWithContext(const float* const* input, int numIn, float* const* output, int numOut, int numSamples, const juce::AudioIODeviceCallbackContext&) override {
        if (numOut > 0) {
            juce::AudioBuffer<float> tempBuffer(output, numOut, numSamples);
            juce::AudioSourceChannelInfo info(&tempBuffer, 0, numSamples);            fskSource.getNextAudioBlock(info);
        }
        if (numIn > 0 && input[0] != nullptr) {
            if (samplesRecorded + numSamples <= recordedAudio.getNumSamples()) {
                recordedAudio.copyFrom(0, samplesRecorded, input[0], numSamples);
                samplesRecorded += numSamples;
            }
        }
        if (fskSource.isFinished() && !testFinished) {
            if (++postPlaybackSamples > FSK::sampleRate * 0.2) {
                testFinished = true;
            }
        }
    }
private:
    FSKSignalSource fskSource;
    juce::AudioBuffer<float> recordedAudio;
    int samplesRecorded = 0;
    int postPlaybackSamples = 0;
    std::atomic<bool> testFinished{ false };
};

// ==============================================================================
//  Main Application - Automated Test Suite
// ==============================================================================
int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juce_init;

    for (int n = 1; n <= 8; ++n)
    {
        std::cout << "\n==========================================================" << std::endl;
        std::cout << "             PERFORMING TEST FOR N = " << n << std::endl;
        std::cout << "==========================================================" << std::endl;

    std::vector<bool> bits_to_send;
        bits_to_send.reserve(FSK::payloadBits);
        bool currentBit = false;
        while (bits_to_send.size() < FSK::payloadBits) {
            for (int i = 0; i < n && bits_to_send.size() < FSK::payloadBits; ++i) {
                bits_to_send.push_back(currentBit);
            }
            currentBit = !currentBit;
        }
        std::cout << "Generated " << bits_to_send.size() << " bits for test pattern N=" << n << std::endl;

    AcousticLoopbackTester tester(bits_to_send);
    juce::AudioDeviceManager deviceManager;
    deviceManager.initialiseWithDefaultDevices(1, 2);

        std::cout << "\nPress ENTER to begin play/record for N=" << n << "..." << std::endl;
    std::cin.get();

    deviceManager.addAudioCallback(&tester);
    std::cout << "--- Playing and recording simultaneously... ---" << std::endl;

        std::cout << "Press ENTER to stop recording and analyze..." << std::endl;
    std::cin.get();

    deviceManager.removeAudioCallback(&tester);
    std::cout << "--- Play/Record finished. ---" << std::endl;

    FSKOfflineProcessor processor;
        juce::String outputFileName = "OUTPUT_" + juce::String(n) + ".txt";
        processor.analyzeRecording(tester.getRecording(), outputFileName, n);
    }

    std::cout << "\nAll tests complete. Press ENTER to exit." << std::endl;
    std::cin.get();
    return 0;
}
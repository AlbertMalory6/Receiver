/*
 * FSK RECEIVER - (with Correlation Debugging)
 *
 * - Detects chirp preamble.
 * - Demodulates FSK data.
 * - **NEW**: Logs the correlation scores (corr_f0, corr_f1)
 * for every bit to "debug_correlation.csv" for visual analysis.
 */

#include <JuceHeader.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <iomanip> // For formatting output

// ==============================================================================
//  CONFIGURATION
// ==============================================================================
#define USE_NCC_DETECTION 1 // 1 = NCC, 0 = Dot Product

namespace FSK {
    constexpr double sampleRate = 44100.0;
    constexpr double f0 = 1000.0;
    constexpr double f1 = 2000.0;
    constexpr double bitRate = 1000.0;
    constexpr int samplesPerBit = static_cast<int>(sampleRate / bitRate);
    constexpr int preambleSamples = 440;
    constexpr int payloadBits = 10000;
    constexpr int crcBits = 8;
    constexpr int totalFrameBits = payloadBits + crcBits;
    constexpr int totalFrameDataSamples = totalFrameBits * samplesPerBit;

    // Chirp parameters (matching sender)
    // Using the 500-4000-500 chirp from the new sender
    double chirp_f_start = 500.0;
    double chirp_f_mid = 4000.0;
}

// ==============================================================================
//  CRC-8 CALCULATION (Same as sender)
// ==============================================================================
uint8_t calculateCRC8(const std::vector<bool>& data) {
    const uint8_t polynomial = 0x07; // Standard CRC-8
    uint8_t crc = 0x00;
    for (bool bit : data) {
        crc ^= (bit ? 0x80 : 0x00); // Feed the next bit
        for (int i = 0; i < 8; ++i) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ polynomial;
            } else {
                crc = crc << 1;
            }
        }
    }
    return crc;
}

// ==============================================================================
//  CHIRP GENERATOR (for template matching)
// ==============================================================================
class ChirpGenerator {
public:
    static juce::AudioBuffer<float> generateChirp() {
        juce::AudioBuffer<float> chirp(1, FSK::preambleSamples);
        auto* signal = chirp.getWritePointer(0);

        double currentPhase = 0.0;
        for (int i = 0; i < FSK::preambleSamples; ++i) {
            double freq;
            if (i < FSK::preambleSamples / 2) {
                freq = juce::jmap((double)i, 0.0, (double)FSK::preambleSamples / 2.0,
                    FSK::chirp_f_start, FSK::chirp_f_mid);
            } else {
                freq = juce::jmap((double)i, (double)FSK::preambleSamples / 2.0,
                    (double)FSK::preambleSamples, FSK::chirp_f_mid, FSK::chirp_f_start);
            }
            double phaseIncrement = 2.0 * juce::MathConstants<double>::pi * freq / FSK::sampleRate;
            currentPhase += phaseIncrement;
            signal[i] = std::sin(currentPhase) * 0.5; // 0.5 amplitude
        }

        return chirp;
    }
};

// ==============================================================================
//  CHIRP DETECTOR (Unchanged)
// ==============================================================================
class ChirpDetector {
private:
    juce::AudioBuffer<float> template_;
    double templateEnergy_;

public:
    ChirpDetector() {
        template_ = ChirpGenerator::generateChirp();
        templateEnergy_ = calculateEnergy(template_.getReadPointer(0), template_.getNumSamples());
        std::cout << "Chirp template generated: " << template_.getNumSamples() << " samples" << std::endl;
        std::cout << "Template energy: " << templateEnergy_ << std::endl;
    }

    int detectChirp(const juce::AudioBuffer<float>& signal) {
        const float* sigData = signal.getReadPointer(0);
        const float* tempData = template_.getReadPointer(0);
        const int tempLen = template_.getNumSamples();

        std::cout << "\nSearching for chirp in " << signal.getNumSamples() << " samples..." << std::endl;

#if USE_NCC_DETECTION
        std::cout << "Using NCC detection method" << std::endl;
        return detectWithNCC(sigData, signal.getNumSamples(), tempData, tempLen);
#else
        std::cout << "Using Dot Product detection method" << std::endl;
        return detectWithDotProduct(sigData, signal.getNumSamples(), tempData, tempLen);
#endif
    }

private:
    int detectWithNCC(const float* sigData, int sigLen, const float* tempData, int tempLen) {
        double maxNCC = 0.0;
        int maxPos = -1;

        for (int i = 0; i <= sigLen - tempLen; ++i) {
            double dotProduct = 0.0;
            double signalEnergy = 0.0;

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

        std::cout << "NCC Detection: Max correlation = " << maxNCC << " at sample " << maxPos << std::endl;

        if (maxNCC < 0.3) { // This threshold may need tuning
            std::cout << "WARNING: Weak correlation (" << maxNCC << "), detection may be unreliable" << std::endl;
            // Don't return -1, just warn.
        }

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

        std::cout << "Dot Product Detection: Max = " << maxDot << " at sample " << maxPos << std::endl;
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
//  MATCHED FILTER DEMODULATOR (*** MODIFIED FOR DEBUGGING ***)
// ==============================================================================
class MatchedFilterDemodulator {
private:
    juce::AudioBuffer<float> reference_f0, reference_f1;
    std::ofstream debugFile; // <-- NEW: Debug file stream

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

        std::cout << "Matched filter references generated (" << FSK::samplesPerBit << " samples per bit)" << std::endl;
    }

public:
    MatchedFilterDemodulator() {
        generateReferences();
        // --- NEW: Open debug file ---
        debugFile.open("debug_correlation.csv");
        if (debugFile.is_open()) {
            std::cout << "Logging correlation scores to debug_correlation.csv" << std::endl;
            debugFile << "Bit_Index,Corr_f0,Corr_f1,Decision\n"; // Header
        } else {
            std::cerr << "WARNING: Could not open debug_correlation.csv" << std::endl;
        }
    }

    ~MatchedFilterDemodulator() {
        if (debugFile.is_open()) {
            debugFile.close();
        }
    }

    std::vector<bool> demodulate(const juce::AudioBuffer<float>& frameData) {
        std::vector<bool> receivedBits;
        receivedBits.reserve(FSK::totalFrameBits);

        const float* data = frameData.getReadPointer(0);
        const float* ref0 = reference_f0.getReadPointer(0);
        const float* ref1 = reference_f1.getReadPointer(0);

        std::cout << "\nDemodulating " << FSK::totalFrameBits << " bits..." << std::endl;

        for (int i = 0; i < FSK::totalFrameBits; ++i) {
            const float* bitSamples = data + (i * FSK::samplesPerBit);

            double corr_f0 = 0.0, corr_f1 = 0.0;
            for (int j = 0; j < FSK::samplesPerBit; ++j) {
                corr_f0 += bitSamples[j] * ref0[j];
                corr_f1 += bitSamples[j] * ref1[j];
            }

            bool bit = corr_f1 > corr_f0;
            receivedBits.push_back(bit);

            // --- NEW: Log to file ---
            if (debugFile.is_open()) {
                debugFile << i << ","
                          << std::fixed << std::setprecision(5) << corr_f0 << ","
                          << std::fixed << std::setprecision(5) << corr_f1 << ","
                          << (bit ? "1" : "0") << "\n";
            }

            if ((i + 1) % 1000 == 0) {
                std::cout << "  Demodulated " << (i + 1) << " / " << FSK::totalFrameBits << " bits" << std::endl;
            }
        }

        std::cout << "Demodulation complete!" << std::endl;
        return receivedBits;
    }
};


// ==============================================================================
//  AUDIO RECORDER (Unchanged)
// ==============================================================================
class AudioRecorder : public juce::AudioIODeviceCallback {
public:
    AudioRecorder(int durationSeconds) {
        int requiredSamples = static_cast<int>(FSK::sampleRate * durationSeconds);
        recordedAudio.setSize(1, requiredSamples);
        recordedAudio.clear();
        samplesRecorded = 0;
        std::cout << "Recorder initialized: " << durationSeconds << "s capacity ("
            << requiredSamples << " samples)" << std::endl;
    }

    void audioDeviceAboutToStart(juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext(const float* const* input, int numIn,
        float* const*, int,
        int numSamples,
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
//  MAIN APPLICATION (Unchanged, but CRC logic updated)
// ==============================================================================
int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║            FSK RECEIVER (with Correlation Debugging)           ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════════╝\n";
    std::cout << "\nConfiguration:" << std::endl;
    std::cout << "  Sample Rate: " << FSK::sampleRate << " Hz" << std::endl;
    std::cout << "  Bit Rate: " << FSK::bitRate << " bps" << std::endl;
    std::cout << "  Expected payload: " << FSK::payloadBits << " bits" << std::endl;
    std::cout << "  Detection method: " << (USE_NCC_DETECTION ? "NCC" : "Dot Product") << std::endl;

    // Setup audio device
    juce::AudioDeviceManager deviceManager;
    deviceManager.initialiseWithDefaultDevices(1, 0); // Input only

    // Create recorder (20 seconds capacity)
    AudioRecorder recorder(20);

    std::cout << "\n>>> READY TO RECORD <<<" << std::endl;
    std::cout << "Press ENTER to start recording..." << std::endl;
    std::cin.get();

    deviceManager.addAudioCallback(&recorder);
    std::cout << "\n RECORDING... (make sure Sender is playing)" << std::endl;
    std::cout << "Press ENTER when transmission is complete..." << std::endl;
    std::cin.get();

    deviceManager.removeAudioCallback(&recorder);
    std::cout << "\n✓ Recording stopped. Recorded " << recorder.getSamplesRecorded()
        << " samples (" << (recorder.getSamplesRecorded() / FSK::sampleRate) << " seconds)" << std::endl;

    // Save recording for debugging
    juce::File outFile = juce::File::getCurrentWorkingDirectory().getChildFile("debug_recording.wav");
    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wavFormat.createWriterFor(new juce::FileOutputStream(outFile),
            FSK::sampleRate, 1, 16, {}, 0));
    if (writer != nullptr) {
        writer->writeFromAudioSampleBuffer(recorder.getRecording(), 0, recorder.getSamplesRecorded());
        std::cout << "Recording saved to: debug_recording.wav" << std::endl;
    }

    // STEP 1: Detect chirp
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "STEP 1: CHIRP DETECTION" << std::endl;
    std::cout << std::string(60, '=') << std::endl;

    ChirpDetector detector;
    int chirpPosition = detector.detectChirp(recorder.getRecording());

    if (chirpPosition < 0) {
        std::cerr << "\n✗ ERROR: Chirp not detected!" << std::endl;
        return 1;
    }

    std::cout << "\n✓ Chirp detected at sample " << chirpPosition << std::endl;

    // STEP 2: Extract data frame
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "STEP 2: EXTRACT DATA FRAME" << std::endl;
    std::cout << std::string(60, '=') << std::endl;

    int frameStartSample = chirpPosition + FSK::preambleSamples;
    std::cout << "Frame data starts at sample " << frameStartSample << std::endl;

    if (frameStartSample + FSK::totalFrameDataSamples > recorder.getSamplesRecorded()) {
        std::cerr << "\n✗ ERROR: Not enough samples for full frame!" << std::endl;
        std::cerr << "  Need: " << (frameStartSample + FSK::totalFrameDataSamples) << " samples" << std::endl;
        std::cerr << "  Have: " << recorder.getSamplesRecorded() << " samples" << std::endl;
        return 1;
    }

    juce::AudioBuffer<float> frameData(1, FSK::totalFrameDataSamples);
    frameData.copyFrom(0, 0, recorder.getRecording(), 0, frameStartSample, FSK::totalFrameDataSamples);
    std::cout << "✓ Extracted " << FSK::totalFrameDataSamples << " samples" << std::endl;

    // STEP 3: Demodulate
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "STEP 3: MATCHED FILTER DEMODULATION" << std::endl;
    std::cout << std::string(60, '=') << std::endl;

    MatchedFilterDemodulator demodulator; // This will open the CSV file
    std::vector<bool> receivedBits = demodulator.demodulate(frameData);
    // CSV file is automatically closed when demodulator goes out of scope

    // STEP 4: Verify CRC
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "STEP 4: CRC VERIFICATION" << std::endl;
    std::cout << std::string(60, '=') << std::endl;

    std::vector<bool> payload(receivedBits.begin(), receivedBits.begin() + FSK::payloadBits);
    std::vector<bool> receivedCRC(receivedBits.begin() + FSK::payloadBits, receivedBits.end());

    uint8_t calculatedCRC = calculateCRC8(payload);
    uint8_t receivedCRCValue = 0;
    for (int i = 0; i < 8; ++i) {
        if (receivedCRC[i]) receivedCRCValue |= (1 << (7 - i));
    }

    std::cout << "Calculated CRC: 0x" << std::hex << (int)calculatedCRC << std::dec << std::endl;
    std::cout << "Received CRC:   0x" << std::hex << (int)receivedCRCValue << std::dec << std::endl;

    if (calculatedCRC == receivedCRCValue) {
        std::cout << "✓ CRC CHECK PASSED - Data integrity verified!" << std::endl;
    } else {
        std::cout << "✗ CRC CHECK FAILED - Data may be corrupted" << std::endl;
    }

    // STEP 5: Save output
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "STEP 5: SAVE OUTPUT" << std::endl;
    std::cout << std::string(60, '=') << std::endl;

    std::ofstream outputFile("OUTPUT.txt");
    if (!outputFile.is_open()) {
        std::cerr << "ERROR: Could not open OUTPUT.txt for writing" << std::endl;
        return 1;
    }

    for (bool bit : payload) {
        outputFile << (bit ? '1' : '0');
    }
    outputFile.close();

    std::cout << "✓ Saved " << payload.size() << " bits to OUTPUT.txt" << std::endl;

    // Calculate bit error rate (if INPUT.txt exists)
    std::ifstream inputFile("INPUT.txt");
    if (inputFile.is_open()) {
        std::vector<bool> originalBits;
        char bitChar;
        while (inputFile.get(bitChar) && originalBits.size() < payload.size()) {
            if (bitChar == '0') originalBits.push_back(false);
            else if (bitChar == '1') originalBits.push_back(true);
        }
        inputFile.close();

        if (originalBits.size() == payload.size()) {
            int errors = 0;
            for (size_t i = 0; i < payload.size(); ++i) {
                if (payload[i] != originalBits[i]) errors++;
            }

            double ber = (double)errors / payload.size();
            std::cout << "\n BIT ERROR RATE: " << errors << " / " << payload.size()
                << " = " << (ber * 100.0) << "%" << std::endl;
        }
    }

    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "RECEPTION COMPLETE" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    std::cout << "\nGenerated files:" << std::endl;
    std::cout << "  - OUTPUT.txt: Received payload" << std::endl;
    std::cout << "  - debug_recording.wav: Full recording" << std::endl;
    std::cout << "  - debug_correlation.csv: Correlation scores for analysis" << std::endl;

    std::cout << "\nPress ENTER to exit..." << std::endl;
    std::cin.get();

    return 0;
}

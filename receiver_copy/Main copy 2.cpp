#include <JuceHeader.h>
#include <fstream>
#include <iostream>
#include <iomanip>

// --- SHARED PARAMETERS AND FUNCTIONS (must be identical for sender and receiver logic) ---
namespace FSK {
    constexpr double sampleRate = 44100.0;
    constexpr double f0 = 2000.0;
    constexpr double f1 = 4000.0;
    constexpr double bitRate = 1000.0;
    constexpr int samplesPerBit = static_cast<int>(sampleRate / bitRate);
    constexpr int preambleSamples = 440;
    constexpr int payloadBits = 1000;
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
//  1. FSK Signal Generator (Plays the audio)
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
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override
    {
        if (isPlaybackFinished) {
            bufferToFill.clearActiveBufferRegion();
            return;
        }
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
    void FSKSignalSource::generateFullSignal(const std::vector<bool>& bits)
    {
        // NEW: Add a silent leader to ensure the recorder is ready
        const int silentLeaderSamples = FSK::sampleRate * 0.5; // 0.5 seconds of silence

        auto payloadWithCrcBits = bits;
        uint8_t crc = FSK::calculateCRC8(bits);
        for (int i = 7; i >= 0; --i) payloadWithCrcBits.push_back((crc >> i) & 1);

        const int totalDataSamples = payloadWithCrcBits.size() * FSK::samplesPerBit;
        const int totalSignalSamples = silentLeaderSamples + FSK::preambleSamples + totalDataSamples;

        signalBuffer.setSize(1, totalSignalSamples);
        signalBuffer.clear(); // Zeros out the buffer, creating the silent leader

        auto* signal = signalBuffer.getWritePointer(0);
        double currentPhase = 0.0;

        // Generate preamble AFTER the silent leader
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

        // Generate FSK data after the preamble
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
//  2. Offline Analyzer (Demodulates the recording)
// ==============================================================================
class FSKOfflineProcessor
{
public:
    FSKOfflineProcessor() {
        generatePreambleTemplate();
        preambleTemplateEnergy = calculateEnergy(preambleTemplate.getReadPointer(0), FSK::preambleSamples);
    }

    void FSKOfflineProcessor::analyzeRecording(const juce::AudioBuffer<float>& recordedAudio)
    {
        std::cout << "\n--- Analyzing Recorded Audio & Detecting Frames ---" << std::endl;
        const float* signal = recordedAudio.getReadPointer(0);

        // --- NCC Detection with Peak Tracking ---
        juce::File nccFile = juce::File::getCurrentWorkingDirectory().getChildFile("debug_ncc_output.csv");
        std::ofstream nccStream(nccFile.getFullPathName().toStdString());

        juce::AudioBuffer<float> circularBuffer(1, FSK::preambleSamples * 2);
        int circularBufferPos = 0;

        // Synchronization state
        double syncPowerLocalMax = 0.0;
        int peakSampleIndex = 0;
        static constexpr double NCC_DETECTION_THRESHOLD = 0.3; // Adjust based on your NCC plot

        std::cout << "Detection threshold: " << NCC_DETECTION_THRESHOLD << std::endl;

        for (int i = 0; i < recordedAudio.getNumSamples(); ++i)
        {
            circularBuffer.setSample(0, circularBufferPos, signal[i]);
            double ncc = calculateNormalizedCrossCorrelation(circularBuffer, circularBufferPos);
            nccStream << i << "," << ncc << "\n"; // Include sample index for plotting

            // --- Peak Detection Logic ---
            if (ncc > syncPowerLocalMax && ncc > NCC_DETECTION_THRESHOLD) {
                // Rising NCC - potential new peak
                syncPowerLocalMax = ncc;
                peakSampleIndex = i;
            }
            else if (peakSampleIndex != 0 && (i - peakSampleIndex) > (FSK::preambleSamples / 2)) {
                // NCC has fallen for preambleSamples/2 - peak confirmed
                std::cout << "\n*** PREAMBLE DETECTED at sample " << peakSampleIndex
                    << " (t=" << std::fixed << std::setprecision(3)
                    << (double)peakSampleIndex / FSK::sampleRate << "s)" << std::endl;
                std::cout << "    Peak NCC: " << std::setprecision(4) << syncPowerLocalMax << std::endl;

                // Calculate frame start with offset correction
                int frameStartSample = findOptimalFrameStart(recordedAudio, peakSampleIndex);

                if (frameStartSample + FSK::totalFrameDataSamples <= recordedAudio.getNumSamples()) {
                    juce::AudioBuffer<float> frameData(1, FSK::totalFrameDataSamples);
                    frameData.copyFrom(0, 0, recordedAudio, 0, frameStartSample, FSK::totalFrameDataSamples);
                    demodulateFrame(frameData, recordedAudio, frameStartSample);
                }
                else {
                    std::cerr << "ERROR: Not enough samples for full frame. Need "
                        << FSK::totalFrameDataSamples << ", have "
						<< recordedAudio.getNumSamples() << "in total, only "
                        << (recordedAudio.getNumSamples() - frameStartSample) << std::endl;
                }

                // Reset for next frame
                peakSampleIndex = 0;
                syncPowerLocalMax = 0.0;
            }

            circularBufferPos = (circularBufferPos + 1) % circularBuffer.getNumSamples();
        }

        nccStream.close();
        std::cout << "\nDiagnostic NCC waveform saved to: " << nccFile.getFullPathName() << std::endl;

        // --- Save the actual recording for inspection ---
        juce::File recordingFile = juce::File::getCurrentWorkingDirectory().getChildFile("debug_loopback_recording.wav");
        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::AudioFormatWriter> writer(wavFormat.createWriterFor(new juce::FileOutputStream(recordingFile), 44100.0, 1, 16, {}, 0));
        if (writer != nullptr) {
            writer->writeFromAudioSampleBuffer(recordedAudio, 0, recordedAudio.getNumSamples());
            std::cout << "Full loopback recording saved to: " << recordingFile.getFullPathName() << std::endl;
        }

        std::cout << "\n--- Analysis Complete ---" << std::endl;
    }
private:

    // NEW: Fine-tune frame start using first bit energy analysis
    int findOptimalFrameStart(const juce::AudioBuffer<float>& recording, int preambleEndSample)
    {
        // The preamble ends at peakSampleIndex, data should start immediately after
        int searchStart = preambleEndSample + 1;
        int searchRange = FSK::samplesPerBit; // Search within one bit period

        double bestScore = -1.0;
        int bestOffset = 0;

        std::cout << "    Fine-tuning frame start (searching " << searchRange << " samples)..." << std::endl;

        // Try different offsets and find the one that gives clearest first bit
        for (int offset = -searchRange / 2; offset < searchRange / 2; ++offset) {
            int testStart = searchStart + offset;
            if (testStart < 0 || testStart + FSK::samplesPerBit >= recording.getNumSamples()) continue;

            const float* samples = recording.getReadPointer(0) + testStart;
            double mag_f0 = goertzelMagnitude(FSK::samplesPerBit, FSK::f0, samples);
            double mag_f1 = goertzelMagnitude(FSK::samplesPerBit, FSK::f1, samples);

            // Score is the ratio difference - higher means clearer signal
            double score = std::abs(mag_f0 - mag_f1) / std::max(mag_f0, mag_f1);

            if (score > bestScore) {
                bestScore = score;
                bestOffset = offset;
            }
        }

        int finalStart = searchStart + bestOffset;
        std::cout << "    Offset correction: " << bestOffset << " samples (clarity score: "
            << std::setprecision(3) << bestScore << ")" << std::endl;

        return finalStart;
    }

    // NEW: demodulateFrame now takes the original buffer to create the debug file
    void demodulateFrame(const juce::AudioBuffer<float>& frameData, const juce::AudioBuffer<float>& originalRecording, int frameStartSample)
    {
        std::vector<bool> receivedBits;
        receivedBits.reserve(FSK::totalFrameBits);
        const float* data = frameData.getReadPointer(0);

        std::cout << "\n--- Demodulator Confidence Scores (First 30 bits) ---" << std::endl;

        double avgConfidence = 0.0;
        int weakBits = 0;

        for (int i = 0; i < FSK::totalFrameBits; ++i) {
            const float* bitSamples = data + (i * FSK::samplesPerBit);
            double mag_f0 = goertzelMagnitude(FSK::samplesPerBit, FSK::f0, bitSamples);
            double mag_f1 = goertzelMagnitude(FSK::samplesPerBit, FSK::f1, bitSamples);
            bool bit = mag_f1 > mag_f0;
            receivedBits.push_back(bit);

            double confidence = (mag_f0 > mag_f1) ? (mag_f0 / mag_f1) : (mag_f1 / mag_f0);
            avgConfidence += confidence;
            if (confidence < 1.5) weakBits++;

            if (i < 30) { // Print confidence for the first 30 bits
                std::cout << "Bit " << std::setw(3) << i << ": " << (bit ? "1" : "0")
                    << " (f0=" << std::setprecision(1) << mag_f0
                    << ", f1=" << mag_f1
                    << ", conf=" << std::setprecision(2) << confidence << ")" << std::endl;
            }
        }

        avgConfidence /= FSK::totalFrameBits;
        std::cout << "\n--- Demodulation Statistics ---" << std::endl;
        std::cout << "Average confidence: " << std::setprecision(2) << avgConfidence << std::endl;
        std::cout << "Weak bits (conf < 1.5): " << weakBits << " / " << FSK::totalFrameBits
            << " (" << std::setprecision(1) << (100.0 * weakBits / FSK::totalFrameBits) << "%)" << std::endl;

        // --- CRC Check ---
        std::vector<bool> payload(receivedBits.begin(), receivedBits.begin() + FSK::payloadBits);
        uint8_t receivedCrcByte = 0;
        for (int i = 0; i < FSK::crcBits; ++i)
            if (receivedBits[FSK::payloadBits + i])
                receivedCrcByte |= (1 << (7 - i));

        uint8_t calculatedCrc = FSK::calculateCRC8(payload);

        if (calculatedCrc == receivedCrcByte) {
            std::cout << "\nCRC OK! Writing " << payload.size() << " bits to OUTPUT.txt" << std::endl;
            std::ofstream outputFile("OUTPUT.txt");
            for (bool b : payload) outputFile << (b ? '1' : '0');
        }
        else {
            std::cout << "\nCRC FAIL! Received: " << (int)receivedCrcByte << ", Calculated: " << (int)calculatedCrc << ". Frame discarded." << std::endl;
        }

        // --- NEW: Generate Debug Beep Track ---
        createBeepTrack(originalRecording, frameStartSample);
    }

    void createBeepTrack(const juce::AudioBuffer<float>& originalRecording, int frameStartSample)
    {
        juce::File beepFile = juce::File::getCurrentWorkingDirectory().getChildFile("debug_beeps.wav");
        juce::AudioBuffer<float> beepBuffer(originalRecording.getNumChannels(), originalRecording.getNumSamples());
        beepBuffer.copyFrom(0, 0, originalRecording, 0, 0, originalRecording.getNumSamples());

        // Add beeps
        for (int i = 0; i < FSK::totalFrameBits; ++i) {
            int beepPos = frameStartSample + (i * FSK::samplesPerBit);
            if (beepPos + 5 < beepBuffer.getNumSamples()) {
                for (int j = 0; j < 5; ++j) { // A 5-sample click
                    beepBuffer.setSample(0, beepPos + j, 1.0f);
                }
            }
        }

        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::AudioFormatWriter> writer(wavFormat.createWriterFor(new juce::FileOutputStream(beepFile), 44100.0, 1, 16, {}, 0));
        if (writer != nullptr) {
            writer->writeFromAudioSampleBuffer(beepBuffer, 0, beepBuffer.getNumSamples());
            std::cout << "Diagnostic beep track saved to: " << beepFile.getFullPathName() << std::endl;
        }
    }

    // (Other helper methods: generatePreambleTemplate, calculateEnergy, calculateNormalizedCrossCorrelation, goertzelMagnitude are unchanged)
    juce::AudioBuffer<float> preambleTemplate;
    double preambleTemplateEnergy = 0.0;
    void generatePreambleTemplate() {
        preambleTemplate.setSize(1, FSK::preambleSamples);
        auto* signal = preambleTemplate.getWritePointer(0);
        double currentPhase = 0.0;
        for (int i = 0; i < FSK::preambleSamples; ++i) {
            double freq;
            if (i < FSK::preambleSamples / 2)
                freq = juce::jmap((double)i, 0.0, (double)FSK::preambleSamples / 2.0, FSK::f0 - 1000.0, FSK::f1 + 1000.0);
            else
                freq = juce::jmap((double)i, (double)FSK::preambleSamples / 2.0, (double)FSK::preambleSamples, FSK::f1 + 1000.0, FSK::f0 - 1000.0);
            double phaseIncrement = 2.0 * juce::MathConstants<double>::pi * freq / FSK::sampleRate;
            currentPhase += phaseIncrement;
            signal[i] = std::sin(currentPhase) * 0.5;
        }
    }
    double calculateEnergy(const float* buffer, int numSamples) {
        double energy = 0.0;
        for (int i = 0; i < numSamples; ++i) { energy += buffer[i] * buffer[i]; }
        return energy;
    }
    double calculateNormalizedCrossCorrelation(const juce::AudioBuffer<float>& circularBuffer, int circularBufferPos) {
        double dotProduct = 0.0;
        float tempSignalWindow[FSK::preambleSamples];
        for (int i = 0; i < FSK::preambleSamples; ++i) {
            int bufferIndex = (circularBufferPos - FSK::preambleSamples + i + circularBuffer.getNumSamples()) % circularBuffer.getNumSamples();
            tempSignalWindow[i] = circularBuffer.getSample(0, bufferIndex);
        }
        double signalEnergy = calculateEnergy(tempSignalWindow, FSK::preambleSamples);
        if (signalEnergy < 1e-9 || preambleTemplateEnergy < 1e-9) return 0.0;
        const float* preamble = preambleTemplate.getReadPointer(0);
        for (int i = 0; i < FSK::preambleSamples; ++i) {
            dotProduct += preamble[i] * tempSignalWindow[i];
        }
        return dotProduct / std::sqrt(signalEnergy * preambleTemplateEnergy);
    }
    double goertzelMagnitude(int numSamplesInBlock, int targetFrequency, const float* data) {
        double k = 0.5 + ((double)numSamplesInBlock * targetFrequency / FSK::sampleRate);
        double w = (2.0 * juce::MathConstants<double>::pi / numSamplesInBlock) * k;
        double cosine = std::cos(w);
        double coeff = 2.0 * cosine;
        double q0 = 0, q1 = 0, q2 = 0;
        for (int i = 0; i < numSamplesInBlock; i++) {
            q0 = coeff * q1 - q2 + data[i];
            q2 = q1;
            q1 = q0;
        }
        double real = q1 - q2 * cosine;
        double imag = q1 * std::sin(w);
        return std::sqrt(real * real + imag * imag);
    }
    void demodulateFrame(const juce::AudioBuffer<float>& frameData) {
        std::vector<bool> receivedBits;
        receivedBits.reserve(FSK::totalFrameBits);
        const float* data = frameData.getReadPointer(0);
        for (int i = 0; i < FSK::totalFrameBits; ++i) {
            const float* bitSamples = data + (i * FSK::samplesPerBit);
            double mag_f0 = goertzelMagnitude(FSK::samplesPerBit, FSK::f0, bitSamples);
            double mag_f1 = goertzelMagnitude(FSK::samplesPerBit, FSK::f1, bitSamples);
            receivedBits.push_back(mag_f1 > mag_f0);
        }
        std::vector<bool> payload(receivedBits.begin(), receivedBits.begin() + FSK::payloadBits);
        uint8_t receivedCrcByte = 0;
        for (int i = 0; i < FSK::crcBits; ++i)
            if (receivedBits[FSK::payloadBits + i])
                receivedCrcByte |= (1 << (7 - i));
        uint8_t calculatedCrc = FSK::calculateCRC8(payload);
        if (calculatedCrc == receivedCrcByte) {
            std::cout << "CRC OK! Writing " << payload.size() << " bits to OUTPUT.txt" << std::endl;
            std::ofstream outputFile("OUTPUT.txt");
            for (bool bit : payload)
                outputFile << (bit ? '1' : '0');
            outputFile.close();
        }
        else {
            std::cout << "\nCRC FAIL! Received: " << (int)receivedCrcByte << ", Calculated: " << (int)calculatedCrc << ". Frame discarded." << std::endl;

            // --- NEW: FULL BITSTREAM DUMP ON FAILURE ---
            std::cout << "\n--- Full Decoded Bitstream ---" << std::endl;
            std::cout << "Payload (" << FSK::payloadBits << " bits):" << std::endl;
            for (size_t i = 0; i < payload.size(); ++i) {
                std::cout << (payload[i] ? '1' : '0');
                if ((i + 1) % 8 == 0) std::cout << " "; // Add a space every 8 bits for readability
                if ((i + 1) % 64 == 0) std::cout << std::endl; // Newline every 8 bytes
            }
            std::cout << "\n\nReceived CRC (8 bits):" << std::endl;
            for (size_t i = 0; i < FSK::crcBits; ++i) {
                std::cout << (receivedBits[FSK::payloadBits + i] ? '1' : '0');
            }
            std::cout << std::endl;
        }

    }
};

// ==============================================================================
//  3. The Loopback Test Manager (Handles simultaneous play/record)
// ==============================================================================
class AcousticLoopbackTester : public juce::AudioIODeviceCallback
{
public:
    AcousticLoopbackTester(const std::vector<bool>& bitsToSend) : fskSource(bitsToSend) {
        int requiredSamples = fskSource.getNumSamples() + (int)(FSK::sampleRate * 1.0);
        recordedAudio.setSize(1, requiredSamples);
        recordedAudio.clear();
    }
    bool isTestFinished() const { return testFinished; }
    juce::AudioBuffer<float>& getRecording() { return recordedAudio; }

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override {
        fskSource.prepareToPlay(device->getCurrentBufferSizeSamples(), device->getCurrentSampleRate());
    }
    void audioDeviceStopped() override {}
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
        float* const* outputChannelData, int numOutputChannels,
        int numSamples, const juce::AudioIODeviceCallbackContext&) override {
        if (numOutputChannels > 0) {
            //juce::AudioSourceChannelInfo bufferToFill(outputChannelData, numOutputChannels, numSamples);
            juce::AudioSourceChannelInfo bufferToFill;
            juce::AudioBuffer<float> tempBuffer(const_cast<float**>(outputChannelData), numOutputChannels, numSamples);
            bufferToFill.buffer = &tempBuffer;            bufferToFill.startSample = 0;
            bufferToFill.numSamples = numSamples;
            fskSource.getNextAudioBlock(bufferToFill);
        }
        if (numInputChannels > 0) {
            if (samplesRecorded + numSamples <= recordedAudio.getNumSamples()) {
                recordedAudio.copyFrom(0, samplesRecorded, inputChannelData[0], numSamples);
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
//  4. Main Application
// ==============================================================================
int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juce_init;

    std::vector<bool> bits_to_send;
    std::ifstream inputFile("INPUT.txt");
    if (!inputFile.is_open()) { std::cerr << "Error: Could not open INPUT.txt." << std::endl; return 1; }
    char bitChar;
    while (inputFile.get(bitChar)) {
        if (bitChar == '0') bits_to_send.push_back(false);
        else if (bitChar == '1') bits_to_send.push_back(true);
    }
    inputFile.close();
    std::cout << "Read " << bits_to_send.size() << " bits from INPUT.txt" << std::endl;

    AcousticLoopbackTester tester(bits_to_send);
    juce::AudioDeviceManager deviceManager;
    deviceManager.initialiseWithDefaultDevices(1, 2);

    std::cout << "Acoustic loopback test is ready." << std::endl;
    std::cout << "Ensure your microphone can hear your speakers." << std::endl;
    std::cout << "Press ENTER to start the play/record process..." << std::endl;
    std::cin.get();

    deviceManager.addAudioCallback(&tester);
    std::cout << "--- Playing and recording simultaneously... ---" << std::endl;


    std::cin.get();


    deviceManager.removeAudioCallback(&tester);
    std::cout << "--- Play/Record finished. ---" << std::endl;

    FSKOfflineProcessor processor;
    processor.analyzeRecording(tester.getRecording());

    std::cout << "Press ENTER to exit." << std::endl;
    std::cin.get();
    return 0;
}
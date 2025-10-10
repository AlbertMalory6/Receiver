#include <JuceHeader.h>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <string>
#include <algorithm> // For std::reverse

// --- SHARED PARAMETERS AND FUNCTIONS ---
namespace FSK {
    constexpr double sampleRate = 44100.0;
    constexpr double f0 = 2000.0;
    constexpr double f1 = 4000.0;
    constexpr double bitRate = 1000.0;
    constexpr int samplesPerBit = static_cast<int>(sampleRate / bitRate);
    constexpr int preambleSamples = 440;
    constexpr int payloadBits = 5000;
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

    // Helper to convert byte to binary string for debug
    std::string byteToBinary(uint8_t byte) {
        std::string binaryString;
        for (int i = 7; i >= 0; --i) {
            binaryString += ((byte >> i) & 1) ? '1' : '0';
        }
        return binaryString;
    }
}

// ==============================================================================
//  1. FSK Signal Generator (Plays the audio)
// ==============================================================================
class FSKSignalSource : public juce::AudioSource
{
public:
    FSKSignalSource(const std::vector<bool>& bitsToSend) { generateFullSignal(bitsToSend); }
    bool isFinished() const { return isPlaybackFinished; }
    int getNumSamples() const { return signalBuffer.getNumSamples(); }
    void prepareToPlay(int, double) override { position = 0; isPlaybackFinished = false; }
    void releaseResources() override {}
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override
    {
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
        const int silentLeaderSamples = FSK::sampleRate * 0.5; // 0.5 seconds of silence
        auto payloadWithCrcBits = bits;
        uint8_t crc = FSK::calculateCRC8(bits);
        for (int i = 7; i >= 0; --i) payloadWithCrcBits.push_back((crc >> i) & 1);

        const int totalDataSamples = payloadWithCrcBits.size() * FSK::samplesPerBit;
        const int totalSignalSamples = silentLeaderSamples + FSK::preambleSamples + totalDataSamples;

        signalBuffer.setSize(1, totalSignalSamples);
        signalBuffer.clear();

        auto* signal = signalBuffer.getWritePointer(0);
        double currentPhase = 0.0;

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

    void analyzeRecording(const juce::AudioBuffer<float>& recordedAudio)
    {
        std::cout << "\n--- Analyzing Recorded Audio & Generating Debug Files ---" << std::endl;
        const float* signal = recordedAudio.getReadPointer(0);

        // --- Generate the full NCC waveform for debugging (still useful for general signal check) ---
        juce::File nccFile = juce::File::getCurrentWorkingDirectory().getChildFile("debug_ncc_output.csv");
        std::ofstream nccStream(nccFile.getFullPathName().toStdString());

        juce::AudioBuffer<float> circularBuffer(1, FSK::preambleSamples * 2);
        int circularBufferPos = 0;

        // --- Preamble Detection Variables ---
        double peakNCC = 0.0;
        int peakSampleIndex = -1;
        bool preambleFound = false;

        // Tune this value! Use your NCC plot to pick a threshold above noise but below true peak.
        // For your current plot, 0.5 is a good starting point.
        static constexpr double NCC_DETECTION_THRESHOLD = 0.3;

        // --- THIS IS THE OFFSET TO TUNE ---
        // Adjust this value based on debug_beeps.wav and confidence scores
        // Try values from -20 to +20, and fine-tune by 1s if needed.
        constexpr int SYNC_OFFSET = 5;

        for (int i = 0; i < recordedAudio.getNumSamples(); ++i)
        {
            circularBuffer.setSample(0, circularBufferPos, signal[i]);
            double ncc = calculateNormalizedCrossCorrelation(circularBuffer, circularBufferPos);
            nccStream << ncc << "\n";

            // --- Robust Preamble Peak Detection ---
            if (!preambleFound) {
                if (ncc > NCC_DETECTION_THRESHOLD) {
                    if (ncc > peakNCC) {
                        peakNCC = ncc;
                        peakSampleIndex = i;
                    }
                }
                else if (peakSampleIndex != -1) { // We had a peak, but now NCC dropped below threshold
                    std::cout << "\n\n*** Preamble DETECTED at " << (double)peakSampleIndex / FSK::sampleRate << "s! Peak NCC: " << peakNCC << " ***\n" << std::endl;

                    int frameStartSample = peakSampleIndex + 1 + SYNC_OFFSET;

                    if (frameStartSample + FSK::totalFrameDataSamples <= recordedAudio.getNumSamples()) {
                        juce::AudioBuffer<float> frameData(1, FSK::totalFrameDataSamples);
                        frameData.copyFrom(0, 0, recordedAudio, 0, frameStartSample, FSK::totalFrameDataSamples);

                        // Pass the original recording to create the debug beep track
                        demodulateFrame(frameData, recordedAudio, frameStartSample);
                        preambleFound = true; // Stop searching after first potential frame
                    }
                    else {
                        std::cerr << "ERROR: Not enough samples for full frame. Need "
                            << FSK::totalFrameDataSamples << ", have "
                            << (recordedAudio.getNumSamples() - frameStartSample) << std::endl;
                    }
                    peakNCC = 0.0;
                    peakSampleIndex = -1;
                }
            }
            circularBufferPos = (circularBufferPos + 1) % circularBuffer.getNumSamples();
        }
        nccStream.close();
        std::cout << "Diagnostic correlation waveform saved to: " << nccFile.getFullPathName() << std::endl;

        // --- Save the actual recording for inspection ---
        juce::File recordingFile = juce::File::getCurrentWorkingDirectory().getChildFile("debug_loopback_recording.wav");
        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::AudioFormatWriter> writer(wavFormat.createWriterFor(new juce::FileOutputStream(recordingFile), 44100.0, 1, 16, {}, 0));
        if (writer != nullptr) {
            writer->writeFromAudioSampleBuffer(recordedAudio, 0, recordedAudio.getNumSamples());
            std::cout << "Full loopback recording saved to: " << recordingFile.getFullPathName() << std::endl;
        }

        std::cout << "\n--- Analysis Complete ---" << std::endl;
        if (!preambleFound) {
            std::cout << "No preamble detected above threshold. Check your NCC plot and threshold." << std::endl;
        }
        else {
            std::cout << "Review debug_beeps.wav and console output (CRC & bit confidence) to fine-tune SYNC_OFFSET." << std::endl;
        }
    }
private:
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

    // NEW: demodulateFrame now takes the original buffer to create the debug file
    void demodulateFrame(const juce::AudioBuffer<float>& frameData, const juce::AudioBuffer<float>& originalRecording, int frameStartSample)
    {
        std::vector<bool> receivedBits;
        receivedBits.reserve(FSK::totalFrameBits);
        const float* data = frameData.getReadPointer(0);

        std::cout << "\n--- Demodulator Output & Confidence Scores (First 100 bits) ---" << std::endl;
        for (int i = 0; i < FSK::totalFrameBits; ++i) {
            const float* bitSamples = data + (i * FSK::samplesPerBit);
            double mag_f0 = goertzelMagnitude(FSK::samplesPerBit, FSK::f0, bitSamples);
            double mag_f1 = goertzelMagnitude(FSK::samplesPerBit, FSK::f1, bitSamples);
            bool bit = mag_f1 > mag_f0;
            receivedBits.push_back(bit);

            if (i < 100) { // Print confidence for the first 100 bits
                double confidence = (mag_f0 > mag_f1) ? (mag_f0 / mag_f1) : (mag_f1 / mag_f0);
                std::cout << "Bit " << std::setw(3) << i << ": " << (bit ? "1" : "0") << " (Conf: " << std::fixed << std::setprecision(2) << confidence << ")" << std::endl;
            }
        }

        // --- CRC Check ---
        std::vector<bool> payload(receivedBits.begin(), receivedBits.begin() + FSK::payloadBits);
        uint8_t receivedCrcByte = 0;
        for (int i = 0; i < FSK::crcBits; ++i)
            if (receivedBits[FSK::payloadBits + i])
                receivedCrcByte |= (1 << (7 - i));

        uint8_t calculatedCrc = FSK::calculateCRC8(payload);

        std::cout << "\n--- CRC Validation ---" << std::endl;
        std::cout << "Received CRC (Dec): " << (int)receivedCrcByte << " (Bin): " << FSK::byteToBinary(receivedCrcByte) << std::endl;
        std::cout << "Calculated CRC (Dec): " << (int)calculatedCrc << " (Bin): " << FSK::byteToBinary(calculatedCrc) << std::endl;

        if (calculatedCrc == receivedCrcByte) {
            std::cout << "\nCRC OK! Writing " << payload.size() << " bits to OUTPUT.txt" << std::endl;
            std::ofstream outputFile("OUTPUT.txt");
            for (bool b : payload) outputFile << (b ? '1' : '0');
            outputFile.close();
        }
        else {
            std::cout << "\nCRC FAIL! Frame discarded." << std::endl;
        }

        // --- Generate Debug Beep Track ---
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
            juce::AudioSourceChannelInfo bufferToFill;
            juce::AudioBuffer<float> tempBuffer(const_cast<float**>(outputChannelData), numOutputChannels, numSamples);
            bufferToFill.buffer = &tempBuffer;            bufferToFill.startSample = 0;            fskSource.getNextAudioBlock(bufferToFill);
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
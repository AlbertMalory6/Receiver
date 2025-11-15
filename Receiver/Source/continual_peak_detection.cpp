/*
 * ASK MODULATION/DEMODULATION WITH CHIRP PREAMBLE DETECTION
 *
 * This program implements ASK audio communication:
 * 1. Reads binary data from INPUT.txt
 * 2. Generates 100 frames with chirp preambles
 * 3. Modulates and transmits
 * 4. Records audio
 * 5. Demodulates using dot product chirp detection
 * 6. Outputs decoded frames
 *
 * Files saved to: D:\fourth_year\cs120\debug_pic\ASK\
 */

#include <JuceHeader.h>
#include <iostream>
#include <fstream>
#include <cmath>
#include <iomanip>
#include <vector>
#include <random>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <thread>
#include <functional>
#include <memory>

class SampleRingBuffer {
public:
    explicit SampleRingBuffer(size_t capacitySamples)
        : capacity_(capacitySamples) {
    }

    void pushSamples(const float* samples, int numSamples) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (int i = 0; i < numSamples; ++i) {
            buffer_.push_back(samples[i]);
            if (buffer_.size() > capacity_) {
                buffer_.pop_front();
            }
        }
    }

    std::vector<float> snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::vector<float>(buffer_.begin(), buffer_.end());
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        buffer_.clear();
    }

private:
    size_t capacity_;
    mutable std::mutex mutex_;
    std::deque<float> buffer_;
};

// ==============================================================================
//  CONFIGURATION
// ==============================================================================
#define USE_RECORDED_AUDIO 0
 // 1 = Play from audio_path, 0 = Play generated signal

namespace ASK {
    constexpr double sampleRate = 88200.0;  // Match MATLAB
    constexpr int preambleSamples = 220;

    // Chirp parameters (1kHz-6kHz sweep, lowered for better channel response)
    constexpr double chirp_f_start = 1000.0;   // Was 2000.0
    constexpr double chirp_f_end = 4000.0;     // Was 10000.0

    // Line coding parameters
    constexpr double baseFreq = 2000.0;        // Base tone for line coding (2kHz)

    // =========================================================================
    // SPEED TUNING PARAMETERS - Adjust these to meet timing requirements
    // =========================================================================
    // Target: 50,000 bits in 20 seconds = 2,500 bps minimum
    // Formula: Bit Rate = sampleRate / samplesPerBit
    //
    // TUNING GUIDE:
    // samplesPerBit | NRZ bps | Duration | Robustness
    // -------------|---------|----------|------------
    //      15      |  2,940  |  17.0s   | Good
    //      11      |  4,009  |  12.5s   | Medium  <- RECOMMENDED START
    //       8      |  5,512  |   9.1s   | Marginal
    //       6      |  7,350  |   6.8s   | Poor (use only if channel is perfect)

    constexpr int samplesPerBit = 6;          // NRZ: 4,009 bps (RECOMMENDED for speed)

    constexpr int dataBitsPerFrame = 100;
    constexpr int crcBitsPerFrame = 8;
    constexpr int bitsPerFrame = dataBitsPerFrame + crcBitsPerFrame;
    constexpr int numFrames = 500;               // Changed from 100 to handle 50,000 bits (500×100=50,000)

    // CRC polynomial: x^8+x^7+x^5+x^2+x+1 = 0xD5 = 0b11010101
    constexpr uint8_t crcPolynomial = 0xD5;

    // Input/Output paths
    const std::string inputPath = "INPUT.bin";  // Use binary file now
    const std::string outputPath = "D:\\fourth_year\\cs120\\debug_pic\\ASK\\";
    const std::string audio_path = "D:\\fourth_year\\cs120\\debug_pic\\ASK\\recorded_signal.wav";

}

namespace DemodulationThresholds {
    constexpr double syncPowerThreshold = 0.5;      // Minimum correlation strength to accept preamble
    constexpr double syncPowerPowerRatio = 12.0;    // syncPower must beat running power by this ratio
}

namespace LinkMonitorConfig {
    constexpr int framesPerPause = 125;             // Pause cadence for sender link check
    constexpr double pauseListenSeconds = 0.1;      // Listen window duration during pause
    constexpr double highPowerRmsThreshold = 0.08;  // RMS power threshold to qualify as noise beacon
    constexpr double highPowerPeakThreshold = 1.1;  // Peak amplitude threshold for noise beacon
    constexpr double beaconCorrelationThreshold = 0.35; // Matched-filter threshold for receiver beacon
}

namespace ReceiverBeaconConfig {
    constexpr double noiseBurstDurationSec = 0.08;       // Duration of receiver noise beacon
    constexpr float noiseBurstAmplitude = 0.98f;         // Max amplitude for beacon samples
    constexpr double beaconFrequencyHz = 7500.0;         // Distinct carrier for beacon (outside data band)
    constexpr double beaconStartDelaySec = 2.0;          // Wait time before beacon transmissions allowed
}

// ==============================================================================
//  CRC GENERATOR
// ==============================================================================
class CRC8Generator {
private:
    uint8_t polynomial;

public:
    CRC8Generator(uint8_t poly) : polynomial(poly) {}

    // Generate CRC for data (returns data + CRC)
    std::vector<bool> generate(const std::vector<bool>& data) {
        std::vector<bool> result = data;
        uint8_t crc = 0;

        for (bool bit : data) {
            bool feedback = (crc >> 7) ^ bit;
            crc <<= 1;
            if (feedback) {
                crc ^= polynomial;
            }
        }

        // Append CRC bits
        for (int i = 7; i >= 0; --i) {
            result.push_back((crc >> i) & 1);
        }

        return result;
    }

    // Check CRC (returns true if valid)
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
    /** Generates the preamble chirp (2kHz-10kHz sweep, matching MATLAB) */
    static juce::AudioBuffer<float> generatePreamble() {
        juce::AudioBuffer<float> preamble(1, ASK::preambleSamples);
        auto* signal = preamble.getWritePointer(0);

        // Generate frequency sweep: 2kHz -> 10kHz -> 2kHz
        std::vector<double> freqSweep;
        for (int i = 0; i < ASK::preambleSamples / 2; ++i) {
            double freq = juce::jmap((double)i, 0.0, (double)(ASK::preambleSamples / 2 - 1),
                ASK::chirp_f_start, ASK::chirp_f_end);
            freqSweep.push_back(freq);
        }
        for (int i = ASK::preambleSamples / 2; i < ASK::preambleSamples; ++i) {
            double freq = juce::jmap((double)i, (double)(ASK::preambleSamples / 2),
                (double)(ASK::preambleSamples - 1),
                ASK::chirp_f_end, ASK::chirp_f_start);
            freqSweep.push_back(freq);
        }

        // Generate signal using cumulative integration (like cumtrapz in MATLAB)
        double currentPhase = 0.0;
        for (int i = 0; i < ASK::preambleSamples; ++i) {
            double phaseIncrement = 2.0 * juce::MathConstants<double>::pi * freqSweep[i] / ASK::sampleRate;
            currentPhase += phaseIncrement;
            signal[i] = std::sin(currentPhase);
        }

        return preamble;
    }

    /** Generates base tone for line coding (2kHz sine wave) */
    static juce::AudioBuffer<float> generateBaseTone(int numSamples) {
        juce::AudioBuffer<float> tone(1, numSamples);
        auto* signal = tone.getWritePointer(0);

        double phase = 0.0;
        double phaseIncrement = 2.0 * juce::MathConstants<double>::pi * ASK::baseFreq / ASK::sampleRate;

        for (int i = 0; i < numSamples; ++i) {
            signal[i] = std::sin(phase);
            phase += phaseIncrement;
            if (phase > 2.0 * juce::MathConstants<double>::pi) {
                phase -= 2.0 * juce::MathConstants<double>::pi;
            }
        }

        return tone;
    }

    /** Generates NRZ encoded bit (bit=1: positive tone, bit=0: negative tone) */
    static juce::AudioBuffer<float> generateNRZBit(bool bitValue, int samplesPerBit) {
        auto tone = generateBaseTone(samplesPerBit);
        auto* signal = tone.getWritePointer(0);

        // Bit 1 = positive amplitude, Bit 0 = negative amplitude
        float polarity = bitValue ? 1.0f : -1.0f;
        for (int i = 0; i < samplesPerBit; ++i) {
            signal[i] *= polarity;
        }

        return tone;
    }

    /** Generates silence */
    static juce::AudioBuffer<float> generateSilence(int numSamples) {
        juce::AudioBuffer<float> silence(1, numSamples);
        silence.clear();
        return silence;
    }

    static juce::AudioBuffer<float> generateChirpBeacon(int silenceSamples = (int)(0.1 * ASK::sampleRate)) {
        auto chirp = generatePreamble();
        juce::AudioBuffer<float> beacon(1, chirp.getNumSamples() + silenceSamples);
        beacon.clear();
        beacon.copyFrom(0, 0, chirp, 0, 0, chirp.getNumSamples());
        return beacon;
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
            std::cerr << "ERROR: Could not open input file: " << filename << std::endl;
            return data;
        }

        // Read all bytes
        std::vector<unsigned char> bytes(
            (std::istreambuf_iterator<char>(file)),
            (std::istreambuf_iterator<char>())
        );



        file.close();

        // Convert bytes into bits (MSB first)
        for (unsigned char byte : bytes) {
            for (int bit = 7; bit >= 0; --bit) {
                bool bitValue = (byte >> bit) & 1;
                data.push_back(bitValue);
            }
        }

        std::cout << "First 16 bits: ";
        for (int i = 0; i < 16 && i < (int)data.size(); ++i)
            std::cout << data[i];
        std::cout << std::endl;

        // Limit to max data size (100 frames * 100 bits)
        int maxDataSize = ASK::numFrames * ASK::dataBitsPerFrame;
        if ((int)data.size() > maxDataSize) {
            data.resize(maxDataSize);
            std::cout << "WARNING: Data truncated to " << maxDataSize << " bits" << std::endl;
        }

        std::cout << "Read " << data.size() << " bits (" << bytes.size()
            << " bytes) from binary file: " << filename << std::endl;

        return data;
    }
};


// ==============================================================================
//  MODULATOR
// ==============================================================================
class Modulator {
public:
    static std::vector<juce::AudioBuffer<float>> generateFrameBuffers(const std::vector<bool>& inputData) {
        auto preamble = SignalGenerator::generatePreamble();

        std::vector<std::vector<bool>> frames(ASK::numFrames, std::vector<bool>(ASK::dataBitsPerFrame, false));
        for (int i = 0; i < ASK::numFrames && (i * ASK::dataBitsPerFrame) < (int)inputData.size(); ++i) {
            int dataStart = i * ASK::dataBitsPerFrame;
            for (int j = 0; j < ASK::dataBitsPerFrame && (dataStart + j) < (int)inputData.size(); ++j) {
                frames[i][j] = inputData[dataStart + j];
            }
        }

        CRC8Generator crcGen(ASK::crcPolynomial);
        std::vector<juce::AudioBuffer<float>> frameBuffers;
        frameBuffers.reserve(ASK::numFrames);

        for (int frameIdx = 0; frameIdx < ASK::numFrames; ++frameIdx) {
            std::vector<bool> frame(ASK::dataBitsPerFrame);
            for (int i = 0; i < ASK::dataBitsPerFrame; ++i) {
                frame[i] = frames[frameIdx][i];
            }

            std::vector<bool> frameWithCRC = crcGen.generate(frame);

            juce::AudioBuffer<float> frameBuffer(1, ASK::preambleSamples + ASK::bitsPerFrame * ASK::samplesPerBit);
            frameBuffer.clear();
            frameBuffer.copyFrom(0, 0, preamble, 0, 0, preamble.getNumSamples());

            int writePos = preamble.getNumSamples();
            for (int bitIdx = 0; bitIdx < ASK::bitsPerFrame; ++bitIdx) {
                bool bitValue = frameWithCRC[bitIdx];
                auto bitSignal = SignalGenerator::generateNRZBit(bitValue, ASK::samplesPerBit);
                frameBuffer.copyFrom(0, writePos, bitSignal, 0, 0, bitSignal.getNumSamples());
                writePos += bitSignal.getNumSamples();
            }

            frameBuffers.push_back(std::move(frameBuffer));
        }

        return frameBuffers;
    }

    static juce::AudioBuffer<float> modulateData(const std::vector<bool>& inputData) {
        std::vector<juce::AudioBuffer<float>> frameBuffers = generateFrameBuffers(inputData);
        std::vector<float> outputSignal;
        std::mt19937 rng(1); // Seed = 1 (magic number from MATLAB)

        std::cout << "Using line coding: NRZ (Non-Return to Zero)" << std::endl;

        for (auto& frameBuffer : frameBuffers) {
            int spacingBefore = rng() % 101;
            outputSignal.insert(outputSignal.end(), spacingBefore, 0.0f);

            for (int i = 0; i < frameBuffer.getNumSamples(); ++i) {
                outputSignal.push_back(frameBuffer.getSample(0, i));
            }

            int spacingAfter = rng() % 101;
            outputSignal.insert(outputSignal.end(), spacingAfter, 0.0f);
        }

        juce::AudioBuffer<float> result(1, (int)outputSignal.size());
        for (size_t i = 0; i < outputSignal.size(); ++i) {
            result.setSample(0, i, outputSignal[i]);
        }

        return result;
    }
};

// ==============================================================================
//  AUDIO RECORDER (Using ThreadedWriter like AudioRecordingDemo)
// ==============================================================================
class AudioRecorder : public juce::AudioIODeviceCallback {
public:
    AudioRecorder() {
        backgroundThread.startThread();
    }

    ~AudioRecorder() override {
        stop();
    }

    void startRecording(const juce::File& file, double sampleRateToUse) {
        stop();

        sampleRate = sampleRateToUse;

        if (sampleRate > 0) {
            file.deleteFile();

            if (std::unique_ptr<juce::OutputStream> fileStream{ file.createOutputStream() }) {
                juce::WavAudioFormat wavFormat;

                using Opts = juce::AudioFormatWriterOptions;
                // createWriterFor takes a reference to unique_ptr and takes ownership if successful
                if (auto writer = wavFormat.createWriterFor(fileStream,
                    Opts{}.withSampleRate(sampleRate)
                    .withNumChannels(1)
                    .withBitsPerSample(16))) {
                    // Writer now owns the stream (fileStream is set to nullptr by createWriterFor)
                    // Create ThreadedWriter for background writing
                    threadedWriter.reset(new juce::AudioFormatWriter::ThreadedWriter(
                        writer.release(), backgroundThread, 32768));

                    nextSampleNum = 0;

                    const juce::CriticalSection::ScopedLockType sl(writerLock);
                    activeWriter = threadedWriter.get();

                    std::cout << "Recording started to: " << file.getFullPathName() << std::endl;
                    std::cout << "Sample rate: " << sampleRate << " Hz" << std::endl;
                }
            }
        }
    }

    void stop() {
        {
            const juce::CriticalSection::ScopedLockType sl(writerLock);
            activeWriter = nullptr;
        }
        threadedWriter.reset();
        std::cout << "Recording stopped." << std::endl;
    }

    bool isRecording() const {
        return activeWriter.load() != nullptr;
    }

    void setSampleCallback(std::function<void(const float*, int)> callback) {
        std::lock_guard<std::mutex> lock(callbackMutex);
        sampleCallback = std::move(callback);
    }

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override {
        // Update sample rate if device provides one
        if (device != nullptr && sampleRate == 0) {
            sampleRate = device->getCurrentSampleRate();
        }
    }

    void audioDeviceStopped() override {
        // Keep sampleRate for file writing
    }

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
        float* const* outputChannelData, int numOutputChannels,
        int numSamples,
        const juce::AudioIODeviceCallbackContext& context) override {
        juce::ignoreUnused(context);

        // Write input to file
        {
            const juce::CriticalSection::ScopedLockType sl(writerLock);
            if (activeWriter.load() != nullptr && numInputChannels > 0) {
                activeWriter.load()->write(inputChannelData, numSamples);
                nextSampleNum += numSamples;
            }
        }

        // Callback sample data
        {
            std::lock_guard<std::mutex> lock(callbackMutex);
            if (sampleCallback && numInputChannels > 0 && inputChannelData[0] != nullptr) {
                sampleCallback(inputChannelData[0], numSamples);
            }
        }

        // Clear output buffers (we don't play anything here)
        for (int i = 0; i < numOutputChannels; ++i) {
            if (outputChannelData[i] != nullptr) {
                juce::FloatVectorOperations::clear(outputChannelData[i], numSamples);
            }
        }
    }

private:
    juce::TimeSliceThread backgroundThread{ "Audio Recorder Thread" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedWriter;
    double sampleRate = 0.0;
    int nextSampleNum = 0;

    juce::CriticalSection writerLock;
    std::atomic<juce::AudioFormatWriter::ThreadedWriter*> activeWriter{ nullptr };

    // Sample callback support
    std::function<void(const float*, int)> sampleCallback;
    std::mutex callbackMutex;
};

// ==============================================================================
//  AUDIO PLAYER (Separate class for playback)
// ==============================================================================
class AudioPlayer : public juce::AudioIODeviceCallback {
public:
    AudioPlayer(const juce::AudioBuffer<float>& bufferToPlay)
        : sourceBuffer(bufferToPlay), samplesPlayed(0) {
    }

    void audioDeviceAboutToStart(juce::AudioIODevice*) override {
        samplesPlayed = 0;
    }

    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
        float* const* outputChannelData, int numOutputChannels,
        int numSamples,
        const juce::AudioIODeviceCallbackContext& context) override {
        juce::ignoreUnused(inputChannelData, numInputChannels, context);

        // Handle playback (output)
        int samplesRemaining = sourceBuffer.getNumSamples() - samplesPlayed;
        int samplesToPlay = std::min(numSamples, samplesRemaining);

        if (samplesToPlay > 0) {
            const float* src = sourceBuffer.getReadPointer(0, samplesPlayed);
            for (int i = 0; i < numOutputChannels; ++i) {
                if (outputChannelData[i] != nullptr) {
                    std::memcpy(outputChannelData[i], src, sizeof(float) * samplesToPlay);
                    if (samplesToPlay < numSamples) {
                        juce::FloatVectorOperations::clear(outputChannelData[i] + samplesToPlay,
                            numSamples - samplesToPlay);
                    }
                }
            }
            samplesPlayed += samplesToPlay;
        }
        else {
            // Playback finished, output silence
            for (int i = 0; i < numOutputChannels; ++i) {
                if (outputChannelData[i] != nullptr) {
                    juce::FloatVectorOperations::clear(outputChannelData[i], numSamples);
                }
            }
        }
    }

private:
    const juce::AudioBuffer<float>& sourceBuffer;
    int samplesPlayed;
};

double bufferDurationSeconds(const juce::AudioBuffer<float>& buffer) {
    return static_cast<double>(buffer.getNumSamples()) / ASK::sampleRate;
}

void playBufferBlocking(juce::AudioDeviceManager& deviceManager, const juce::AudioBuffer<float>& buffer, int paddingMs = 25) {
    AudioPlayer player(buffer);
    deviceManager.addAudioCallback(&player);
    int durationMs = static_cast<int>(bufferDurationSeconds(buffer) * 1000.0) + paddingMs;
    juce::Thread::sleep(durationMs);
    deviceManager.removeAudioCallback(&player);
}

// ==============================================================================
//  CHIRP DETECTOR (Modified for full analysis logging)
// ==============================================================================
class ChirpDetector {
private:
    juce::AudioBuffer<float> template_;
    double templateEnergy_;

    double calculateEnergy(const float* buffer, int numSamples) {
        double energy = 0.0;
        for (int i = 0; i < numSamples; ++i) {
            energy += buffer[i] * buffer[i];
        }
        return energy;
    }

public:
    ChirpDetector() {
        template_ = SignalGenerator::generatePreamble();
        templateEnergy_ = calculateEnergy(template_.getReadPointer(0), template_.getNumSamples());
        std::cout << "Chirp template generated: " << template_.getNumSamples() << " samples" << std::endl;
        std::cout << "Template energy: " << templateEnergy_ << std::endl;
    }

    /**
     * Analyzes the entire signal and logs correlation scores at every sample index
     */
    void logCorrelation(const juce::AudioBuffer<float>& signal, const std::string& logFilePath) {
        const float* sigData = signal.getReadPointer(0);
        const float* tempData = template_.getReadPointer(0);
        const int sigLen = signal.getNumSamples();
        const int tempLen = template_.getNumSamples();

        std::ofstream logFile(logFilePath);
        if (!logFile.is_open()) {
            std::cerr << "ERROR: Could not open log file: " << logFilePath << std::endl;
            return;
        }

        std::cout << "\nAnalyzing " << sigLen << " samples..." << std::endl;
        std::cout << "Logging correlation scores to: " << logFilePath << std::endl;

        logFile << "Sample_Index,Dot_Product_Value\n";

        double maxScore = -1e10;
        int maxPos = -1;

        for (int i = 0; i <= sigLen - tempLen; ++i) {
            double dotProduct = 0.0;
            double score = 0.0;

            for (int j = 0; j < tempLen; ++j) {
                dotProduct += sigData[i + j] * tempData[j];
            }

            score = dotProduct;

            logFile << i << "," << std::fixed << std::setprecision(8) << score << "\n";

            if (score > maxScore) {
                maxScore = score;
                maxPos = i;
            }

            if ((i + 1) % 100000 == 0) {
                std::cout << "  Analyzed " << (i + 1) << " / " << sigLen << " samples..." << std::endl;
            }
        }

        logFile.close();
        std::cout << "\n✓ Analysis complete." << std::endl;
        std::cout << "  Max Dot Product: " << maxScore << " at sample " << maxPos << std::endl;
    }

    bool detectPreamble(const std::vector<float>& signal, double threshold = 0.65) const {
        const int tempLen = template_.getNumSamples();
        if ((int)signal.size() < tempLen) {
            return false;
        }
        const float* tempData = template_.getReadPointer(0);

        for (size_t i = 0; i + tempLen <= signal.size(); ++i) {
            double dotProduct = 0.0;
            double signalEnergy = 0.0;
            for (int j = 0; j < tempLen; ++j) {
                float sample = signal[i + j];
                dotProduct += sample * tempData[j];
                signalEnergy += sample * sample;
            }
            if (signalEnergy < 1e-9) {
                continue;
            }
            double ncc = dotProduct / std::sqrt(signalEnergy * templateEnergy_);
            if (ncc >= threshold) {
                return true;
            }
        }
        return false;
    }
};

// ==============================================================================
//  UTILITY FUNCTIONS
// ==============================================================================
void saveWavFile(const juce::AudioBuffer<float>& buffer, const juce::File& file, int numSamples) {
    juce::WavAudioFormat wavFormat;
    if (std::unique_ptr<juce::OutputStream> fileStream{ file.createOutputStream() }) {
        using Opts = juce::AudioFormatWriterOptions;
        if (auto writer = wavFormat.createWriterFor(fileStream.release(),
            ASK::sampleRate,        // double sampleRate
            1,                      // unsigned int numChannels
            16,                     // unsigned int bitsPerSample
            {},                     // juce::StringPairArray* metadata = nullptr
            0))                     // int qualityOptionIndex = 0
        {
            fileStream.release();
            writer->writeFromAudioSampleBuffer(buffer, 0, numSamples);
            std::cout << "✓ Audio saved to: " << file.getFullPathName() << std::endl;
        }
        else {
            std::cerr << "ERROR: Could not create writer for " << file.getFullPathName() << std::endl;
        }
    }
    else {
        std::cerr << "ERROR: Could not create output stream for " << file.getFullPathName() << std::endl;
    }
}

bool loadWavFile(juce::AudioBuffer<float>& buffer, const juce::File& file) {
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr) {
        std::cerr << "ERROR: Could not read audio file: " << file.getFullPathName() << std::endl;
        return false;
    }

    buffer.setSize((int)reader->numChannels, (int)reader->lengthInSamples);
    if (!reader->read(&buffer, 0, (int)reader->lengthInSamples, 0, true, true)) {
        std::cerr << "ERROR: Failed to read audio data from file" << std::endl;
        return false;
    }

    std::cout << "✓ Audio loaded from: " << file.getFullPathName() << std::endl;
    std::cout << "  Channels: " << buffer.getNumChannels() << std::endl;
    std::cout << "  Samples: " << buffer.getNumSamples() << std::endl;
    std::cout << "  Sample rate: " << reader->sampleRate << " Hz" << std::endl;

    return true;
}

void analyzeRecordingDiagnostics(const juce::AudioBuffer<float>& buffer) {
    if (buffer.getNumSamples() == 0) {
        std::cout << "  [Diagnostics] Buffer is empty!" << std::endl;
        return;
    }

    const float* data = buffer.getReadPointer(0);
    int numSamples = buffer.getNumSamples();

    // Calculate statistics
    float minVal = data[0], maxVal = data[0];
    double sum = 0.0, sumSq = 0.0;

    for (int i = 0; i < numSamples; ++i) {
        float val = data[i];
        minVal = std::min(minVal, val);
        maxVal = std::max(maxVal, val);
        sum += val;
        sumSq += val * val;
    }

    double mean = sum / numSamples;
    double variance = (sumSq / numSamples) - (mean * mean);
    double stdDev = std::sqrt(variance);

    std::cout << "\n[Recording Diagnostics]" << std::endl;
    std::cout << "  Min value: " << std::fixed << std::setprecision(6) << minVal << std::endl;
    std::cout << "  Max value: " << maxVal << std::endl;
    std::cout << "  Mean: " << mean << std::endl;
    std::cout << "  Std deviation: " << stdDev << std::endl;

    // Check if signal is constant (very low variance)
    if (stdDev < 1e-6) {
        std::cout << "  ⚠ WARNING: Signal appears to be CONSTANT (std dev < 1e-6)" << std::endl;
        std::cout << "     This suggests the microphone may not be receiving the audio." << std::endl;
    }


}

// Forward declaration
bool waitForChirpDetection(ChirpDetector& detector,
    SampleRingBuffer& buffer,
    double timeoutSeconds,
    double threshold = 0.65);

void runChirpReceiverMode(juce::AudioDeviceManager& deviceManager, const juce::File& outputDir);

void runLinkMonitoredSender(juce::AudioDeviceManager& deviceManager,
    const std::vector<bool>& inputBits,
    const juce::File& outputDir) {
    std::cout << "\n[MODE 5: SENDER WITH CHIRP LINK CHECK]\n";
    std::cout << "Press ENTER at any time to abort and return to main menu.\n" << std::endl;
    auto frameBuffers = Modulator::generateFrameBuffers(inputBits);
    if (frameBuffers.empty()) {
        std::cout << "No frames available for transmission." << std::endl;
        return;
    }

    AudioRecorder recorder;
    SampleRingBuffer sampleBuffer(static_cast<size_t>(ASK::sampleRate * 3));
    recorder.setSampleCallback([&sampleBuffer](const float* samples, int numSamples) {
        sampleBuffer.pushSamples(samples, numSamples);
        });

    juce::File recFile = outputDir.getChildFile("link_sender_record.wav");
    recorder.startRecording(recFile, ASK::sampleRate);
    deviceManager.addAudioCallback(&recorder);

    const size_t minSamplesForPowerCheck = static_cast<size_t>(ASK::sampleRate * 0.05);  // 50ms of samples

    bool linkError = false;
    auto abortRequested = std::make_shared<std::atomic<bool>>(false);

    // Thread to monitor for Enter key press to abort
    std::thread inputThread([abortRequested]() {
        std::cin.get();
        abortRequested->store(true);
        });

    // Helper function to calculate signal power
    auto calculateSignalPower = [](const std::vector<float>& samples) -> double {
        if (samples.empty()) return 0.0;
        double sumSq = 0.0;
        for (float s : samples) {
            sumSq += s * s;
        }
        return sumSq / samples.size();  // Average power
        };
    auto calculatePeak = [](const std::vector<float>& samples) -> double {
        double peak = 0.0;
        for (float s : samples) {
            peak = std::max(peak, std::abs(static_cast<double>(s)));
        }
        return peak;
        };

    // Matched-filter reference for receiver beacon frequency
    std::vector<float> beaconReference(minSamplesForPowerCheck, 0.0f);
    {
        double phase = 0.0;
        const double phaseIncrement = 2.0 * juce::MathConstants<double>::pi
            * ReceiverBeaconConfig::beaconFrequencyHz / ASK::sampleRate;
        for (size_t i = 0; i < beaconReference.size(); ++i) {
            beaconReference[i] = ReceiverBeaconConfig::noiseBurstAmplitude
                * static_cast<float>(std::sin(phase));
            phase += phaseIncrement;
        }
    }

    auto calculateBeaconCorrelation = [&](const std::vector<float>& samples) -> double {
        size_t refSize = beaconReference.size();
        if (samples.size() < refSize || refSize == 0) return 0.0;
        auto startIt = samples.end() - refSize;
        double dot = 0.0;
        double refEnergy = 0.0;
        double sampEnergy = 0.0;
        auto it = startIt;
        for (size_t i = 0; i < refSize; ++i, ++it) {
            double refVal = beaconReference[i];
            double sampVal = static_cast<double>(*it);
            dot += sampVal * refVal;
            refEnergy += refVal * refVal;
            sampEnergy += sampVal * sampVal;
        }
        if (refEnergy <= 0.0 || sampEnergy <= 0.0) return 0.0;
        return dot / std::sqrt(refEnergy * sampEnergy); // Normalized correlation [-1,1]
        };

    for (size_t frameIdx = 0; frameIdx < frameBuffers.size(); ++frameIdx) {
        // Check for abort request
        if (abortRequested->load()) {
            std::cout << "\n[ABORT] User requested abort. Stopping transmission..." << std::endl;
            break;
        }

        // Normal transmission
        std::cout << "[TX] Frame " << (frameIdx + 1) << " / " << frameBuffers.size() << std::endl;
        playBufferBlocking(deviceManager, frameBuffers[frameIdx]);

        // Periodic pause to listen for high-power noise beacon from receiver
        if ((frameIdx + 1) % LinkMonitorConfig::framesPerPause == 0) {
            std::cout << "[PAUSE] Monitoring link (" << LinkMonitorConfig::pauseListenSeconds << "s)..." << std::endl;
            sampleBuffer.clear();  // Clear buffer before listening

            auto pauseStart = std::chrono::steady_clock::now();
            bool signalDetected = false;

            // Listen for configured duration
            while (std::chrono::duration<double>(std::chrono::steady_clock::now() - pauseStart).count()
                < LinkMonitorConfig::pauseListenSeconds) {
                if (abortRequested->load()) {
                    break;
                }

                auto snapshot = sampleBuffer.snapshot();
                if (snapshot.size() >= minSamplesForPowerCheck) {
                    size_t recentCount = std::min(snapshot.size(), minSamplesForPowerCheck);
                    std::vector<float> recentSamples(snapshot.end() - recentCount, snapshot.end());

                    double signalPower = calculateSignalPower(recentSamples);
                    double signalPeak = calculatePeak(recentSamples);
                    double beaconCorrelation = calculateBeaconCorrelation(recentSamples);

                    if (beaconCorrelation > LinkMonitorConfig::beaconCorrelationThreshold
                        && (signalPower > LinkMonitorConfig::highPowerRmsThreshold
                            || signalPeak > LinkMonitorConfig::highPowerPeakThreshold)) {
                        signalDetected = true;
                        std::cout << "[PAUSE] Beacon detected (corr=" << beaconCorrelation
                            << ", power=" << signalPower << ", peak=" << signalPeak << ")" << std::endl;
                        sampleBuffer.clear();
                        break;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }

            if (!signalDetected && !abortRequested->load()) {
                std::cout << "line error" << std::endl;
                linkError = true;
                abortRequested->store(true);
                break;
            }
            else if (signalDetected) {
                std::cout << "[PAUSE] High-power noise detected, continuing." << std::endl;
            }
        }
    }

    // Clean up input thread
    if (inputThread.joinable()) {
        if (abortRequested->load()) {
            inputThread.join();
        }
        else {
            inputThread.detach();
        }
    }

    recorder.stop();
    deviceManager.removeAudioCallback(&recorder);

    if (linkError) {
        // "line error" already printed, process aborted
    }
    else if (abortRequested->load()) {
        std::cout << "[SENDER] Transmission aborted by user." << std::endl;
    }
    else {
        std::cout << "[SENDER] Completed transmission of " << frameBuffers.size()
            << " frames with periodic link checks." << std::endl;
    }
}

// ==============================================================================
//  FRAME DEMODULATOR (With Dot Product Chirp Detection)
// ==============================================================================
class FrameDemodulator {
private:
    juce::AudioBuffer<float> preambleTemplate_;
    double preambleEnergy_;
    CRC8Generator crcGen_;
    bool verboseOutput_;

    double calculateEnergy(const float* buffer, int numSamples) {
        double energy = 0.0;
        for (int i = 0; i < numSamples; ++i) {
            energy += buffer[i] * buffer[i];
        }
        return energy;
    }

    std::vector<bool> demodulateFrameNRZ(const float* frameData, int frameSamples) {
        // NRZ Demodulation: Detect sign of signal in each bit period
        std::vector<bool> bits(ASK::bitsPerFrame);

        for (int bitIdx = 0; bitIdx < ASK::bitsPerFrame; ++bitIdx) {
            int bitStart = bitIdx * ASK::samplesPerBit;
            int bitEnd = (bitIdx + 1) * ASK::samplesPerBit;

            // Sample the middle 50% of the bit period (skip edges for stability)
            int sampleStart = bitStart + ASK::samplesPerBit / 4;
            int sampleEnd = bitEnd - ASK::samplesPerBit / 4;

            // Integrate signal over sample period
            double sum = 0.0;
            for (int j = sampleStart; j < sampleEnd && j < frameSamples; ++j) {
                sum += frameData[j];
            }

            // Positive sum = bit 1, Negative sum = bit 0
            bits[bitIdx] = (sum > 0.0);
        }

        return bits;
    }

    std::vector<bool> demodulateFrame(const float* frameData, int frameSamples) {
        return demodulateFrameNRZ(frameData, frameSamples);
    }

public:
    struct DemodulationSummary {
        int detectionAttempts = 0;
        int validFrames = 0;
        int invalidCRC = 0;
        std::vector<std::vector<bool>> decodedFramesInOrder;  // Frames in detection order
        bool targetDetectionCountReached = false;  // True when 500 detections reached
    };

    FrameDemodulator(bool verbose = false) : crcGen_(ASK::crcPolynomial), verboseOutput_(verbose) {
        preambleTemplate_ = SignalGenerator::generatePreamble();
        preambleEnergy_ = calculateEnergy(preambleTemplate_.getReadPointer(0), preambleTemplate_.getNumSamples());
    }

    void setVerbose(bool verbose) { verboseOutput_ = verbose; }

    DemodulationSummary demodulate(const juce::AudioBuffer<float>& signal,
        const std::string& logPath,
        bool emitLogs = true,
        bool writeOutputFile = true) {
        const float* sigData = signal.getReadPointer(0);
        const float* tempData = preambleTemplate_.getReadPointer(0);
        const int sigLen = signal.getNumSamples();
        const int tempLen = preambleTemplate_.getNumSamples();

        // Detect and decode frames using dot product like MATLAB
        if (emitLogs) {
            std::cout << "\nDetecting preambles and decoding frames (dot product method)...\n";
            std::cout << "Demodulation method: NRZ line coding\n";
        }
        double power = 0.0, syncPowerLocalMax = 0.0;
        int startIndex = 0, state = 0;
        std::vector<float> syncFIFO(tempLen, 0.0f);
        std::vector<float> decodeFIFO;
        std::vector<int> detectedPositions;

        // Store decoded frame data in detection order (not by frame ID)
        std::vector<std::vector<bool>> decodedFramesInOrder;

        int frameSamples = ASK::bitsPerFrame * ASK::samplesPerBit;
        int expectedInterval = ASK::preambleSamples + frameSamples + 200;

        // Counters for summary
        int detectionAttempts = 0;
        int validFrames = 0;
        int invalidCRC = 0;

        for (int i = 0; i < sigLen; ++i) {
            power = power * (1.0 - 1.0 / 64.0) + (sigData[i] * sigData[i]) / 64.0;

            if (state == 0) {
                // Maintain sliding window: syncFIFO = [syncFIFO(2:end), current_sample]
                std::rotate(syncFIFO.begin(), syncFIFO.begin() + 1, syncFIFO.end());
                syncFIFO.back() = sigData[i];

                // Calculate syncPower = sum(syncFIFO.*preamble)/200
                double dotProduct = 0.0;
                for (int j = 0; j < tempLen; ++j) {
                    dotProduct += syncFIFO[j] * tempData[j];
                }
                double syncPower = dotProduct / 200.0;

                if (syncPower > DemodulationThresholds::syncPowerThreshold
                    && syncPower > syncPowerLocalMax
                    && syncPower > power / DemodulationThresholds::syncPowerPowerRatio) {
                    syncPowerLocalMax = syncPower; startIndex = i;
                    if (emitLogs && verboseOutput_) {
                        //print local syncPower to tune threshold
                        std::cout << "[Detection] SyncPower at sample " << i << ": " << syncPower
                            << " (power=" << power << ")" << std::endl;
                    }
                }
                else if ((i - startIndex > 200) && (startIndex != 0)) { //
                    detectedPositions.push_back(startIndex);
                    detectionAttempts++;
                    if (emitLogs && verboseOutput_) {
                        std::cout << "\n[Frame Detection #" << detectionAttempts << "] Peak at sample "
                            << startIndex << " (max syncPower=" << syncPowerLocalMax << ")" << std::endl;
                    }
                    syncPowerLocalMax = 0.0; state = 1; decodeFIFO.clear();
                    // Reset syncFIFO like MATLAB: syncFIFO = zeros(1, length(syncFIFO))
                    std::fill(syncFIFO.begin(), syncFIFO.end(), 0.0f);
                    for (int j = startIndex + 1; j <= i && j < sigLen; ++j) decodeFIFO.push_back(sigData[j]);
                }
            }
            else if (state == 1) {
                decodeFIFO.push_back(sigData[i]);
                if ((int)decodeFIFO.size() >= frameSamples) {
                    std::vector<bool> bits = demodulateFrame(decodeFIFO.data(), frameSamples);

                    // Extract data bits (entire payload before CRC)
                    std::vector<bool> frameData(bits.begin(),
                        bits.begin() + ASK::dataBitsPerFrame);

                    // Check CRC using entire demodulated frame (data+CRC)
                    bool crcValid = crcGen_.check(bits);

                    // Always show result in verbose mode
                    if (emitLogs && verboseOutput_) {
                        std::cout << "  Detection #" << detectionAttempts
                            << " | CRC: " << (crcValid ? "VALID" : "INVALID")
                            << " | Data bits: ";
                        // Print first 20 data bits for preview
                        for (int j = 0; j < std::min(100, ASK::dataBitsPerFrame); ++j) {
                            std::cout << (frameData[j] ? '1' : '0');
                        }
                        std::cout << std::endl;
                    }

                    // Store frame in detection order (regardless of CRC or frame ID)
                    validFrames++;
                    decodedFramesInOrder.push_back(frameData);

                    if (!crcValid) {
                        invalidCRC++;
                    }

                    if (emitLogs && !verboseOutput_) {
                        std::cout << "Detection #" << detectionAttempts << " decoded successfully" << std::endl;
                    }

                    startIndex = 0; decodeFIFO.clear(); state = 0;
                }
            }
        }

        if (emitLogs) {
            std::cout << "\n[Demodulation Summary]" << std::endl;
            std::cout << "  Total chirp detections: " << detectionAttempts << std::endl;
            std::cout << "  Frames decoded: " << validFrames << std::endl;
            std::cout << "  Invalid CRC: " << invalidCRC << std::endl;
        }

        if (writeOutputFile) {
            // Write decoded data to OUTPUT.txt in detection order
            std::string outputFilePath = ASK::outputPath + "OUTPUT.txt";

            std::ofstream outputFile;
            outputFile.open(outputFilePath, std::ios::out | std::ios::binary);
            if (!outputFile.is_open()) {
                std::cerr << "ERROR: Could not open output file: " << outputFilePath << std::endl;
            }
            else {
                // Write frames in detection order (not by frame ID)
                for (const auto& frameData : decodedFramesInOrder) {
                    for (bool b : frameData) {
                        outputFile << (b ? '1' : '0');
                    }
                }

                outputFile.flush();
                outputFile.close();

                std::cout << "[OK] Decoded data saved to: " << outputFilePath << std::endl;
            }
        }

        DemodulationSummary summary;
        summary.detectionAttempts = detectionAttempts;
        summary.validFrames = validFrames;
        summary.invalidCRC = invalidCRC;
        summary.decodedFramesInOrder = decodedFramesInOrder;
        summary.targetDetectionCountReached = (detectionAttempts >= ASK::numFrames);
        return summary;
    }
};

bool waitForChirpDetection(ChirpDetector& detector,
    SampleRingBuffer& buffer,
    double timeoutSeconds,
    double threshold) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(
        static_cast<int>(timeoutSeconds * 1000.0));
    while (std::chrono::steady_clock::now() < deadline) {
        auto snapshot = buffer.snapshot();
        if (!snapshot.empty() && detector.detectPreamble(snapshot, threshold)) {
            buffer.clear();
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

enum class ReceiverStopReason : int {
    None = 0,
    UserAbort,
    DetectionCountReached
};

void runChirpReceiverMode(juce::AudioDeviceManager& deviceManager, const juce::File& outputDir) {
    std::cout << "\n[MODE 6: RECEIVER CHIRP BEACON]\n";
    std::cout << "Recording, demodulating, and monitoring signal intensity (press ENTER to stop)." << std::endl;

    AudioRecorder recorder;
    juce::File recFile = outputDir.getChildFile("receiver_chirp_record.wav");
    recorder.startRecording(recFile, ASK::sampleRate);
    deviceManager.addAudioCallback(&recorder);

    // Prepare beacon burst (distinct tone) to signal sender
    const double noiseDurationSec = ReceiverBeaconConfig::noiseBurstDurationSec;
    const int noiseSamples = (int)(ASK::sampleRate * noiseDurationSec);
    juce::AudioBuffer<float> noiseBuffer(1, noiseSamples);
    double phase = 0.0;
    const double phaseIncrement = 2.0 * juce::MathConstants<double>::pi
        * ReceiverBeaconConfig::beaconFrequencyHz / ASK::sampleRate;
    for (int i = 0; i < noiseSamples; ++i) {
        float sample = ReceiverBeaconConfig::noiseBurstAmplitude
            * static_cast<float>(std::sin(phase));
        noiseBuffer.setSample(0, i, sample);
        phase += phaseIncrement;
    }

    // Buffers for demodulation and intensity monitoring
    SampleRingBuffer demodBuffer(static_cast<size_t>(ASK::sampleRate * 12));
    const double intensityThreshold = 0.005;
    const size_t intensityWindowSamples = static_cast<size_t>(ASK::sampleRate * 0.05);
    SampleRingBuffer intensityBuffer(intensityWindowSamples * 2);

    std::atomic<bool> stopRequested{ false };
    std::atomic<ReceiverStopReason> stopReason{ ReceiverStopReason::None };
    auto userAbortFlag = std::make_shared<std::atomic<bool>>(false);

    auto calculateSignalPower = [](const std::vector<float>& samples) -> double {
        if (samples.empty()) return 0.0;
        double sumSq = 0.0;
        for (float s : samples) sumSq += s * s;
        return sumSq / samples.size();
        };

    auto requestStop = [&](ReceiverStopReason reason) {
        stopReason.store(reason);
        stopRequested.store(true);
        };

    recorder.setSampleCallback([&demodBuffer, &intensityBuffer](const float* samples, int numSamples) {
        demodBuffer.pushSamples(samples, numSamples);
        intensityBuffer.pushSamples(samples, numSamples);
        });

    std::thread inputThread([userAbortFlag]() {
        std::cin.get();
        userAbortFlag->store(true);
        });

    FrameDemodulator demodulator(false);
    const size_t minDemodSamples = static_cast<size_t>(ASK::sampleRate * 2);
    std::thread demodThread([&]() {
        while (!stopRequested.load()) {
            if (userAbortFlag->load()) {
                requestStop(ReceiverStopReason::UserAbort);
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
            auto snapshot = demodBuffer.snapshot();
            if (snapshot.size() >= minDemodSamples) {
                juce::AudioBuffer<float> audioBuffer(1, (int)snapshot.size());
                for (size_t i = 0; i < snapshot.size(); ++i) {
                    audioBuffer.setSample(0, (int)i, snapshot[i]);
                }

                auto summary = demodulator.demodulate(
                    audioBuffer,
                    ASK::outputPath + "correlation_values_receiver_live.csv",
                    false,
                    false);

                if (summary.targetDetectionCountReached) {
                    std::cout << "[RECEIVER] " << ASK::numFrames << " chirp detections completed. Initiating shutdown." << std::endl;
                    requestStop(ReceiverStopReason::DetectionCountReached);
                    break;
                }
            }
        }
        });

    std::thread noiseThread([&]() {
        auto lastIntensityCheck = std::chrono::steady_clock::now();
        auto lastNoiseSend = std::chrono::steady_clock::now();
        auto beaconEnableTime = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(static_cast<int>(ReceiverBeaconConfig::beaconStartDelaySec * 1000.0));
        const double intensityCheckInterval = 0.02;
        const double minNoiseInterval = 0.15;

        while (!stopRequested.load()) {
            if (userAbortFlag->load()) {
                requestStop(ReceiverStopReason::UserAbort);
                break;
            }

            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration<double>(now - lastIntensityCheck).count() >= intensityCheckInterval) {
                lastIntensityCheck = now;
                auto snapshot = intensityBuffer.snapshot();
                if (snapshot.size() >= intensityWindowSamples) {
                    size_t recentCount = std::min(snapshot.size(), intensityWindowSamples);
                    std::vector<float> recentSamples(snapshot.end() - recentCount, snapshot.end());
                    double signalPower = calculateSignalPower(recentSamples);
                    if (signalPower < intensityThreshold && now >= beaconEnableTime) {
                        double timeSinceLastNoise = std::chrono::duration<double>(now - lastNoiseSend).count();
                        if (timeSinceLastNoise >= minNoiseInterval) {
                            std::cout << "[RECEIVER] Signal intensity dropped, sending noise ping." << std::endl;
                            playBufferBlocking(deviceManager, noiseBuffer);
                            lastNoiseSend = now;
                        }
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        });

    // Wait for stop request (frame 500 or user)
    while (!stopRequested.load()) {
        if (userAbortFlag->load()) {
            requestStop(ReceiverStopReason::UserAbort);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    stopRequested.store(true);

    if (inputThread.joinable()) {
        if (userAbortFlag->load()) {
            inputThread.join();
        }
        else {
            inputThread.detach();
        }
    }

    if (demodThread.joinable()) demodThread.join();
    if (noiseThread.joinable()) noiseThread.join();

    auto finalReason = stopReason.load();
    if (finalReason == ReceiverStopReason::DetectionCountReached) {
        std::cout << "[RECEIVER] All " << ASK::numFrames << " chirp detections completed. Performing final demodulation..." << std::endl;
    }
    else if (finalReason == ReceiverStopReason::UserAbort) {
        std::cout << "[RECEIVER] User requested stop. Performing final demodulation..." << std::endl;
    }
    else {
        std::cout << "[RECEIVER] Stop condition met. Performing final demodulation..." << std::endl;
    }

    auto finalSnapshot = demodBuffer.snapshot();
    if (!finalSnapshot.empty()) {
        juce::AudioBuffer<float> finalAudio(1, (int)finalSnapshot.size());
        for (size_t i = 0; i < finalSnapshot.size(); ++i) {
            finalAudio.setSample(0, (int)i, finalSnapshot[i]);
        }
        demodulator.demodulate(finalAudio,
            ASK::outputPath + "correlation_values_receiver.csv",
            true,
            true);
    }

    recorder.stop();
    deviceManager.removeAudioCallback(&recorder);
    std::cout << "[RECEIVER] Chirp beacon stopped. Recording saved to: "
        << recFile.getFullPathName() << std::endl;
}

// ==============================================================================
//  MAIN APPLICATION WITH INTERACTIVE MODES
// ==============================================================================
int main(int argc, char* argv[]) {
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::cout << "\n╔════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║         ASK MODULATION/DEMODULATION WITH CHIRP DETECTION      ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════════╝\n";

    // Ensure output directory exists
    juce::File outputDir(ASK::outputPath);
    if (!outputDir.exists()) {
        auto result = outputDir.createDirectory();
        if (result.failed()) {
            std::cerr << "ERROR: Could not create output directory: " << ASK::outputPath << std::endl;
            std::cin.get();
            return 1;
        }
        std::cout << "Created output directory: " << ASK::outputPath << std::endl;
    }

    // Setup audio devices (once at startup)
    juce::AudioDeviceManager deviceManager;
    auto result = deviceManager.initialiseWithDefaultDevices(1, 2);
    if (result.isNotEmpty()) std::cerr << "WARNING: " << result << std::endl;

    auto* device = deviceManager.getCurrentAudioDevice();
    if (device != nullptr) {
        std::cout << "Audio device initialized" << std::endl;
        std::cout << "  Sample rate: " << device->getCurrentSampleRate() << " Hz" << std::endl;
    }

    // Read input data (once at startup)
    std::cout << "\nReading input data..." << std::endl;
    std::vector<bool> inputData = FileIO::readInputFile(ASK::inputPath);
    if (inputData.empty()) {
        std::cerr << "ERROR: No data read from input file!" << std::endl;
        std::cin.get();


    }

    // Generate modulated signal (once at startup)
    std::cout << "Modulating data into frames..." << std::endl;
    juce::AudioBuffer<float> modulatedSignal = Modulator::modulateData(inputData);
    std::cout << "Modulated signal generated: " << modulatedSignal.getNumSamples() << " samples ";
    std::cout << "(" << (modulatedSignal.getNumSamples() / ASK::sampleRate) << " seconds)" << std::endl;
    saveWavFile(modulatedSignal, outputDir.getChildFile("generated_signal.wav"), modulatedSignal.getNumSamples());

    // Interactive loop
    bool continueRunning = true;
    while (continueRunning) {
        std::cout << "\n" << std::string(60, '=') << std::endl;
        std::cout << "SELECT MODE:" << std::endl;
        std::cout << "  1. Play only (transmit)" << std::endl;
        std::cout << "  2. Record only (receive)" << std::endl;
        std::cout << "  3. Play and Record (simultaneous)" << std::endl;
        std::cout << "  4. Demodulate existing recording" << std::endl;
        std::cout << "  5. Sender (handshake every 5s)" << std::endl;
        std::cout << "  6. Receiver (chirp beacon)" << std::endl;
        std::cout << "  0. Exit" << std::endl;
        std::cout << "Enter choice: ";

        int choice;
        std::cin >> choice;
        std::cin.ignore(); // Clear newline

        if (choice == 0) {
            continueRunning = false;
            continue;
        }

        switch (choice) {
        case 1: { // Play only
            std::cout << "\n[MODE 1: PLAY ONLY]" << std::endl;
            std::cout << "Press ENTER to start transmission..." << std::endl;
            std::cin.get();

            AudioPlayer player(modulatedSignal);
            deviceManager.addAudioCallback(&player);

            std::cout << "... TRANSMITTING ...\nPress ENTER to stop." << std::endl;
            std::cin.get();

            deviceManager.removeAudioCallback(&player);
            std::cout << "Transmission stopped." << std::endl;
            break;
        }

        case 2: { // Record only
            std::cout << "\n[MODE 2: RECORD ONLY]" << std::endl;
            juce::File recordedFile = outputDir.getChildFile("recorded_signal_mode2.wav");

            std::cout << "Press ENTER to start recording..." << std::endl;
            std::cin.get();

            AudioRecorder recorder;
            recorder.startRecording(recordedFile, ASK::sampleRate);
            deviceManager.addAudioCallback(&recorder);

            std::cout << "... RECORDING ...\nPress ENTER to stop." << std::endl;
            std::cin.get();

            recorder.stop();
            deviceManager.removeAudioCallback(&recorder);
            juce::Thread::sleep(500);

            // Load and analyze
            juce::AudioBuffer<float> recordedAudio;
            if (loadWavFile(recordedAudio, recordedFile)) {
                analyzeRecordingDiagnostics(recordedAudio);

                std::cout << "\nDemodulate this recording? (y/n): ";
                char response;
                std::cin >> response;
                std::cin.ignore();

                if (response == 'y' || response == 'Y') {
                    std::cout << "Use verbose output? (y/n): ";
                    char verboseResp;
                    std::cin >> verboseResp;
                    std::cin.ignore();

                    FrameDemodulator demodulator(verboseResp == 'y' || verboseResp == 'Y');
                    demodulator.demodulate(recordedAudio, ASK::outputPath + "correlation_values_mode2.csv");
                }
            }
            break;
        }

        case 3: { // Play and Record
            std::cout << "\n[MODE 3: PLAY AND RECORD]" << std::endl;
            juce::File recordedFile = outputDir.getChildFile("recorded_signal_mode3.wav");

            std::cout << "Press ENTER to start..." << std::endl;
            std::cin.get();

            AudioRecorder recorder;
            AudioPlayer player(modulatedSignal);

            recorder.startRecording(recordedFile, ASK::sampleRate);
            deviceManager.addAudioCallback(&recorder);
            deviceManager.addAudioCallback(&player);

            std::cout << "... TRANSMITTING AND RECORDING ...\nPress ENTER to stop." << std::endl;
            std::cin.get();

            recorder.stop();
            deviceManager.removeAudioCallback(&player);
            deviceManager.removeAudioCallback(&recorder);
            juce::Thread::sleep(500);

            // Load and analyze
            juce::AudioBuffer<float> recordedAudio;
            if (loadWavFile(recordedAudio, recordedFile)) {
                analyzeRecordingDiagnostics(recordedAudio);

                std::cout << "\nDemodulate this recording? (y/n): ";
                char response;
                std::cin >> response;
                std::cin.ignore();

                if (response == 'y' || response == 'Y') {
                    std::cout << "Use verbose output? (y/n): ";
                    char verboseResp;
                    std::cin >> verboseResp;
                    std::cin.ignore();

                    FrameDemodulator demodulator(verboseResp == 'y' || verboseResp == 'Y');
                    demodulator.demodulate(recordedAudio, ASK::outputPath + "correlation_values_mode3.csv");
                }
            }
            break;
        }

        case 4: { // Demodulate existing
            std::cout << "\n[MODE 4: DEMODULATE EXISTING RECORDING]" << std::endl;
            std::cout << "Enter WAV file path (or press ENTER for default recorded_signal.wav): ";
            std::string path;
            std::getline(std::cin, path);

            if (path.empty()) {
                path = ASK::outputPath + "recorded_signal.wav";
            }

            juce::File audioFile(path);
            juce::AudioBuffer<float> recordedAudio;

            if (loadWavFile(recordedAudio, audioFile)) {
                analyzeRecordingDiagnostics(recordedAudio);

                std::cout << "\nUse verbose output? (y/n): ";
                char verboseResp;
                std::cin >> verboseResp;
                std::cin.ignore();

                FrameDemodulator demodulator(verboseResp == 'y' || verboseResp == 'Y');
                demodulator.demodulate(recordedAudio, ASK::outputPath + "correlation_values_mode4.csv");
            }
            break;
        }

        case 5: {
            runLinkMonitoredSender(deviceManager, inputData, outputDir);
            break;
        }

        case 6: {
            runChirpReceiverMode(deviceManager, outputDir);
            break;
        }

        default:
            std::cout << "Invalid choice. Please try again." << std::endl;
            break;
        }

        // Ask if user wants to continue
        if (continueRunning && choice >= 1 && choice <= 6) {
            std::cout << "\nContinue with another operation? (y/n): ";
            char response;
            std::cin >> response;
            std::cin.ignore();
            continueRunning = (response == 'y' || response == 'Y');
        }
    }

    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "EXIT COMPLETE" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    std::cout << "Files saved to: " << ASK::outputPath << std::endl;

    return 0;
}


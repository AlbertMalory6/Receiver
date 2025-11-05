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
 
  // ==============================================================================
  //  CONFIGURATION
  // ==============================================================================
 #define USE_NCC_DETECTION 1 // 1 = NCC, 0 = Dot Product
 #define USE_RECORDED_AUDIO 0
  // 1 = Play from audio_path, 0 = Play generated signal
 
 
 namespace ASK {
     constexpr double sampleRate = 44100.0;  // Match MATLAB
     constexpr int preambleSamples = 440;
 
     // Chirp parameters (2kHz-10kHz sweep, matching MATLAB)
     constexpr double chirp_f_start = 2000.0;   // 10kHz - 8kHz
     constexpr double chirp_f_end = 10000.0;     // 10kHz
 
     // Modulation parameters
     constexpr double carrierFreq = 10000.0;     // 10 kHz carrier
     constexpr int samplesPerBit = 44;            // Baud rate ~1000 bps
     constexpr int bitsPerFrame = 108;            // 8 ID + 100 data + 8 CRC
     constexpr int dataBitsPerFrame = 100;
     constexpr int idBitsPerFrame = 8;
     constexpr int crcBitsPerFrame = 8;
     constexpr int numFrames = 100;
 
     // CRC polynomial: x^8+x^7+x^5+x^2+x+1 = 0xD5 = 0b11010101
     constexpr uint8_t crcPolynomial = 0xD5;
 
     // Input/Output paths
     const std::string inputPath = "INPUT.txt";
     const std::string outputPath = "D:\\fourth_year\\cs120\\debug_pic\\ASK\\";
     const std::string audio_path = "D:\\fourth_year\\cs120\\debug_pic\\ASK\\recorded_signal.wav";
 
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
 
     /** Generates carrier signal */
     static juce::AudioBuffer<float> generateCarrier(int numSamples) {
         juce::AudioBuffer<float> carrier(1, numSamples);
         auto* signal = carrier.getWritePointer(0);
 
         double phase = 0.0;
         double phaseIncrement = 2.0 * juce::MathConstants<double>::pi * ASK::carrierFreq / ASK::sampleRate;
 
         for (int i = 0; i < numSamples; ++i) {
             signal[i] = std::sin(phase);
             phase += phaseIncrement;
             if (phase > 2.0 * juce::MathConstants<double>::pi) {
                 phase -= 2.0 * juce::MathConstants<double>::pi;
             }
         }
 
         return carrier;
     }
 
     /** Generates silence */
     static juce::AudioBuffer<float> generateSilence(int numSamples) {
         juce::AudioBuffer<float> silence(1, numSamples);
         silence.clear();
         return silence;
     }
 };
 
 // ==============================================================================
 //  FILE I/O
 // ==============================================================================
 class FileIO {
 public:
     static std::vector<bool> readInputFile(const std::string& filename) {
         std::vector<bool> data;
         std::ifstream file(filename);
 
         if (!file.is_open()) {
             std::cerr << "ERROR: Could not open input file: " << filename << std::endl;
             return data;
         }
 
         std::string line;
         while (std::getline(file, line)) {
             for (char c : line) {
                 if (c == '0') {
                     data.push_back(false);
                 }
                 else if (c == '1') {
                     data.push_back(true);
                 }
             }
         }
 
         file.close();
 
         // Limit to max data size (100 frames * 100 bits)
         int maxDataSize = ASK::numFrames * ASK::dataBitsPerFrame;
         if ((int)data.size() > maxDataSize) {
             data.resize(maxDataSize);
             std::cout << "WARNING: Data truncated to " << maxDataSize << " bits" << std::endl;
         }
 
         std::cout << "Read " << data.size() << " bits from " << filename << std::endl;
         return data;
     }
 };
 
 // ==============================================================================
 //  MODULATOR
 // ==============================================================================
 class Modulator {
 public:
     static juce::AudioBuffer<float> modulateData(const std::vector<bool>& inputData) {
         // Generate preamble and carrier templates
         auto preamble = SignalGenerator::generatePreamble();
         auto carrier = SignalGenerator::generateCarrier(ASK::bitsPerFrame * ASK::samplesPerBit);
 
         // Initialize frames: 100 frames, each 100 bits
         std::vector<std::vector<bool>> frames(ASK::numFrames, std::vector<bool>(ASK::dataBitsPerFrame, false));
 
         // Fill frames with input data
         for (int i = 0; i < ASK::numFrames && (i * ASK::dataBitsPerFrame) < (int)inputData.size(); ++i) {
             int dataStart = i * ASK::dataBitsPerFrame;
             for (int j = 0; j < ASK::dataBitsPerFrame && (dataStart + j) < (int)inputData.size(); ++j) {
                 frames[i][j] = inputData[dataStart + j];
             }
         }
 
         // Build complete frames with ID and CRC
         CRC8Generator crcGen(ASK::crcPolynomial);
         std::vector<float> outputSignal;
         std::mt19937 rng(1); // Seed = 1 (magic number from MATLAB)
 
         for (int frameIdx = 0; frameIdx < ASK::numFrames; ++frameIdx) {
             // Build frame: [8-bit ID] + [100-bit data]
             std::vector<bool> frame(ASK::idBitsPerFrame + ASK::dataBitsPerFrame);
 
             // Set frame ID (8 bits, binary representation of frameIdx+1)
             int frameId = frameIdx + 1;
             for (int i = 0; i < ASK::idBitsPerFrame; ++i) {
                 frame[i] = (frameId >> (ASK::idBitsPerFrame - 1 - i)) & 1;
             }
 
             // Add data bits
             for (int i = 0; i < ASK::dataBitsPerFrame; ++i) {
                 frame[ASK::idBitsPerFrame + i] = frames[frameIdx][i];
             }
 
             // Add CRC
             std::vector<bool> frameWithCRC = crcGen.generate(frame);
 
             // Modulate frame: each bit becomes 44 samples
             std::vector<float> frameWave(ASK::bitsPerFrame * ASK::samplesPerBit);
             for (int bitIdx = 0; bitIdx < ASK::bitsPerFrame; ++bitIdx) {
                 bool bitValue = frameWithCRC[bitIdx];
                 int waveStart = bitIdx * ASK::samplesPerBit;
 
                 // Modulate: bit=1 -> +carrier, bit=0 -> -carrier
                 for (int j = 0; j < ASK::samplesPerBit; ++j) {
                     int carrierIdx = (waveStart + j) % carrier.getNumSamples();
                     frameWave[waveStart + j] = carrier.getSample(0, carrierIdx) * (bitValue ? 1.0f : -1.0f);
                 }
             }
 
             // Add random inter-frame spacing (0-100 samples)
             int spacingBefore = rng() % 101;
             outputSignal.insert(outputSignal.end(), spacingBefore, 0.0f);
 
             // Add preamble
             for (int i = 0; i < preamble.getNumSamples(); ++i) {
                 outputSignal.push_back(preamble.getSample(0, i));
             }
 
             // Add modulated frame
             outputSignal.insert(outputSignal.end(), frameWave.begin(), frameWave.end());
 
             // Add random spacing after frame
             int spacingAfter = rng() % 101;
             outputSignal.insert(outputSignal.end(), spacingAfter, 0.0f);
         }
 
         // Convert to AudioBuffer
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
 
 #if USE_NCC_DETECTION
         logFile << "Sample_Index,NCC_Value\n";
 #else
         logFile << "Sample_Index,Dot_Product_Value\n";
 #endif
 
         double maxScore = -1e10;
         int maxPos = -1;
 
         for (int i = 0; i <= sigLen - tempLen; ++i) {
             double dotProduct = 0.0;
             double signalEnergy = 0.0;
             double score = 0.0;
 
             for (int j = 0; j < tempLen; ++j) {
                 dotProduct += sigData[i + j] * tempData[j];
 #if USE_NCC_DETECTION
                 signalEnergy += sigData[i + j] * sigData[i + j];
 #endif
             }
 
 #if USE_NCC_DETECTION
             if (signalEnergy < 1e-10 || templateEnergy_ < 1e-10) {
                 score = 0.0;
             }
             else {
                 score = dotProduct / std::sqrt(signalEnergy * templateEnergy_);
             }
 #else
             score = dotProduct;
 #endif
 
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
 #if USE_NCC_DETECTION
         std::cout << "  Max NCC Score: " << maxScore << " at sample " << maxPos << std::endl;
 #else
         std::cout << "  Max Dot Product: " << maxScore << " at sample " << maxPos << std::endl;
 #endif
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

    double calculateNCC(const float* signal, const float* template_, int len, double templateEnergy) {
        double dotProduct = 0.0;
        double signalEnergy = 0.0;
        for (int i = 0; i < len; ++i) {
            dotProduct += signal[i] * template_[i];
            signalEnergy += signal[i] * signal[i];
        }
        if (signalEnergy < 1e-10 || templateEnergy < 1e-10) return 0.0;
        return dotProduct / std::sqrt(signalEnergy * templateEnergy);
    }

    std::vector<bool> demodulateFrame(const float* frameData, int frameSamples) {
        auto carrier = SignalGenerator::generateCarrier(frameSamples);
        std::vector<float> decoded(frameSamples);
        for (int i = 0; i < frameSamples; ++i) {
            decoded[i] = frameData[i] * carrier.getSample(0, i % carrier.getNumSamples());
        }
        // Smooth (moving average)
        std::vector<float> smoothed(frameSamples);
        for (int i = 0; i < frameSamples; ++i) {
            double sum = 0.0; int count = 0;
            for (int j = -5; j <= 5; ++j) {
                int idx = i + j;
                if (idx >= 0 && idx < frameSamples) { sum += decoded[idx]; count++; }
            }
            smoothed[i] = (float)(sum / count);
        }
        // Extract bits
        std::vector<bool> bits(ASK::bitsPerFrame);
        for (int bitIdx = 0; bitIdx < ASK::bitsPerFrame; ++bitIdx) {
            int bitStart = 10 + bitIdx * ASK::samplesPerBit;
            int bitEnd = 30 + bitIdx * ASK::samplesPerBit;
            double bitPower = 0.0;
            for (int j = bitStart; j < bitEnd && j < frameSamples; ++j) bitPower += smoothed[j];
            bits[bitIdx] = (bitPower > 0.0);
        }
        return bits;
    }

public:
    FrameDemodulator(bool verbose = false) : crcGen_(ASK::crcPolynomial), verboseOutput_(verbose) {
        preambleTemplate_ = SignalGenerator::generatePreamble();
        preambleEnergy_ = calculateEnergy(preambleTemplate_.getReadPointer(0), preambleTemplate_.getNumSamples());
    }

    void setVerbose(bool verbose) { verboseOutput_ = verbose; }

    void demodulate(const juce::AudioBuffer<float>& signal, const std::string& nccLogPath) {
        const float* sigData = signal.getReadPointer(0);
        const float* tempData = preambleTemplate_.getReadPointer(0);
        const int sigLen = signal.getNumSamples();
        const int tempLen = preambleTemplate_.getNumSamples();

        // Detect and decode frames using dot product like MATLAB
        std::cout << "\nDetecting preambles and decoding frames (dot product method)...\n";
        double power = 0.0, syncPowerLocalMax = 0.0;
        int startIndex = 0, state = 0;
        std::vector<float> syncFIFO(tempLen, 0.0f);
        std::vector<float> decodeFIFO;
        std::vector<int> detectedPositions, decodedFrameIds;

        // Store decoded frame data for output file
        std::map<int, std::vector<bool>> decodedFrames;

        int frameSamples = ASK::bitsPerFrame * ASK::samplesPerBit;
        int expectedInterval = ASK::preambleSamples + frameSamples + 200;

        // Counters for summary
        int detectionAttempts = 0;
        int validFrames = 0;
        int invalidCRC = 0;
        int invalidID = 0;

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

                if (syncPower > 0.1 && syncPower > syncPowerLocalMax) { // 0  &&  && syncPower > power / 8.0 
                    syncPowerLocalMax = syncPower; startIndex = i;
                    if (verboseOutput_) {
                        //print local syncPower to tune threshold
                        std::cout << "[Detection] SyncPower at sample " << i << ": " << syncPower 
                                  << " (power*2.0=" << power * 2.0 << ")" << std::endl;
                    }
                }
                else if ((i - startIndex > 400) && (startIndex != 0)) { //
                    detectedPositions.push_back(startIndex);
                    detectionAttempts++;
                    if (verboseOutput_) {
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
                    
                    // Extract frame ID
                    int frameId = 0;
                    for (int j = 0; j < ASK::idBitsPerFrame; ++j) frameId = (frameId << 1) | (bits[j] ? 1 : 0);

                    // Check CRC
                    std::vector<bool> dataBits(bits.begin(), bits.begin() + ASK::idBitsPerFrame + ASK::dataBitsPerFrame);
                    bool crcValid = crcGen_.check(dataBits);

                    // Always show result in verbose mode
                    if (verboseOutput_) {
                        std::cout << "  Frame ID: " << frameId 
                                  << " | CRC: " << (crcValid ? "VALID" : "INVALID")
                                  << " | Data bits: ";
                        // Print first 20 data bits for preview
                        for (int j = 0; j < std::min(100, ASK::dataBitsPerFrame); ++j) {
                            std::cout << (bits[ASK::idBitsPerFrame + j] ? '1' : '0');
                        }
                        //if (ASK::dataBitsPerFrame > 20) std::cout << "...";
                        std::cout << std::endl;
                    }

                    // Categorize result
                    if (frameId > 0 && frameId <= ASK::numFrames) { // && crcValid
                        validFrames++;
                        decodedFrameIds.push_back(frameId);
                        std::vector<bool> frameData(bits.begin() + ASK::idBitsPerFrame,
                            bits.begin() + ASK::idBitsPerFrame + ASK::dataBitsPerFrame);
                        decodedFrames[frameId] = frameData;
                        
                        if (!verboseOutput_) {
                            std::cout << "Frame " << frameId << " decoded successfully" << std::endl;
                        }
                    } else if (frameId <= 0 || frameId > ASK::numFrames) {
                        invalidID++;
                    } else if (!crcValid) {
                        invalidCRC++;
                    }

                    startIndex = 0; decodeFIFO.clear(); state = 0;
                }
            }
        }

        std::cout << "\n[Demodulation Summary]" << std::endl;
        std::cout << "  Total detection attempts: " << detectionAttempts << std::endl;
        std::cout << "  Valid frames: " << validFrames << " / " << ASK::numFrames << std::endl;
        std::cout << "  Invalid CRC: " << invalidCRC << std::endl;
        std::cout << "  Invalid ID: " << invalidID << std::endl;
        std::cout << "  Success rate: " << std::fixed << std::setprecision(2) 
                  << (100.0 * validFrames / ASK::numFrames) << "%" << std::endl;

        // Write decoded data to OUTPUT.txt
        std::string outputFilePath = ASK::outputPath + "OUTPUT.txt";
        std::ofstream outputFile(outputFilePath);
        if (!outputFile.is_open()) {
            std::cerr << "ERROR: Could not open output file: " << outputFilePath << std::endl;
            return;
        }

        // Write frames in order (1 to numFrames)
        for (int i = 1; i <= ASK::numFrames; ++i) {
            if (decodedFrames.find(i) != decodedFrames.end()) {
                for (bool b : decodedFrames[i]) {
                    outputFile << (b ? '1' : '0');
                }
            }
        }
        outputFile.close();

        std::cout << "✓ Decoded data saved to: " << outputFilePath << std::endl;
    }
};
 
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
        return 1;
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
                    demodulator.demodulate(recordedAudio, ASK::outputPath + "ncc_values_mode2.csv");
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
                    demodulator.demodulate(recordedAudio, ASK::outputPath + "ncc_values_mode3.csv");
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
                demodulator.demodulate(recordedAudio, ASK::outputPath + "ncc_values_mode4.csv");
            }
            break;
        }

        default:
            std::cout << "Invalid choice. Please try again." << std::endl;
            break;
        }

        // Ask if user wants to continue
        if (continueRunning && choice >= 1 && choice <= 4) {
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
 
 
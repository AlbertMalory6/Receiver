/*
  ==============================================================================
    Minimal JUCE application for simultaneous audio I/O and replay.
  ==============================================================================
*/

#include <JuceHeader.h>
#include <combaseapi.h>

#define PI acos(-1)

using namespace juce;

// 1. AudioRecorder Class: Now includes a flag to control tone generation.
class AudioRecorder : public AudioIODeviceCallback
{
public:
    AudioRecorder() { backgroundThread.startThread(); }

    ~AudioRecorder() override { stop(); }

    // Updated startRecording to accept a flag for tone generation
    void startRecording(const File& file, bool generateTone)
    {
        stop();
        if (sampleRate <= 0) return;

        file.deleteFile();
        shouldGenerateTone = generateTone;

        if (auto fileStream = std::unique_ptr<FileOutputStream>(file.createOutputStream()))
        {
            WavAudioFormat wavFormat;

            if (auto writer = wavFormat.createWriterFor(fileStream.get(), sampleRate, 1, 16, {}, 0))
            {
                fileStream.release();
                threadedWriter.reset(new AudioFormatWriter::ThreadedWriter(writer, backgroundThread, 32768));
                nextSampleNum = 0;

                {
                    const ScopedLock sl(writerLock);
                    activeWriter = threadedWriter.get();
                }
                std::cout << "--- Recording Started to: " << file.getFileName()
                    << (shouldGenerateTone ? " (with sine tone playback)" : " (input only)") << " ---\n";
            }
        }
    }

    void stop()
    {
        {
            const ScopedLock sl(writerLock);
            activeWriter = nullptr;
        }
        threadedWriter.reset();
        std::cout << "--- Recording Stopped. ---\n";
    }

    bool isRecording() const { return activeWriter.load() != nullptr; }

    void audioDeviceAboutToStart(AudioIODevice* device) override
    {
        sampleRate = device->getCurrentSampleRate();
    }

    void audioDeviceStopped() override { sampleRate = 0; }

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
        int numInputChannels,
        float* const* outputChannelData,
        int numOutputChannels,
        int numSamples,
        const AudioIODeviceCallbackContext& context) override
    {
        ignoreUnused(context);
        const ScopedLock sl(writerLock);

        // Input/Recording Logic
        if (activeWriter.load() != nullptr && numInputChannels >= 1)
        {
            activeWriter.load()->write(inputChannelData, numSamples);
            nextSampleNum += numSamples;
        }

        // Output/Playback Logic: ONLY run if the flag is set (for Objective 2)
        if (shouldGenerateTone && numOutputChannels > 0 && outputChannelData[0] != nullptr)
        {
            double currentSampleRate = sampleRate > 0 ? sampleRate : 48000.0;
            dPhasePerSample1 = 2 * PI * ((float)freq1 / (float)currentSampleRate);
            dPhasePerSample2 = 2 * PI * ((float)freq2 / (float)currentSampleRate);

            for (int i = 0; i < numSamples; i++) {
                Phase1 += dPhasePerSample1;
                Phase2 += dPhasePerSample2;
                data = 0.6f * std::sin(Phase1) + 0.1f * std::sin(Phase2);

                if (Phase1 >= 2 * PI) Phase1 -= 2 * PI;
                if (Phase2 >= 2 * PI) Phase2 -= 2 * PI;

                outputChannelData[0][i] = data;
            }
            for (int ch = 1; ch < numOutputChannels; ++ch)
                FloatVectorOperations::clear(outputChannelData[ch], numSamples);
        }
        else if (numOutputChannels > 0) // Ensure silence if not generating tone
        {
            for (int ch = 0; ch < numOutputChannels; ++ch)
                if (outputChannelData[ch] != nullptr)
                    FloatVectorOperations::clear(outputChannelData[ch], numSamples);
        }
    }

private:
    TimeSliceThread backgroundThread{ "Audio Recorder Thread" };
    std::unique_ptr<AudioFormatWriter::ThreadedWriter> threadedWriter;
    CriticalSection writerLock;
    std::atomic<AudioFormatWriter::ThreadedWriter*> activeWriter{ nullptr };
    bool shouldGenerateTone = false; // NEW FLAG

    float dPhasePerSample1 = 0;
    float dPhasePerSample2 = 0;
    int freq1 = 1000;
    int freq2 = 10000;
    float Phase1 = 0;
    float Phase2 = 0;
    float data;
    double sampleRate = 44100;
    int64 nextSampleNum = 0;
};

// 2. Playback Callback Wrapper
class PlaybackWrapper : public AudioIODeviceCallback
{
public:
    PlaybackWrapper(AudioTransportSource& source) : transportSource(source) {}

    void audioDeviceAboutToStart(AudioIODevice* device) override
    {
        transportSource.prepareToPlay(device->getCurrentBufferSizeSamples(), device->getCurrentSampleRate());
    }

    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
        int numInputChannels,
        float* const* outputChannelData,
        int numOutputChannels,
        int numSamples,
        const AudioIODeviceCallbackContext& context) override
    {
        ignoreUnused(inputChannelData, numInputChannels, context);

        AudioBuffer<float> outputBuffer(outputChannelData, numOutputChannels, numSamples);
        AudioSourceChannelInfo info(outputBuffer);

        transportSource.getNextAudioBlock(info);
    }

private:
    AudioTransportSource& transportSource;
};

// ==============================================================================
// 3. Main function logic
// ==============================================================================
int main(int argc, char* argv[])
{
    ScopedJuceInitialiser_GUI juce_init;

    // --- Setup ---
    AudioDeviceManager dev_manager;
    dev_manager.initialiseWithDefaultDevices(1, 1);

    AudioDeviceManager::AudioDeviceSetup dev_info = dev_manager.getAudioDeviceSetup();
    dev_info.sampleRate = 48000;
    dev_manager.setAudioDeviceSetup(dev_info, false);
    double currentSampleRate = dev_manager.getAudioDeviceSetup().sampleRate;

    AudioFormatManager formatManager;
    formatManager.registerBasicFormats();
    std::unique_ptr<AudioRecorder> audioRecorder(new AudioRecorder());

    // Playback components for Replay
    AudioTransportSource transportSource;
    std::unique_ptr<AudioFormatReader> currentReader;
    std::unique_ptr<AudioFormatReaderSource> currentSource;
    PlaybackWrapper playbackWrapper(transportSource);

    auto parentDir = File::getSpecialLocation(File::tempDirectory);
    File recordingFileObj2 = parentDir.getNonexistentChildFile("CS120_Obj2", ".wav");
    File recordingFileObj1 = parentDir.getNonexistentChildFile("CS120_Obj1", ".wav");
   // --- Helper Lambda for Replay ---
    auto replayAudio = [&](const File& file)
        {
            std::cout << "\n--- Replaying Recorded Sound ---\n";
            currentReader.reset(formatManager.createReaderFor(file));
            if (currentReader == nullptr)
            {
                std::cout << "Error: Could not read file for replay.\n";
                return;
            }

            // --- Output directory ---
            File outputDir("D:\\fourth_year\\cs120\\record_data\\chirp_debug\\wav_csv");
            if (!outputDir.exists())
            {
                bool created = outputDir.createDirectory();
                if (!created)
                {
                    std::cerr << "Error: Could not create output directory at " << outputDir.getFullPathName() << "\n";
                    return;
                }
            }

            // --- Create output file name ---
            File playedCopy = outputDir.getChildFile(file.getFileNameWithoutExtension() + "_Played.wav");
            WavAudioFormat wavFormat;
            std::unique_ptr<FileOutputStream> playedStream(playedCopy.createOutputStream());
            std::unique_ptr<AudioFormatWriter> playedWriter;
            if (playedStream != nullptr)
                playedWriter.reset(wavFormat.createWriterFor(playedStream.get(), currentSampleRate, 1, 16, {}, 0));

            currentSource.reset(new AudioFormatReaderSource(currentReader.get(), true));
            transportSource.setSource(currentSource.get(), 0, nullptr, currentSampleRate);
            dev_manager.addAudioCallback(&playbackWrapper); // Add wrapper for playback

            transportSource.start();

            const double bufferSizeSec = 0.1;
            const int bufferSamples = int(currentSampleRate * bufferSizeSec);
            AudioBuffer<float> capture(1, bufferSamples);

            while (transportSource.isPlaying() && transportSource.getCurrentPosition() < 10.0)
            {
                AudioSourceChannelInfo info(capture);
                transportSource.getNextAudioBlock(info);

                // Write playback samples directly to file
                if (playedWriter)
                    playedWriter->writeFromAudioSampleBuffer(capture, 0, bufferSamples);

                Thread::sleep(100);
            }

            transportSource.stop();
            transportSource.setSource(nullptr);
            dev_manager.removeAudioCallback(&playbackWrapper);

            std::cout << "--- Playback Finished ---\n";

            if (playedWriter)
            {
                std::cout << "Saved played audio to: " << playedCopy.getFullPathName() << "\n";
                playedWriter.reset();
            }
        };


    // Objective 2: Simultaneous Play/Record (10s) - Tone generation is ON
    std::cout << "\n--- Objective 2: Simultaneous Play/Record ---\n";
    std::cout << "Press ENTER to start recording (Sine wave will play).\n";
    getchar();

    dev_manager.addAudioCallback(audioRecorder.get());
    audioRecorder->startRecording(recordingFileObj2, true); // Tone ON

    std::cout << "Recording for 10s (Press ENTER to stop early)...\n";
    for (int i = 0; i < 100 && audioRecorder->isRecording(); ++i) Thread::sleep(100);

    audioRecorder->stop();
    dev_manager.removeAudioCallback(audioRecorder.get());
    if (recordingFileObj2.existsAsFile()) replayAudio(recordingFileObj2);

    // Objective 1: Record TA's voice for 10 seconds and replay - Tone generation is OFF
    std::cout << "\n--- Objective 1: Record and Replay TA's Voice ---\n";
    std::cout << "Press ENTER to start recording (Just speak into mic).\n";
    getchar();

    dev_manager.addAudioCallback(audioRecorder.get());
    audioRecorder->startRecording(recordingFileObj1, false); // Tone OFF

    std::cout << "Recording for 10s (Press ENTER to stop early)...\n";
    for (int i = 0; i < 100 && audioRecorder->isRecording(); ++i) Thread::sleep(100);

    audioRecorder->stop();
    dev_manager.removeAudioCallback(audioRecorder.get());
    if (recordingFileObj1.existsAsFile()) replayAudio(recordingFileObj1);

    // --- Terminate ---
    std::cout << "\nAll objectives complete. Press ENTER to quit.\n";
    getchar();

    return 0;
}
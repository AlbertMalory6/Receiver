# Chirp Detection Standalone Test

This directory contains a standalone chirp detection test that validates the chirp preamble detection logic.

## Overview

The test performs the following steps:
1. **Generate** a test signal with 0.5s silence + click + chirp (500-4000-500 Hz) + click
2. **Save** the original generated signal
3. **Play** the signal and **record** it simultaneously using ASIO
4. **Save** the recorded signal
5. **Analyze** the recording by computing NCC or Dot Product correlation scores throughout
6. **Save** correlation data to CSV
7. **Visualize** results with the Python plotting script

## Files

### C++ Program
- **`chirp_test_standalone.cpp`** - Main standalone test program
  - Located in root directory: `D:\fourth_year\cs120\chirp_test_standalone.cpp`

### Python Visualization
- **`plot_chirp_analysis.py`** - Plots waveform and correlation scores

### Output Files
All generated files are saved to: `D:\fourth_year\cs120\debug_pic\chirp\`

1. **`generated_chirp_signal.wav`** - Original generated signal
2. **`recorded_chirp_signal.wav`** - Recorded audio from playback
3. **`chirp_correlation_log.csv`** - Correlation scores for every sample position
4. **`chirp_detection_analysis.png`** - Visualization plot

## Building and Running

### Prerequisites
- JUCE framework (version 8.0.10 or compatible)
- Audio device with input/output capabilities
- Visual Studio 2022 (or compatible C++ compiler)

### Build Instructions
1. Create a JUCE project or add to existing project
2. Include the file `chirp_test_standalone.cpp`
3. Configure JUCE modules: Core, Audio Devicing, Audio Formats
4. Build in Release or Debug mode

### Running the Test
1. Run the compiled executable
2. Follow the on-screen prompts:
   - Press ENTER to start playback and recording
   - Ensure microphone can hear speaker
   - Press ENTER to stop recording
3. Review the console output for detection results

### Visualization
After running the C++ test, visualize results:
```bash
cd D:\fourth_year\cs120\debug_pic\chirp
python plot_chirp_analysis.py
```

## Configuration

### Chirp Parameters
- **Sample Rate:** 48000 Hz
- **Chirp Length:** 440 samples
- **Frequency Sweep:** 500 Hz → 4000 Hz → 500 Hz (triangular)
- **Amplitude:** 0.5

### Detection Method
Toggle between NCC and Dot Product detection:
```cpp
#define USE_NCC_DETECTION 1  // 1 = NCC, 0 = Dot Product
```

### Output Path
Modify the output directory in `chirp_test_standalone.cpp`:
```cpp
namespace ChirpTest {
    constexpr std::string outputPath = "D:\\fourth_year\\cs120\\debug_pic\\chirp\\";
}
```

## Expected Results

### Detection Accuracy
- **Excellent:** Within 10ms of expected location
- **Good:** Within 50ms of expected location
- **Warning:** Greater than 50ms offset

### Correlation Scores
- **NCC Detection:**
  - Normalized range: -1.0 to +1.0
  - Strong correlation: > 0.3
  - Peak correlation typically: 0.5 - 0.9

- **Dot Product Detection:**
  - Unnormalized range depends on signal energy
  - Peak indicates best match location

## Troubleshooting

### No Audio Output
- Check audio device initialization in console output
- Verify ASIO drivers are installed
- Ensure default audio devices are set correctly

### Poor Detection
- Check microphone positioning relative to speaker
- Reduce background noise
- Increase playback volume
- Verify sample rate matches (48000 Hz)

### CSV Generation Issues
- Ensure output directory exists or can be created
- Check file permissions
- Review console for error messages

## Architecture

```
┌─────────────────────────────────────────────────────┐
│              SignalGenerator                        │
│  - generateChirp()                                  │
│  - generateClick()                                  │
│  - generateSilence()                                │
└─────────────────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────┐
│  Step 1: Generate Test Signal                       │
│  [Silence] + [Click] + [Chirp] + [Click]           │
└─────────────────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────┐
│  Step 2: Save Generated Signal                      │
│  generated_chirp_signal.wav                         │
└─────────────────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────┐
│  AudioPlayer ────────────────────┐                  │
│  AudioRecorder ──────────────────┤  ASIO            │
│                                  ├─────────────────►│
└──────────────────────────────────┴──────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────┐
│  Step 3-4: Play and Record                          │
│  recorded_chirp_signal.wav                          │
└─────────────────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────┐
│              ChirpDetector                          │
│  - logCorrelation()                                 │
│    • NCC or Dot Product                             │
│    • All sample positions                           │
│    • CSV output                                     │
└─────────────────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────┐
│  Step 5-6: Analyze and Save                         │
│  chirp_correlation_log.csv                          │
└─────────────────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────┐
│  Python Visualization Script                        │
│  plot_chirp_analysis.py                             │
│  • Waveform plot                                    │
│  • Correlation overlay                              │
│  • Detection accuracy analysis                      │
└─────────────────────────────────────────────────────┘
```

## Notes

- The test uses JUCE's AudioDeviceManager for ASIO audio I/O
- Correlation calculations are done for every possible sample position
- The chirp template matches the FSK receiver's preamble detection
- Output files overwrite previous test runs
- The click markers help visually locate the chirp in the waveform


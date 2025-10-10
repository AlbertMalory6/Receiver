# Integrated Transceiver Testing Guide

## Overview

This integrated transceiver implements:
- **FSK Modulation**: Two carrier frequencies (5kHz and 10kHz) for binary transmission
- **Chirp Preamble**: 2-10kHz frequency sweep for frame synchronization
- **CRC-8 Error Detection**: Polynomial x^8 + x^7 + x^5 + x^2 + x + 1
- **Comprehensive Debugging**: Detailed output for troubleshooting

## Building the Project

### Using Visual Studio 2022

1. Open the solution file:
   ```
   Receiver\Builds\VisualStudio2022\Receiver.sln
   ```

2. Set build configuration to **Debug** or **Release**

3. Build the project (F7 or Build > Build Solution)

4. Run the executable

## Testing Modes

### Mode 1: Transmission Only
- Tests signal generation
- Verifies modulation and chirp generation
- Use this to test your speakers

### Mode 2: Reception Only
- Tests microphone input
- Verifies preamble detection
- Use this when another device is transmitting

### Mode 3: Process Recorded Audio
- Processes previously recorded audio
- Analyzes NCC values and synchronization

### Mode 4: Full Loopback Test (RECOMMENDED)
- Transmits and receives simultaneously
- Tests complete communication pipeline
- **Requires**: Speaker and microphone enabled
- **Note**: Adjust volume to avoid clipping

## Understanding the Output

### Successful Detection Example
```
=== Processing Recorded Audio ===
Recorded samples: 120000
Duration: 2.500 seconds

*** PREAMBLE DETECTED ***
Position: 5234 samples
Time: 0.109 seconds
Peak NCC value: 0.8523
Power at detection: 0.0234

Collected 5184 samples for decoding

=== Decoding Frame ===
Trying offset: 0 samples
Offset 0: CRC PASS

*** FRAME DECODED SUCCESSFULLY ***
Sync offset used: 0 samples
Data bits: 100
First 20 bits: 10110100101110010101
Bit errors: 0 / 100
Bit error rate: 0.0%
```

### Failed Detection Scenarios

#### No Preamble Detected
```
*** WARNING: No preamble detected! ***
Max NCC value encountered: 0.032
Average power: 0.001234
Maximum NCC in entire recording: 0.045
Threshold used: NCC > power*2 AND NCC > 0.05
```

**Possible causes:**
- Volume too low → Increase speaker volume
- Microphone muted → Check system settings
- Poor speaker/microphone quality
- Room acoustics (too much echo/noise)

**Solutions:**
- Test with headphones (speaker) and headset mic
- Move speaker closer to microphone
- Reduce background noise
- Check `ncc_values.txt` for NCC patterns

#### CRC Failure
```
*** FRAME DECODE FAILED ***
CRC check failed for all sync offsets
Possible issues:
  - Synchronization offset out of range
  - Signal distortion or noise
  - Incorrect demodulation
```

**Possible causes:**
- Synchronization offset > 16 samples
- Severe signal distortion
- Interference or noise
- Bit errors during transmission

**Solutions:**
- Adjust volume (not too loud - causes clipping)
- Try different sync offsets in code
- Check signal quality in debug files
- Verify carrier frequencies are clear

## Debug Files Generated

### 1. `ncc_values.txt`
Format: `sample_index NCC_value power_value`

Use the visualization script:
```bash
python visualize_ncc.py
```

This shows:
- NCC correlation over time
- Power estimate
- Detection criteria satisfaction
- Potential preamble locations

### 2. `debug_processing.txt`
Contains:
- Peak detection events
- Bit decoding information
- Correlation values for each bit

## Signal Parameters

| Parameter | Value | Description |
|-----------|-------|-------------|
| Sample Rate | 48000 Hz | Audio sampling rate |
| Carrier A | 5000 Hz | Frequency for bit 0 |
| Carrier B | 10000 Hz | Frequency for bit 1 |
| Samples/bit | 48 | Symbol duration |
| Bit rate | 1000 bps | Effective data rate |
| Chirp length | 480 samples | Preamble duration (10ms) |
| Frame size | 100 bits | Data bits per frame |
| CRC | 8 bits | Error detection |

## Troubleshooting Guide

### Problem: "No preamble detected"

1. **Check NCC values**:
   ```bash
   python visualize_ncc.py
   ```
   - Look for peaks in NCC plot
   - Check if any peaks approach 0.05 threshold
   - Compare NCC peaks with power×2 line

2. **Adjust detection threshold**:
   - In `Main.cpp`, line ~453:
   ```cpp
   bool criteriaC = ncc > 0.05;  // Try lowering to 0.03
   ```

3. **Test with pure tone**:
   - Generate 5kHz test signal
   - Verify audio path works

### Problem: "CRC check failed"

1. **Check sync offset**:
   - Look at console output for correlation values
   - If corrA and corrB are both small, signal is weak
   - If they're similar magnitude, demodulation is ambiguous

2. **Expand offset search range**:
   - In `Main.cpp`, line ~525:
   ```cpp
   std::vector<int> offsetsToTry = {0, -4, -2, -1, 1, 2, 4, 8, -8, 16, -16, 24, -24, 32, -32};
   ```

3. **Verify carrier frequencies**:
   - Use audio spectrum analyzer
   - Check for interference at 5kHz or 10kHz

### Problem: "Signal too weak"

- Increase volume gradually
- Check system mixer settings
- Verify device isn't muted
- Try different audio devices

### Problem: "Signal clipping"

- Reduce volume
- Check for distortion in `debug_processing.txt`
- Look for NCC values that are unstable

## Testing Checklist

- [ ] Build completes without errors
- [ ] Program starts and shows menu
- [ ] Audio device initialized correctly
- [ ] Transmission generates audio (hear chirp + data)
- [ ] Recording captures audio (check file size)
- [ ] Preamble detected (NCC peak found)
- [ ] Frame decoded (CRC passes)
- [ ] Bit error rate < 10%

## Advanced: Two-Computer Testing

1. **On Computer 1 (Sender)**:
   - Use Mode 1 (Transmission Only)
   - Play the generated signal

2. **On Computer 2 (Receiver)**:
   - Use Mode 2 (Recording)
   - Start recording before transmission begins
   - Stop after transmission completes

3. **Verify**:
   - Check NCC plot shows clear peak
   - Verify CRC passes
   - Compare transmitted vs received bits

## Performance Goals

| Metric | Target | Acceptable |
|--------|--------|------------|
| Preamble Detection Rate | 100% | >90% |
| CRC Pass Rate | 100% | >95% |
| Bit Error Rate | 0% | <5% |
| Sync Offset | 0 samples | ±8 samples |

## Next Steps

After successful loopback testing:

1. **Increase bit rate**: Reduce `samplesPerBit`
2. **Add multiple frames**: Test burst transmission
3. **Test longer distances**: Separate speaker and microphone
4. **Add noise resilience**: Test with background noise
5. **Implement file transfer**: Read from INPUT.txt, write to OUTPUT.txt

## Common Issues and Solutions

### Audio Device Not Found
```
Error: No audio input/output devices found
```
**Solution**: Check Windows sound settings, ensure devices are enabled

### Buffer Underrun/Overrun
```
Warning: Audio buffer underrun detected
```
**Solution**: 
- Switch to Performance power mode (Windows Settings > Power)
- Increase buffer size in audio device setup
- Close background applications

### Compilation Errors
```
Error: Cannot open include file 'JuceHeader.h'
```
**Solution**: Build through Visual Studio with proper JUCE configuration

## Support

If you encounter issues:

1. Check the debug output files
2. Run the visualization script
3. Verify audio hardware is working (test with other apps)
4. Try adjusting detection thresholds
5. Test with different sync offsets

## Reference

Based on:
- `SamplePHY.m` - MATLAB reference implementation
- JUCE Audio Framework - https://juce.com/
- CS120 Project 1 Specifications




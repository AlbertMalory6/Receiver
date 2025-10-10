# Quick Tuning Guide for Integrated Transceiver

## Key Parameters in Main.cpp

### Modulation Parameters (Line ~70-80)
```cpp
sampleRate = 48000;       // Audio sample rate
carrierFreqA = 5000;      // Carrier for bit 0 (Hz)
carrierFreqB = 10000;     // Carrier for bit 1 (Hz)
samplesPerBit = 48;       // Symbol duration (48 = 1000 bps)
bitsPerFrame = 100;       // Data bits per frame
headerLength = 480;       // Chirp preamble length (samples)
```

**To adjust bit rate:**
- Increase `samplesPerBit` → Slower, more reliable
- Decrease `samplesPerBit` → Faster, less reliable
- Formula: `bitrate = sampleRate / samplesPerBit`

**To change carrier frequencies:**
- Keep them audible: 2kHz - 15kHz
- Maintain separation: at least 2kHz apart
- Avoid multiples of each other

### Preamble Detection (Line ~450-460)
```cpp
// Detection criteria
bool criteriaA = ncc > power * 2;        // NCC > 2× power
bool criteriaB = ncc > syncPower_localMax; // Local maximum
bool criteriaC = ncc > 0.05;             // Absolute threshold

if (criteriaA && criteriaB && criteriaC) {
    // Preamble candidate detected
}
```

**If preamble not detected:**
1. Lower `criteriaC` threshold: `ncc > 0.03` or `ncc > 0.02`
2. Adjust power multiplier: `power * 1.5` instead of `power * 2`
3. Reduce wait time: change `200` to `150` on line ~467

**If too many false detections:**
1. Raise `criteriaC` threshold: `ncc > 0.08`
2. Increase power multiplier: `power * 2.5`
3. Increase wait time: change `200` to `300`

### Synchronization Offsets (Line ~525)
```cpp
std::vector<int> offsetsToTry = {0, -4, -2, -1, 1, 2, 4, 8, -8, 16, -16};
```

**If CRC fails with all offsets:**
- Add more offsets: `24, -24, 32, -32, 48, -48`
- Try finer granularity: `3, 5, 6, 7, 9, 10, ...`
- Check if offset pattern is consistent (always needs +8, etc.)

**If a specific offset always works:**
- Update demodulation to use that offset by default
- Indicates clock drift or system delay

### Power Estimation (Line ~407)
```cpp
power = power * (1 - 1.0f / 64.0f) + currentSample * currentSample / 64.0f;
```

**Time constant = 64 samples:**
- Increase → Smoother, slower to adapt
- Decrease → More responsive, noisier
- Formula: `time_constant = 1 / (1 - multiplier)`

### Chirp Generation (Line ~113-140)
```cpp
int startFreq = 2000;     // Chirp start frequency
int endFreq = 10000;      // Chirp end frequency
```

**To make chirp more distinctive:**
- Increase frequency range: `startFreq = 1000`, `endFreq = 12000`
- Increase length: `headerLength = 960` (20ms instead of 10ms)

**To make chirp shorter:**
- Decrease length: `headerLength = 240` (5ms)
- Note: Shorter chirp = harder to detect reliably

## Common Tuning Scenarios

### Scenario 1: Noisy Environment

**Problem:** Lots of background noise interfering

**Solutions:**
```cpp
// 1. Stricter detection criteria
bool criteriaC = ncc > 0.08;  // Raise threshold
bool criteriaA = ncc > power * 3;  // Increase power ratio

// 2. Longer preamble
headerLength = 960;  // Longer chirp

// 3. More averaging in power estimation
power = power * (1 - 1.0f / 128.0f) + currentSample * currentSample / 128.0f;
```

### Scenario 2: Weak Signal

**Problem:** Speaker/microphone too far apart, or volume low

**Solutions:**
```cpp
// 1. More sensitive detection
bool criteriaC = ncc > 0.02;  // Lower threshold
bool criteriaA = ncc > power * 1.5;  // Lower power ratio

// 2. Slower bit rate
samplesPerBit = 96;  // 500 bps instead of 1000 bps

// 3. More correlation averaging
ncc /= 300.0f;  // Increase normalization factor if NCC too high
```

### Scenario 3: Clock Drift / Synchronization Issues

**Problem:** CRC fails, needs large offset, offset increases over time

**Solutions:**
```cpp
// 1. Expand offset search
std::vector<int> offsetsToTry = {
    0, -1, 1, -2, 2, -4, 4, -8, 8, 
    -16, 16, -24, 24, -32, 32, -48, 48
};

// 2. Try fractional sample offsets (advanced)
// Implement interpolation in demodulation

// 3. Re-sync more frequently
// For longer frames, detect preamble periodically
```

### Scenario 4: Fast Transmission Needed

**Problem:** Need higher bit rate

**Solutions:**
```cpp
// 1. Reduce samples per bit
samplesPerBit = 24;  // 2000 bps (but less reliable!)

// 2. Use shorter preamble
headerLength = 240;  // 5ms chirp

// 3. Smaller CRC
// Implement CRC-4 or CRC-6 instead of CRC-8

// 4. Multiple carriers (advanced)
// Implement OFDM-like parallel transmission
```

## Testing Parameter Changes

### Systematic Testing Process

1. **Baseline Test**
   - Run with default parameters
   - Record: detection rate, CRC pass rate, BER

2. **Change ONE Parameter**
   - Document what you changed
   - Run 10 tests
   - Calculate average performance

3. **Compare Results**
   - Better → Keep change
   - Worse → Revert change
   - Similar → Parameter not critical

4. **Iterate**
   - Move to next parameter
   - Repeat process

### Parameter Sensitivity (Most to Least Critical)

1. **Detection threshold (`criteriaC`)** - Most impact on detection rate
2. **Sync offsets** - Most impact on CRC success
3. **Power ratio (`power * 2`)** - Moderate impact on false positives
4. **Samples per bit** - Fundamental tradeoff (speed vs reliability)
5. **Carrier frequencies** - Usually fine if in audible range
6. **Preamble length** - Longer = more reliable but slower

## Debug Output Interpretation

### Good Signal
```
Peak NCC value: 0.8523        → Strong correlation
Power at detection: 0.0234    → Reasonable signal level
Sync offset used: 0 samples   → Perfect synchronization
Bit error rate: 0.0%          → Clean reception
```

### Marginal Signal
```
Peak NCC value: 0.0623        → Just above threshold
Power at detection: 0.0156    → Weak signal
Sync offset used: 8 samples   → Some clock drift
Bit error rate: 3.2%          → Some errors but acceptable
```

### Poor Signal
```
Peak NCC value: 0.0323        → Below threshold
Power at detection: 0.0034    → Very weak
CRC check failed              → Too many errors
Max NCC: 0.045               → Never reached detection level
```

## Quick Fixes

| Symptom | Quick Fix | Line to Change |
|---------|-----------|----------------|
| No detection | Lower threshold to 0.03 | ~455 |
| False detections | Raise threshold to 0.08 | ~455 |
| CRC fails | Add more offsets | ~525 |
| Too slow | Reduce samplesPerBit to 24 | ~76 |
| Unreliable | Increase samplesPerBit to 96 | ~76 |
| Weak signal | Increase headerLength to 960 | ~78 |

## Optimization Tips

1. **Don't change multiple parameters at once** - You won't know what helped
2. **Keep detailed notes** - Document all changes and results
3. **Use the visualization script** - `python visualize_ncc.py`
4. **Test in different conditions** - Quiet room, noisy room, etc.
5. **Verify with loopback first** - Before testing between computers

## Advanced: Algorithm Alternatives

If standard FSK isn't working well:

1. **PSK (Phase Shift Keying)** - More robust to amplitude variations
2. **OFDM** - Multiple carriers for higher throughput
3. **Differential Encoding** - Reduces phase ambiguity
4. **Forward Error Correction** - Add redundancy instead of just CRC
5. **Adaptive Equalization** - Compensate for channel distortion

## Performance Targets by Environment

| Environment | Detection Rate | CRC Pass | BER |
|-------------|---------------|----------|-----|
| Loopback (same device) | >99% | >99% | <1% |
| Direct (speaker→mic, 10cm) | >95% | >95% | <3% |
| Room (speaker→mic, 1m) | >90% | >90% | <5% |
| Noisy Room | >80% | >85% | <10% |
| Between Computers | >85% | >85% | <8% |

## Checklist Before Deployment

- [ ] Loopback test passes reliably (>95%)
- [ ] Parameters documented
- [ ] Detection threshold optimized for environment
- [ ] Sync offset range adequate
- [ ] Bit rate meets requirements (<15s for 10,000 bits)
- [ ] Error rate acceptable (<20% as per spec)
- [ ] Tested with realistic audio levels
- [ ] Works with target hardware (NODE1/NODE2)

## Final Notes

- Start conservative (slower, more reliable)
- Optimize incrementally
- Test thoroughly at each step
- Document everything
- Have fun! 🎵📡




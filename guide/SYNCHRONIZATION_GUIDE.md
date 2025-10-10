# FSK Synchronization Offset Detection & Correction Guide

## Overview

This document explains the synchronization mechanisms in your FSK acoustic communication system and how to diagnose/fix timing offset issues.

---

## 📋 System Architecture

### Signal Flow
```
[Transmitter]                    [Receiver]
    ↓                                ↓
Silent Leader (0.5s)         Recording starts
    ↓                                ↓
Chirp Preamble (440 samples) → NCC Detection → Peak Finding
    ↓                                ↓
FSK Data (5008 bits)         → Offset Correction → Demodulation
    ↓                                ↓
Audio ends                    → CRC Validation → OUTPUT.txt
```

---

## 🔍 Synchronization Offset Issues

### Common Problems

#### 1. **Preamble Detection Offset**
**Symptom**: NCC peak doesn't align exactly with preamble end  
**Cause**: Cross-correlation peaks when signals have maximum overlap  
**Impact**: Data starts at wrong sample → all bits misaligned

#### 2. **Frame Start Misalignment**  
**Symptom**: First few bits have low confidence, errors accumulate  
**Cause**: Using `peakSampleIndex + 1` without fine-tuning  
**Impact**: Reading bits from "in-between" samples → weak detection

#### 3. **Clock Drift**
**Symptom**: Early bits correct, later bits fail  
**Cause**: Sample rate mismatch (44100.0 vs actual hardware rate)  
**Impact**: Offset grows linearly with time

---

## 🛠️ Detection Methods Implemented

### 1. **NCC Peak Detection** (Lines 149-188)

```cpp
// Tracks NCC values and detects peaks
if (ncc > syncPowerLocalMax && ncc > NCC_DETECTION_THRESHOLD) {
    syncPowerLocalMax = ncc;
    peakSampleIndex = i;  // Mark rising edge
}
else if (peakSampleIndex != 0 && (i - peakSampleIndex) > (FSK::preambleSamples / 2)) {
    // NCC has fallen for preamble/2 duration → peak confirmed!
}
```

**How it works**:
- Computes NCC at every sample
- Tracks local maximum above threshold
- Confirms peak after NCC falls for 220 samples (half preamble)
- Prevents false triggers from noise spikes

**Tuning**:
- `NCC_DETECTION_THRESHOLD = 0.35` (adjust based on `debug_ncc_output.csv`)
- Lower = more sensitive (may detect noise)
- Higher = more robust (may miss weak signals)

---

### 2. **Fine-Tuned Frame Start** (Lines 207-241)

```cpp
int findOptimalFrameStart(const juce::AudioBuffer<float>& recording, int preambleEndSample)
{
    // Search ±22 samples around initial estimate
    for (int offset = -searchRange/2; offset < searchRange/2; ++offset) {
        // Test Goertzel magnitude at this offset
        double score = std::abs(mag_f0 - mag_f1) / std::max(mag_f0, mag_f1);
        // Keep offset with maximum frequency separation
    }
}
```

**Algorithm**:
1. Start with `preambleEndSample + 1`
2. Test offsets within one bit period (44 samples)
3. For each offset, demodulate the first bit
4. Choose offset that gives **maximum** f0/f1 separation
5. High separation = bit is well-aligned

**Why it works**:
- If offset is wrong, Goertzel averages across two bits
- Correct offset reads pure f0 or f1 → high magnitude difference
- Automatically compensates for NCC peak position error

---

### 3. **Confidence Metrics** (Lines 250-278)

```cpp
double confidence = (mag_f0 > mag_f1) ? (mag_f0 / mag_f1) : (mag_f1 / mag_f0);
```

**Interpretation**:
- `confidence = 1.0` → both frequencies equal → **ambiguous bit**
- `confidence = 2.0` → one frequency 2× stronger → **good bit**
- `confidence > 3.0` → very clear signal → **excellent bit**

**Diagnostics**:
- Average confidence across frame
- Count "weak bits" (confidence < 1.5)
- If >10% weak bits → offset or noise problem

---

## 🧪 Diagnostic Workflow

### Step 1: Generate Debug Files

Run your receiver program. It now generates:

1. **`debug_ncc_output.csv`** - NCC correlation at every sample
2. **`debug_loopback_recording.wav`** - Full audio recording
3. **`debug_beeps.wav`** - Recording with clicks at bit boundaries

### Step 2: Analyze NCC Data

Run the Python diagnostic tool:

```bash
python diagnose_sync_offset.py
```

**Output includes**:
- Detected peaks (sample index, time, NCC value)
- Peak quality analysis
- Alignment error for each peak
- Visualization plot saved as `debug_sync_analysis.png`

**Example Output**:
```
Peak 1 alignment:
  Data should start at sample: 23861
  Offset within bit period: 17/44 samples
  Alignment error: 17 samples
  ⚠️  WARNING: Significant offset detected!
```

### Step 3: Listen to Debug Audio

1. Open `debug_loopback_recording.wav` in Audacity
2. Enable "View → Show Clipping"
3. Zoom into the preamble region
4. Verify chirp is present and clear

5. Open `debug_beeps.wav`
6. Listen for regular clicks (should be 1000 Hz = every 0.001s)
7. If clicks drift or are irregular → offset problem

### Step 4: Inspect Confidence Scores

Check the console output:

```
Bit   0: 1 (f0=123.4, f1=456.7, conf=3.70)  ← Excellent
Bit   1: 0 (f0=389.2, f1=145.6, conf=2.67)  ← Good
Bit  15: 1 (f0=234.5, f1=256.3, conf=1.09)  ← Weak! Problem here
```

**Action items**:
- If early bits weak → frame start offset
- If later bits weak → clock drift
- If random bits weak → noise

---

## 🔧 Fixing Synchronization Issues

### Issue: No Peaks Detected

**Diagnosis**: `debug_ncc_output.csv` has no values > threshold

**Solutions**:
1. Check Python diagnostic's recommended threshold
2. Lower `NCC_DETECTION_THRESHOLD` in `Main.cpp` line 145
3. Increase speaker volume
4. Increase silent leader duration (line 74):
   ```cpp
   const int silentLeaderSamples = FSK::sampleRate * 1.0; // 1 second
   ```

---

### Issue: Peaks Detected but CRC Fails

**Diagnosis**: 
```
Peak NCC: 0.65
Average confidence: 1.8
Weak bits: 23%
CRC FAIL! Received: 145, Calculated: 78
```

**Solutions**:

1. **If offset correction doesn't help** (still showing large offset):
   ```cpp
   int searchRange = FSK::samplesPerBit * 2; // Search wider range
   ```

2. **If early bits are weak**:
   - Frame start is wrong
   - Manually adjust in `findOptimalFrameStart()`:
     ```cpp
     int searchStart = preambleEndSample + 10; // Add manual offset
     ```

3. **If later bits degrade**:
   - Clock drift - check actual sample rate
   - Add resampling or adaptive bit timing

---

### Issue: Multiple False Peaks

**Diagnosis**: 
```
Peaks detected: 7  (expected: 1)
Peak 2: t=0.534s, NCC=0.41
Peak 3: t=0.587s, NCC=0.38
```

**Solutions**:
1. Increase threshold:
   ```cpp
   static constexpr double NCC_DETECTION_THRESHOLD = 0.50;
   ```

2. Increase minimum peak separation (line 161):
   ```cpp
   else if (peakSampleIndex != 0 && (i - peakSampleIndex) > FSK::preambleSamples)
   ```

---

### Issue: Offset Drifts Over Time

**Symptom**: First 1000 bits OK, last 4000 bits fail

**Cause**: Sample rate mismatch

**Solutions**:

1. **Measure actual sample rate**:
   ```cpp
   // In audioDeviceAboutToStart():
   std::cout << "Actual sample rate: " << device->getCurrentSampleRate() << std::endl;
   ```

2. **Use adaptive bit timing**:
   ```cpp
   // Track phase error and adjust samplesPerBit dynamically
   double actualBitPeriod = FSK::samplesPerBit * (actualSampleRate / FSK::sampleRate);
   ```

---

## 📊 Performance Benchmarks

### Excellent Performance
- Average NCC peak: > 0.7
- Average confidence: > 2.5
- Weak bits: < 5%
- CRC success: 100%

### Acceptable Performance  
- Average NCC peak: 0.4-0.7
- Average confidence: 1.8-2.5
- Weak bits: 5-15%
- CRC success: > 90%

### Poor Performance (Needs Fixing)
- Average NCC peak: < 0.4
- Average confidence: < 1.8
- Weak bits: > 15%
- CRC success: < 90%

---

## 🎯 Advanced Techniques

### 1. **Gardner Timing Recovery**

For production systems, implement symbol timing recovery:

```cpp
double timing_error = (prev_mag - next_mag) * current_mag;
bit_phase_offset += timing_error * loop_gain;
```

### 2. **Interpolation-Based Resampling**

Read at fractional sample positions:

```cpp
float interpolated = signal[floor(pos)] * (1 - frac) + signal[ceil(pos)] * frac;
```

### 3. **Pilot Tone Tracking**

Add a continuous tone to track clock drift:

```cpp
// Transmit: mix 500 Hz pilot tone
// Receive: measure pilot frequency → correct sample rate
```

---

## 🧩 Quick Reference

### Key Parameters
| Parameter | Location | Default | Notes |
|-----------|----------|---------|-------|
| `NCC_DETECTION_THRESHOLD` | Line 145 | 0.35 | Preamble detection sensitivity |
| `silentLeaderSamples` | Line 74 | 22050 | 0.5s pre-silence |
| `searchRange` | Line 211 | 44 | Frame start search window |
| `samplesPerBit` | Line 12 | 44 | 1000 bps at 44.1 kHz |

### Debug Files
| File | Contents | Use Case |
|------|----------|----------|
| `debug_ncc_output.csv` | NCC correlation values | Find peaks, tune threshold |
| `debug_loopback_recording.wav` | Raw audio recording | Verify signal presence |
| `debug_beeps.wav` | Recording with bit markers | Check alignment |
| `debug_sync_analysis.png` | NCC plot with annotations | Visual diagnosis |

### Console Outputs
```
*** PREAMBLE DETECTED at sample 23860 (t=0.541s)
    Peak NCC: 0.6543
    Fine-tuning frame start (searching 44 samples)...
    Offset correction: -8 samples (clarity score: 0.847)

--- Demodulation Statistics ---
Average confidence: 2.34
Weak bits (conf < 1.5): 145 / 5008 (2.9%)

CRC OK! Writing 5000 bits to OUTPUT.txt
```

---

## 📞 Troubleshooting Checklist

- [ ] Run receiver and check console for "PREAMBLE DETECTED"
- [ ] Run `python diagnose_sync_offset.py`
- [ ] Check if average NCC > 0.4
- [ ] Verify offset correction is applied
- [ ] Check weak bits percentage < 10%
- [ ] Validate CRC passes
- [ ] Compare INPUT.txt with OUTPUT.txt

---

**Last Updated**: 2025-10-09  
**Version**: 2.0 (with fine-tuned offset correction)


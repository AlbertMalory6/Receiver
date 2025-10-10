# Multi-Frame Synchronization Testing Guide

## 🎯 Testing Objectives

Validate that the synchronization system correctly:
1. Detects multiple consecutive frames
2. Maintains offset correction across frames
3. Handles frame boundaries properly
4. Reports accurate statistics per frame

---

## 🧪 Test Scenarios

### Test 1: Single Frame (Baseline)

**Setup**:
```bash
# Generate 5000-bit input
python input_text_generator.py
# Creates INPUT.txt with 5000 random bits
```

**Expected Results**:
- ✅ 1 preamble detected
- ✅ Frame start offset correction applied
- ✅ CRC passes
- ✅ OUTPUT.txt matches INPUT.txt

**Validation**:
```bash
python test_synchronization.py
# Should show BER < 1%
```

---

### Test 2: Multi-Frame Detection

**Setup**:
Modify sender to transmit multiple frames with gaps:

```cpp
// In Sender/Source/Main.cpp or similar
void generateMultipleFrames(int numFrames) {
    for (int frame = 0; frame < numFrames; ++frame) {
        // Add 0.5s silence between frames
        const int gapSamples = FSK::sampleRate * 0.5;
        signalBuffer.setSize(1, signalBuffer.getNumSamples() + gapSamples);
        // ... add preamble and data ...
    }
}
```

**Expected Results**:
- ✅ All frames detected
- ✅ Each frame gets independent offset correction
- ✅ No crosstalk between frames

**Console output should show**:
```
*** PREAMBLE DETECTED at sample 23860 (t=0.541s)
    Peak NCC: 0.654
    Offset correction: -8 samples
    CRC OK! Frame 1 complete

*** PREAMBLE DETECTED at sample 67350 (t=1.527s)
    Peak NCC: 0.671
    Offset correction: -5 samples
    CRC OK! Frame 2 complete

*** PREAMBLE DETECTED at sample 110840 (t=2.513s)
    Peak NCC: 0.648
    Offset correction: -9 samples
    CRC OK! Frame 3 complete
```

---

### Test 3: Offset Variation Across Frames

**Purpose**: Verify that each frame gets independently corrected

**Method**:
1. Manually inject different offsets in test data
2. Check that `findOptimalFrameStart()` adapts

**Test Code** (add to receiver):
```cpp
// After detecting each frame
std::cout << "Frame " << frameCount++ << " offset: " << bestOffset << " samples" << std::endl;
```

**Expected Pattern**:
```
Frame 1 offset: -8 samples
Frame 2 offset: -7 samples  ← Should vary slightly
Frame 3 offset: -9 samples
```

**Pass Criteria**: Offsets vary by ±5 samples (normal variation)  
**Fail Criteria**: All offsets identical (not actually optimizing)

---

### Test 4: Stress Test - Weak Signal

**Setup**:
```cpp
// In sender, reduce amplitude
signalBuffer.applyGain(0.2f);  // Very quiet signal
```

**Expected Behavior**:
- ⚠️ Lower NCC peaks (0.3-0.5 range)
- ⚠️ More weak bits (10-20%)
- ✅ Still detects with lower threshold
- ✅ Offset correction still works

**Validation**:
```bash
python diagnose_sync_offset.py
# Check if recommended threshold is lower
# Adjust NCC_DETECTION_THRESHOLD accordingly
```

---

### Test 5: Clock Drift Simulation

**Purpose**: Detect sample rate mismatch

**Setup**:
```python
# Create INPUT.txt with known pattern
with open('INPUT.txt', 'w') as f:
    # Alternating pattern makes drift visible
    f.write('01' * 2500)  # 5000 bits
```

**Expected if NO drift**:
```
First 100 bits: 100% correct
Middle 4800 bits: 100% correct
Last 100 bits: 100% correct
```

**Expected if DRIFT present**:
```
First 100 bits: 100% correct
Middle 4800 bits: 98% correct  ← Errors increase
Last 100 bits: 95% correct     ← Worst here
```

**Detection**:
```bash
python test_synchronization.py
# Look for "LATE ERROR CONCENTRATION"
```

**Fix if detected**:
```cpp
// Log actual sample rate
std::cout << "Actual SR: " << device->getCurrentSampleRate() << std::endl;

// Adjust if needed
constexpr double actualSampleRate = 44099.8;  // Measured value
```

---

### Test 6: Boundary Conditions

#### 6.1 Very Short Recording
**Setup**: Stop recording early

**Expected**: 
```
ERROR: Not enough samples for full frame. Need 221352, have 150000
```

#### 6.2 No Preamble
**Setup**: Send only data, no preamble

**Expected**:
```
Analysis Complete
Peaks detected: 0
⚠️  WARNING: NO PEAKS DETECTED!
```

#### 6.3 Corrupted Preamble
**Setup**: Add noise to first 1000 samples

**Expected**:
```
Peaks detected: 0 or 1 with low NCC
Peak NCC: 0.25 (below threshold)
```

---

## 📊 Validation Checklist

Use this checklist for each test run:

### Detection Phase
- [ ] Preamble detected within 1 second
- [ ] Peak NCC > 0.4 (clean signal) or > 0.25 (noisy)
- [ ] Detection sample position matches debug plot
- [ ] Multiple frames detected independently

### Offset Correction Phase
- [ ] `findOptimalFrameStart()` executes
- [ ] Offset correction value is reported
- [ ] Clarity score > 0.5
- [ ] Offset is within ±22 samples

### Demodulation Phase
- [ ] First 30 bits show confidence values
- [ ] Average confidence > 1.8
- [ ] Weak bits < 15%
- [ ] No obvious error pattern (all 0s or all 1s)

### Validation Phase
- [ ] CRC passes (shows "CRC OK!")
- [ ] OUTPUT.txt created
- [ ] OUTPUT.txt length = INPUT.txt length
- [ ] BER < 5% (ideally < 1%)

---

## 🔬 Advanced Multi-Frame Testing

### Test Setup for Frame Burst

```cpp
// Pseudo-code for transmitting 5 frames in succession
void transmitFrameBurst() {
    const int numFrames = 5;
    const int gapSamples = FSK::sampleRate * 0.3;  // 300ms gaps
    
    for (int i = 0; i < numFrames; ++i) {
        appendSilence(gapSamples);
        appendPreamble();
        appendData(inputBits, i * 1000, 1000);  // 1000 bits per frame
        appendCRC();
    }
}
```

### Expected Console Output

```
=== FRAME BURST TEST ===
Analyzing 5 frames...

*** PREAMBLE DETECTED at sample 23860 (t=0.541s)
    Peak NCC: 0.654
    Fine-tuning frame start (searching 44 samples)...
    Offset correction: -8 samples (clarity score: 0.847)

--- Demodulation Statistics ---
Average confidence: 2.76
Weak bits (conf < 1.5): 18 / 1008 (1.8%)
CRC OK! Frame 1/5 complete

*** PREAMBLE DETECTED at sample 47320 (t=1.073s)
    Peak NCC: 0.671
    Offset correction: -7 samples (clarity score: 0.852)
    CRC OK! Frame 2/5 complete

[... frames 3-4 ...]

*** PREAMBLE DETECTED at sample 118240 (t=2.681s)
    Peak NCC: 0.648
    Offset correction: -9 samples (clarity score: 0.841)
    CRC OK! Frame 5/5 complete

=== BURST TEST COMPLETE ===
Frames detected: 5/5 (100%)
Average offset: -8.2 samples (σ=1.3)
Average confidence: 2.73
Overall CRC success: 100%
```

### Performance Metrics to Track

| Metric | Good | Acceptable | Poor |
|--------|------|------------|------|
| Frame detection rate | 100% | >90% | <90% |
| Average NCC | >0.6 | 0.4-0.6 | <0.4 |
| Offset std dev | <5 samples | 5-10 | >10 |
| Avg confidence | >2.5 | 1.8-2.5 | <1.8 |
| CRC success rate | 100% | >95% | <95% |

---

## 🐛 Debugging Failed Tests

### Issue: Some frames missed

**Diagnosis**:
```bash
python diagnose_sync_offset.py
# Check if all expected peaks are present
```

**Possible causes**:
1. Peaks too close → Hysteresis blocks second peak
   ```cpp
   // Reduce hysteresis delay
   else if (peakSampleIndex != 0 && (i - peakSampleIndex) > (FSK::preambleSamples / 4))
   ```

2. Threshold too high → Weak frames rejected
   ```cpp
   static constexpr double NCC_DETECTION_THRESHOLD = 0.25;  // Lower
   ```

---

### Issue: Offset correction fails on later frames

**Diagnosis**: Check console for clarity scores

```
Frame 1: Offset -8, clarity 0.85  ✅
Frame 2: Offset -7, clarity 0.82  ✅
Frame 3: Offset 0, clarity 0.15   ❌  ← Problem!
```

**Possible causes**:
1. Insufficient samples remain
   ```cpp
   if (testStart + FSK::samplesPerBit >= recording.getNumSamples()) continue;
   // Make sure recording is long enough
   ```

2. Noise increased
   ```cpp
   // Expand search range
   int searchRange = FSK::samplesPerBit * 2;
   ```

---

### Issue: CRC fails on specific frames

**Diagnosis**:
```bash
python test_synchronization.py
# Check error distribution
```

**Action**:
- If errors in Frame 1 only → Warm-up issue, increase silent leader
- If errors in Frame N only → Check input data for that frame
- If errors in all frames → Global sync problem

---

## 📈 Automated Test Script

Create `run_all_tests.sh`:

```bash
#!/bin/bash

echo "=== FSK Synchronization Test Suite ==="

# Test 1: Single frame
echo -e "\n[TEST 1] Single Frame"
python input_text_generator.py
./Receiver  # Or however you run it
python test_synchronization.py | tee test1_result.txt

# Test 2: Check diagnostics
echo -e "\n[TEST 2] NCC Diagnostics"
python diagnose_sync_offset.py | tee test2_result.txt

# Test 3: Pattern test
echo -e "\n[TEST 3] Known Pattern"
echo "01" | python -c "import sys; print(sys.stdin.read().strip() * 2500)" > INPUT.txt
./Receiver
python test_synchronization.py | tee test3_result.txt

# Summary
echo -e "\n=== TEST SUMMARY ==="
grep "Success Rate" test1_result.txt
grep "Peaks detected" test2_result.txt
grep "BER" test3_result.txt

echo -e "\nAll tests complete. Check *_result.txt for details."
```

---

## ✅ Acceptance Criteria

Your multi-frame synchronization passes if:

1. **Detection**: All frames detected (100% detection rate)
2. **Correction**: Each frame gets independent offset correction
3. **Quality**: Average confidence > 2.0 across all frames
4. **Accuracy**: Overall BER < 2%
5. **Robustness**: Works with signal variation (NCC 0.4-0.8)
6. **Diagnostics**: All debug files generated correctly

---

## 📝 Test Report Template

```markdown
# Synchronization Test Report

**Date**: 2025-10-09
**Test Type**: Multi-Frame Synchronization
**Signal Type**: Acoustic Loopback

## Test Configuration
- Frames transmitted: 5
- Bits per frame: 1000
- Gap between frames: 300ms
- Signal amplitude: 0.9
- Background noise: None

## Results

### Frame Detection
- Frames detected: 5/5 (100%)
- Average NCC peak: 0.658
- Peak std dev: 0.012

### Offset Correction
- Average offset: -8.2 samples
- Offset std dev: 1.3 samples
- Average clarity score: 0.845

### Demodulation
- Average confidence: 2.73
- Weak bits: 1.9%
- CRC success: 5/5 (100%)

### Overall Performance
- Bit Error Rate: 0.4%
- Success Rate: 99.6%

## Verdict
✅ PASS - All acceptance criteria met

## Recommendations
- System is production-ready for clean environments
- Consider lowering threshold to 0.3 for noisy environments
```

---

**Test Status**: Ready to execute  
**Next Step**: Compile receiver, run tests, collect results


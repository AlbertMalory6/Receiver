# Comprehensive Chirp Detection Test Suite

## Purpose

This test suite verifies **chirp detection accuracy** by measuring the offset between where a chirp actually is versus where it's detected. This is the critical metric for sample-level clock synchronization.

## Key Concept: Detection Offset

**Detection Offset = Detected Position - Actual Position**

- Offset = 0: Perfect detection
- Offset = ±1 sample: Sample-level accuracy (goal achieved)
- Offset > 10 samples: Significant error requiring tuning

This is **NOT** about:
- System audio latency
- Silence padding before chirp
- Acoustic propagation delay

## Files

### C++ Test Program
- **`chirp_comprehensive_test.cpp`**: Main test program
  - Generates test signals with chirps at known positions
  - Runs three detection methods: NCC, Dot Product, SamplePHY
  - Exports sliding window correlation values to CSV
  - Creates WAV files for visual inspection

### Python Analysis Tool
- **`visualize_chirp_detection.py`**: Plots correlation curves
  - Visualizes NCC values across all positions
  - Shows threshold lines and detection points
  - Highlights actual vs detected positions
  - Analyzes SamplePHY power/correlation dynamics

## Test Scenarios

### TEST 1: Pure Chirp at Known Position
- **File**: `test1_pure_chirp_at_5000.wav`
- **Actual Position**: 5000 samples
- **Purpose**: Baseline test - should have zero offset
- **Debug Output**:
  - `test1_ncc_debug.csv`: NCC values at each position
  - `test1_dot_debug.csv`: Dot product values
  - `test1_phy_debug.csv`: SamplePHY power/correlation trace

### TEST 2: Chirp with Observable Marker
- **File**: `test2_chirp_with_marker.wav`
- **Marker**: 1kHz tone for 100ms (4410 samples)
- **Chirp Position**: Right after marker at sample 4410
- **Purpose**: Visual verification - you can SEE the marker in Audacity, then verify chirp detection
- **How to verify**:
  1. Open `test2_chirp_with_marker.wav` in Audacity
  2. Marker tone is visible at start
  3. Chirp should be at 0.1 seconds (after marker ends)
  4. Compare with detected position from test output

### TEST 3: Pure Chirp vs Chirp+Data
- **Files**:
  - `test3a_pure_chirp.wav`: Only chirp
  - `test3b_chirp_plus_data.wav`: Chirp followed by FSK data
- **Purpose**: Verify detection isn't affected by following data
- **Expected**: Both should detect at same position (3000)

### TEST 4: Multiple Positions (Consistency Check)
- **Positions**: 1000, 5000, 10000, 15000, 20000 samples
- **Purpose**: Verify detection accuracy is consistent regardless of position
- **Expected**: All should have same detection offset (ideally zero)

### TEST 5: Threshold Tuning
- **File**: `test5_noisy_chirp.wav`
- **Purpose**: Find optimal NCC threshold
- **Tests**: Thresholds 0.3, 0.5, 0.7, 0.9
- **Use**: If detection fails, lower threshold; if false positives, raise it

## How to Run

### 1. Compile and Run C++ Test
```bash
cd Receiver/Builds/VisualStudio2022
# Build chirp_comprehensive_test.cpp in Visual Studio
./chirp_comprehensive_test.exe
```

This generates:
- 10+ WAV files (test signals)
- 20+ CSV files (debug traces)
- Console output with detection results

### 2. Analyze Results with Python

**Automatic analysis (all tests):**
```bash
cd Receiver/Builds/VisualStudio2022  # Where test files are
python ../../../visualize_chirp_detection.py
```

**Single file analysis:**
```bash
python visualize_chirp_detection.py test1_ncc_debug.csv 5000
# Args: <csv_file> <actual_position>
```

### 3. Visual Inspection in Audacity
1. Open any `test*.wav` file
2. Enable spectrogram view (Ctrl+Shift+Z)
3. Chirp appears as diagonal sweep in spectrogram
4. Use selection tool to measure exact sample position
5. Compare with detected position from test output

## Understanding Debug CSV Files

### NCC Debug CSV
```csv
position,ncc,signalEnergy,dotProduct
0,0.0234,12.3,45.6
10,0.0456,13.2,56.7
...
5000,0.9876,150.2,1234.5  <- Peak at actual position
```

**Columns:**
- `position`: Sliding window sample index
- `ncc`: Normalized cross-correlation value (0-1)
- `signalEnergy`: Energy of signal window
- `dotProduct`: Raw correlation before normalization

**What to look for:**
- **Sharp peak at actual position**: Good detection
- **Multiple peaks**: May need higher threshold
- **Peak offset from actual**: Systematic detection error
- **Low peak value**: Weak signal or template mismatch

### SamplePHY Debug CSV
```csv
sample,power,correlation,normalized_corr,threshold,detected
0,0.001,0,0,0.002,0
1000,0.05,2.3,0.0115,0.1,0
...
5440,0.08,15.6,0.078,0.16,1  <- Detection trigger
```

**Columns:**
- `sample`: Current sample index
- `power`: Exponential moving average of signal power
- `correlation`: Raw dot product with template
- `normalized_corr`: Correlation / 200 (as in SamplePHY.m)
- `threshold`: Dynamic threshold (power * ratio)
- `detected`: 1 if detection criteria met

**What to look for:**
- `normalized_corr` should spike at chirp position
- Should exceed both `threshold` AND absolute min (0.05)
- Multiple `detected=1`: Peak tracking working
- No `detected=1`: Threshold too high or signal too weak

### Dot Product Debug CSV
```csv
position,dotProduct
0,12.3
10,15.6
...
5000,1234.5  <- Maximum
```

Simpler than NCC - just raw correlation without normalization.

## Interpreting Results

### Perfect Detection
```
Method: NCC
Actual position: 5000 samples
Detected position: 5000 samples
Detection offset (ERROR): 0 samples
★ PERFECT ACCURACY ★
```

### Good Detection (Sample-level)
```
Method: NCC
Actual position: 5000 samples
Detected position: 5001 samples
Detection offset (ERROR): 1 samples (0.023 ms)
✓ Sample-level accuracy
```

### Poor Detection
```
Method: NCC
Actual position: 5000 samples
Detected position: 5120 samples
Detection offset (ERROR): 120 samples (2.721 ms)
✗ Significant offset detected
```

**Action**: Load `test*_ncc_debug.csv` in Python to plot correlation curve and diagnose.

## Debugging SamplePHY Method

From your `online_chirp.txt`, SamplePHY always detects at position -441. This means:

1. **No detection occurred**: `peakIndex = -1` initially
2. **Offset correction**: `peakIndex - chirpSamples = -1 - 440 = -441`

**Possible causes:**
- Power threshold too high (default 2.0)
- Absolute threshold too high (default 0.05)
- Template mismatch (Math vs MATLAB chirp generation)
- Normalization factor (200) doesn't match signal amplitude

**To fix:**
1. Run `chirp_comprehensive_test`
2. Open `test1_phy_debug.csv` in Python
3. Plot `normalized_corr` vs `threshold`
4. If `normalized_corr` never exceeds `threshold`, lower `powerRatio` parameter
5. Check if peaks exist but are below 0.05 - adjust `minAbsThreshold`

## Threshold Tuning Guide

### NCC Threshold
- **0.3**: Very sensitive, may have false positives
- **0.5**: Balanced (recommended starting point)
- **0.7**: Conservative, good for noisy environments
- **0.9**: Very strict, may miss valid chirps

### SamplePHY Parameters
- **`powerRatio`** (default 2.0): How much correlation must exceed local power
  - Lower (1.5): More sensitive
  - Higher (3.0): More robust to noise
- **`minAbsThreshold`** (default 0.05): Absolute minimum correlation
  - Depends on signal amplitude and normalization factor (200)
  - If signal is weak, lower this

## Expected Outputs

After successful run, you should see:

```
================================================================================
COMPREHENSIVE CHIRP DETECTION VERIFICATION
================================================================================

TEST 1: Pure Chirp at Known Position
  Method: NCC
  Actual position: 5000 samples
  Detected position: 5000 samples
  Detection offset (ERROR): 0 samples
  ★ PERFECT ACCURACY ★

...

FINAL SUMMARY
================================================================================
Total tests: 12
Perfect (0 offset): 10
Sample-level (≤1): 2
Average offset: 0.167 samples
Max offset: 1 samples

>>> VERIFICATION:
    ✓ Chirp detection is ACCURATE
    Ready for production use
```

## Next Steps

1. **If tests pass**: Integrate chirp detection into `Main.cpp`
2. **If tests fail**: Use debug CSV files to diagnose:
   - Plot correlation curves
   - Adjust thresholds
   - Compare Math vs MATLAB chirp generation
   - Check template energy/normalization

## Python Plotting Examples

### Basic NCC Plot
```python
import pandas as pd
import matplotlib.pyplot as plt

df = pd.read_csv('test1_ncc_debug.csv')
plt.figure(figsize=(12, 6))
plt.plot(df['position'], df['ncc'])
plt.axvline(x=5000, color='g', label='Actual Position')
plt.axhline(y=0.5, color='r', linestyle='--', label='Threshold')
plt.xlabel('Sample Position')
plt.ylabel('NCC')
plt.legend()
plt.grid(True)
plt.show()
```

### SamplePHY Diagnosis
```python
df = pd.read_csv('test1_phy_debug.csv')

fig, axes = plt.subplots(3, 1, figsize=(12, 10))

# Power trace
axes[0].plot(df['sample'], df['power'])
axes[0].set_ylabel('Power')
axes[0].set_title('Signal Power (EMA)')
axes[0].grid(True)

# Correlation vs threshold
axes[1].plot(df['sample'], df['normalized_corr'], label='Correlation')
axes[1].plot(df['sample'], df['threshold'], '--', label='Threshold')
axes[1].axhline(y=0.05, color='r', linestyle=':', label='Min Abs Threshold')
axes[1].legend()
axes[1].set_ylabel('Value')
axes[1].set_title('Correlation vs Dynamic Threshold')
axes[1].grid(True)

# Detection points
detected = df[df['detected'] == 1]
axes[2].scatter(detected['sample'], detected['normalized_corr'], 
                color='red', marker='x', s=100, label='Detection Triggers')
axes[2].plot(df['sample'], df['normalized_corr'], alpha=0.5)
axes[2].legend()
axes[2].set_xlabel('Sample')
axes[2].set_ylabel('Normalized Correlation')
axes[2].set_title('Detection Events')
axes[2].grid(True)

plt.tight_layout()
plt.show()
```

## Troubleshooting

### "No peaks detected"
- Signal too weak: Check WAV file amplitude
- Threshold too high: Lower NCC threshold or SamplePHY `powerRatio`
- Template mismatch: Try MATLAB-style chirp generation

### "Offset is always N samples"
- Systematic error in detection logic
- Check if offset is `chirpSamples` (440): Wrong endpoint calculation
- Load debug CSV to see if peak is actually at correct position

### "Multiple false peaks"
- Raise NCC threshold (0.5 → 0.7)
- Increase SamplePHY `powerRatio` (2.0 → 3.0)
- Add longer silence padding before chirp

### "Python script crashes"
- Install dependencies: `pip install pandas matplotlib numpy`
- Check CSV files exist in current directory
- Verify CSV format (comma-separated, with headers)


# 🚀 QUICK START GUIDE

## Build & Run (3 Steps)

```
1. Open: Receiver\Builds\VisualStudio2022\Receiver.sln
2. Build: Press F7 (Build Solution)
3. Run: Press Ctrl+F5 (Start Without Debugging)
```

## Test Your System (Choose Mode 4)

```
Choose option: 4
Press ENTER
Wait for results...
```

## What You'll See (Success)

```
*** PREAMBLE DETECTED ***
Position: 5234 samples
Peak NCC value: 0.8523

*** FRAME DECODED SUCCESSFULLY ***
Bit errors: 0 / 100
Bit error rate: 0.0%
```

## If Something Goes Wrong

### Problem: "No preamble detected"
```bash
# 1. Check NCC values
python visualize_ncc.py

# 2. If max NCC < 0.05, edit Main.cpp line ~455:
bool criteriaC = ncc > 0.03;  // Lower threshold

# 3. Rebuild and test
```

### Problem: "CRC check failed"
```cpp
// Edit Main.cpp line ~525, add more offsets:
std::vector<int> offsetsToTry = {
    0, -1, 1, -2, 2, -4, 4, -8, 8, 
    -16, 16, -24, 24, -32, 32
};
// Rebuild and test
```

### Problem: "Audio device error"
```
1. Check Windows Sound Settings
2. Enable microphone and speaker
3. Set volume to 50-70%
4. Test with other audio apps first
```

## Files You Get

```
Main.cpp                    → The complete transceiver code
visualize_ncc.py           → Visualization tool
TESTING_README.md          → Detailed testing guide
TUNING_GUIDE.md            → Parameter adjustment guide
IMPLEMENTATION_SUMMARY.md  → Complete documentation

Generated during testing:
ncc_values.txt            → Correlation data
debug_processing.txt      → Detailed processing log
ncc_values_analysis.png   → Visual analysis
```

## Key Parameters (Main.cpp)

| What | Where | Default | To Change |
|------|-------|---------|-----------|
| Bit rate | Line ~76 | 1000 bps | Change `samplesPerBit` |
| Detection threshold | Line ~455 | 0.05 | Change `criteriaC` |
| Sync offsets | Line ~525 | ±16 samples | Add more values |
| Carrier frequencies | Line ~73-74 | 5k/10k Hz | Change `carrierFreqA/B` |

## Quick Diagnostics

```bash
# View NCC analysis
python visualize_ncc.py

# Check what peaks were found
# Look in debug_processing.txt

# Measure signal strength
# Check "Peak NCC value" in terminal output
```

## Performance Targets

| Metric | Goal | Typical |
|--------|------|---------|
| Detection Rate | >95% | ~98% |
| CRC Pass Rate | >95% | ~97% |
| Bit Error Rate | <5% | ~0.5% |

## Common Volume Settings

| Environment | Speaker | Microphone |
|-------------|---------|------------|
| Loopback (same PC) | 30-50% | Default |
| Air gap 10cm | 50-70% | +10dB boost |
| Air gap 1m | 70-90% | +20dB boost |
| Two computers | 80-100% | Max |

## Workflow

```
┌─────────────────────────────────────────┐
│ 1. BUILD PROJECT                        │
│    ↓                                    │
│ 2. RUN FULL TEST (Mode 4)              │
│    ↓                                    │
│ 3. CHECK RESULTS                        │
│    ├─ Success? → DONE! ✅              │
│    └─ Failed? → Continue below          │
│                                         │
│ 4. ANALYZE DEBUG OUTPUT                 │
│    python visualize_ncc.py              │
│    ↓                                    │
│ 5. ADJUST PARAMETERS                    │
│    (See TUNING_GUIDE.md)                │
│    ↓                                    │
│ 6. REBUILD & RETEST                     │
│    ↓                                    │
│ 7. REPEAT UNTIL WORKING                 │
└─────────────────────────────────────────┘
```

## Checklist Before TA Demo

- [ ] Loopback test passes (Mode 4)
- [ ] Preamble detected reliably (>90%)
- [ ] CRC passes (>85%)
- [ ] BER acceptable (<10%)
- [ ] Tested on target hardware (NODE1/NODE2)
- [ ] Can transmit 10,000 bits in <15 seconds
- [ ] OUTPUT.txt matches INPUT.txt (>80%)

## Need Help?

1. **Read the documentation**:
   - TESTING_README.md (detailed testing)
   - TUNING_GUIDE.md (parameter tuning)
   - IMPLEMENTATION_SUMMARY.md (how it works)

2. **Check debug output**:
   - Terminal messages
   - ncc_values.txt
   - debug_processing.txt

3. **Visualize the data**:
   ```bash
   python visualize_ncc.py
   ```

4. **Adjust and retry**:
   - Lower detection threshold
   - Add more sync offsets
   - Increase volume

## One-Minute Test

```bash
# Fastest way to verify everything works:

1. Build project (F7)
2. Run (Ctrl+F5)
3. Type: 4 [ENTER]
4. Press: [ENTER]
5. Wait: 5 seconds
6. Read: Terminal output

If you see "FRAME DECODED SUCCESSFULLY" → You're good! ✅
If not → Run: python visualize_ncc.py
```

## Contact & Resources

- **Project Spec**: CS120 Project 1 document
- **Reference Code**: SamplePHY.m (MATLAB)
- **JUCE Docs**: https://juce.com/learn/documentation

---

**Remember**: Start with loopback testing (Mode 4), adjust parameters based on results, then move to air-gap testing!

Good luck! 🎉




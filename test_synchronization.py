#!/usr/bin/env python3
"""
Synchronization Validation Test
Compares INPUT.txt with OUTPUT.txt and reports sync quality
"""

import numpy as np
from pathlib import Path
import re

def hamming_distance(s1, s2):
    """Calculate bit error count"""
    return sum(c1 != c2 for c1, c2 in zip(s1, s2))

def analyze_bit_errors(input_bits, output_bits):
    """Analyze error patterns to diagnose sync issues"""
    
    if len(input_bits) != len(output_bits):
        print(f"⚠️  Length mismatch: INPUT={len(input_bits)}, OUTPUT={len(output_bits)}")
        min_len = min(len(input_bits), len(output_bits))
        input_bits = input_bits[:min_len]
        output_bits = output_bits[:min_len]
    
    errors = []
    for i, (expected, actual) in enumerate(zip(input_bits, output_bits)):
        if expected != actual:
            errors.append(i)
    
    total_bits = len(input_bits)
    error_count = len(errors)
    ber = error_count / total_bits if total_bits > 0 else 0
    
    print("="*70)
    print("SYNCHRONIZATION VALIDATION REPORT")
    print("="*70)
    print(f"\nTotal bits: {total_bits}")
    print(f"Bit errors: {error_count}")
    print(f"Bit Error Rate (BER): {ber*100:.2f}%")
    print(f"Success Rate: {(1-ber)*100:.2f}%")
    
    if error_count == 0:
        print("\n✅ PERFECT SYNCHRONIZATION - No errors detected!")
        return
    
    # Analyze error distribution
    print(f"\n--- Error Distribution ---")
    
    # Early errors (first 20%)
    early_threshold = int(total_bits * 0.2)
    early_errors = [e for e in errors if e < early_threshold]
    
    # Late errors (last 20%)
    late_threshold = int(total_bits * 0.8)
    late_errors = [e for e in errors if e >= late_threshold]
    
    # Middle errors
    middle_errors = [e for e in errors if early_threshold <= e < late_threshold]
    
    print(f"First 20% ({early_threshold} bits): {len(early_errors)} errors ({len(early_errors)/early_threshold*100:.1f}%)")
    print(f"Middle 60%: {len(middle_errors)} errors")
    print(f"Last 20% ({total_bits - late_threshold} bits): {len(late_errors)} errors ({len(late_errors)/(total_bits-late_threshold)*100:.1f}%)")
    
    # Detect patterns
    print(f"\n--- Error Pattern Analysis ---")
    
    if len(early_errors) > error_count * 0.5:
        print("⚠️  EARLY ERROR CONCENTRATION detected!")
        print("   → Likely cause: Frame start offset")
        print("   → Solution: Adjust findOptimalFrameStart() search range or manual offset")
        
    elif len(late_errors) > error_count * 0.5:
        print("⚠️  LATE ERROR CONCENTRATION detected!")
        print("   → Likely cause: Clock drift / sample rate mismatch")
        print("   → Solution: Verify sample rates match exactly (44100.0 Hz)")
        
    else:
        print("ℹ️  Errors distributed across frame")
        print("   → Likely cause: Noise or low SNR")
        print("   → Solution: Improve acoustic environment, increase volume")
    
    # Check for burst errors vs random errors
    if len(errors) > 1:
        gaps = [errors[i+1] - errors[i] for i in range(len(errors)-1)]
        avg_gap = np.mean(gaps)
        std_gap = np.std(gaps)
        
        if std_gap < avg_gap * 0.3:
            print(f"\n⚠️  PERIODIC ERRORS detected (avg gap: {avg_gap:.1f} bits)")
            print("   → Likely cause: Systematic offset every N bits")
            print("   → Solution: Check for interference pattern or timing jitter")
        
        burst_threshold = 10  # errors within 10 bits = burst
        bursts = []
        current_burst = [errors[0]]
        
        for i in range(1, len(errors)):
            if errors[i] - errors[i-1] <= burst_threshold:
                current_burst.append(errors[i])
            else:
                if len(current_burst) >= 3:
                    bursts.append(current_burst)
                current_burst = [errors[i]]
        
        if len(current_burst) >= 3:
            bursts.append(current_burst)
        
        if bursts:
            print(f"\n⚠️  BURST ERRORS detected: {len(bursts)} bursts")
            for i, burst in enumerate(bursts[:3]):  # Show first 3
                print(f"   Burst {i+1}: bits {burst[0]}-{burst[-1]} ({len(burst)} errors)")
            print("   → Likely cause: Interference or signal dropout")
    
    # Show first few errors for manual inspection
    print(f"\n--- First 10 Error Positions ---")
    for i, err_pos in enumerate(errors[:10]):
        context_start = max(0, err_pos - 2)
        context_end = min(total_bits, err_pos + 3)
        
        expected_context = input_bits[context_start:context_end]
        actual_context = output_bits[context_start:context_end]
        
        print(f"Error {i+1} at bit {err_pos}:")
        print(f"  Expected: ...{expected_context}...")
        print(f"  Received: ...{actual_context}...")
        print(f"             {'  ' * (err_pos - context_start)}^")
    
    # Recommendation
    print("\n" + "="*70)
    print("💡 RECOMMENDATIONS:")
    
    if ber < 0.01:
        print("✓ Excellent performance (BER < 1%)")
        print("  Minor tuning may improve to 100%")
    elif ber < 0.05:
        print("⚠️  Acceptable performance (BER < 5%)")
        print("  1. Check debug_sync_analysis.png for offset issues")
        print("  2. Review demodulation confidence scores")
    elif ber < 0.15:
        print("⚠️  Poor performance (BER < 15%)")
        print("  1. Verify NCC peak is detected correctly")
        print("  2. Check if findOptimalFrameStart() is working")
        print("  3. Increase signal strength")
    else:
        print("❌ Critical failure (BER > 15%)")
        print("  1. Run diagnose_sync_offset.py to check NCC detection")
        print("  2. Verify preamble is present in debug_loopback_recording.wav")
        print("  3. Check if threshold is too high")
    
    print("="*70)

def test_offset_simulation():
    """Simulate what happens with various offsets"""
    print("\n" + "="*70)
    print("OFFSET SIMULATION TEST")
    print("="*70)
    
    # Generate test pattern
    test_bits = '01' * 100 + '1100' * 50 + '0011' * 50 + '10' * 100
    
    print(f"\nSimulating offsets on {len(test_bits)} bit test pattern...")
    
    for offset in [0, 1, 5, 10, 22]:  # 22 = half bit period
        # Simulate offset by shifting
        if offset == 0:
            received = test_bits
        else:
            # In reality, offset causes bits to be sampled from wrong positions
            # Simplified: every Nth bit gets corrupted
            corruption_rate = offset / 44.0  # 44 samples per bit
            received = list(test_bits)
            for i in range(len(received)):
                if np.random.random() < corruption_rate:
                    received[i] = '1' if received[i] == '0' else '0'
            received = ''.join(received)
        
        errors = hamming_distance(test_bits, received)
        ber = errors / len(test_bits)
        
        print(f"Offset {offset:2d} samples: {errors:3d} errors (BER={ber*100:.1f}%)")
    
    print("\nThis shows why offset correction is critical!")
    print("Even 5 sample offset can cause ~11% BER")

def main():
    input_file = Path("INPUT.txt")
    output_file = Path("OUTPUT.txt")
    
    if not input_file.exists():
        print("❌ ERROR: INPUT.txt not found")
        return
    
    if not output_file.exists():
        print("❌ ERROR: OUTPUT.txt not found")
        print("   Run the receiver first to generate OUTPUT.txt")
        return
    
    input_bits = input_file.read_text().strip()
    output_bits = output_file.read_text().strip()
    
    # Validate format
    if not re.match(r'^[01]+$', input_bits):
        print("❌ ERROR: INPUT.txt contains non-binary characters")
        return
    
    if not re.match(r'^[01]+$', output_bits):
        print("❌ ERROR: OUTPUT.txt contains non-binary characters")
        return
    
    analyze_bit_errors(input_bits, output_bits)
    
    # Optional: run simulation
    response = input("\nRun offset simulation? (y/n): ").strip().lower()
    if response == 'y':
        test_offset_simulation()

if __name__ == "__main__":
    main()


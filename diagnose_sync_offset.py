#!/usr/bin/env python3
"""
Synchronization Offset Diagnostic Tool
Analyzes NCC output and provides recommendations for fixing timing issues
"""

import numpy as np
import matplotlib.pyplot as plt
from pathlib import Path

def analyze_ncc_file(ncc_csv_path="Receiver\Builds\VisualStudio2022\debug_ncc_output.csv"):
    """Analyze the NCC correlation output to detect synchronization issues"""
    
    if not Path(ncc_csv_path).exists():
        print(f"ERROR: {ncc_csv_path} not found. Run the receiver first!")
        return
    
    # Load NCC data
    data = np.loadtxt(ncc_csv_path, delimiter=',')
    
    if data.ndim == 2:
        # Format: sample_index, ncc_value
        sample_indices = data[:, 0].astype(int)
        ncc_values = data[:, 1]
    else:
        # Legacy format: just ncc values
        ncc_values = data
        sample_indices = np.arange(len(ncc_values))
    
    sample_rate = 44100.0
    time_axis = sample_indices / sample_rate
    
    # Detect peaks
    threshold = 0.35  # Match the C++ threshold
    peaks = []
    
    for i in range(1, len(ncc_values) - 1):
        if ncc_values[i] > threshold:
            # Check if it's a local maximum
            if ncc_values[i] > ncc_values[i-1] and ncc_values[i] > ncc_values[i+1]:
                peaks.append({
                    'sample': sample_indices[i],
                    'time': time_axis[i],
                    'ncc': ncc_values[i]
                })
    
    # Filter peaks - only keep the highest in each 0.1s window
    if peaks:
        filtered_peaks = []
        current_peak = peaks[0]
        
        for peak in peaks[1:]:
            if peak['time'] - current_peak['time'] < 0.1:
                # Same peak region - keep the higher one
                if peak['ncc'] > current_peak['ncc']:
                    current_peak = peak
            else:
                filtered_peaks.append(current_peak)
                current_peak = peak
        filtered_peaks.append(current_peak)
        peaks = filtered_peaks
    
    # Print analysis
    print("="*70)
    print("SYNCHRONIZATION OFFSET DIAGNOSTIC REPORT")
    print("="*70)
    print(f"\nNCC Data: {len(ncc_values)} samples ({time_axis[-1]:.2f} seconds)")
    print(f"Detection threshold: {threshold}")
    print(f"\nPeaks detected: {len(peaks)}")
    
    if not peaks:
        print("\n⚠️  WARNING: NO PEAKS DETECTED!")
        print("   Possible issues:")
        print("   1. Threshold too high - try lowering from 0.35 to 0.2")
        print("   2. Signal too weak - check speaker/microphone volume")
        print("   3. Recording started too late - increase silent leader duration")
        max_ncc = np.max(ncc_values)
        print(f"\n   Maximum NCC value found: {max_ncc:.4f}")
        print(f"   Recommended threshold: {max_ncc * 0.7:.4f}")
    else:
        print("\n--- Detected Preambles ---")
        for i, peak in enumerate(peaks):
            print(f"  Peak {i+1}: t={peak['time']:.3f}s, sample={peak['sample']}, NCC={peak['ncc']:.4f}")
        
        # Analyze peak quality
        avg_ncc = np.mean([p['ncc'] for p in peaks])
        print(f"\n--- Peak Quality Analysis ---")
        print(f"  Average peak NCC: {avg_ncc:.4f}")
        print(f"  Max peak NCC: {max([p['ncc'] for p in peaks]):.4f}")
        print(f"  Min peak NCC: {min([p['ncc'] for p in peaks]):.4f}")
        
        if avg_ncc < 0.5:
            print("\n  ⚠️  Low correlation - possible issues:")
            print("     • Acoustic reflections/multipath")
            print("     • Background noise")
            print("     • Sample rate mismatch")
        elif avg_ncc > 0.8:
            print("\n  ✓ Excellent correlation quality!")
        else:
            print("\n  ✓ Acceptable correlation quality")
    
    # Offset analysis
    print("\n--- Synchronization Offset Analysis ---")
    samples_per_bit = int(sample_rate / 1000.0)  # bitRate = 1000
    preamble_samples = 440
    
    if peaks:
        # Check expected data start alignment
        for i, peak in enumerate(peaks[:3]):  # Analyze first 3 peaks
            data_start_sample = peak['sample'] + 1
            offset_within_bit = data_start_sample % samples_per_bit
            
            print(f"\nPeak {i+1} alignment:")
            print(f"  Data should start at sample: {data_start_sample}")
            print(f"  Offset within bit period: {offset_within_bit}/{samples_per_bit} samples")
            print(f"  Alignment error: {min(offset_within_bit, samples_per_bit - offset_within_bit)} samples")
            
            if offset_within_bit > samples_per_bit / 4 and offset_within_bit < 3 * samples_per_bit / 4:
                print(f"  ⚠️  WARNING: Significant offset detected!")
                print(f"     This will cause bit errors. Use fine-tuning in findOptimalFrameStart()")
    
    # Create visualization
    fig, axes = plt.subplots(2, 1, figsize=(14, 10))
    
    # Full NCC waveform
    axes[0].plot(time_axis, ncc_values, linewidth=0.5, alpha=0.7)
    axes[0].axhline(y=threshold, color='r', linestyle='--', label=f'Threshold ({threshold})')
    
    if peaks:
        peak_times = [p['time'] for p in peaks]
        peak_nccs = [p['ncc'] for p in peaks]
        axes[0].scatter(peak_times, peak_nccs, color='red', s=100, zorder=5, label='Detected Peaks')
    
    axes[0].set_xlabel('Time (seconds)')
    axes[0].set_ylabel('Normalized Cross-Correlation')
    axes[0].set_title('NCC Correlation Over Time')
    axes[0].grid(True, alpha=0.3)
    axes[0].legend()
    
    # Zoomed view of first peak (if exists)
    if peaks:
        first_peak_time = peaks[0]['time']
        zoom_start = max(0, first_peak_time - 0.05)
        zoom_end = min(time_axis[-1], first_peak_time + 0.15)
        
        zoom_mask = (time_axis >= zoom_start) & (time_axis <= zoom_end)
        axes[1].plot(time_axis[zoom_mask], ncc_values[zoom_mask], linewidth=1)
        axes[1].axhline(y=threshold, color='r', linestyle='--', label='Threshold')
        axes[1].axvline(x=peaks[0]['time'], color='g', linestyle='--', label='Peak')
        axes[1].axvline(x=peaks[0]['time'] + preamble_samples/sample_rate, 
                       color='orange', linestyle='--', label='Expected Data Start')
        
        axes[1].set_xlabel('Time (seconds)')
        axes[1].set_ylabel('Normalized Cross-Correlation')
        axes[1].set_title(f'Zoomed View: First Preamble at {first_peak_time:.3f}s')
        axes[1].grid(True, alpha=0.3)
        axes[1].legend()
    else:
        axes[1].text(0.5, 0.5, 'No peaks detected', ha='center', va='center', 
                    transform=axes[1].transAxes, fontsize=14)
    
    plt.tight_layout()
    plt.savefig('debug_sync_analysis.png', dpi=150)
    print(f"\n📊 Visualization saved to: debug_sync_analysis.png")
    plt.show()
    
    print("\n" + "="*70)
    
    # Recommendations
    print("\n💡 RECOMMENDATIONS:")
    if not peaks:
        print("  1. Lower NCC_DETECTION_THRESHOLD in Main.cpp")
        print("  2. Increase audio output volume")
        print("  3. Reduce background noise")
    elif avg_ncc < 0.5:
        print("  1. Improve acoustic environment (reduce reflections)")
        print("  2. Position microphone closer to speaker")
        print("  3. Consider using cable loopback instead of acoustic")
    else:
        print("  1. Use the findOptimalFrameStart() function to correct small offsets")
        print("  2. Monitor 'weak bits' percentage - should be < 10%")
        print("  3. Check CRC success rate")
    
    print("="*70)

if __name__ == "__main__":
    analyze_ncc_file()


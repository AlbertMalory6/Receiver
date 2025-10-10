#!/usr/bin/env python3
"""
Visualization tool for NCC values from the integrated transceiver
Helps debug preamble detection issues
"""

import matplotlib.pyplot as plt
import numpy as np
import sys

def load_ncc_data(filename='ncc_values.txt'):
    """Load NCC data from file"""
    try:
        data = np.loadtxt(filename)
        if len(data.shape) == 1:
            # Only one column
            indices = np.arange(len(data))
            ncc_values = data
            power_values = np.zeros_like(data)
        elif data.shape[1] >= 2:
            indices = data[:, 0]
            ncc_values = data[:, 1]
            if data.shape[1] >= 3:
                power_values = data[:, 2]
            else:
                power_values = np.zeros_like(ncc_values)
        else:
            print(f"Error: Unexpected data format in {filename}")
            return None, None, None
        
        return indices, ncc_values, power_values
    except Exception as e:
        print(f"Error loading {filename}: {e}")
        return None, None, None

def plot_ncc_analysis(indices, ncc_values, power_values, sample_rate=48000):
    """Create comprehensive NCC analysis plots"""
    
    # Create figure with subplots
    fig, axes = plt.subplots(3, 1, figsize=(14, 10))
    fig.suptitle('Preamble Detection Analysis', fontsize=16, fontweight='bold')
    
    # Convert sample indices to time
    time = indices / sample_rate
    
    # Plot 1: NCC values
    ax1 = axes[0]
    ax1.plot(time, ncc_values, 'b-', linewidth=0.5, label='NCC')
    ax1.axhline(y=0.01, color='r', linestyle='--', linewidth=1, label='Threshold (0.01)')
    
    # Find peaks above threshold
    threshold = 0.01
    peaks = np.where(ncc_values > threshold)[0]
    if len(peaks) > 0:
        ax1.plot(time[peaks], ncc_values[peaks], 'ro', markersize=3, label='Above threshold')
        max_idx = np.argmax(ncc_values)
        ax1.plot(time[max_idx], ncc_values[max_idx], 'g*', markersize=15, 
                label=f'Max NCC = {ncc_values[max_idx]:.3f}')
    
    ax1.set_xlabel('Time (seconds)')
    ax1.set_ylabel('NCC Value')
    ax1.set_title('Normalized Cross-Correlation with Chirp Preamble')
    ax1.grid(True, alpha=0.3)
    ax1.legend()
    
    # Plot 2: Power estimate
    ax2 = axes[1]
    ax2.plot(time, power_values, 'g-', linewidth=0.5, label='Power')
    ax2.plot(time, power_values * 2, 'r--', linewidth=1, label='Power × 2 (threshold)')
    ax2.set_xlabel('Time (seconds)')
    ax2.set_ylabel('Power')
    ax2.set_title('Signal Power Estimate')
    ax2.grid(True, alpha=0.3)
    ax2.legend()
    
    # Plot 3: Detection criteria
    ax3 = axes[2]
    criteria = (ncc_values > power_values * 2) & (ncc_values > 0.01)
    ax3.plot(time, criteria.astype(float), 'r-', linewidth=1)
    ax3.fill_between(time, 0, criteria.astype(float), alpha=0.3, color='red')
    ax3.set_xlabel('Time (seconds)')
    ax3.set_ylabel('Detection Criteria Met')
    ax3.set_title('Detection Criteria: (NCC > Power×2) AND (NCC > 0.01)')
    ax3.set_ylim([-0.1, 1.1])
    ax3.grid(True, alpha=0.3)
    
    plt.tight_layout()
    
    # Print statistics
    print("\n" + "="*60)
    print("NCC ANALYSIS STATISTICS")
    print("="*60)
    print(f"Total samples analyzed: {len(ncc_values)}")
    print(f"Duration: {time[-1]:.3f} seconds")
    print(f"\nNCC Statistics:")
    print(f"  Max NCC value: {np.max(ncc_values):.4f}")
    print(f"  Mean NCC value: {np.mean(ncc_values):.4f}")
    print(f"  Std NCC value: {np.std(ncc_values):.4f}")
    print(f"  Samples above 0.01: {np.sum(ncc_values > 0.01)}")
    
    if len(power_values) > 0 and np.max(power_values) > 0:
        print(f"\nPower Statistics:")
        print(f"  Max power: {np.max(power_values):.6f}")
        print(f"  Mean power: {np.mean(power_values):.6f}")
        print(f"  Std power: {np.std(power_values):.6f}")
        
        # Detection analysis
        crit_a = ncc_values > power_values * 2
        crit_b = ncc_values > 0.01
        combined = crit_a & crit_b
        
        print(f"\nDetection Criteria:")
        print(f"  Samples where NCC > Power×2: {np.sum(crit_a)}")
        print(f"  Samples where NCC > 0.01: {np.sum(crit_b)}")
        print(f"  Samples meeting both: {np.sum(combined)}")
        
        if np.sum(combined) > 0:
            detection_regions = np.where(combined)[0]
            print(f"\nPotential detection regions:")
            
            # Find continuous regions
            regions = []
            start = detection_regions[0]
            for i in range(1, len(detection_regions)):
                if detection_regions[i] - detection_regions[i-1] > 200:
                    regions.append((start, detection_regions[i-1]))
                    start = detection_regions[i]
            regions.append((start, detection_regions[-1]))
            
            for i, (s, e) in enumerate(regions[:5]):  # Show first 5 regions
                print(f"  Region {i+1}: samples {int(indices[s])} to {int(indices[e])} "
                      f"({time[s]:.3f}s to {time[e]:.3f}s)")
                print(f"    Max NCC in region: {np.max(ncc_values[s:e+1]):.4f}")
    
    print("="*60)
    
    return fig

def main():
    filename = 'ncc_values.txt'
    
    if len(sys.argv) > 1:
        filename = sys.argv[1]
    
    print(f"Loading NCC data from: {filename}")
    
    indices, ncc_values, power_values = load_ncc_data(filename)
    
    if indices is None:
        print("Failed to load data. Make sure to run the receiver first!")
        return
    
    print(f"Loaded {len(indices)} data points")
    
    # Create plots
    fig = plot_ncc_analysis(indices, ncc_values, power_values)
    
    # Save figure
    output_file = filename.replace('.txt', '_analysis.png')
    fig.savefig(output_file, dpi=150, bbox_inches='tight')
    print(f"\nPlot saved to: {output_file}")
    
    # Show interactive plot
    plt.show()

if __name__ == '__main__':
    main()



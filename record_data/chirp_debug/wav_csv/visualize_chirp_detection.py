"""
Visualize Chirp Detection Debug Data

This script loads the CSV debug files from chirp_comprehensive_test
and plots the correlation curves to help diagnose detection accuracy.
"""

import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import os
import glob

def plot_ncc_debug(csv_file, actual_position=None):
    """Plot NCC sliding window correlation"""
    if not os.path.exists(csv_file):
        print(f"File not found: {csv_file}")
        return
    
    df = pd.read_csv(csv_file)
    
    plt.figure(figsize=(14, 8))
    
    # Plot 1: NCC values
    plt.subplot(3, 1, 1)
    plt.plot(df['position'], df['ncc'], linewidth=0.8)
    plt.axhline(y=0.5, color='r', linestyle='--', label='Threshold 0.5', alpha=0.7)
    plt.axhline(y=0.7, color='orange', linestyle='--', label='Threshold 0.7', alpha=0.7)
    if actual_position is not None:
        plt.axvline(x=actual_position, color='g', linestyle='-', linewidth=2, 
                   label=f'Actual Position {actual_position}', alpha=0.8)
    
    detected_pos = df.loc[df['ncc'].idxmax(), 'position']
    plt.axvline(x=detected_pos, color='b', linestyle='--', linewidth=2, 
               label=f'Detected Position {int(detected_pos)}', alpha=0.8)
    
    plt.xlabel('Sample Position')
    plt.ylabel('NCC Value')
    plt.title(f'Normalized Cross-Correlation\nFile: {os.path.basename(csv_file)}')
    plt.legend()
    plt.grid(True, alpha=0.3)
    
    # Plot 2: Signal energy
    plt.subplot(3, 1, 2)
    plt.plot(df['position'], df['signalEnergy'], linewidth=0.8, color='orange')
    if actual_position is not None:
        plt.axvline(x=actual_position, color='g', linestyle='-', linewidth=2, alpha=0.8)
    plt.xlabel('Sample Position')
    plt.ylabel('Signal Energy')
    plt.title('Signal Window Energy')
    plt.grid(True, alpha=0.3)
    
    # Plot 3: Dot product (before normalization)
    plt.subplot(3, 1, 3)
    plt.plot(df['position'], df['dotProduct'], linewidth=0.8, color='purple')
    if actual_position is not None:
        plt.axvline(x=actual_position, color='g', linestyle='-', linewidth=2, alpha=0.8)
    plt.xlabel('Sample Position')
    plt.ylabel('Dot Product')
    plt.title('Raw Dot Product (before normalization)')
    plt.grid(True, alpha=0.3)
    
    plt.tight_layout()
    
    # Print summary
    max_ncc = df['ncc'].max()
    max_pos = df.loc[df['ncc'].idxmax(), 'position']
    
    print(f"\n{'='*60}")
    print(f"Analysis: {os.path.basename(csv_file)}")
    print(f"{'='*60}")
    print(f"Max NCC: {max_ncc:.6f} at position {int(max_pos)}")
    if actual_position is not None:
        offset = int(max_pos - actual_position)
        print(f"Actual position: {actual_position}")
        print(f"Detection offset: {offset} samples ({abs(offset)/44100*1000:.3f} ms)")
        
        if abs(offset) <= 1:
            print("✓ SAMPLE-LEVEL ACCURACY ACHIEVED")
        elif abs(offset) <= 10:
            print("⚠ Within 10 samples (sub-millisecond)")
        else:
            print(f"✗ Significant offset: {offset} samples")
    
    # Find peaks above threshold
    threshold = 0.5
    peaks = df[df['ncc'] > threshold]
    print(f"\nPeaks above {threshold}: {len(peaks)}")
    if len(peaks) > 0:
        print(f"  Top 3 peaks:")
        for i, (idx, row) in enumerate(peaks.nlargest(3, 'ncc').iterrows()):
            print(f"    {i+1}. Position {int(row['position'])}, NCC={row['ncc']:.4f}")
    
    return plt.gcf()

def plot_samplephy_debug(csv_file, actual_position=None):
    """Plot SamplePHY method debug data"""
    if not os.path.exists(csv_file):
        print(f"File not found: {csv_file}")
        return
    
    df = pd.read_csv(csv_file)
    
    plt.figure(figsize=(14, 10))
    
    # Plot 1: Power estimate
    plt.subplot(4, 1, 1)
    plt.plot(df['sample'], df['power'], linewidth=0.8, color='red')
    plt.xlabel('Sample')
    plt.ylabel('Power (EMA)')
    plt.title(f'Power Estimation (Exponential Moving Average)\nFile: {os.path.basename(csv_file)}')
    plt.grid(True, alpha=0.3)
    
    # Plot 2: Raw correlation
    plt.subplot(4, 1, 2)
    plt.plot(df['sample'], df['correlation'], linewidth=0.8, color='blue')
    if actual_position is not None:
        plt.axvline(x=actual_position, color='g', linestyle='-', linewidth=2, 
                   label=f'Actual {actual_position}', alpha=0.8)
    plt.xlabel('Sample')
    plt.ylabel('Correlation')
    plt.title('Raw Correlation Value')
    plt.legend()
    plt.grid(True, alpha=0.3)
    
    # Plot 3: Normalized correlation
    plt.subplot(4, 1, 3)
    plt.plot(df['sample'], df['normalized_corr'], linewidth=0.8, color='purple')
    plt.plot(df['sample'], df['threshold'], linewidth=0.8, color='orange', 
            linestyle='--', label='Dynamic Threshold', alpha=0.7)
    plt.axhline(y=0.05, color='r', linestyle='--', label='Min Threshold 0.05', alpha=0.7)
    if actual_position is not None:
        plt.axvline(x=actual_position, color='g', linestyle='-', linewidth=2, alpha=0.8)
    plt.xlabel('Sample')
    plt.ylabel('Value')
    plt.title('Normalized Correlation vs Threshold')
    plt.legend()
    plt.grid(True, alpha=0.3)
    
    # Plot 4: Detection markers
    plt.subplot(4, 1, 4)
    detected_samples = df[df['detected'] == 1]
    plt.scatter(detected_samples['sample'], detected_samples['normalized_corr'], 
               color='red', s=50, marker='x', label='Detection Triggers', zorder=5)
    plt.plot(df['sample'], df['normalized_corr'], linewidth=0.8, color='purple', alpha=0.5)
    if actual_position is not None:
        plt.axvline(x=actual_position, color='g', linestyle='-', linewidth=2, alpha=0.8)
    plt.xlabel('Sample')
    plt.ylabel('Normalized Correlation')
    plt.title('Detection Trigger Points')
    plt.legend()
    plt.grid(True, alpha=0.3)
    
    plt.tight_layout()
    
    # Print summary
    print(f"\n{'='*60}")
    print(f"Analysis: {os.path.basename(csv_file)}")
    print(f"{'='*60}")
    
    if len(detected_samples) > 0:
        peak_sample = detected_samples['sample'].iloc[-1]  # Last detection
        peak_corr = detected_samples['normalized_corr'].iloc[-1]
        print(f"Detection at sample: {int(peak_sample)}")
        print(f"Peak correlation: {peak_corr:.6f}")
        
        if actual_position is not None:
            offset = int(peak_sample - actual_position)
            print(f"Actual position: {actual_position}")
            print(f"Detection offset: {offset} samples ({abs(offset)/44100*1000:.3f} ms)")
    else:
        print("⚠ NO DETECTION OCCURRED")
        print("  Possible causes:")
        print("  - Threshold too high")
        print("  - Signal too weak")
        print("  - Chirp template mismatch")
    
    return plt.gcf()

def plot_dot_product_debug(csv_file, actual_position=None):
    """Plot simple dot product correlation"""
    if not os.path.exists(csv_file):
        print(f"File not found: {csv_file}")
        return
    
    df = pd.read_csv(csv_file)
    
    plt.figure(figsize=(12, 6))
    plt.plot(df['position'], df['dotProduct'], linewidth=0.8)
    
    if actual_position is not None:
        plt.axvline(x=actual_position, color='g', linestyle='-', linewidth=2, 
                   label=f'Actual Position {actual_position}', alpha=0.8)
    
    detected_pos = df.loc[df['dotProduct'].idxmax(), 'position']
    plt.axvline(x=detected_pos, color='b', linestyle='--', linewidth=2, 
               label=f'Detected Position {int(detected_pos)}', alpha=0.8)
    
    plt.xlabel('Sample Position')
    plt.ylabel('Dot Product')
    plt.title(f'Dot Product Correlation\nFile: {os.path.basename(csv_file)}')
    plt.legend()
    plt.grid(True, alpha=0.3)
    
    plt.tight_layout()
    
    max_dot = df['dotProduct'].max()
    max_pos = df.loc[df['dotProduct'].idxmax(), 'position']
    
    print(f"\n{'='*60}")
    print(f"Analysis: {os.path.basename(csv_file)}")
    print(f"{'='*60}")
    print(f"Max dot product: {max_dot:.2f} at position {int(max_pos)}")
    if actual_position is not None:
        offset = int(max_pos - actual_position)
        print(f"Actual position: {actual_position}")
        print(f"Detection offset: {offset} samples")
    
    return plt.gcf()

def analyze_all_tests():
    """Analyze all test CSV files in current directory"""
    print("\n" + "="*70)
    print("CHIRP DETECTION DEBUG ANALYSIS")
    print("="*70)
    
    # Find all debug CSV files
    ncc_files = glob.glob("rt_test*_ncc_debug.csv")
    dot_files = glob.glob("rt_test*_dot_debug.csv")
    phy_files = glob.glob("rt_test*_phy_debug.csv")
    
    if not ncc_files and not dot_files and not phy_files:
        print("\n No debug CSV files found!")
        print("Run chirp_comprehensive_test first to generate test data.")
        return
    
    # Known positions for each test
    positions = {
        'test1': 5000,
        'test2': 4410,  # 100ms marker = 4410 samples
        'test3a': 3000,
        'test3b': 3000,
        'test4_pos1000': 1000,
        'test4_pos5000': 5000,
        'test4_pos10000': 10000,
        'test4_pos15000': 15000,
        'test4_pos20000': 20000,
        'test5': 5000,
    }
    
    # Analyze NCC files
    for ncc_file in sorted(ncc_files):
        basename = os.path.basename(ncc_file).replace('_ncc_debug.csv', '')
        actual_pos = positions.get(basename, None)
        
        fig = plot_ncc_debug(ncc_file, actual_pos)
        plt.savefig(ncc_file.replace('.csv', '.png'), dpi=150)
        plt.close(fig)
    
    # Analyze Dot Product files
    for dot_file in sorted(dot_files):
        basename = os.path.basename(dot_file).replace('_dot_debug.csv', '')
        actual_pos = positions.get(basename, None)
        
        fig = plot_dot_product_debug(dot_file, actual_pos)
        plt.savefig(dot_file.replace('.csv', '.png'), dpi=150)
        plt.close(fig)
    
    # Analyze SamplePHY files
    for phy_file in sorted(phy_files):
        basename = os.path.basename(phy_file).replace('_phy_debug.csv', '')
        actual_pos = positions.get(basename, None)
        
        fig = plot_samplephy_debug(phy_file, actual_pos)
        plt.savefig(phy_file.replace('.csv', '.png'), dpi=150)
        plt.close(fig)
    
    print("\n" + "="*70)
    print("Analysis complete! PNG files saved for each CSV.")
    print("="*70)

if __name__ == "__main__":
    import sys
    
    if len(sys.argv) > 1:
        # Analyze specific file
        csv_file = sys.argv[1]
        actual_pos = int(sys.argv[2]) if len(sys.argv) > 2 else None
        
        if 'ncc' in csv_file:
            plot_ncc_debug(csv_file, actual_pos)
        elif 'dot' in csv_file:
            plot_dot_product_debug(csv_file, actual_pos)
        elif 'phy' in csv_file:
            plot_samplephy_debug(csv_file, actual_pos)
        
        plt.show()
    else:
        # Analyze all test files
        analyze_all_tests()


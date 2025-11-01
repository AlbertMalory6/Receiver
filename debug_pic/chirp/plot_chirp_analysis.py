"""
Chirp Detection Visualization Script
====================================

Plots the recorded audio waveform alongside the NCC or Dot Product correlation values
to validate chirp detection accuracy.

This script reads:
- recorded_chirp_signal.wav (the recorded audio)
- chirp_correlation_log.csv (the correlation scores)

And generates:
- chirp_detection_analysis.png (visualization plot)

All files are in: D:\fourth_year\cs120\debug_pic\chirp\
"""

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from scipy.io import wavfile
import os

# ==============================================================================
#  CONFIGURATION
# ==============================================================================
DATA_PATH = r"D:\fourth_year\cs120\debug_pic\chirp"
RECORDED_WAV_FILE = "recorded_chirp_signal.wav"
CORRELATION_LOG_FILE = "chirp_correlation_log.csv"
OUTPUT_PLOT_FILE = "chirp_detection_analysis.png"

# Chirp template length (must match the chirp generation)
PREAMBLE_SAMPLES = 440

def plot_analysis():
    """Main plotting function"""
    print("="*70)
    print(" " + "CHIRP DETECTION VISUALIZATION")
    print("="*70)
    print(f"\nLoading data from: {DATA_PATH}")
    
    # Construct full file paths
    wav_path = os.path.join(DATA_PATH, RECORDED_WAV_FILE)
    csv_path = os.path.join(DATA_PATH, CORRELATION_LOG_FILE)
    plot_path = os.path.join(DATA_PATH, OUTPUT_PLOT_FILE)
    
    # --- 1. Load Recorded Audio ---
    print("\n[STEP 1] Loading recorded audio...")
    try:
        sample_rate, wav_data = wavfile.read(wav_path)
        print(f"  ✓ Loaded: {wav_path}")
        print(f"    Sample Rate: {sample_rate} Hz")
        print(f"    Samples: {len(wav_data)}")
        print(f"    Duration: {len(wav_data)/sample_rate:.3f} seconds")
    except FileNotFoundError:
        print(f"\n✗ ERROR: Cannot find WAV file: {wav_path}")
        print("Please run chirp_test_standalone.cpp first to generate test data.")
        input("\nPress ENTER to exit...")
        return
    except Exception as e:
        print(f"\n✗ ERROR: Could not read WAV file. {e}")
        input("\nPress ENTER to exit...")
        return
    
    # Normalize audio data to [-1.0, 1.0] for plotting
    if np.issubdtype(wav_data.dtype, np.integer):
        wav_data_norm = wav_data.astype(np.float32) / np.iinfo(wav_data.dtype).max
    else:
        wav_data_norm = wav_data.astype(np.float32)
    
    # Create time axis for waveform
    wav_time = np.arange(len(wav_data_norm)) / sample_rate
    
    # --- 2. Load Correlation Data ---
    print("\n[STEP 2] Loading correlation data...")
    try:
        corr_data = pd.read_csv(csv_path)
        print(f"  ✓ Loaded: {csv_path}")
        print(f"    Correlation points: {len(corr_data)}")
    except FileNotFoundError:
        print(f"\n✗ ERROR: Cannot find CSV file: {csv_path}")
        print("Please run chirp_test_standalone.cpp first to generate test data.")
        input("\nPress ENTER to exit...")
        return
    except Exception as e:
        print(f"\n✗ ERROR: Could not read CSV file. {e}")
        input("\nPress ENTER to exit...")
        return
    
    # Get the column names (handles either NCC or Dot Product)
    sample_col = corr_data.columns[0]
    score_col = corr_data.columns[1]
    print(f"    Using columns: '{sample_col}' and '{score_col}'")
    
    # Create time axis for correlation data
    # The logged index is the *start* of the correlation window.
    # We plot the score at the *center* of the window for better alignment.
    window_center_offset_samples = PREAMBLE_SAMPLES / 2.0
    corr_time = (corr_data[sample_col] + window_center_offset_samples) / sample_rate
    corr_scores = corr_data[score_col]
    
    # Find maximum correlation point
    max_idx = np.argmax(corr_scores)
    max_time = corr_time.iloc[max_idx]
    max_score = corr_scores.iloc[max_idx]
    print(f"\n  Maximum correlation: {max_score:.6f}")
    print(f"  Detection time: {max_time:.3f} seconds")
    
    # --- 3. Plot the Data ---
    print("\n[STEP 3] Generating visualization...")
    
    # Create figure with subplots
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(16, 10))
    
    # --- Top plot: Waveform ---
    color_wav = 'royalblue'
    ax1.set_xlabel('Time (seconds)', fontsize=12)
    ax1.set_ylabel('Waveform Amplitude', color=color_wav, fontsize=12)
    ax1.plot(wav_time, wav_data_norm, label='Recorded Waveform', color=color_wav, alpha=0.7, linewidth=0.5)
    ax1.tick_params(axis='y', labelcolor=color_wav)
    ax1.set_ylim(-1.05, 1.05)
    ax1.grid(True, linestyle='--', alpha=0.3)
    ax1.set_title('Recorded Audio Signal', fontsize=14, fontweight='bold')
    
    # Mark expected chirp location (0.5s silence + 1ms click = ~0.501s start)
    expected_chirp_start = 0.5 + 0.001
    expected_chirp_end = expected_chirp_start + (PREAMBLE_SAMPLES / sample_rate)
    ax1.axvline(x=expected_chirp_start, color='green', linestyle='--', alpha=0.7, linewidth=2, label='Expected Chirp Start')
    ax1.axvline(x=expected_chirp_end, color='green', linestyle='--', alpha=0.7, linewidth=2, label='Expected Chirp End')
    
    # Mark detected location if we found one
    ax1.axvline(x=max_time, color='red', linestyle='-', alpha=0.8, linewidth=2, label=f'Detected Chirp (NCC={max_score:.3f})')
    
    ax1.legend(loc='upper right', fontsize=9)
    
    # --- Bottom plot: Correlation Score ---
    color_corr = 'crimson'
    ax2.set_xlabel('Time (seconds)', fontsize=12)
    ax2.set_ylabel(f'{score_col}', color=color_corr, fontsize=12)
    ax2.plot(corr_time, corr_scores, label=f'{score_col}', color=color_corr, linewidth=2, alpha=0.8)
    ax2.tick_params(axis='y', labelcolor=color_corr)
    
    # Set Y-limit based on score type
    if "NCC" in score_col:
        ax2.set_ylim(-0.1, 1.1)
        ax2.axhline(y=0.3, color='orange', linestyle=':', alpha=0.6, linewidth=1, label='Detection Threshold (0.3)')
    else:
        min_score = corr_scores.min()
        max_score_val = corr_scores.max()
        ax2.set_ylim(min_score - 0.1 * abs(min_score), max_score_val + 0.1 * abs(max_score_val))
    
    # Mark expected and detected locations
    ax2.axvline(x=expected_chirp_start, color='green', linestyle='--', alpha=0.7, linewidth=2)
    ax2.axvline(x=expected_chirp_end, color='green', linestyle='--', alpha=0.7, linewidth=2)
    ax2.axvline(x=max_time, color='red', linestyle='-', alpha=0.8, linewidth=2)
    
    ax2.grid(True, linestyle='--', alpha=0.3)
    ax2.set_title(f'Chirp Detection: {score_col} vs Time', fontsize=14, fontweight='bold')
    ax2.legend(loc='upper right', fontsize=9)
    
    # Ensure x-axes are aligned
    ax1.set_xlim(0, max(wav_time))
    ax2.set_xlim(0, max(wav_time))
    
    # Overall title
    fig.suptitle('Chirp Detection Analysis', fontsize=18, fontweight='bold', y=0.995)
    
    # Adjust layout
    fig.tight_layout(rect=[0, 0, 1, 0.98])
    
    # Save the plot
    print("\n[STEP 4] Saving plot...")
    try:
        plt.savefig(plot_path, dpi=150, bbox_inches='tight')
        print(f"  ✓ Saved to: {plot_path}")
    except Exception as e:
        print(f"  ✗ ERROR: Could not save plot. {e}")
    
    # Show the plot
    print("\n[STEP 5] Displaying plot...")
    plt.show()
    
    # SUMMARY
    print("\n" + "="*70)
    print("VISUALIZATION COMPLETE")
    print("="*70)
    
    # Detection accuracy analysis
    time_diff = abs(max_time - expected_chirp_start)
    samples_diff = int(time_diff * sample_rate)
    
    print(f"\nDetection Accuracy:")
    print(f"  Expected chirp start: {expected_chirp_start:.3f} seconds")
    print(f"  Detected chirp start: {max_time:.3f} seconds")
    print(f"  Time difference: {time_diff:.3f} seconds ({samples_diff} samples)")
    
    if time_diff < 0.01:  # Within 10ms
        print("\n  ✓ EXCELLENT: Detection within 10ms of expected location!")
    elif time_diff < 0.05:  # Within 50ms
        print("\n  ✓ GOOD: Detection within 50ms of expected location")
    else:
        print("\n  ⚠ WARNING: Detection offset is significant. Check audio latency.")
    
    print("\nPress ENTER to exit...")
    input()

# ==============================================================================
#  ENTRY POINT
# ==============================================================================

if __name__ == "__main__":
    try:
        plot_analysis()
    except KeyboardInterrupt:
        print("\n\nVisualization interrupted by user.")
    except Exception as e:
        print(f"\n\nERROR: {e}")
        import traceback
        traceback.print_exc()
        print("\nPress ENTER to exit...")
        input()


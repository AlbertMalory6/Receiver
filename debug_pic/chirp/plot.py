import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from scipy.io import wavfile
import os

# --- Configuration ---
# Set this to the exact same path used in your C++ code
DATA_PATH = r"D:\fourth_year\cs120\debug_pic\chirp"

# File names
RECORDED_WAV_FILE = "recorded_chirp_signal.wav"
CORRELATION_LOG_FILE = "chirp_correlation_log.csv"
OUTPUT_PLOT_FILE = "chirp_analysis_plot.png"

# Chirp template length (must match FSK::preambleSamples)
PREAMBLE_SAMPLES = 440

def plot_analysis():
    print(f"Starting analysis on path: {DATA_PATH}")

    # Construct full file paths
    wav_path = os.path.join(DATA_PATH, RECORDED_WAV_FILE)
    csv_path = os.path.join(DATA_PATH, CORRELATION_LOG_FILE)
    plot_path = os.path.join(DATA_PATH, OUTPUT_PLOT_FILE)

    # --- 1. Load Recorded Audio ---
    try:
        sample_rate, wav_data = wavfile.read(wav_path)
        print(f"Loaded WAV file: {wav_path}")
        print(f"  Sample Rate: {sample_rate} Hz")
        print(f"  Samples: {len(wav_data)}")
    except FileNotFoundError:
        print(f"ERROR: Cannot find WAV file: {wav_path}")
        print("Please run the C++ test program first.")
        return
    except Exception as e:
        print(f"ERROR: Could not read WAV file. {e}")
        return

    # Normalize audio data to [-1.0, 1.0] for plotting
    if np.issubdtype(wav_data.dtype, np.integer):
        wav_data_norm = wav_data.astype(np.float32) / np.iinfo(wav_data.dtype).max
    else:
        wav_data_norm = wav_data.astype(np.float32)

    # Create time axis for waveform
    wav_time = np.arange(len(wav_data_norm)) / sample_rate

    # --- 2. Load Correlation Data ---
    try:
        corr_data = pd.read_csv(csv_path)
        print(f"Loaded CSV file: {csv_path}")
        print(f"  Correlation points: {len(corr_data)}")
    except FileNotFoundError:
        print(f"ERROR: Cannot find CSV file: {csv_path}")
        print("Please run the C++ test program first.")
        return
    except Exception as e:
        print(f"ERROR: Could not read CSV file. {e}")
        return

    # Get the column names (handles either NCC or Dot Product)
    sample_col = corr_data.columns[0]
    score_col = corr_data.columns[1]
    print(f"  Using columns: '{sample_col}' and '{score_col}'")

    # Create time axis for correlation data
    # The logged index 'i' is the *start* of the correlation window.
    # We plot the score at the *center* of the window for better alignment.
    window_center_offset_samples = PREAMBLE_SAMPLES / 2.0
    corr_time = (corr_data[sample_col] + window_center_offset_samples) / sample_rate
    corr_scores = corr_data[score_col]

    # --- 3. Plot the Data ---
    print("Generating plot...")
    fig, ax1 = plt.subplots(figsize=(20, 10))

    # Plot Waveform
    color_wav = 'royalblue'
    ax1.set_xlabel('Time (seconds)', fontsize=14)
    ax1.set_ylabel('Waveform Amplitude', color=color_wav, fontsize=14)
    ax1.plot(wav_time, wav_data_norm, label='Waveform', color=color_wav, alpha=0.7)
    ax1.tick_params(axis='y', labelcolor=color_wav)
    ax1.set_ylim(-1.05, 1.05)
    ax1.grid(True, linestyle='--', alpha=0.5)

    # Create a second y-axis for the correlation score
    ax2 = ax1.twinx()
    color_corr = 'crimson'
    ax2.set_ylabel(f'{score_col}', color=color_corr, fontsize=14)
    ax2.plot(corr_time, corr_scores, label=f'{score_col}', color=color_corr, linewidth=2)
    ax2.tick_params(axis='y', labelcolor=color_corr)
    
    # Set Y-limit based on score type
    if "NCC" in score_col:
        ax2.set_ylim(-0.1, 1.1)
    else:
        min_score = corr_scores.min()
        max_score = corr_scores.max()
        ax2.set_ylim(min_score - 0.1 * abs(min_score), max_score + 0.1 * abs(max_score))


    # Final plot formatting
    fig.tight_layout(rect=[0, 0.03, 1, 0.95]) # Adjust layout
    plt.title(f'Chirp Detection Analysis: Waveform vs. {score_col}', fontsize=18)
    
    # Save the plot
    try:
        plt.savefig(plot_path)
        print(f"\n✓ Successfully saved plot to: {plot_path}")
    except Exception as e:
        print(f"\nERROR: Could not save plot. {e}")

    # Show the plot
    plt.show()

if __name__ == "__main__":
    plot_analysis()
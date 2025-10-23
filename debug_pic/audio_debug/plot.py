import numpy as np
import matplotlib.pyplot as plt
from scipy.io import wavfile
from scipy import signal
import os

def find_delay(original, recorded):
    """Finds the delay of the recorded signal relative to the original using cross-correlation."""
    print("Aligning signals using cross-correlation...")
    # Normalize signals to have zero mean and unit variance
    original_norm = (original - np.mean(original)) / np.std(original)
    recorded_norm = (recorded - np.mean(recorded)) / np.std(recorded)
    
    # Compute cross-correlation
    correlation = signal.correlate(recorded_norm, original_norm, mode='full')
    # The delay is the lag at which the correlation is maximum
    delay = np.argmax(correlation) - (len(original_norm) - 1)
    
    print(f"Detected delay of {delay} samples.")
    return delay

def main():
    """Main function to load, align, analyze, and plot audio waveforms."""
    # --- Configuration ---
    FILE_PATH = "D:/fourth_year/cs120/debug_pic/audio_debug/"
    ORIGINAL_FILE = os.path.join(FILE_PATH, "original_audio.wav")
    RECORDED_FILE = os.path.join(FILE_PATH, "recorded_audio.wav")

    # --- Load Audio Files ---
    try:
        print(f"Loading original audio from: {ORIGINAL_FILE}")
        fs_orig, original_data = wavfile.read(ORIGINAL_FILE)
        print(f"Loading recorded audio from: {RECORDED_FILE}")
        fs_rec, recorded_data = wavfile.read(RECORDED_FILE)
    except FileNotFoundError as e:
        print(f"\nERROR: Could not find file - {e.filename}")
        print("Please ensure you have run the C++ AudioQualityTest program first.")
        return

    if fs_orig != fs_rec:
        print(f"Warning: Sample rates differ ({fs_orig} Hz vs {fs_rec} Hz).")

    # Convert to float and normalize amplitude to [-1, 1]
    original_float = original_data.astype(np.float32) / np.iinfo(original_data.dtype).max
    recorded_float = recorded_data.astype(np.float32) / np.iinfo(recorded_data.dtype).max

    # --- Align Signals ---
    delay = find_delay(original_float, recorded_float)
    
    if delay >= 0: # Recorded signal is delayed
        recorded_aligned = recorded_float[delay:]
        original_aligned = original_float[:len(recorded_aligned)]
    else: # Recorded signal is ahead (less likely, but possible)
        original_aligned = original_float[-delay:]
        recorded_aligned = recorded_float[:len(original_aligned)]

    # --- Quantitative Analysis ---
    # Calculate Mean Squared Error on the aligned signals
    mse = np.mean((original_aligned - recorded_aligned) ** 2)
    print(f"Mean Squared Error (MSE) between aligned signals: {mse:.10f}")
    if mse < 0.01:
        print("-> Quality Assessment: Good (Low error between signals)")
    elif mse < 0.1:
        print("-> Quality Assessment: Fair (Moderate error, likely some noise/distortion)")
    else:
        print("-> Quality Assessment: Poor (High error, significant noise/distortion)")


    # --- Plotting ---
    print("Generating plot...")
    time = np.arange(len(original_aligned)) / fs_orig
    
    plt.style.use('seaborn-v0_8-darkgrid')
    fig, ax = plt.subplots(figsize=(15, 7))
    
    ax.plot(time, original_aligned, label='Original Signal', color='blue', alpha=0.7, linewidth=1.5)
    ax.plot(time, recorded_aligned, label='Recorded Signal', color='red', alpha=0.7, linewidth=1.0, linestyle='--')
    
    ax.set_title('Original vs. Recorded Audio Waveform (Aligned)', fontsize=16)
    ax.set_xlabel('Time (s)', fontsize=12)
    ax.set_ylabel('Amplitude', fontsize=12)
    ax.legend(fontsize=10)
    ax.grid(True)
    ax.set_xlim(0, time[-1])
    
    # Add MSE value to the plot
    plt.text(0.02, 0.05, f'MSE: {mse:.6f}', transform=ax.transAxes,
             fontsize=12, verticalalignment='bottom', bbox=dict(boxstyle='round,pad=0.5', fc='wheat', alpha=0.5))

    plt.tight_layout()
    plt.show()

if __name__ == '__main__':
    main()
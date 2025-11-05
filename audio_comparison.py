"""
AUDIO COMPARISON TOOL
=====================
Analyzes and compares transmitted vs recorded audio to identify channel distortions

FEATURES:
1. Alignment using cross-correlation (handles timing offset)
2. Amplitude comparison
3. Frequency spectrum comparison
4. Sample rate verification
5. Bit-level waveform comparison
6. Visual plots for diagnosis

USAGE:
    python audio_comparison.py <transmitted.wav> <recorded.wav>

OUTPUT:
    - Console report with key metrics
    - Plots saved to debug_pic/ASK/comparison_*.png
"""

import numpy as np
import matplotlib.pyplot as plt
from scipy import signal
from scipy.io import wavfile
import sys
import os

class AudioComparator:
    def __init__(self, transmitted_path, recorded_path):
        """Load two audio files for comparison"""
        self.transmitted_path = transmitted_path
        self.recorded_path = recorded_path
        
        # Load audio files
        self.tx_rate, self.tx_data = wavfile.read(transmitted_path)
        self.rx_rate, self.rx_data = wavfile.read(recorded_path)
        
        # Convert to mono if stereo
        if len(self.tx_data.shape) > 1:
            self.tx_data = self.tx_data[:, 0]
        if len(self.rx_data.shape) > 1:
            self.rx_data = self.rx_data[:, 0]
        
        # Normalize to [-1, 1]
        self.tx_data = self.tx_data.astype(np.float64)
        self.rx_data = self.rx_data.astype(np.float64)
        
        # Normalize based on data type
        if self.tx_data.max() > 1.0 or self.tx_data.min() < -1.0:
            self.tx_data = self.tx_data / 32768.0
        if self.rx_data.max() > 1.0 or self.rx_data.min() < -1.0:
            self.rx_data = self.rx_data / 32768.0
        
        print(f"Loaded transmitted: {transmitted_path}")
        print(f"  Sample rate: {self.tx_rate} Hz")
        print(f"  Samples: {len(self.tx_data)}")
        print(f"  Duration: {len(self.tx_data)/self.tx_rate:.2f} seconds")
        
        print(f"\nLoaded recorded: {recorded_path}")
        print(f"  Sample rate: {self.rx_rate} Hz")
        print(f"  Samples: {len(self.rx_data)}")
        print(f"  Duration: {len(self.rx_data)/self.rx_rate:.2f} seconds")
        
        self.aligned = False
        self.offset = 0
        
    def check_sample_rate(self):
        """Verify sample rates match"""
        print("\n" + "="*60)
        print("SAMPLE RATE CHECK")
        print("="*60)
        
        if self.tx_rate != self.rx_rate:
            print(f"⚠ WARNING: Sample rate mismatch!")
            print(f"  Transmitted: {self.tx_rate} Hz")
            print(f"  Recorded: {self.rx_rate} Hz")
            print(f"  Ratio: {self.rx_rate/self.tx_rate:.6f}")
            return False
        else:
            print(f"✓ Sample rates match: {self.tx_rate} Hz")
            return True
    
    def align_signals(self, max_offset=50000):
        """Align signals using cross-correlation"""
        print("\n" + "="*60)
        print("SIGNAL ALIGNMENT")
        print("="*60)
        
        # Limit search range for efficiency
        search_len = min(len(self.tx_data), len(self.rx_data), max_offset)
        
        # Use first part of transmitted signal as template
        template = self.tx_data[:search_len]
        
        # Search in recorded signal
        search_signal = self.rx_data[:min(len(self.rx_data), search_len + max_offset)]
        
        # Cross-correlate
        correlation = signal.correlate(search_signal, template, mode='valid')
        
        # Find peak
        self.offset = np.argmax(correlation)
        max_corr = correlation[self.offset]
        
        print(f"Cross-correlation peak at offset: {self.offset} samples")
        print(f"  Time offset: {self.offset/self.tx_rate*1000:.2f} ms")
        print(f"  Correlation value: {max_corr:.2e}")
        
        # Align the signals
        if self.offset > 0:
            # Recorded signal starts later
            min_len = min(len(self.tx_data), len(self.rx_data) - self.offset)
            self.tx_aligned = self.tx_data[:min_len]
            self.rx_aligned = self.rx_data[self.offset:self.offset + min_len]
        else:
            # Signals already aligned or transmitted starts later
            min_len = min(len(self.tx_data), len(self.rx_data))
            self.tx_aligned = self.tx_data[:min_len]
            self.rx_aligned = self.rx_data[:min_len]
        
        print(f"Aligned signals: {len(self.tx_aligned)} samples")
        self.aligned = True
        
        return self.offset
    
    def compare_amplitude(self):
        """Compare amplitude characteristics"""
        if not self.aligned:
            self.align_signals()
        
        print("\n" + "="*60)
        print("AMPLITUDE COMPARISON")
        print("="*60)
        
        tx_rms = np.sqrt(np.mean(self.tx_aligned**2))
        rx_rms = np.sqrt(np.mean(self.rx_aligned**2))
        
        tx_peak = np.max(np.abs(self.tx_aligned))
        rx_peak = np.max(np.abs(self.rx_aligned))
        
        print(f"Transmitted RMS: {tx_rms:.6f}")
        print(f"Recorded RMS:    {rx_rms:.6f}")
        print(f"RMS Ratio:       {rx_rms/tx_rms:.6f}")
        
        print(f"\nTransmitted Peak: {tx_peak:.6f}")
        print(f"Recorded Peak:    {rx_peak:.6f}")
        print(f"Peak Ratio:       {rx_peak/tx_peak:.6f}")
        
        # Check for clipping
        if rx_peak > 0.95:
            print("⚠ WARNING: Recorded signal may be clipping!")
        
        # Check for weak signal
        if rx_rms/tx_rms < 0.1:
            print("⚠ WARNING: Recorded signal is significantly weaker (< 10%)")
        
        return tx_rms, rx_rms, tx_peak, rx_peak
    
    def compare_spectrum(self):
        """Compare frequency spectra"""
        if not self.aligned:
            self.align_signals()
        
        print("\n" + "="*60)
        print("FREQUENCY SPECTRUM COMPARISON")
        print("="*60)
        
        # Compute FFT
        tx_fft = np.fft.rfft(self.tx_aligned)
        rx_fft = np.fft.rfft(self.rx_aligned)
        
        tx_mag = np.abs(tx_fft)
        rx_mag = np.abs(rx_fft)
        
        freqs = np.fft.rfftfreq(len(self.tx_aligned), 1/self.tx_rate)
        
        # Find peaks in relevant range (2kHz - 12kHz for chirp and carrier)
        freq_mask = (freqs >= 2000) & (freqs <= 12000)
        
        tx_peak_idx = np.argmax(tx_mag[freq_mask])
        rx_peak_idx = np.argmax(rx_mag[freq_mask])
        
        tx_peak_freq = freqs[freq_mask][tx_peak_idx]
        rx_peak_freq = freqs[freq_mask][rx_peak_idx]
        
        print(f"Transmitted peak frequency: {tx_peak_freq:.1f} Hz")
        print(f"Recorded peak frequency:    {rx_peak_freq:.1f} Hz")
        print(f"Frequency difference:       {rx_peak_freq - tx_peak_freq:.1f} Hz")
        
        if abs(rx_peak_freq - tx_peak_freq) > 10:
            print("⚠ WARNING: Significant frequency shift detected!")
            print("  This could indicate clock drift between sound cards.")
        
        return freqs, tx_mag, rx_mag
    
    def compute_snr(self):
        """Compute Signal-to-Noise Ratio"""
        if not self.aligned:
            self.align_signals()
        
        print("\n" + "="*60)
        print("SIGNAL-TO-NOISE RATIO")
        print("="*60)
        
        # Compute error signal
        error = self.rx_aligned - self.tx_aligned
        
        signal_power = np.mean(self.tx_aligned**2)
        noise_power = np.mean(error**2)
        
        if noise_power > 0:
            snr = 10 * np.log10(signal_power / noise_power)
        else:
            snr = float('inf')
        
        print(f"Signal power: {signal_power:.6e}")
        print(f"Noise power:  {noise_power:.6e}")
        print(f"SNR:          {snr:.2f} dB")
        
        if snr < 20:
            print("⚠ WARNING: Low SNR (< 20 dB) - significant distortion present")
        
        return snr, error
    
    def plot_waveform_comparison(self, start_sample=0, num_samples=4000, output_path=None):
        """Plot waveform comparison"""
        if not self.aligned:
            self.align_signals()
        
        end_sample = min(start_sample + num_samples, len(self.tx_aligned))
        time_axis = np.arange(start_sample, end_sample) / self.tx_rate * 1000  # in ms
        
        plt.figure(figsize=(14, 8))
        
        # Plot transmitted
        plt.subplot(3, 1, 1)
        plt.plot(time_axis, self.tx_aligned[start_sample:end_sample], 'b-', linewidth=0.5)
        plt.title('Transmitted Signal')
        plt.ylabel('Amplitude')
        plt.grid(True, alpha=0.3)
        plt.xlim(time_axis[0], time_axis[-1])
        
        # Plot recorded
        plt.subplot(3, 1, 2)
        plt.plot(time_axis, self.rx_aligned[start_sample:end_sample], 'r-', linewidth=0.5)
        plt.title('Recorded Signal')
        plt.ylabel('Amplitude')
        plt.grid(True, alpha=0.3)
        plt.xlim(time_axis[0], time_axis[-1])
        
        # Plot overlay
        plt.subplot(3, 1, 3)
        plt.plot(time_axis, self.tx_aligned[start_sample:end_sample], 'b-', 
                 linewidth=0.5, alpha=0.7, label='Transmitted')
        plt.plot(time_axis, self.rx_aligned[start_sample:end_sample], 'r-', 
                 linewidth=0.5, alpha=0.7, label='Recorded')
        plt.title('Overlay Comparison')
        plt.xlabel('Time (ms)')
        plt.ylabel('Amplitude')
        plt.legend()
        plt.grid(True, alpha=0.3)
        plt.xlim(time_axis[0], time_axis[-1])
        
        plt.tight_layout()
        
        if output_path:
            plt.savefig(output_path, dpi=150)
            print(f"✓ Waveform plot saved to: {output_path}")
        else:
            plt.show()
    
    def plot_spectrum_comparison(self, output_path=None):
        """Plot frequency spectrum comparison"""
        freqs, tx_mag, rx_mag = self.compare_spectrum()
        
        plt.figure(figsize=(14, 6))
        
        # Convert to dB
        tx_db = 20 * np.log10(tx_mag + 1e-10)
        rx_db = 20 * np.log10(rx_mag + 1e-10)
        
        plt.subplot(1, 2, 1)
        plt.plot(freqs/1000, tx_db, 'b-', linewidth=1, label='Transmitted')
        plt.plot(freqs/1000, rx_db, 'r-', linewidth=1, alpha=0.7, label='Recorded')
        plt.xlim(0, 15)
        plt.xlabel('Frequency (kHz)')
        plt.ylabel('Magnitude (dB)')
        plt.title('Frequency Spectrum')
        plt.legend()
        plt.grid(True, alpha=0.3)
        
        plt.subplot(1, 2, 2)
        # Compute ratio
        ratio = rx_mag / (tx_mag + 1e-10)
        ratio_db = 20 * np.log10(ratio)
        plt.plot(freqs/1000, ratio_db, 'g-', linewidth=1)
        plt.xlim(0, 15)
        plt.ylim(-20, 20)
        plt.xlabel('Frequency (kHz)')
        plt.ylabel('Ratio (dB)')
        plt.title('Recorded/Transmitted Ratio')
        plt.axhline(0, color='k', linestyle='--', alpha=0.5)
        plt.grid(True, alpha=0.3)
        
        plt.tight_layout()
        
        if output_path:
            plt.savefig(output_path, dpi=150)
            print(f"✓ Spectrum plot saved to: {output_path}")
        else:
            plt.show()
    
    def plot_error_analysis(self, output_path=None):
        """Plot error signal analysis"""
        snr, error = self.compute_snr()
        
        plt.figure(figsize=(14, 10))
        
        # Error waveform
        plt.subplot(3, 1, 1)
        time_axis = np.arange(len(error)) / self.tx_rate
        plt.plot(time_axis, error, 'r-', linewidth=0.3)
        plt.title('Error Signal (Recorded - Transmitted)')
        plt.xlabel('Time (s)')
        plt.ylabel('Amplitude')
        plt.grid(True, alpha=0.3)
        
        # Error histogram
        plt.subplot(3, 1, 2)
        plt.hist(error, bins=100, color='orange', alpha=0.7, edgecolor='black')
        plt.title('Error Distribution')
        plt.xlabel('Error Value')
        plt.ylabel('Count')
        plt.grid(True, alpha=0.3)
        
        # Error spectrum
        plt.subplot(3, 1, 3)
        error_fft = np.fft.rfft(error)
        error_mag = np.abs(error_fft)
        freqs = np.fft.rfftfreq(len(error), 1/self.tx_rate)
        plt.plot(freqs/1000, 20*np.log10(error_mag + 1e-10), 'r-', linewidth=1)
        plt.xlim(0, 15)
        plt.xlabel('Frequency (kHz)')
        plt.ylabel('Magnitude (dB)')
        plt.title(f'Error Spectrum (SNR = {snr:.2f} dB)')
        plt.grid(True, alpha=0.3)
        
        plt.tight_layout()
        
        if output_path:
            plt.savefig(output_path, dpi=150)
            print(f"✓ Error analysis plot saved to: {output_path}")
        else:
            plt.show()
    
    def detect_chirps(self):
        """Detect chirp locations in both signals"""
        print("\n" + "="*60)
        print("CHIRP DETECTION COMPARISON")
        print("="*60)
        
        # Generate chirp template (simplified)
        chirp_len = 440
        t = np.arange(chirp_len) / self.tx_rate
        chirp_template = np.sin(2 * np.pi * (2000 + (10000-2000) * t / (chirp_len/self.tx_rate / 2)) * t)
        
        # Detect in transmitted
        tx_corr = signal.correlate(self.tx_aligned, chirp_template, mode='valid')
        tx_peaks, _ = signal.find_peaks(tx_corr, height=np.max(tx_corr)*0.5, distance=1000)
        
        # Detect in recorded
        rx_corr = signal.correlate(self.rx_aligned, chirp_template, mode='valid')
        rx_peaks, _ = signal.find_peaks(rx_corr, height=np.max(rx_corr)*0.5, distance=1000)
        
        print(f"Chirps detected in transmitted: {len(tx_peaks)}")
        print(f"Chirps detected in recorded:    {len(rx_peaks)}")
        
        if len(tx_peaks) != len(rx_peaks):
            print("⚠ WARNING: Number of detected chirps differs!")
            print("  Some frames may have been lost or corrupted.")
        
        return tx_peaks, rx_peaks

def main():
    if len(sys.argv) < 3:
        print("Usage: python audio_comparison.py <transmitted.wav> <recorded.wav>")
        sys.exit(1)
    
    transmitted_path = sys.argv[1]
    recorded_path = sys.argv[2]
    
    if not os.path.exists(transmitted_path):
        print(f"ERROR: Transmitted file not found: {transmitted_path}")
        sys.exit(1)
    
    if not os.path.exists(recorded_path):
        print(f"ERROR: Recorded file not found: {recorded_path}")
        sys.exit(1)
    
    # Output directory
    output_dir = "D:\\fourth_year\\cs120\\debug_pic\\ASK\\"
    os.makedirs(output_dir, exist_ok=True)
    
    print("\n╔════════════════════════════════════════════════════════════════╗")
    print("║           AUDIO CHANNEL COMPARISON TOOL                       ║")
    print("╚════════════════════════════════════════════════════════════════╝\n")
    
    # Create comparator
    comparator = AudioComparator(transmitted_path, recorded_path)
    
    # Run all analyses
    comparator.check_sample_rate()
    comparator.align_signals()
    comparator.compare_amplitude()
    comparator.compare_spectrum()
    comparator.compute_snr()
    comparator.detect_chirps()
    
    # Generate plots
    print("\n" + "="*60)
    print("GENERATING PLOTS")
    print("="*60)
    
    comparator.plot_waveform_comparison(
        start_sample=0, 
        num_samples=4000,
        output_path=os.path.join(output_dir, "comparison_waveform.png")
    )
    
    comparator.plot_spectrum_comparison(
        output_path=os.path.join(output_dir, "comparison_spectrum.png")
    )
    
    comparator.plot_error_analysis(
        output_path=os.path.join(output_dir, "comparison_error.png")
    )
    
    print("\n" + "="*60)
    print("ANALYSIS COMPLETE")
    print("="*60)
    print(f"All plots saved to: {output_dir}")

if __name__ == "__main__":
    main()


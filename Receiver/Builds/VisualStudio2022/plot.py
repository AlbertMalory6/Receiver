import pandas as pd
import matplotlib.pyplot as plt
import os
import numpy as np

def plot_individual_analysis(n, df):
    """Generates and saves a 3-panel plot for a single test case."""
    
    # Create a figure with 3 subplots stacked vertically
    fig, (ax1, ax2, ax3) = plt.subplots(3, 1, figsize=(15, 12))
    fig.suptitle(f'Demodulator Performance Analysis for N = {n}', fontsize=16)

    # --- Plot 1: Confidence over Time ---
    ax1.plot(df.index, df['confidence'], label='Confidence Score', color='purple', alpha=0.8)
    ax1.axhline(y=df['confidence'].mean(), color='r', linestyle='--', label=f'Avg: {df["confidence"].mean():.2f}')
    ax1.set_title('Confidence Score Over Time')
    ax1.set_xlabel('Bit Index')
    ax1.set_ylabel('Confidence')
    ax1.grid(True, linestyle='--', alpha=0.6)
    ax1.legend()
    ax1.set_ylim(0, 1.05)

    # --- Plot 2: Correlations for f0 and f1 ---
    ax2.plot(df.index, df['correlation_f0'], label='Correlation with f0 (2000 Hz)', color='blue', alpha=0.7)
    ax2.plot(df.index, df['correlation_f1'], label='Correlation with f1 (4000 Hz)', color='green', alpha=0.7)
    ax2.set_title('Matched Filter Correlation Scores')
    ax2.set_xlabel('Bit Index')
    ax2.set_ylabel('Correlation Value')
    ax2.grid(True, linestyle='--', alpha=0.6)
    ax2.legend()

    # --- Plot 3: Decoded Bitstream ---
    ax3.plot(df.index, df['bit'], 'o', markersize=2, label='Decoded Bits', color='black')
    ax3.set_title('Decoded Bitstream')
    ax3.set_xlabel('Bit Index')
    ax3.set_ylabel('Bit Value (0 or 1)')
    ax3.grid(True, linestyle='--', alpha=0.6)
    ax3.set_yticks([0, 1])
    ax3.set_ylim(-0.1, 1.1)
    
    plt.tight_layout(rect=[0, 0, 1, 0.96])
    save_path = f'analysis_plots_N_{n}.png'
    plt.savefig(save_path)
    plt.close(fig)
    print(f"Saved individual analysis plot to {save_path}")

def plot_summary_analysis(summary_data):
    """Generates the two conclusive summary plots."""
    
    n_values = [d['n'] for d in summary_data]
    avg_confidences = [d['avg_confidence'] for d in summary_data]
    zero_percentages = [d['zero_percent'] for d in summary_data]
    one_percentages = [d['one_percent'] for d in summary_data]
    
    # --- Plot 1: Average Confidence vs. N ---
    fig1, ax1 = plt.subplots(figsize=(10, 6))
    ax1.plot(n_values, avg_confidences, marker='o', linestyle='-', color='b')
    ax1.set_title('Average Demodulation Confidence vs. N', fontsize=14)
    ax1.set_xlabel('N (Length of Repeated Bit Sequence)')
    ax1.set_ylabel('Average Confidence Score')
    ax1.grid(True, linestyle='--', alpha=0.7)
    ax1.set_xticks(n_values)
    ax1.set_ylim(bottom=0)
    
    plt.tight_layout()
    save_path1 = 'summary_confidence_vs_N.png'
    plt.savefig(save_path1)
    plt.close(fig1)
    print(f"Saved summary confidence plot to {save_path1}")

    # --- Plot 2: Bit Distribution vs. N (Stacked Bar Chart) ---
    fig2, ax2 = plt.subplots(figsize=(10, 6))
    width = 0.6
    ax2.bar(n_values, zero_percentages, width, label='Decoded as 0', color='#ff7f0e')
    ax2.bar(n_values, one_percentages, width, bottom=zero_percentages, label='Decoded as 1', color='#1f77b4')
    
    ax2.set_title('Percentage of Decoded 0s and 1s vs. N', fontsize=14)
    ax2.set_xlabel('N (Length of Repeated Bit Sequence)')
    ax2.set_ylabel('Percentage (%)')
    ax2.set_xticks(n_values)
    ax2.set_ylim(0, 100)
    ax2.legend()
    
    # Add text labels inside bars for clarity
    for i, n in enumerate(n_values):
        ax2.text(n, zero_percentages[i]/2, f'{zero_percentages[i]:.1f}%', ha='center', va='center', color='white', fontweight='bold')
        ax2.text(n, zero_percentages[i] + one_percentages[i]/2, f'{one_percentages[i]:.1f}%', ha='center', va='center', color='white', fontweight='bold')
        
    plt.tight_layout()
    save_path2 = 'summary_bit_distribution_vs_N.png'
    plt.savefig(save_path2)
    plt.close(fig2)
    print(f"Saved summary bit distribution plot to {save_path2}")


def main():
    """
    Main function to find CSV files, process them, and generate all plots.
    """
    summary_data = []
    
    for n in range(1, 9):
        filename = f'matched_filter_constellation_{n}.csv'
        
        if not os.path.exists(filename):
            print(f"Warning: File not found, skipping: {filename}")
            continue
            
        print(f"\nProcessing {filename}...")
        
        # Load the data
        try:
            df = pd.read_csv(filename)
        except Exception as e:
            print(f"Error reading {filename}: {e}")
            continue
        
        # Generate the individual 3-panel plot for this N
        plot_individual_analysis(n, df)
        
        # Calculate summary statistics for the conclusive plots
        avg_confidence = df['confidence'].mean()
        bit_counts = df['bit'].value_counts(normalize=True).sort_index()
        
        zero_percent = bit_counts.get(0, 0) * 100
        one_percent = bit_counts.get(1, 0) * 100
        
        summary_data.append({
            'n': n,
            'avg_confidence': avg_confidence,
            'zero_percent': zero_percent,
            'one_percent': one_percent
        })
        
    # Generate the conclusive summary plots if any data was processed
    if summary_data:
        print("\nGenerating summary plots...")
        plot_summary_analysis(summary_data)
        print("\nAll processing complete.")
    else:
        print("\nNo data files found. No summary plots generated.")

if __name__ == '__main__':
    main()

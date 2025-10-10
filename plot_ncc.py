import numpy as np
import matplotlib.pyplot as plt

# Path to your CSV
csv_path = 'Receiver/Builds/VisualStudio2022/debug_ncc_output.csv'

values = []
with open(csv_path, 'r') as f:
    for line in f:
        line = line.strip()
        if not line:
            continue
        # Split by comma
        parts = line.split(',')
        # If line has one number
        if len(parts) == 1:
            try:
                values.append(float(parts[0]))
            except ValueError:
                pass
        # If line has two numbers (index,value)
        elif len(parts) >= 2:
            try:
                # Use the second one as the NCC value
                values.append(float(parts[1]))
            except ValueError:
                pass

# Convert to numpy array
values = np.array(values)
num_points = len(values)
time = np.linspace(0, 2.5, num_points)

plt.figure(figsize=(10, 6))
plt.plot(time, values)
plt.xlabel('Time (seconds)')
plt.ylabel('NCC Value')
plt.title('Normalized Cross-Correlation over Time')
plt.grid(True)
plt.show()

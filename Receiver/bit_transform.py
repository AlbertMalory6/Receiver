# Converts /mnt/data/INPUT.bin into two text files:
# 1) output_bits_raw.txt         - a continuous stream of '0' and '1' (no separators)
# 2) output_bits_grouped.txt     - one byte per line (8 bits per line), easier to inspect
#
# The code prints a short summary (original size in bytes, total bits, and output paths).
# After this runs, download links will be provided in the chat.

from pathlib import Path

input_path = Path("Receiver/INPUT.bin")
out_raw = Path("Receiver/output_bits_raw.txt")
out_grouped = Path("Receiver/output_bits_grouped.txt")

if not input_path.exists():
    raise FileNotFoundError(f"Input file not found at {input_path}")

data = input_path.read_bytes()
num_bytes = len(data)
num_bits = num_bytes * 8

# Generate bit-strings
# Use generator to avoid building huge intermediate list (though 50k chars is small).
bits_iter = (f"{b:08b}" for b in data)

# Write raw continuous stream
with out_raw.open("w", newline="") as f_raw:
    for byte_bits in bits_iter:
        f_raw.write(byte_bits)

# For grouped output we need a fresh iterator
with out_grouped.open("w", newline="") as f_grp:
    for b in data:
        f_grp.write(f"{b:08b}\n")

print(f"Input file: {input_path}")
print(f"Size: {num_bytes} bytes  →  {num_bits} bits")
print(f"Wrote continuous bitstream to: {out_raw}")
print(f"Wrote 8-bits-per-line file to: {out_grouped}")

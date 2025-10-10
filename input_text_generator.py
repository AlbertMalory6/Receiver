"""Small utility to generate a continuous stream of '0'/'1' characters and
write them to a file. Default length is 10000 and default output path is
Sender/Builds/VisualStudio2022/INPUT.txt (relative to the repo root).

Usage examples:
  python input_text_generator.py               # write 10000 random bits
  python input_text_generator.py --length 10000 --out "path/to/INPUT.txt"
  python input_text_generator.py --mode alternating --length 8

The script supports an optional seed for reproducible output.
"""

from __future__ import annotations

import argparse
import random
from pathlib import Path
import sys


def generate_bits(length: int, mode: str = "random", seed: int | None = None) -> str:
	if length <= 0:
		raise ValueError("length must be positive")

	if seed is not None:
		random.seed(seed)

	if mode == "random":
		# fast generation using getrandbits in chunks
		bits = []
		chunk = 64
		full_chunks = length // chunk
		rem = length % chunk
		for _ in range(full_chunks):
			n = random.getrandbits(chunk)
			bits.append(f"{n:0{chunk}b}")
		if rem:
			n = random.getrandbits(rem)
			bits.append(f"{n:0{rem}b}")
		return "".join(bits)[:length]

	if mode == "alternating":
		# produce 010101... starting with 0
		return ("01" * ((length + 1) // 2))[:length]

	if mode.startswith("pattern:"):
		_, pat = mode.split(":", 1)
		if not pat:
			raise ValueError("pattern must be non-empty")
		return (pat * ((length + len(pat) - 1) // len(pat)))[:length]

	raise ValueError(f"unknown mode: {mode}")


def main(argv: list[str] | None = None) -> int:
	p = argparse.ArgumentParser(description="Generate a continuous 0/1 bitstring and write to a file")
	p.add_argument("--length", "-n", type=int, default=10000, help="number of bits to generate (default: 10000)")
	p.add_argument("--out", "-o", type=Path, default=Path("Sender") / "Builds" / "VisualStudio2022" / "INPUT.txt", help="output file path")
	p.add_argument("--seed", type=int, default=None, help="optional RNG seed for reproducible output")
	p.add_argument("--mode", choices=["random", "alternating"], default="random", help="generation mode")
	args = p.parse_args(argv)

	out_path: Path = args.out
	out_path_parent = out_path.parent
	try:
		out_path_parent.mkdir(parents=True, exist_ok=True)
	except Exception as e:
		print(f"Failed to create directories for {out_path_parent}: {e}", file=sys.stderr)
		return 2

	try:
		bits = generate_bits(args.length, mode=args.mode, seed=args.seed)
	except Exception as e:
		print(f"Error generating bits: {e}", file=sys.stderr)
		return 3

	try:
		# write as a single line with no newline at end to match existing format
		out_path.write_text(bits)
	except Exception as e:
		print(f"Failed to write to {out_path}: {e}", file=sys.stderr)
		return 4

	print(f"Wrote {len(bits)} bits to {out_path}")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())

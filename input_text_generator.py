from __future__ import annotations
import argparse
import random
from pathlib import Path
import sys


def generate_bits(length: int, mode: str = "random", seed: int | None = None, n: int = 1) -> str:
    if length <= 0:
        raise ValueError("length must be positive")

    if seed is not None:
        random.seed(seed)

    if mode == "random":
        bits = []
        chunk = 64
        full_chunks = length // chunk
        rem = length % chunk
        for _ in range(full_chunks):
            n64 = random.getrandbits(chunk)
            bits.append(f"{n64:0{chunk}b}")
        if rem:
            nrem = random.getrandbits(rem)
            bits.append(f"{nrem:0{rem}b}")
        return "".join(bits)[:length]

    if mode == "alternating":
        return ("01" * ((length + 1) // 2))[:length]

    if mode == "zero":
        return "0" * length

    if mode == "one":
        return "1" * length

    if mode == "continued":
        if n <= 0:
            raise ValueError("--n must be positive for continued mode")
        pattern = "0" * n + "1" * n
        return (pattern * ((length + len(pattern) - 1) // len(pattern)))[:length]

    raise ValueError(f"unknown mode: {mode}")


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description="Generate bit sequences and write to file(s)")
    p.add_argument("--length", "-n", type=int, default=10000,
                   help="number of bits to generate (default: 10000)")
    p.add_argument("--outdir", "-o", type=Path,
                   default=Path(r"D:\fourth_year\cs120\Receiver\Builds\VisualStudio2022"),
                   help="output directory path")
    p.add_argument("--seed", type=int, default=None,
                   help="optional RNG seed for reproducible output")
    p.add_argument("--mode",
                   default="random",
                   help="generation mode: random | alternating | zero | one | continued")
    p.add_argument("--n", type=int, default=1,
                   help="repeat block size for continued mode (e.g. n=3 → 000111...)")
    p.add_argument("--max", type=int, default=1,
                   help="if >1 and mode=continued, generate files INPUT_1..INPUT_max")
    args = p.parse_args(argv)

    out_dir: Path = args.outdir
    out_dir.mkdir(parents=True, exist_ok=True)

    if args.mode == "continued" and args.max > 1:
        # Generate multiple files
        for i in range(1, args.max + 1):
            bits = generate_bits(args.length, mode="continued", n=i)
            out_path = out_dir / f"INPUT_{i}.txt"
            out_path.write_text(bits)
            print(f"Wrote {len(bits)} bits to {out_path}")
    else:
        bits = generate_bits(args.length, mode=args.mode, seed=args.seed, n=args.n)
        if args.mode == "continued":
            out_path = out_dir / f"INPUT_{args.n}.txt"
        else:
            out_path = out_dir / "INPUT.txt"
        out_path.write_text(bits)
        print(f"Wrote {len(bits)} bits to {out_path}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

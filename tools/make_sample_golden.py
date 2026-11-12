#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Build a tiny *synthetic* golden set so the identity gate can be smoke-tested.

This is **not** the 300-image golden set from docs/17. It exists so that CI and a
fresh checkout can run `atlas-cli eval-set` without shipping photos (privacy and
licensing). The synthetic faces are simple soft-edged blobs: enough to exercise
"same person" vs "different person" behaviour, not enough to calibrate anything.

Usage:
    python tools/make_sample_golden.py --out /tmp/atlas-golden
    ./build/atlas-cli eval-set /tmp/atlas-golden/manifest.tsv
"""

from __future__ import annotations

import argparse
import math
import struct
import zlib
from pathlib import Path


def _png(path: Path, width: int, height: int, pixels: bytearray) -> None:
    raw = bytearray()
    stride = width * 3
    for y in range(height):
        raw.append(0)  # filter: none
        raw += pixels[y * stride:(y + 1) * stride]

    def chunk(tag: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) +
                     chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


def face(width: int, height: int, eye_offset: float, mouth_offset: float,
         tint: float) -> bytearray:
    out = bytearray(width * height * 3)
    for y in range(height):
        for x in range(width):
            fx = (x - width * 0.5) / (width * 0.5)
            fy = (y - height * 0.5) / (height * 0.5)
            v = 0.55 + 0.25 * math.exp(-(fx * fx * 3.0 + fy * fy * 2.0))
            for sign in (-1, 1):
                ex = fx - eye_offset * sign
                ey = fy + 0.2
                v -= 0.35 * math.exp(-(ex * ex * 90.0 + ey * ey * 120.0))
            v -= 0.25 * math.exp(-(fx * fx * 12.0 + (fy - mouth_offset) ** 2 * 150.0))
            r = min(1.0, max(0.0, v * (1.0 + tint)))
            g = min(1.0, max(0.0, v * 0.85))
            b = min(1.0, max(0.0, v * 0.75))
            i = (y * width + x) * 3
            out[i] = int(r * 255 + 0.5)
            out[i + 1] = int(g * 255 + 0.5)
            out[i + 2] = int(b * 255 + 0.5)
    return out


def scale(pixels: bytearray, width: int, height: int, factor: int) -> bytearray:
    """Nearest-neighbour upscale: keeps the harness honest about resolution only."""
    out = bytearray(width * factor * height * factor * 3)
    ow = width * factor
    for y in range(height * factor):
        sy = y // factor
        for x in range(ow):
            sx = x // factor
            si = (sy * width + sx) * 3
            di = (y * ow + x) * 3
            out[di:di + 3] = pixels[si:si + 3]
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path, default=Path("sample-golden"))
    ap.add_argument("--size", type=int, default=160, help="side of the synthetic face")
    args = ap.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)
    size = args.size
    base = face(size, size, 0.35, 0.35, 0.0)
    other = face(size, size, 0.55, 0.15, -0.06)
    soft = face(size, size, 0.35, 0.35, 0.0)

    _png(args.out / "original.png", size, size, base)
    _png(args.out / "restored_good.png", size * 2, size * 2, scale(base, size, size, 2))
    _png(args.out / "restored_other.png", size * 2, size * 2, scale(other, size, size, 2))
    _png(args.out / "restored_flat.png", size * 2, size * 2, scale(soft, size, size, 2))

    manifest = args.out / "manifest.tsv"
    manifest.write_text(
        "id\toriginal\trestored\tx\ty\tw\th\tmode\n"
        "synthetic-same\toriginal.png\trestored_good.png\t-\t-\t-\t-\tblind\n"
        "synthetic-different\toriginal.png\trestored_other.png\t-\t-\t-\t-\tblind\n"
        "synthetic-flat\toriginal.png\trestored_flat.png\t-\t-\t-\t-\tblind\n",
        encoding="utf-8",
    )
    print(f"wrote {manifest} (3 cases)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

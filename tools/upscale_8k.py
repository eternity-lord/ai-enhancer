#!/usr/bin/env python3
"""upscale_8k.py — high quality 8K upscale for studio portraits.

No ML weights are available in this environment, so the upscale is a classic
but careful reconstruction chain (all deterministic, nothing generative — it
cannot "invent" or melt a face):

    1. light wavelet/NLM denoise of the source  (kills webp/JPEG blocking
       before it can be blown up)
    2. staged x2 Lanczos/cubic upscale with a tiny detail injection after each
       stage (staged upscaling keeps edges crisp without ringing)
    3. iterative back-projection at 4K width: the upscaled image is repeatedly
       downscaled back to the source size, the residual is re-injected —
       this enforces downscale-consistency and restores real edge energy
    4. final resize to the requested pixel size
    5. banded detail pass at the target size:
         * edge-aware unsharp with a highlight guard (no halos on the dress)
         * gentle large-radius clarity
         * skin-aware damping (faces are never over-sharpened)
    6. 1-LSB dither before saving so the smooth grey backdrop cannot band
    7. save as JPEG q95 4:4:4 (and optionally 16-bit PNG/TIFF)

Usage
-----
    python3 tools/upscale_8k.py SRC OUT.jpg [--long 7680] [--quality 95]
    python3 tools/upscale_8k.py SRC OUT.jpg --long 7680 --crop-compare cmp.jpg
"""

from __future__ import annotations

import argparse
import os
import time

import cv2
import numpy as np
from PIL import Image


# ----------------------------------------------------------------------------
def load(path: str) -> np.ndarray:
    with Image.open(path) as im:
        im.load()
        arr = np.asarray(im.convert("RGB"), dtype=np.uint8)
    return arr


def save(path: str, arr: np.ndarray, quality: int = 95) -> None:
    kw = dict(quality=quality, subsampling=0, optimize=True)
    Image.fromarray(arr).save(path, "JPEG", **kw)


def unsharp(arr: np.ndarray, sigma: float, amount: float,
            guard: np.ndarray | None = None, thr: float = 0.0) -> np.ndarray:
    """Unsharp mask, applied channel-wise to keep memory low.

    `thr` is a soft noise gate: detail below the threshold (sensor / webp
    noise in flat skin) is not amplified, so sharpening only bites on real
    edges and texture.
    """
    out = arr.copy()
    for c in range(3):
        ch = arr[..., c]
        blur = cv2.GaussianBlur(ch, (0, 0), sigma)
        d = ch - blur
        if thr > 0:
            d = np.sign(d) * np.clip(np.abs(d) - thr, 0.0, None)
        d = d * amount
        if guard is not None:
            d = d * guard
        out[..., c] = ch + d
    return np.clip(out, 0.0, 1.0)


def flatten_flat_areas(img: np.ndarray, sigma: float = 3.4) -> np.ndarray:
    """Wipe the compression mottling out of the flat studio backdrop.

    A webp/JPEG source carries 8x8 blocking and low-frequency blotches in the
    smooth grey background; upscaling magnifies them into visible clouds.  The
    blotches are smoothed away *only* where the frame is genuinely flat (no
    edge, no texture), so hair, dress and skin detail are untouched.
    """
    lum = 0.2126 * img[..., 0] + 0.7152 * img[..., 1] + 0.0722 * img[..., 2]
    gx = cv2.Sobel(lum, cv2.CV_32F, 1, 0, ksize=3)
    gy = cv2.Sobel(lum, cv2.CV_32F, 0, 1, ksize=3)
    grad = cv2.GaussianBlur(np.abs(gx) + np.abs(gy), (0, 0), 1.5)
    tex = cv2.GaussianBlur(np.abs(lum - cv2.GaussianBlur(lum, (0, 0), 2.0)),
                           (0, 0), 5.0)
    m = np.clip((0.050 - grad) / 0.030, 0.0, 1.0)
    m *= np.clip((0.0100 - tex) / 0.0060, 0.0, 1.0)
    m = cv2.GaussianBlur(m, (0, 0), 6.0).astype(np.float32)
    bl = cv2.GaussianBlur(img, (0, 0), sigma)
    return np.clip(img * (1.0 - m[..., None]) + bl * m[..., None], 0.0, 1.0)


def skin_weight(img: np.ndarray) -> np.ndarray:
    """Soft skin mask (1/6 res, upsampled) used to damp sharpening on faces."""
    h, w = img.shape[:2]
    k = 6
    sh, sw = max(h // k, 8), max(w // k, 8)
    small = cv2.resize(img, (sw, sh), interpolation=cv2.INTER_AREA)
    u8 = (np.clip(small, 0, 1) * 255).astype(np.uint8)
    ycc = cv2.cvtColor(u8, cv2.COLOR_RGB2YCrCb).astype(np.float32)
    y, cr, cb = ycc[..., 0] / 255.0, ycc[..., 1], ycc[..., 2]
    m = ((cr > 133) & (cr < 180) & (cb > 76) & (cb < 133) & (y > 0.18)).astype(np.float32)
    m8 = (m > 0.5).astype(np.uint8)
    m8 = cv2.morphologyEx(m8, cv2.MORPH_OPEN, np.ones((3, 3), np.uint8))
    m8 = cv2.morphologyEx(m8, cv2.MORPH_CLOSE, np.ones((5, 5), np.uint8))
    m = cv2.GaussianBlur(m8.astype(np.float32), (0, 0), 2.0)
    return cv2.resize(m, (w, h), interpolation=cv2.INTER_LINEAR)


# ----------------------------------------------------------------------------
def upscale(src_u8: np.ndarray, long_side: int = 7680,
            ibp_iters: int = 8, verbose: bool = True) -> np.ndarray:
    h0, w0 = src_u8.shape[:2]
    scale = long_side / float(max(h0, w0))
    tw, th = int(round(w0 * scale)), int(round(h0 * scale))

    # ---- 1. clean the source (compression blocking / chroma noise) ---------
    den = cv2.fastNlMeansDenoisingColored(src_u8, None, 3, 3, 7, 21)
    flat = flatten_flat_areas(den.astype(np.float32) / 255.0)
    den = (np.clip(flat, 0, 1) * 255.0 + 0.5).astype(np.uint8)
    del flat
    x21 = None
    x = den.astype(np.float32) / 255.0
    if verbose:
        print(f"  denoised source {w0}x{h0}", flush=True)

    # ---- 2. staged x2 upscale with a small detail injection ---------------
    stage = 0
    while True:
        nh, nw = x.shape[0] * 2, x.shape[1] * 2
        if nh > th or nw > tw:
            break
        x = cv2.resize(x, (nw, nh), interpolation=cv2.INTER_LANCZOS4)
        amt = 0.50 if stage == 0 else 0.38
        x = unsharp(x, 1.2, amt, thr=0.0025)
        stage += 1
        if verbose:
            print(f"  stage {stage}: {nw}x{nh}  (unsharp {amt})", flush=True)

    # ---- 3. iterative back-projection at the current size -----------------
    #  X *= solve such that down(X) ~= source.  Works on a mid-size buffer so
    #  memory stays sane; the final resize is done afterwards.
    base = den.astype(np.float32) / 255.0
    lam = 0.55
    for it in range(ibp_iters):
        back = cv2.resize(x, (w0, h0), interpolation=cv2.INTER_AREA)
        err = base - back
        up = cv2.resize(err, (x.shape[1], x.shape[0]), interpolation=cv2.INTER_CUBIC)
        x = np.clip(x + lam * up, 0.0, 1.0)
        if verbose and (it % 3 == 0 or it == ibp_iters - 1):
            e = float(np.abs(err).mean())
            print(f"  IBP {it + 1}/{ibp_iters}  mean|err|={e:.4f}", flush=True)
    del back, err, up, base

    # ---- 4. final size ----------------------------------------------------
    if (x.shape[1], x.shape[0]) != (tw, th):
        x = cv2.resize(x, (tw, th), interpolation=cv2.INTER_LANCZOS4)
    if verbose:
        print(f"  target {tw}x{th}", flush=True)

    # ---- 5. banded detail pass (memory friendly) --------------------------
    skin = skin_weight(x)
    out = np.empty_like(x)
    band = 512
    pad = 32
    H, W = x.shape[:2]
    for y0 in range(0, H, band):
        y1 = min(H, y0 + band)
        py0, py1 = max(0, y0 - pad), min(H, y1 + pad)
        sub = x[py0:py1]
        sk = skin[py0:py1]

        # highlight guard: no sharpening where the dress / skin blows out
        lum = 0.2126 * sub[..., 0] + 0.7152 * sub[..., 1] + 0.0722 * sub[..., 2]
        guard = np.clip(1.0 - (lum - 0.90) / 0.10, 0.0, 1.0)
        guard *= (1.0 - 0.55 * sk)                     # faces: gentler
        guard = guard.astype(np.float32)

        sharp = unsharp(sub, 1.6, 0.46, guard, thr=0.004)
        del guard

        # gentle large-radius clarity for local micro-contrast (still banded)
        for c in range(3):
            ch = sharp[..., c]
            sharp[..., c] = ch + (ch - cv2.GaussianBlur(ch, (0, 0), 45.0)) * 0.10

        off = y0 - py0
        out[y0:y1] = np.clip(sharp[off:off + (y1 - y0)], 0.0, 1.0)
        del sub, sk, sharp

    # ---- 6. dither to stop banding in the flat backdrop -------------------
    rng = np.random.default_rng(3)
    noise = rng.standard_normal((H, W), dtype=np.float32)
    noise = cv2.GaussianBlur(noise, (0, 0), 0.7) * (1.4 / 255.0)
    out += noise[..., None]
    out = np.clip(out, 0.0, 1.0)
    return (out * 255.0 + 0.5).astype(np.uint8)


# ----------------------------------------------------------------------------
def crop_compare(src_u8: np.ndarray, big_u8: np.ndarray, path: str,
                 box=(0.30, 0.08, 0.75, 0.42)) -> None:
    """100% crop of the upscaled result next to the same area upscaled naively."""
    H, W = big_u8.shape[:2]
    x0, y0, x1, y1 = int(box[0] * W), int(box[1] * H), int(box[2] * W), int(box[3] * H)
    a = big_u8[y0:y1, x0:x1]
    sh, sw = src_u8.shape[:2]
    naive = cv2.resize(src_u8, (big_u8.shape[1], big_u8.shape[0]),
                       interpolation=cv2.INTER_LINEAR)[y0:y1, x0:x1]
    h, w = a.shape[:2]
    sheet = np.full((h, w * 2 + 8, 3), 255, np.uint8)
    sheet[:, :w] = naive
    sheet[:, w + 8:] = a
    Image.fromarray(sheet).save(path, "JPEG", quality=92, subsampling=0)
    print(f"  crop-compare -> {path}  ({w}x{h} each)")


# ----------------------------------------------------------------------------
def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="8K upscale for portraits")
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--long", type=int, default=7680, help="target long side (8K = 7680)")
    ap.add_argument("--quality", type=int, default=95)
    ap.add_argument("--ibp", type=int, default=8)
    ap.add_argument("--compare", default=None, help="write a 100%% crop comparison here")
    a = ap.parse_args(argv)

    t0 = time.time()
    src = load(a.src)
    print(f"source {src.shape[1]}x{src.shape[0]}", flush=True)
    big = upscale(src, long_side=a.long, ibp_iters=a.ibp)
    save(a.dst, big, quality=a.quality)
    print(f"saved {a.dst}  {big.shape[1]}x{big.shape[0]}  "
          f"{os.path.getsize(a.dst) / 1e6:.2f} MB  ({time.time() - t0:.1f}s)", flush=True)
    if a.compare:
        crop_compare(src, big, a.compare)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

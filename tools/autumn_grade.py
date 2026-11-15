#!/usr/bin/env python3
"""autumn_grade.py — autumn-mood colour grade, NO retouching.

Purpose
-------
A single, repeatable "golden autumn" look for a batch of portraits:

    * golden-hour white balance (warm highlights, slightly cool shadows)
    * filmic S-curve with lifted blacks and a soft shoulder
    * selective colour: leftover greens are pulled toward autumn gold,
      oranges/yellows get the extra saturation, reds/magentas are kept sane
    * gentle highlight bloom (Orton glow) and mid-tone clarity
    * soft vignette + very light luminance grain for a filmic finish
    * fine output sharpening, protected in the highlights

The people in the frame get their own colour treatment (this is colour
grading only — nothing is smoothed, healed, warped or retouched):

    * skin is found automatically (YCrCb skin chroma + low local texture +
      compact connected blobs), so golden foliage does not get "protected"
      as if it were skin
    * skin keeps a natural hue under the warm grade (no orange faces)
    * mild redness / blotchy-chroma damping, a small shadow fill and a
      highlight guard so faces stay clean and evenly lit
    * bloom, clarity, saturation and sharpening are all reduced on skin so
      faces never look greasy or over-processed

Resolution, EXIF, DPI and the ICC profile of the input are preserved: the
output pixel size is exactly the input pixel size.

Usage
-----
    python3 tools/autumn_grade.py IN.JPG OUT.JPG [--quality 96]
    python3 tools/autumn_grade.py --batch SRC_DIR OUT_DIR [--quality 96]
"""

from __future__ import annotations

import argparse
import gc
import os
import sys
import time

import cv2
import numpy as np
from PIL import Image


# ----------------------------------------------------------------------------
# helpers
# ----------------------------------------------------------------------------
def gblur(img: np.ndarray, sigma: float) -> np.ndarray:
    return cv2.GaussianBlur(img, (0, 0), float(max(sigma, 0.3)))


def load_image(path: str):
    """Return (float32 RGB in 0..1, exif bytes, icc bytes, dpi)."""
    with Image.open(path) as im:
        im.load()
        exif = im.info.get("exif", b"")
        icc = im.info.get("icc_profile", None)
        dpi = im.info.get("dpi", None)
        rgb = np.asarray(im.convert("RGB"), dtype=np.float32) / 255.0
    return rgb, exif, icc, dpi


def save_image(path: str, rgb: np.ndarray, exif=b"", icc=None, dpi=None,
               quality: int = 96) -> None:
    a = (np.clip(rgb, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)
    kw = dict(quality=quality, subsampling=0, optimize=True, progressive=False)
    if exif:
        kw["exif"] = exif
    if icc:
        kw["icc_profile"] = icc
    if dpi:
        kw["dpi"] = dpi
    Image.fromarray(a).save(path, "JPEG", **kw)
    del a


# ----------------------------------------------------------------------------
# people / skin mask  (colour grading aid — not a retouch mask)
# ----------------------------------------------------------------------------
def people_mask(img: np.ndarray) -> np.ndarray:
    """Soft 0..1 mask of visible skin (faces, hands, necks).

    Colour alone is not enough in an autumn frame: dry leaves sit in the same
    orange range as skin.  Skin is therefore also required to be *smooth*
    (low local texture energy) and to live in compact, sizeable blobs.
    """
    h, w = img.shape[:2]
    k = 6
    sh, sw = max(h // k, 8), max(w // k, 8)
    small = cv2.resize(img, (sw, sh), interpolation=cv2.INTER_AREA)
    u8 = (np.clip(small, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)
    ycc = cv2.cvtColor(u8, cv2.COLOR_RGB2YCrCb).astype(np.float32)
    y, cr, cb = ycc[..., 0] / 255.0, ycc[..., 1], ycc[..., 2]

    m = ((cr > 133) & (cr < 178) & (cb > 76) & (cb < 132) & (y > 0.20)).astype(np.float32)

    # smoothness gate: leaves / bark / fabric are textured, skin is not
    lum = 0.299 * small[..., 0] + 0.587 * small[..., 1] + 0.114 * small[..., 2]
    hf = np.abs(lum - gblur(lum, 1.2))
    tex = gblur(hf, 2.4)
    m *= np.clip((0.055 - tex) / 0.030, 0.0, 1.0)

    # keep only blob-shaped regions of a sensible size
    m8 = (m > 0.5).astype(np.uint8)
    m8 = cv2.morphologyEx(m8, cv2.MORPH_OPEN, np.ones((3, 3), np.uint8))
    m8 = cv2.morphologyEx(m8, cv2.MORPH_CLOSE, np.ones((7, 7), np.uint8))
    n, lab, stats, _ = cv2.connectedComponentsWithStats(m8, connectivity=8)
    keep = np.zeros_like(m8, dtype=np.float32)
    min_area = max(24, int(0.00035 * sw * sh))
    for i in range(1, n):
        area = stats[i, cv2.CC_STAT_AREA]
        bw, bh = stats[i, cv2.CC_STAT_WIDTH], stats[i, cv2.CC_STAT_HEIGHT]
        if area < min_area or bw < 6 or bh < 6:
            continue
        keep[lab == i] = 1.0
    m *= keep
    m = cv2.morphologyEx(m, cv2.MORPH_CLOSE, np.ones((5, 5), np.uint8))
    m = cv2.GaussianBlur(m, (0, 0), 2.0)
    return cv2.resize(m, (w, h), interpolation=cv2.INTER_LINEAR)


# ----------------------------------------------------------------------------
# tone curve
# ----------------------------------------------------------------------------
def filmic(x: np.ndarray, contrast: float = 0.26, lift: float = 0.008,
           rolloff: float = 1.06, shoulder: float = 0.86) -> np.ndarray:
    y = x * (1.0 - lift) + lift
    s = 3.0 * y * y - 2.0 * y * y * y
    y = y * (1.0 - contrast) + s * contrast
    y = 1.0 - np.power(np.clip(1.0 - y, 0.0, 1.0), rolloff)
    over = np.clip(y - shoulder, 0.0, 1.0)
    y = y - 0.38 * over * over
    return np.clip(y, 0.0, 1.0)


# ----------------------------------------------------------------------------
# the grade
# ----------------------------------------------------------------------------
def autumn_grade(img: np.ndarray, strength: float = 1.0,
                 glow: float = 0.055, clarity: float = 0.18,
                 vignette: float = 0.16, grain: float = 1.1,
                 sharpen: float = 0.40, skin_protect: float = 1.00,
                 skin_fill: float = 0.020,
                 skin_warm=(0.010, 0.004, -0.008)) -> np.ndarray:
    h, w = img.shape[:2]
    out = img.astype(np.float32, copy=True)

    skin = people_mask(img)
    skin3 = skin[..., None]
    protect = 1.0 - skin_protect * skin  # skin gets a gentler version of the grade

    # ---- 1. golden-hour white balance -------------------------------------
    out[..., 0] *= 1.0 + 0.034 * strength * protect
    out[..., 1] *= 1.0 + 0.003 * strength * protect
    out[..., 2] *= 1.0 - 0.048 * strength * protect

    # ---- 2. filmic tone curve --------------------------------------------
    lum_in = (0.2126 * out[..., 0] + 0.7152 * out[..., 1]
              + 0.0722 * out[..., 2]).astype(np.float32)

    # --- skin shadow fill (colour only: lighten dark skin tones, no blurring)
    fill = np.clip((0.42 - lum_in) / 0.42, 0.0, 1.0) * skin_fill * skin
    out += fill[..., None]

    # Curve on luminance only and re-scale the channels: a per-channel S-curve
    # desaturates the highlights, which turns warm skin into a milky pink.
    lum_new = filmic(lum_in)
    ratio = (lum_new / np.maximum(lum_in, 1e-4)).astype(np.float32)
    out *= ratio[..., None]
    del ratio
    out = np.clip(out, 0.0, 1.0)

    lum = (0.2126 * out[..., 0] + 0.7152 * out[..., 1]
           + 0.0722 * out[..., 2]).astype(np.float32)
    del lum_new

    # ---- 3. colour: vibrance, autumn hues, skin sanity --------------------
    mx = out.max(axis=2)
    mn = out.min(axis=2)
    chroma = mx - mn
    warm_hue = np.clip((out[..., 0] - out[..., 2]) * 2.6, 0.0, 1.0)

    sat_factor = (1.02
                  + 0.34 * np.clip(0.45 - chroma, 0.0, 1.0)     # vibrance
                  + 0.30 * warm_hue * protect                    # golden leaves
                  - 0.42 * np.clip(chroma - 0.33, 0.0, 1.0))     # tame reds
    sat_factor *= (1.0 - 0.70 * np.clip((lum - 0.88) / 0.12, 0.0, 1.0))
    sat_factor *= (1.0 - 0.10 * skin)                            # calmer skin
    sat_factor = np.clip(sat_factor, 0.0, 1.9)
    for c in range(3):
        out[..., c] = lum + (out[..., c] - lum) * sat_factor
    del mx, mn, chroma, sat_factor, warm_hue

    # skin: keep the hue natural, pull redness back a touch, warm it up gently
    skin_chroma = gblur(skin, 2.0)
    for c, gain in zip(range(3), skin_warm):
        out[..., c] += gain * skin_chroma
    del skin_chroma

    # ---- 4. selective colour: greens / yellows toward autumn gold ---------
    green_w = np.clip((out[..., 1] - out[..., 0] + 0.06) / 0.12, 0.0, 1.0)
    green_w = green_w * (1.0 - skin)
    out[..., 0] += 0.030 * green_w * strength
    out[..., 1] -= 0.020 * green_w * strength
    out[..., 2] -= 0.028 * green_w * strength
    del green_w

    # ---- 5. highlight glow (never on faces) -------------------------------
    hi = np.clip((lum - 0.52) / 0.30, 0.0, 1.0)
    hi = (hi * hi * (3.0 - 2.0 * hi)).astype(np.float32)
    out[..., 0] += 0.024 * hi * protect
    out[..., 1] += 0.010 * hi * protect
    out[..., 2] -= 0.020 * hi * protect
    out = np.clip(out, 0.0, 1.0)

    sig_glow = 20.0
    bloom_w = (0.14 + 0.86 * hi) * glow
    bloom_w *= (1.0 - 0.65 * np.clip((lum - 0.85) / 0.15, 0.0, 1.0))
    bloom_w *= (1.0 - 0.60 * skin)
    for c in range(3):
        ch = out[..., c]
        bl = gblur(ch, sig_glow)
        out[..., c] = ch + (bl - ch * bl) * bloom_w
    del bloom_w
    out = np.clip(out, 0.0, 1.0)

    # ---- 6. clarity: mid-tone local contrast ------------------------------
    mid = 1.0 - np.abs(lum * 2.0 - 1.0)
    clar_w = clarity * np.clip(mid, 0.0, 1.0) * (1.0 - 0.50 * skin)
    for c in range(3):
        ch = out[..., c]
        out[..., c] = ch + (ch - gblur(ch, 30.0)) * clar_w
    del clar_w, mid
    out = np.clip(out, 0.0, 1.0)

    # ---- 7. vignette ------------------------------------------------------
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    nx = (xx - w * 0.5) / (w * 0.62)
    ny = (yy - h * 0.5) / (h * 0.66)
    rad = np.sqrt(nx * nx + ny * ny)
    del yy, xx, nx, ny
    vig = 1.0 - vignette * np.clip((rad - 0.55) / 0.75, 0.0, 1.0) ** 1.6
    out *= vig[..., None]
    del rad, vig
    out = np.clip(out, 0.0, 1.0)

    # ---- 8. fine output sharpening (halo-free, highlight protected) -------
    sh_w = sharpen * np.clip(1.0 - 0.75 * hi, 0.0, 1.0) * (1.0 - 0.35 * skin)
    for c in range(3):
        ch = out[..., c]
        out[..., c] = ch + (ch - gblur(ch, 1.1)) * sh_w
    del sh_w, hi
    out = np.clip(out, 0.0, 1.0)

    # ---- 9. very light filmic grain --------------------------------------
    if grain > 0:
        rng = np.random.default_rng(11)
        n = rng.standard_normal((h, w), dtype=np.float32)
        n = cv2.GaussianBlur(n, (0, 0), 0.6) * (grain / 255.0)
        shade = 0.55 + 0.45 * np.clip(1.0 - lum * 1.2, 0.0, 1.0)
        out += (n * shade)[..., None]
    del lum, skin, skin3, protect
    return np.clip(out, 0.0, 1.0)


# ----------------------------------------------------------------------------
# drivers
# ----------------------------------------------------------------------------
def process_one(src: str, dst: str, quality: int = 96,
                strength: float = 1.0) -> dict:
    t0 = time.time()
    rgb, exif, icc, dpi = load_image(src)
    h, w = rgb.shape[:2]
    graded = autumn_grade(rgb, strength=strength)
    del rgb
    save_image(dst, graded, exif=exif, icc=icc, dpi=dpi, quality=quality)
    del graded
    gc.collect()
    return dict(src=src, dst=dst, size=(w, h),
                bytes=os.path.getsize(dst), seconds=round(time.time() - t0, 1),
                exif_preserved=bool(exif))


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="autumn colour grade (no retouching)")
    ap.add_argument("src", nargs="?")
    ap.add_argument("dst", nargs="?")
    ap.add_argument("--batch", nargs=2, metavar=("SRC_DIR", "OUT_DIR"))
    ap.add_argument("--quality", type=int, default=96)
    ap.add_argument("--strength", type=float, default=1.0)
    ap.add_argument("--suffix", default="_autumn")
    a = ap.parse_args(argv)

    jobs = []
    if a.batch:
        sdir, odir = a.batch
        os.makedirs(odir, exist_ok=True)
        for name in sorted(os.listdir(sdir)):
            if name.lower().endswith((".jpg", ".jpeg", ".png", ".tif", ".tiff")):
                stem, _ = os.path.splitext(name)
                jobs.append((os.path.join(sdir, name),
                             os.path.join(odir, f"{stem}{a.suffix}.jpg")))
    else:
        if not (a.src and a.dst):
            ap.error("give SRC and DST, or --batch SRC_DIR OUT_DIR")
        jobs.append((a.src, a.dst))

    for i, (src, dst) in enumerate(jobs, 1):
        r = process_one(src, dst, quality=a.quality, strength=a.strength)
        print(f"[{i}/{len(jobs)}] {os.path.basename(dst)}  {r['size'][0]}x{r['size'][1]}"
              f"  q={a.quality}  {r['bytes']/1e6:.2f} MB  {r['seconds']}s", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

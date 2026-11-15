#!/usr/bin/env python3
"""
Retouch + "Autumn Golden" preset for portrait photos.

Everything runs at the *original* resolution of the input file: no resampling,
no cropping, no quality loss beyond the JPEG re-encode (q=96, 4:4:4).

Pipeline
--------
1. Retouch (local, face regions only)
     - surface smoothing by edge-preserving bilateral filtering,
     - high-frequency texture added back so skin keeps real pores,
     - small blemish / spot removal by median-difference masking,
     - crisp eyes / lashes / brows / hair (non-skin edges inside the face oval).
2. Global grade: autumn golden preset
     - warm white balance, filmic S-curve with lifted blacks,
     - vibrance + extra warmth on golden hues,
     - golden highlights, deep neutral-cool shadows,
     - soft Orton glow, local contrast (clarity),
     - output sharpening, vignette, very light film grain.

Usage
-----
    python3 tools/retouch_autumn.py IN.JPG OUT.JPG [--scale 1.0]
                                                 [--face x,y,rx,ry ...]
"""
from __future__ import annotations

import argparse
import sys

import cv2
import numpy as np
from PIL import Image

# ----------------------------------------------------------------------------
# defaults: face ovals in original-image pixel coordinates (cx, cy, rx, ry)
# ----------------------------------------------------------------------------
DEFAULT_FACES = [
    (3750, 2330, 250, 300),   # bride
    (4410, 1745, 235, 265),   # groom
]


# ----------------------------------------------------------------------------
# small helpers
# ----------------------------------------------------------------------------
def gblur(img: np.ndarray, sigma: float) -> np.ndarray:
    return cv2.GaussianBlur(img, (0, 0), sigma, borderType=cv2.BORDER_REFLECT)


def ellipse_weight(h: int, w: int, cx: float, cy: float, rx: float, ry: float,
                   feather: float = 0.22) -> np.ndarray:
    """Soft oval mask (1 in the middle, 0 outside)."""
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    d = np.sqrt(((xx - cx) / max(rx, 1.0)) ** 2 + ((yy - cy) / max(ry, 1.0)) ** 2)
    return np.clip((1.0 + feather - d) / feather, 0.0, 1.0)


def skin_mask(rgb: np.ndarray) -> np.ndarray:
    """YCrCb skin detection, softly feathered."""
    u8 = np.clip(rgb * 255.0, 0, 255).astype(np.uint8)
    ycc = cv2.cvtColor(u8, cv2.COLOR_RGB2YCrCb).astype(np.float32)
    y, cr, cb = ycc[..., 0], ycc[..., 1], ycc[..., 2]
    m = ((cr > 131) & (cr < 180) & (cb > 74) & (cb < 134) & (y > 40)).astype(np.float32)
    m = cv2.morphologyEx(m, cv2.MORPH_CLOSE, np.ones((7, 7), np.uint8))
    return cv2.GaussianBlur(m, (0, 0), 3.0)


# ----------------------------------------------------------------------------
# 1) retouch
# ----------------------------------------------------------------------------
def retouch_face(img: np.ndarray, oval, s: float,
                 smooth: float = 0.85, detail_keep: float = 0.55,
                 spots: float = 0.9, edge_sharpen: float = 0.55) -> np.ndarray:
    """img: float32 RGB [0,1] full frame. Returns the same frame, face retouched."""
    h, w = img.shape[:2]
    cx, cy, rx, ry = [v * s for v in oval]
    pad = int(max(rx, ry) * 0.35)
    x0 = max(int(cx - rx - pad), 0)
    x1 = min(int(cx + rx + pad), w)
    y0 = max(int(cy - ry - pad), 0)
    y1 = min(int(cy + ry + pad), h)
    if x1 - x0 < 8 or y1 - y0 < 8:
        return img

    roi = img[y0:y1, x0:x1]
    rh, rw = roi.shape[:2]

    oval_w = ellipse_weight(rh, rw, cx - x0, cy - y0, rx, ry)
    skin = skin_mask(roi) * oval_w

    # --- surface blur that keeps edges (two passes ≈ wide, gentle smoothing)
    u8 = np.clip(roi * 255.0, 0, 255).astype(np.uint8)
    d = max(5, int(round(9 * s / 1.0)))
    b1 = cv2.bilateralFilter(u8, d, 26, d)
    b2 = cv2.bilateralFilter(b1, d, 26, int(round(d * 1.6)))
    surf = b2.astype(np.float32) / 255.0

    # --- put real texture back on top of the smoothed surface
    low = gblur(roi, max(2.0, 3.0 * s))
    detail = roi - low
    base = surf + detail * detail_keep

    # --- blemish / spot pass
    med = cv2.medianBlur(u8, max(5, int(round(15 * s)) | 1)).astype(np.float32) / 255.0
    diff = np.abs(roi - med).max(axis=2)
    spot_m = np.clip((diff - 0.045) / 0.045, 0.0, 1.0) * skin
    spot_m = (spot_m * spots)[..., None]
    base = base * (1.0 - spot_m) + med * spot_m

    a = (skin * smooth)[..., None]
    out = roi * (1.0 - a) + base * a

    # --- crisp eyes / lashes / brows / hair: sharpen everything that is not skin
    hf = roi - gblur(roi, max(1.0, 1.4 * s))
    sharp = roi + hf * (1.4 * edge_sharpen)
    non_skin = (oval_w * (1.0 - skin) * 0.45)[..., None]
    out = out * (1.0 - non_skin) + sharp * non_skin

    img[y0:y1, x0:x1] = np.clip(out, 0.0, 1.0)
    return img


# ----------------------------------------------------------------------------
# 2) autumn golden preset
# ----------------------------------------------------------------------------
def apply_curve(x: np.ndarray, k_contrast: float, lift: float,
                rolloff: float) -> np.ndarray:
    """Filmic S-curve: matte lifted blacks, gentle shoulder."""
    y = x * (1.0 - lift) + lift
    s = 3.0 * y * y - 2.0 * y * y * y          # smoothstep
    y = y * (1.0 - k_contrast) + s * k_contrast
    y = 1.0 - np.power(np.clip(1.0 - y, 0.0, 1.0), rolloff)
    # soft shoulder: tame the blown sky so the frame does not look hazy
    over = np.clip(y - 0.78, 0.0, 1.0)
    y = y - 0.42 * over * over
    return np.clip(y, 0.0, 1.0)


def skin_mask_frame(img: np.ndarray, ovals, s: float = 1.0) -> np.ndarray:
    """Soft full-frame skin mask (computed at 1/8 res, upscaled) for colour work.

    The colour test is gated by generous ovals around the faces: golden leaves
    live in the same YCrCb range as skin, so colour alone would protect the
    whole autumn background and mute the preset.
    """
    h, w = img.shape[:2]
    k = 8
    sh, sw = max(h // k, 8), max(w // k, 8)
    small = cv2.resize(img, (sw, sh), interpolation=cv2.INTER_AREA)
    u8 = np.clip(small * 255.0, 0, 255).astype(np.uint8)
    ycc = cv2.cvtColor(u8, cv2.COLOR_RGB2YCrCb).astype(np.float32)
    y, cr, cb = ycc[..., 0], ycc[..., 1], ycc[..., 2]
    m = ((cr > 132) & (cr < 180) & (cb > 74) & (cb < 133) & (y > 45)).astype(np.float32)
    m = cv2.morphologyEx(m, cv2.MORPH_CLOSE, np.ones((5, 5), np.uint8))
    m = cv2.GaussianBlur(m, (0, 0), 1.6)

    gate = np.zeros((sh, sw), np.float32)
    for cx, cy, rx, ry in ovals:
        gate = np.maximum(gate, ellipse_weight(sh, sw, cx * s / k, cy * s / k,
                                               rx * s * 1.5 / k, ry * s * 1.5 / k,
                                               feather=0.9))
    m = m * gate
    return cv2.resize(m, (w, h), interpolation=cv2.INTER_LINEAR)


def autumn_grade(img: np.ndarray, s: float = 1.0, ovals=DEFAULT_FACES,
                 warmth: float = 1.0, glow_amt: float = 0.072,
                 clarity: float = 0.20, vignette_amt: float = 0.18,
                 grain: float = 1.2) -> np.ndarray:
    h, w = img.shape[:2]
    out = img.copy()

    # skin is protected from most of the warmth so faces stay natural
    skin = skin_mask_frame(img, ovals, s)
    protect = 1.0 - 0.52 * skin

    # ---- white balance: golden hour tilt
    out[..., 0] *= (1.0 + 0.036 * warmth * protect)
    out[..., 1] *= (1.0 + 0.004 * warmth * protect)
    out[..., 2] *= (1.0 - 0.050 * warmth * protect)

    # ---- tone curve, per channel (filmic, lifted blacks)
    k_c = 0.20
    for c in range(3):
        out[..., c] = apply_curve(out[..., c], k_c, 0.011, 1.06)

    # ---- luminance / chroma work
    luma = (0.2126 * out[..., 0] + 0.7152 * out[..., 1] + 0.0722 * out[..., 2])
    luma = luma.astype(np.float32)

    # vibrance + extra saturation on golden hues (R > G > B pattern)
    mx = out.max(axis=2)
    mn = out.min(axis=2)
    chroma = mx - mn
    warm_hue = np.clip((out[..., 0] - out[..., 2]) * 2.6, 0.0, 1.0)
    sat_factor = (1.02
                  + 0.38 * np.clip(0.45 - chroma, 0.0, 1.0)   # vibrance: dull colours gain most
                  + 0.32 * warm_hue * protect
                  - 0.45 * np.clip(chroma - 0.33, 0.0, 1.0))   # keep lipstick/reds sane
    # keep near-blown highlights clean (no colour fringing on bokeh/sky)
    sat_factor = sat_factor * (1.0 - 0.72 * np.clip((luma - 0.88) / 0.12, 0.0, 1.0))
    sat_factor = np.clip(sat_factor, 0.0, 1.9)
    for c in range(3):
        ch = out[..., c]
        out[..., c] = luma + (ch - luma) * sat_factor
    del mx, mn, chroma, sat_factor

    # ---- golden highlights
    hi = np.clip((luma - 0.52) / 0.30, 0.0, 1.0)
    hi = hi * hi * (3.0 - 2.0 * hi)
    out[..., 0] += (0.026 * hi) * protect
    out[..., 1] += (0.012 * hi) * protect
    out[..., 2] -= (0.022 * hi) * protect

    # ---- deeper, warm-brown shadows (depth without a cold cast)
    lo = np.clip((0.46 - luma) / 0.40, 0.0, 1.0)
    lo = lo * lo * (3.0 - 2.0 * lo)
    out[..., 0] -= 0.002 * lo
    out[..., 1] += 0.001 * lo
    out[..., 2] += 0.008 * lo

    # ---- selective colour: turn leftover greens into autumn gold
    green_w = np.clip((out[..., 1] - out[..., 0] + 0.05) / 0.10, 0.0, 1.0)
    out[..., 0] += 0.024 * green_w
    out[..., 1] -= 0.016 * green_w
    out[..., 2] -= 0.024 * green_w

    out = np.clip(out, 0.0, 1.0)

    # ---- soft Orton glow (highlight bloom, channel by channel to save memory)
    sig_glow = 22.0 * s
    bloom_w = (0.12 + 0.88 * hi) * glow_amt
    bloom_w = bloom_w * (1.0 - 0.6 * np.clip((luma - 0.85) / 0.15, 0.0, 1.0))
    for c in range(3):
        ch = out[..., c]
        bl = gblur(ch, sig_glow)
        out[..., c] = ch + (bl - ch * bl) * bloom_w

    # ---- clarity: large-radius local contrast in the midtones
    sig_clar = 34.0 * s
    mid = 1.0 - np.abs(luma * 2.0 - 1.0)
    clar_w = clarity * np.clip(mid, 0.0, 1.0)
    for c in range(3):
        ch = out[..., c]
        out[..., c] = ch + (ch - gblur(ch, sig_clar)) * clar_w

    out = np.clip(out, 0.0, 1.0)

    # ---- output sharpening (fine detail)
    sig_sh = 1.1 * s
    sh_w = 0.42 * np.clip(1.0 - hi * 0.7, 0.0, 1.0)   # protect blown highlights
    for c in range(3):
        ch = out[..., c]
        out[..., c] = ch + (ch - gblur(ch, sig_sh)) * sh_w

    out = np.clip(out, 0.0, 1.0)

    # ---- subject separation: gently burn the backdrop, keep the couple forward
    subj = ellipse_weight(h, w, w * 0.615, h * 0.66, w * 0.30, h * 0.47,
                          feather=0.75)
    out *= (1.0 - 0.07 * (1.0 - subj))[..., None]
    out = np.clip(out, 0.0, 1.0)

    # ---- vignette, oval centred on the couple
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    nx = (xx - w * 0.53) / (w * 0.62)
    ny = (yy - h * 0.55) / (h * 0.66)
    r = np.sqrt(nx * nx + ny * ny)
    vig = 1.0 - vignette_amt * np.clip((r - 0.55) / 0.75, 0.0, 1.0) ** 1.6
    out *= vig[..., None]

    # ---- very light luminance grain (filmic, not noise)
    if grain > 0:
        rng = np.random.default_rng(7)
        n = rng.standard_normal((h, w), dtype=np.float32)
        n = cv2.GaussianBlur(n, (0, 0), 0.6) * (grain / 255.0)
        shade = 0.55 + 0.45 * np.clip(1.0 - luma * 1.2, 0.0, 1.0)
        out += (n * shade)[..., None]

    return np.clip(out, 0.0, 1.0)


# ----------------------------------------------------------------------------
# main
# ----------------------------------------------------------------------------
def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--scale", type=float, default=1.0,
                    help="render scale (1.0 keeps the original pixel size)")
    ap.add_argument("--face", action="append", default=None,
                    help="face oval cx,cy,rx,ry in original pixels (repeatable)")
    ap.add_argument("--quality", type=int, default=96)
    ap.add_argument("--no-retouch", action="store_true")
    ap.add_argument("--no-grade", action="store_true")
    a = ap.parse_args(argv)

    faces = DEFAULT_FACES
    if a.face:
        faces = [tuple(float(v) for v in f.split(",")) for f in a.face]

    src = Image.open(a.src)
    src.load()
    exif = src.info.get("exif")
    dpi = src.info.get("dpi", (72, 72))
    icc = src.info.get("icc_profile")
    W, H = src.size
    s = a.scale

    img = np.asarray(src.convert("RGB"), dtype=np.float32) / 255.0
    del src

    if s != 1.0:
        img = cv2.resize(img, (int(round(W * s)), int(round(H * s))),
                         interpolation=cv2.INTER_AREA)

    if not a.no_retouch:
        for oval in faces:
            img = retouch_face(img, oval, s)

    if not a.no_grade:
        img = autumn_grade(img, s, faces)

    out8 = np.clip(img * 255.0 + 0.5, 0, 255).astype(np.uint8)
    del img
    res = Image.fromarray(out8, "RGB")

    kw = dict(quality=a.quality, subsampling=0, optimize=True, dpi=dpi)
    if exif:
        kw["exif"] = exif
    if icc:
        kw["icc_profile"] = icc
    res.save(a.dst, "JPEG", **kw)
    print(f"saved {a.dst}  {res.size[0]}x{res.size[1]}  q={a.quality} 4:4:4")
    return 0


if __name__ == "__main__":
    sys.exit(main())

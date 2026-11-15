#!/usr/bin/env python3
"""
Full retouch + "Autumn Golden" preset for portrait photos.

Everything runs at the *original* resolution of the input file: no resampling,
no cropping, no quality loss beyond the JPEG re-encode (q=96, 4:4:4).

Pipeline
--------
1. Full retouch (local, around the faces; optional hands)
     - skin mask = hand-placed face oval, refined by skin colour, and gated by
       three exclusion tests: relative darkness, absolute darkness and local
       texture energy -> beards, hair, knitwear, fabric and jewellery are never
       smoothed (that is what turns a beard to mush or a coat to plastic)
     - chroma smoothing: redness / blotchiness of the skin evened out
     - two local-shading passes (narrow + wide): dark circles, philtrum and
       patchy tone flattened toward the local mean, big modelling kept
     - three-pass edge-preserving bilateral surface blur ("surface blur")
     - real skin micro-texture added back at a low ratio, so it stays a photo
     - multi-scale blemish removal (median-difference masks, two radii,
       restricted to confident skin)
     - lip polish that softens the crease and wrinkles but keeps a hint of
       contour (never sharpened: a hard lip line reads as drawn eyeliner)
     - eyes / lashes / brows protected and then sharpened hard
     - gentle skin brightening + shadow lift inside the face ovals
2. Global grade: autumn golden preset
     - warm white balance (skin protected), filmic S-curve with lifted blacks,
     - vibrance + extra warmth on golden hues, greens pushed to gold,
     - golden highlights, deep warm shadows,
     - soft Orton glow, clarity, subject separation,
     - output sharpening, vignette, very light film grain.

Usage
-----
    python3 tools/retouch_autumn.py IN.JPG OUT.JPG [--scale 1.0] [--beauty 1.0]
                                                 [--face x,y,rx,ry ...]

    --beauty 0.6   lighter, more natural skin
    --beauty 1.0   full beauty retouch (default)
    --beauty 1.4   very strong / glossy-magazine look
"""
from __future__ import annotations

import argparse
import sys

import cv2
import numpy as np
from PIL import Image

# ----------------------------------------------------------------------------
# defaults: ovals in original-image pixel coordinates (cx, cy, rx, ry)
# ----------------------------------------------------------------------------
DEFAULT_FACES = [
    (3755, 2345, 230, 255),   # bride
    (4442, 1706, 176, 216),   # groom
]
# eyes / lips: excluded from the smoothing and sharpened instead
DEFAULT_EYES = [
    (3728, 2228, 95, 55),     # bride, left eye (closed, lashes)
    (3898, 2242, 95, 55),     # bride, right eye
    (4415, 1692, 195, 75),    # groom, both eyes
]
# (only where real lipstick/lips are visible — a mouth buried in a beard must
#  never be smeared, so the groom has no lip zone)
DEFAULT_LIPS = [
    (3800, 2470, 118, 52),    # bride
]
DEFAULT_HANDS = [
    (3915, 3395, 235, 195),   # bride, hands + bracelets
    (4415, 3340, 275, 205),   # groom, hand + watch
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


def _feather_oval_mask(h: int, w: int, ovals, s: float, feather: float) -> np.ndarray:
    m = np.zeros((h, w), np.float32)
    for cx, cy, rx, ry in ovals:
        m = np.maximum(m, ellipse_weight(h, w, cx * s, cy * s, rx * s, ry * s,
                                         feather=feather))
    return m


# ----------------------------------------------------------------------------
# 1) full retouch
# ----------------------------------------------------------------------------
def retouch_skin(img: np.ndarray, oval, s: float, beauty: float = 1.0,
                 protect=(), no_sharp=(), feather: float = 0.16,
                 texture_keep: float = 0.28, edge_sharpen: float = 1.0,
                 brighten: float = 0.010, bil_sigma: float = 32.0,
                 tone_even: float = 0.55, dark_thr: float = 0.09,
                 do_spots: bool = True, do_sharp: bool = True,
                 do_tone: bool = True) -> np.ndarray:
    """Heavy but photographic beauty retouch inside one oval region."""
    h, w = img.shape[:2]
    cx, cy, rx, ry = [v * s for v in oval]
    pad = int(max(rx, ry) * 0.40)
    x0, x1 = max(int(cx - rx - pad), 0), min(int(cx + rx + pad), w)
    y0, y1 = max(int(cy - ry - pad), 0), min(int(cy + ry + pad), h)
    if x1 - x0 < 8 or y1 - y0 < 8:
        return img

    roi = img[y0:y1, x0:x1]
    rh, rw = roi.shape[:2]
    u8 = np.clip(roi * 255.0, 0, 255).astype(np.uint8)
    u8g = u8

    oval_w = ellipse_weight(rh, rw, cx - x0, cy - y0, max(rx, 2), max(ry, 2), feather)

    # Skin coverage: the hand-placed oval does the bulk of the job, colour only
    # refines it (shadowed skin falls outside the classic YCrCb window), while
    # anything clearly darker than its surroundings (brows, lashes, beard, hair)
    # and anything very dark is excluded.
    luma_roi = 0.299 * roi[..., 0] + 0.587 * roi[..., 1] + 0.114 * roi[..., 2]
    luma_s = gblur(luma_roi, max(3.0, 12.0 * s))
    dark_rel = np.clip(((luma_s - luma_roi) - dark_thr) / 0.10, 0.0, 1.0)
    very_dark = np.clip((0.16 - luma_roi) / 0.08, 0.0, 1.0)
    # absolute darkness gate: a beard / hair mass is not skin, even deep inside it
    dark_abs = np.clip((0.26 - luma_roi) / 0.10, 0.0, 1.0)
    mx = roi.max(axis=2)
    mn = roi.min(axis=2)
    sat = (mx - mn) / np.maximum(mx, 1e-3)
    color_w = np.clip(skin_mask(roi) * 1.6, 0.0, 1.0)
    color_w = color_w * np.clip((0.55 - sat) / 0.20, 0.0, 1.0)   # keep the coat out
    skin = oval_w * (0.55 + 0.45 * color_w)
    skin = skin * (1.0 - 0.97 * np.maximum(np.maximum(dark_rel, very_dark), dark_abs))

    # texture gate: beards, hair, knitwear and fabric grain carry far more local
    # detail energy than skin — they must never be smoothed (that is what turns
    # a beard into mush and a coat into plastic)
    hf_t = np.abs(luma_roi - gblur(luma_roi, max(1.2, 3.0 * s)))
    tex = gblur(hf_t, max(2.0, 6.0 * s))
    skin = skin * np.clip((0.048 - tex) / 0.020, 0.0, 1.0)

    # eyes: no smoothing, sharpened instead.  lips: smoothed, never sharpened.
    prot_w = np.zeros((rh, rw), np.float32)
    for px, py, prx, pry in protect:
        prot_w = np.maximum(prot_w, ellipse_weight(rh, rw, px * s - x0, py * s - y0,
                                                   prx * s, pry * s, feather=0.70))
    lips_w = np.zeros((rh, rw), np.float32)
    for px, py, prx, pry in no_sharp:
        lips_w = np.maximum(lips_w, ellipse_weight(rh, rw, px * s - x0, py * s - y0,
                                                   prx * s, pry * s, feather=0.55))
    skin = skin * (1.0 - 0.92 * prot_w)
    # lips stay in the smoothing pass (soft, unlined) — but only where the pixels
    # really are lips (reddish), never on a moustache or beard around the mouth.
    lips_ell = lips_w.copy()          # whole mouth oval: smoothed, never sharpened
    if lips_w.any():
        ycc_l = cv2.cvtColor(u8g, cv2.COLOR_RGB2YCrCb).astype(np.float32)
        lip_gate = np.clip((ycc_l[..., 1] - 138.0) / 24.0, 0.0, 1.0)
        lip_gate = gblur(lip_gate, max(1.0, 2.0 * s))
        lips_w = lips_w * lip_gate
    # the mouth oval is smoothed as a whole (its dark lip-line included), so the
    # lip contour melts into the skin instead of turning into a drawn line
    skin = np.clip(skin + 0.80 * lips_ell * (1.0 - prot_w), 0.0, 1.0)

    # ---------- 1. chroma smoothing: kill redness and blotchiness ----------
    ycc = cv2.cvtColor(u8, cv2.COLOR_RGB2YCrCb).astype(np.float32)
    cr_s = gblur(ycc[..., 1], max(2.0, 7.0 * s))
    cb_s = gblur(ycc[..., 2], max(2.0, 7.0 * s))
    a_c = (0.62 * beauty * skin)[..., None]
    chroma = np.stack([ycc[..., 0],
                       ycc[..., 1] * (1 - a_c[..., 0]) + cr_s * a_c[..., 0],
                       ycc[..., 2] * (1 - a_c[..., 0]) + cb_s * a_c[..., 0]], axis=-1)
    evened = cv2.cvtColor(np.clip(chroma, 0, 255).astype(np.uint8),
                          cv2.COLOR_YCrCb2RGB).astype(np.float32) / 255.0

    # ---------- 2. local shading evening (dark circles, patchy tone) ------
    skin_t = skin * (1.0 - lips_w)          # lips keep their own tone
    luma = (0.299 * evened[..., 0] + 0.587 * evened[..., 1] + 0.114 * evened[..., 2])
    luma_s = gblur(luma, max(4.0, 16.0 * s))
    lift = (luma_s - luma)
    tone = np.clip(lift, -0.25, 0.25) * (tone_even * beauty * skin_t *
                                         (1.0 if do_tone else 0.0))
    evened = evened + tone[..., None]

    # a second, much wider pass: flattens big shadows (philtrum, under-nose,
    # jaw shading) that would otherwise read as smudges on evened-out skin
    luma_w = 0.299 * evened[..., 0] + 0.587 * evened[..., 1] + 0.114 * evened[..., 2]
    luma_w2 = gblur(luma_w, max(10.0, 40.0 * s))
    tone2 = np.clip(luma_w2 - luma_w, -0.30, 0.30) * (0.28 * beauty * skin_t *
                                                      (1.0 if do_tone else 0.0))
    evened = np.clip(evened + tone2[..., None], 0.0, 1.0)

    # ---------- 3. three-pass edge-preserving surface blur ----------------
    u8b = np.clip(evened * 255.0, 0, 255).astype(np.uint8)
    d1 = max(5, int(round(9 * s)) | 1)
    surf = cv2.bilateralFilter(u8b, d1, bil_sigma, d1)
    surf = cv2.bilateralFilter(surf, d1, bil_sigma, int(round(d1 * 1.7)))
    surf = cv2.bilateralFilter(surf, d1, bil_sigma, int(round(d1 * 2.6)))
    surf = surf.astype(np.float32) / 255.0

    # ---------- 4. put a little real texture back ------------------------
    detail = evened - gblur(evened, max(1.4, 2.4 * s))
    base = surf + detail * (texture_keep * (1.15 - 0.35 * min(beauty, 2.0)))

    # ---------- 5. multi-scale blemish / spot removal --------------------
    u8s = np.clip(base * 255.0, 0, 255).astype(np.uint8)
    scales = ((max(9, int(round(13 * s)) | 1), 0.038),
              (max(15, int(round(27 * s)) | 1), 0.044)) if do_spots else ()
    for k, thr in scales:
        med = cv2.medianBlur(u8s, k).astype(np.float32) / 255.0
        diff = np.abs(base - med).max(axis=2)
        conf = np.clip((skin - 0.55) / 0.30, 0.0, 1.0)     # confident skin only
        m = np.clip((diff - thr) / 0.045, 0.0, 1.0) * conf * min(beauty, 1.6)
        m3 = (m * 0.95)[..., None]
        base = base * (1 - m3) + med * m3
        u8s = np.clip(base * 255.0, 0, 255).astype(np.uint8)

    # ---------- 5b. lip polish: soften the crease and lip wrinkles -------
    if lips_w.any():
        u8s = np.clip(base * 255.0, 0, 255).astype(np.uint8)
        lip_soft = cv2.medianBlur(u8s, max(7, int(9 * s) | 1)).astype(np.float32) / 255.0
        lip_soft = gblur(lip_soft, max(1.0, 1.3 * s))
        lip_m = cv2.dilate(lips_w, np.ones((9, 9), np.uint8))
        lip_m = np.clip(gblur(lip_m, max(1.5, 2.5 * s)) * 0.38, 0.0, 0.38)[..., None]
        base = base * (1 - lip_m) + lip_soft * lip_m

    # ---------- 6. blend the smoothed skin back in -----------------------
    a = np.clip(skin * min(beauty, 1.0) * 0.98, 0.0, 1.0)[..., None]
    out = roi * (1 - a) + base * a

    # ---------- 6b. final soft-focus pass: even out residual speckle -----
    pore_k = np.clip(0.30 * beauty * skin, 0.0, 0.80)[..., None]
    out = out * (1 - pore_k) + gblur(out, max(1.6, 3.0 * s)) * pore_k

    # ---------- 7. skin polish: bring a little life back + brighten ------
    lum_out = (0.299 * out[..., 0] + 0.587 * out[..., 1] + 0.114 * out[..., 2])
    sat_w = np.clip(0.16 * beauty * skin_t + 0.06 * lips_w, 0.0, 0.35)[..., None]
    out = out + (out - lum_out[..., None]) * sat_w
    s3 = (skin_t * min(beauty, 1.0) * 0.98)[..., None]
    out = out + s3 * (brighten * (0.6 + 0.4 * (1.0 - luma)))[..., None]
    out = np.clip(out, 0.0, 1.0)

    # ---------- 8. sharpen eyes / lashes / brows / hair ------------------
    hf = roi - gblur(roi, max(1.0, 1.3 * s))
    sharp = roi + hf * (edge_sharpen if do_sharp else 0.0)
    w_sharp = np.clip(oval_w * (1.0 - skin) * 0.10 + prot_w * oval_w * 0.65, 0.0, 1.0)
    w_sharp = w_sharp * (1.0 - 0.9 * lips_w)
    # lips: only a whisper of contour definition (a full sharpen turns the lip
    # line into drawn-on eyeliner)
    w_sharp = np.clip(w_sharp + 0.16 * lips_w, 0.0, 1.0)
    out = out * (1 - w_sharp[..., None]) + sharp * w_sharp[..., None]

    img[y0:y1, x0:x1] = np.clip(out, 0.0, 1.0)
    return img


# ----------------------------------------------------------------------------
# 2) autumn golden preset
# ----------------------------------------------------------------------------
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
    ap.add_argument("--beauty", type=float, default=1.0,
                    help="retouch strength, 0 = off, 1 = full, 1.4 = very strong")
    ap.add_argument("--no-hands", action="store_true",
                    help="skip the optional hand / wrist light retouch")
    ap.add_argument("--face", action="append", default=None,
                    help="face oval cx,cy,rx,ry in original pixels (repeatable)")
    ap.add_argument("--eye", action="append", default=None,
                    help="protected eye oval cx,cy,rx,ry (repeatable)")
    ap.add_argument("--quality", type=int, default=96)
    ap.add_argument("--no-retouch", action="store_true")
    ap.add_argument("--no-grade", action="store_true")
    a = ap.parse_args(argv)

    faces = DEFAULT_FACES
    if a.face:
        faces = [tuple(float(v) for v in f.split(",")) for f in a.face]
    eyes = DEFAULT_EYES
    if a.eye:
        eyes = [tuple(float(v) for v in f.split(",")) for f in a.eye]
    lips = DEFAULT_LIPS

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

    if not a.no_retouch and a.beauty > 0:
        for i, oval in enumerate(faces):
            def _near(lst):
                return [e for e in lst
                        if ((e[0] - oval[0]) / max(oval[2], 1)) ** 2 +
                           ((e[1] - oval[1]) / max(oval[3], 1)) ** 2 < 3.2]
            img = retouch_skin(img, oval, s, beauty=a.beauty,
                               protect=_near(eyes), no_sharp=_near(lips))
        if not a.no_hands:
            for oval in DEFAULT_HANDS:
                img = retouch_skin(img, oval, s, beauty=a.beauty * 0.60,
                                   protect=(), feather=0.30,
                                   texture_keep=0.34, edge_sharpen=0.9,
                                   brighten=0.012)

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

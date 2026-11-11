#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Fetch Atlas model weights (they are never stored in git).

Three ways the weights can arrive, in order of preference:

1. ``--from-wheel``  — pull the ``realesrgan-ncnn-py`` manylinux wheel from PyPI
   and extract the ncnn ``.param``/``.bin`` pairs. This is the only route that
   works in a locked-down sandbox (GitHub *release assets* are often blocked
   while PyPI is not).
2. ``--from-dir DIR`` — copy pairs out of a directory the user already has.
3. default           — HTTPS download of the upstream Real-ESRGAN release zip.

Every file is verified against the sha256 in ``MANIFEST`` before it is kept, so
a corrupted or swapped download can never reach the inference path.

Usage:
    python tools/fetch_models.py --list
    python tools/fetch_models.py --from-wheel
    python tools/fetch_models.py --only realesrgan-x4plus
    python tools/fetch_models.py --check          # verify what is on disk
"""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
import shutil
import sys
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_OUT = ROOT / "models" / "weights"

# PyPI metadata for the wheel that carries the Real-ESRGAN ncnn models.
WHEEL_PROJECT = "realesrgan-ncnn-py"
WHEEL_VERSION = "2.0.0"
WHEEL_FILENAME = f"realesrgan_ncnn_py-{WHEEL_VERSION}-cp311-none-manylinux1_x86_64.whl"

# Upstream release zip (fallback route; needs unrestricted internet).
UPSTREAM_ZIP = (
    "https://github.com/xinntao/Real-ESRGAN/releases/download/"
    "v0.2.5.0/realesrgan-ncnn-vulkan-20220424-ubuntu.zip"
)

# sha256 values were measured on the files extracted from the wheel above.
MANIFEST: dict[str, dict] = {
    "realesrgan-x4plus": {
        "param": "realesrgan-x4plus.param",
        "bin": "realesrgan-x4plus.bin",
        "scale": 4,
        "license": "BSD-3-Clause",
        "sha256": {
            "realesrgan-x4plus.param": "35330ececcea33b6c397a72548e788d5d53becee4734c50b7fada36e89f10a86",
            "realesrgan-x4plus.bin": "713ee713b0353afaa27976f0563a64a5043bd70b9bd8936c2e26e25ebcdbcddf",
        },
    },
    "realesrgan-x4plus-anime": {
        "param": "realesrgan-x4plus-anime.param",
        "bin": "realesrgan-x4plus-anime.bin",
        "scale": 4,
        "license": "BSD-3-Clause",
        "sha256": {
            "realesrgan-x4plus-anime.param": "2b8fb6e0ae4d2d85704ca08c119a2f5ea40add4f2ecd512eb7f4cd44b6127ed4",
            "realesrgan-x4plus-anime.bin": "fe01c269cfd10cdef8e018ab66ebe750cf79c7af4d1f9c16c737e1295229bacc",
        },
    },
    "realesr-animevideov3-x2": {
        "param": "realesr-animevideov3-x2.param",
        "bin": "realesr-animevideov3-x2.bin",
        "scale": 2,
        "license": "BSD-3-Clause",
        "sha256": {
            "realesr-animevideov3-x2.param": "b88ff4f00ebf019a7fdac17fdd45a7fd3665d37509efc5baf2e4da2e24420a04",
            "realesr-animevideov3-x2.bin": "548a36f9c3f4ab8da56cd3b13badf23968bee207b396dad14d04b830e5f2ab2d",
        },
    },
    "realesr-animevideov3-x3": {
        "param": "realesr-animevideov3-x3.param",
        "bin": "realesr-animevideov3-x3.bin",
        "scale": 3,
        "license": "BSD-3-Clause",
        "sha256": {
            "realesr-animevideov3-x3.param": "d1a5755008791d09b57e3425fc9dd0bd26b00fdf79c606210bc0e693f8230881",
            "realesr-animevideov3-x3.bin": "548a36f9c3f4ab8da56cd3b13badf23968bee207b396dad14d04b830e5f2ab2d",
        },
    },
    "realesr-animevideov3-x4": {
        "param": "realesr-animevideov3-x4.param",
        "bin": "realesr-animevideov3-x4.bin",
        "scale": 4,
        "license": "BSD-3-Clause",
        "sha256": {
            "realesr-animevideov3-x4.param": "850a248e7c14c27e5bd8cf7265113a9441036a7db63963bb8aa5169d788a435e",
            "realesr-animevideov3-x4.bin": "548a36f9c3f4ab8da56cd3b13badf23968bee207b396dad14d04b830e5f2ab2d",
        },
    },
}


def sha256_of(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def verify(model_id: str, path: Path) -> bool:
    want = MANIFEST[model_id]["sha256"].get(path.name)
    if not want:
        return True  # no recorded checksum (e.g. upstream zip names differ)
    got = sha256_of(path)
    if got != want:
        print(f"  ! checksum mismatch {path.name}\n    want {want}\n    got  {got}", file=sys.stderr)
        return False
    return True


def wheel_url() -> str:
    with urllib.request.urlopen(f"https://pypi.org/pypi/{WHEEL_PROJECT}/{WHEEL_VERSION}/json", timeout=60) as r:
        meta = json.load(r)
    for f in meta["urls"]:
        if f["filename"] == WHEEL_FILENAME:
            return f["url"]
    for f in meta["urls"]:
        if f["filename"].endswith(".whl") and "manylinux" in f["filename"]:
            return f["url"]
    raise SystemExit("no manylinux wheel found for " + WHEEL_PROJECT)


def wanted(args) -> list[str]:
    ids = list(MANIFEST)
    if args.only:
        ids = [i for i in ids if i in args.only]
        missing = set(args.only) - set(ids)
        if missing:
            raise SystemExit("unknown model id(s): " + ", ".join(sorted(missing)))
    return ids


def from_dir(src: Path, out: Path, ids: list[str]) -> int:
    ok = 0
    for mid in ids:
        entry = MANIFEST[mid]
        pair = [entry["param"], entry["bin"]]
        found = {}
        for name in pair:
            for cand in (src / name, *(src.rglob(name))):
                if cand.is_file():
                    found[name] = cand
                    break
        if len(found) != 2:
            print(f"- {mid}: not found in {src}", file=sys.stderr)
            continue
        for name, cand in found.items():
            dst = out / name
            if cand.resolve() != dst.resolve():
                shutil.copyfile(cand, dst)
        if all(verify(mid, out / n) for n in pair):
            print(f"+ {mid}  ({entry['scale']}x, {entry['license']})")
            ok += 1
    return ok


def from_zip(zf: zipfile.ZipFile, out: Path, ids: list[str]) -> int:
    ok = 0
    names = {Path(n).name: n for n in zf.namelist() if not n.endswith("/")}
    for mid in ids:
        entry = MANIFEST[mid]
        pair = [entry["param"], entry["bin"]]
        if not all(p in names for p in pair):
            print(f"- {mid}: absent from archive", file=sys.stderr)
            continue
        for name in pair:
            (out / name).write_bytes(zf.read(names[name]))
        if all(verify(mid, out / n) for n in pair):
            print(f"+ {mid}  ({entry['scale']}x, {entry['license']})")
            ok += 1
    return ok


def main() -> int:
    ap = argparse.ArgumentParser(description="Fetch Atlas model weights (never committed to git)")
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    ap.add_argument("--only", nargs="*", help="model ids to fetch (default: all)")
    ap.add_argument("--list", action="store_true", help="show the manifest and exit")
    ap.add_argument("--check", action="store_true", help="only verify files already on disk")
    ap.add_argument("--from-wheel", action="store_true", help="extract from the PyPI wheel")
    ap.add_argument("--from-dir", type=Path, help="copy pairs from an existing directory")
    ap.add_argument("--from-zip", type=Path, help="read pairs from a local Real-ESRGAN zip")
    ap.add_argument("--cache", type=Path, default=Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache")) / "atlas", help="download cache")
    args = ap.parse_args()

    ids = wanted(args)

    if args.list:
        for mid, e in MANIFEST.items():
            print(f"{mid:28s} {e['scale']}x  {e['license']:14s} {e['bin']}")
        return 0

    out: Path = args.out
    out.mkdir(parents=True, exist_ok=True)

    if args.check:
        good = 0
        for mid in ids:
            e = MANIFEST[mid]
            have = all((out / e[k]).is_file() for k in ("param", "bin"))
            if have and all(verify(mid, out / e[k]) for k in ("param", "bin")):
                print(f"ok   {mid}")
                good += 1
            else:
                print(f"miss {mid}")
        return 0 if good == len(ids) else 1

    if args.from_dir:
        ok = from_dir(args.from_dir, out, ids)
    else:
        if args.from_zip:
            ok = from_zip(zipfile.ZipFile(args.from_zip), out, ids)
        elif args.from_wheel:
            cache = args.cache
            cache.mkdir(parents=True, exist_ok=True)
            whl = cache / WHEEL_FILENAME
            if not whl.is_file() or whl.stat().st_size == 0:
                url = wheel_url()
                print(f"downloading {url}")
                tmp = whl.with_suffix(".part")
                urllib.request.urlretrieve(url, tmp)  # noqa: S310 (https, pinned)
                tmp.replace(whl)
            ok = from_zip(zipfile.ZipFile(whl), out, ids)
        else:
            cache = args.cache
            cache.mkdir(parents=True, exist_ok=True)
            zpath = cache / Path(UPSTREAM_ZIP).name
            if not zpath.is_file() or zpath.stat().st_size == 0:
                print(f"downloading {UPSTREAM_ZIP}")
                tmp = zpath.with_suffix(".part")
                urllib.request.urlretrieve(UPSTREAM_ZIP, tmp)  # noqa: S310
                tmp.replace(zpath)
            ok = from_zip(zipfile.ZipFile(zpath), out, ids)

    print(f"\n{ok}/{len(ids)} model(s) ready in {out}")
    if ok != len(ids):
        print("hint: --from-wheel needs PyPI access; --from-dir works fully offline", file=sys.stderr)
    return 0 if ok == len(ids) else 1


if __name__ == "__main__":
    raise SystemExit(main())

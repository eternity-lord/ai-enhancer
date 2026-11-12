#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""سرور محلی برنامهٔ Atlas — بدون هیچ وابستگی بیرونی (فقط stdlib).

این فایل چهار کار می‌کند:
  ۱) فایل‌های استاتیک رابط کاربری (`desktop/web`) را سرو می‌کند،
  ۲) آپلود تصویر را در یک پوشهٔ موقت ذخیره می‌کند،
  ۳) پردازش واقعی را با `atlas-cli` (هستهٔ C++) اجرا می‌کند: پاک‌سازی →
     نور/رنگ (Light AI) → بزرگ‌نمایی،
  ۴) صف دسته‌ای را با پیشرفت/لغو و ذخیرهٔ خودکار خروجی در پوشهٔ کاربر اداره می‌کند.

هیچ تصویری از دستگاه خارج نمی‌شود و هیچ CDN/فونتی از اینترنت نمی‌آید: همه‌چیز محلی است.

اجرا:
    python3 desktop/server.py --port 8000
"""

from __future__ import annotations

import argparse
import http.server
import json
import mimetypes
import os
import queue
import re
import shutil
import socketserver
import subprocess
import sys
import threading
import time
import uuid
import webbrowser
from pathlib import Path
from urllib.parse import parse_qs, urlparse

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parent


def bundle_root() -> Path:
    """ریشهٔ فایل‌های همراه: در توسعه پوشهٔ desktop، در exe پوشهٔ استخراج PyInstaller."""
    return Path(getattr(sys, "_MEIPASS", ROOT))


def data_dir() -> Path:
    """دادهٔ کار: کنار مخزن در توسعه، در پوشهٔ کاربر وقتی برنامه بسته‌بندی شده است."""
    env = os.environ.get("ATLAS_UI_DATA")
    if env:
        return Path(env)
    if getattr(sys, "frozen", False):
        base = os.environ.get("LOCALAPPDATA") or os.environ.get("APPDATA") or str(Path.home())
        return Path(base) / "Atlas" / "data"
    return ROOT / ".data"


WEB = bundle_root() / "web"
DATA = data_dir()

MAX_UPLOAD_BYTES = 80 * 1024 * 1024
CLI_TIMEOUT = 1800  # ثانیه؛ ۴× روی CPU برای عکس بزرگ می‌تواند دقیقه‌ها طول بکشد
MAX_BATCH = 200

IMAGE_EXTS = {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".webp"}
OUTPUT_FORMATS = ("png", "jpg", "webp")


# ---------------------------------------------------------------------------
# atlas-cli
# ---------------------------------------------------------------------------


def cli_candidates(exe: str | None = None) -> list[Path]:
    """همهٔ جاهایی که atlas-cli ممکن است باشد — روی ویندوز/مک/لینوکس.

    روی ویندوز نام فایل `.exe` است و با ژنراتور Visual Studio خروجی داخل
    پوشهٔ `Release/` می‌رود؛ هر دو حالت پوشش داده می‌شود تا کاربر ویندوزی
    مجبور نباشد چیزی را دستی جابه‌جا کند.
    """
    if exe is None:
        exe = ".exe" if os.name == "nt" else ""
    out: list[Path] = []
    env = os.environ.get("ATLAS_CLI")
    if env:
        out.append(Path(env))
    for root in ("build", "build-ncnn", "build-nowebp", "build-release"):
        for sub in ("", "Release", "Debug"):
            out.append(REPO / root / sub / f"atlas-cli{exe}")
    # وقتی برنامه بسته‌بندی شده است: کنار Atlas.exe، و داخل بستهٔ PyInstaller.
    out.append(Path(sys.executable).resolve().parent / f"atlas-cli{exe}")
    meipass = getattr(sys, "_MEIPASS", None)
    if meipass:
        out.append(Path(meipass) / f"atlas-cli{exe}")
    found = shutil.which("atlas-cli")
    if found:
        out.append(Path(found))
    return out


def find_cli() -> Path | None:
    """مسیر atlas-cli؛ اول متغیر محیطی، بعد buildهای متعارف، آخر PATH."""
    for candidate in cli_candidates():
        if candidate.is_file() and (os.name == "nt" or os.access(candidate, os.X_OK)):
            return candidate
    return None


CLI = find_cli()
LOCK = threading.Lock()  # atlas-cli هم‌زمان روی یک فایل خروجی ننویسد


def require_cli() -> Path:
    if CLI is None:
        raise RuntimeError(
            "atlas-cli پیدا نشد؛ اول پروژه را بسازید "
            "(ویندوز: tools\\windows\\build.bat — لینوکس/مک: cmake --build build)."
        )
    return CLI


def run_cli(args: list[str], timeout: int = CLI_TIMEOUT) -> subprocess.CompletedProcess:
    """فراخوانی کوتاه (version/devices/convert). قفل نمی‌گیرد تا UI هنگام صف قفل نشود؛
    هر فراخوانی روی فایل خودش کار می‌کند و مسیرهای خروجی یکتا هستند."""
    cli = require_cli()
    return subprocess.run(
        [str(cli), *args],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",  # کنسول ویندوز ممکن است cp1256 باشد
        timeout=timeout,
        check=False,
    )


TILES_RE = re.compile(r"tiles\s*:\s*(\d+)/(\d+)\s*\((\d+)%\)")


def run_cli_progress(
    args: list[str],
    on_progress=None,
    should_cancel=None,
    timeout: int = CLI_TIMEOUT,
) -> tuple[int, str, str]:
    """اجرای atlas-cli با خواندن زندهٔ پیشرفت کاشی‌ها و امکان لغو واقعی.

    لغو با کشتن فرآیند انجام می‌شود، نه با «فراموش‌کردن» نتیجه: اگر کاربر
    لغو کند، فایل نیمه‌کاره هم باقی نمی‌ماند (فراخوان مسئول پاک‌سازی است).
    """
    cli = require_cli()
    started = time.perf_counter()
    with LOCK:
        process = subprocess.Popen(
            [str(cli), *args],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            bufsize=1,
        )

        stopped = threading.Event()

        def watch() -> None:
            while process.poll() is None:
                if should_cancel is not None and should_cancel():
                    stopped.set()
                    process.kill()
                    return
                if time.perf_counter() - started > timeout:
                    stopped.set()
                    process.kill()
                    return
                time.sleep(0.12)

        watcher = threading.Thread(target=watch, daemon=True)
        watcher.start()

        text = ""
        reported = -1
        assert process.stdout is not None
        while True:
            chunk = process.stdout.read(64)
            if not chunk:
                break
            text += chunk
            if on_progress is not None:
                for match in TILES_RE.finditer(text):
                    pct = int(match.group(3))
                    if pct != reported:
                        reported = pct
                        on_progress(pct)
        error_text = process.stderr.read() if process.stderr else ""
        returncode = process.wait()
        watcher.join(timeout=1.0)
        if stopped.is_set() and returncode != 0:
            returncode = -9
        return returncode, text, error_text or ""


def last_json(text: str) -> dict | None:
    for line in reversed(text.strip().splitlines()):
        line = line.strip()
        if line.startswith("{") and line.endswith("}"):
            try:
                return json.loads(line)
            except json.JSONDecodeError:
                continue
    return None


def png_size(path: Path) -> dict:
    """ابعاد از هدر IHDR؛ برای PNGهایی که خود هسته نوشته است همیشه درست است."""
    try:
        with path.open("rb") as fh:
            header = fh.read(24)
    except OSError:
        return {}
    if len(header) < 24 or header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
        return {}
    width = int.from_bytes(header[16:20], "big")
    height = int.from_bytes(header[20:24], "big")
    return {"width": width, "height": height}


# ---------------------------------------------------------------------------
# تنظیمات (ماندگار بین اجراها)
# ---------------------------------------------------------------------------

DEFAULT_SETTINGS: dict = {
    "scale": 2,
    "quality": 92,
    "format": "png",
    "denoise": "auto",
    "deblock": "auto",
    "light_mode": "auto",  # auto | manual | off
    "light": {
        "exposure": 0.0,
        "contrast": 0.0,
        "saturation": 0.0,
        "shadows": 0.0,
        "highlights": 0.0,
        "warmth": 0.0,
    },
    "auto_light": True,
    "auto_wb": True,
    "tile": 256,
    "threads": 0,
    "keep_metadata": False,
    "auto_orient": True,
    "preset": "photo",
    "pattern": "{name}-{scale}",
    "restore_mode": "auto",
    "denoise_on": True,
    "deblock_on": True,
    "denoise_pct": 35,
    "deblock_pct": 55,
    "output_dir": "",
    "export": True,
}


class Settings:
    def __init__(self) -> None:
        self.path = DATA / "settings.json"
        self.data = json.loads(json.dumps(DEFAULT_SETTINGS))
        if self.path.is_file():
            try:
                stored = json.loads(self.path.read_text("utf-8"))
            except (OSError, json.JSONDecodeError):
                stored = {}
            if isinstance(stored, dict):
                self.data.update({k: v for k, v in stored.items() if k in DEFAULT_SETTINGS})
                if isinstance(stored.get("light"), dict):
                    self.data["light"].update(
                        {k: v for k, v in stored["light"].items() if k in DEFAULT_SETTINGS["light"]}
                    )

    def save(self) -> None:
        DATA.mkdir(parents=True, exist_ok=True)
        self.path.write_text(json.dumps(self.data, ensure_ascii=False, indent=2), "utf-8")

    def update(self, patch: dict) -> dict:
        for key, value in patch.items():
            if key not in DEFAULT_SETTINGS:
                continue
            if key == "light" and isinstance(value, dict):
                self.data["light"].update(value)
            else:
                self.data[key] = value
        self.save()
        return self.data

    def output_dir(self) -> Path:
        raw = str(self.data.get("output_dir") or "").strip()
        if raw:
            return Path(os.path.expanduser(os.path.expandvars(raw)))
        return DATA / "output"


SETTINGS = Settings()


def light_requested(options: dict) -> bool:
    light = options.get("light") or {}
    values = [abs(float(light.get(key) or 0.0)) for key in DEFAULT_SETTINGS["light"]]
    return bool(options.get("auto_light") or options.get("auto_wb") or any(v > 0 for v in values))


def light_args(options: dict) -> list[str]:
    light = options.get("light") or {}
    args: list[str] = []
    for key in ("exposure", "contrast", "saturation", "shadows", "highlights", "warmth"):
        value = float(light.get(key) or 0.0)
        if value:
            args += [f"--{key}", f"{value:.3f}"]
    if options.get("auto_light"):
        args.append("--auto-light")
    if options.get("auto_wb"):
        args.append("--auto-wb")
    return args


# ---------------------------------------------------------------------------
# پریست‌ها — «یک کلیک، تنظیمات کامل». اعداد همان‌هایی هستند که در docs/23
# با تصویر واقعی سنجیده شده‌اند؛ پریست جادو نیست، فقط میان‌بر است.
# ---------------------------------------------------------------------------

PRESETS: list[dict] = [
    {
        "id": "photo",
        "name": "عکس واقعی ۲×",
        "note": "پیش‌فرض: پاک‌سازی خودکار + نور خودکار، بزرگ‌نمایی ۲×",
        "options": {
            "scale": 2,
            "denoise": "auto",
            "deblock": "auto",
            "auto_light": True,
            "auto_wb": True,
            "format": "png",
            "quality": 95,
        },
    },
    {
        "id": "print",
        "name": "چاپ ۴×",
        "note": "۴× با کیفیت ۹۸ برای چاپ؛ خروجی PNG بدون افت",
        "options": {
            "scale": 4,
            "denoise": "auto",
            "deblock": "auto",
            "auto_light": True,
            "auto_wb": True,
            "format": "png",
            "quality": 98,
        },
    },
    {
        "id": "clean",
        "name": "فقط پاک‌سازی",
        "note": "بدون تغییر اندازه، بدون تغییر نور؛ نویز و بلوک JPEG می‌رود",
        "options": {
            "scale": 1,
            "denoise": "auto",
            "deblock": "auto",
            "auto_light": False,
            "auto_wb": False,
            "format": "png",
            "quality": 95,
        },
    },
    {
        "id": "old",
        "name": "عکس قدیمی",
        "note": "پاک‌سازی شدیدتر + بالابردن سایه‌ها و اشباع، بزرگ‌نمایی ۲×",
        "options": {
            "scale": 2,
            "denoise": "55",
            "deblock": "65",
            "auto_light": True,
            "auto_wb": True,
            "light": {"shadows": 0.3, "saturation": 0.2, "contrast": 0.1},
            "format": "png",
            "quality": 95,
        },
    },
    {
        "id": "jpeg",
        "name": "سبک برای اشتراک",
        "note": "۲× با خروجی JPEG کیفیت ۸۸؛ چند برابر کوچک‌تر",
        "options": {
            "scale": 2,
            "denoise": "auto",
            "deblock": "auto",
            "auto_light": True,
            "auto_wb": False,
            "format": "jpg",
            "quality": 88,
        },
    },
]


def preset_payload() -> list[dict]:
    return [{"id": p["id"], "name": p["name"], "note": p["note"], "options": p["options"]} for p in PRESETS]


# ---------------------------------------------------------------------------
# فرادادهٔ تصویرها
# ---------------------------------------------------------------------------


class State:
    """فرادادهٔ تصویرهای آپلودشده؛ روی دیسک نگه داشته می‌شود تا ری‌استارت سرور هم بماند."""

    def __init__(self) -> None:
        self.items: dict[str, dict] = {}
        self.path = DATA / "state.json"
        if self.path.is_file():
            try:
                self.items = json.loads(self.path.read_text("utf-8"))
            except (OSError, json.JSONDecodeError):
                self.items = {}

    def save(self) -> None:
        DATA.mkdir(parents=True, exist_ok=True)
        tmp = self.path.with_suffix(".tmp")
        tmp.write_text(json.dumps(self.items, ensure_ascii=False, indent=2), "utf-8")
        tmp.replace(self.path)

    def add(self, item: dict) -> dict:
        self.items[item["id"]] = item
        self.save()
        return item

    def get(self, item_id: str) -> dict | None:
        return self.items.get(item_id)

    def remove(self, item_id: str) -> dict | None:
        item = self.items.pop(item_id, None)
        if item is not None:
            self.save()
        return item


STATE = State()


def item_dir(item_id: str) -> Path:
    return DATA / "items" / item_id


# ---------------------------------------------------------------------------
# گزینه‌ها: نرمال‌سازی + اعتبارسنجی
# ---------------------------------------------------------------------------


def parse_restore_value(value, name: str) -> str:
    text = str(value).strip().lower()
    if text in ("auto", "off", "none"):
        return "off" if text == "none" else text
    try:
        number = int(text)
    except ValueError as exc:
        raise ValueError(f"{name} باید auto | off | ۰..۱۰۰ باشد") from exc
    if not 0 <= number <= 100:
        raise ValueError(f"{name} باید بین ۰ و ۱۰۰ باشد")
    return str(number)


def normalise_options(data: dict | None, base: dict | None = None) -> dict:
    """گزینه‌ها را از UI/پریست می‌گیرد و یک دیکشنری کامل و معتبر می‌سازد."""
    source = dict(base or SETTINGS.data)
    incoming = data or {}
    if not isinstance(incoming, dict):
        raise ValueError("options باید شیء باشد")

    merged = dict(source)
    for key, value in incoming.items():
        merged[key] = value
    light = dict(source.get("light") or {})
    for key, value in (incoming.get("light") or {}).items():
        light[key] = value
    merged["light"] = light

    try:
        scale = int(merged.get("scale", 2))
    except (TypeError, ValueError) as exc:
        raise ValueError("scale باید عدد باشد") from exc
    if scale not in (1, 2, 3, 4):
        raise ValueError("scale باید ۱ تا ۴ باشد")

    try:
        quality = int(merged.get("quality", 92))
    except (TypeError, ValueError) as exc:
        raise ValueError("quality باید عدد باشد") from exc
    if not 1 <= quality <= 100:
        raise ValueError("quality باید ۱ تا ۱۰۰ باشد")

    fmt = str(merged.get("format", "png")).lower().lstrip(".")
    if fmt == "jpeg":
        fmt = "jpg"
    if fmt not in OUTPUT_FORMATS:
        raise ValueError("format باید png | jpg | webp باشد")
    if fmt == "webp" and not CAPABILITIES.get("webp", False):
        raise ValueError("این نسخهٔ atlas-cli بدون WebP ساخته شده است؛ PNG یا JPEG انتخاب کنید")

    try:
        tile = int(merged.get("tile", 256))
    except (TypeError, ValueError) as exc:
        raise ValueError("tile باید عدد باشد") from exc
    if not 32 <= tile <= 1024:
        raise ValueError("tile باید بین ۳۲ و ۱۰۲۴ باشد")

    try:
        threads = int(merged.get("threads", 0))
    except (TypeError, ValueError) as exc:
        raise ValueError("threads باید عدد باشد") from exc
    if not 0 <= threads <= 128:
        raise ValueError("threads باید بین ۰ و ۱۲۸ باشد")

    light_values: dict[str, float] = {}
    for key, limit in (("exposure", 2.0), ("contrast", 1.0), ("saturation", 1.0),
                       ("shadows", 1.0), ("highlights", 1.0), ("warmth", 1.0)):
        try:
            value = float(light.get(key) or 0.0)
        except (TypeError, ValueError) as exc:
            raise ValueError(f"light.{key} باید عدد باشد") from exc
        light_values[key] = max(-limit, min(limit, value))

    options = {
        "scale": scale,
        "quality": quality,
        "format": fmt,
        "denoise": parse_restore_value(merged.get("denoise", "auto"), "denoise"),
        "deblock": parse_restore_value(merged.get("deblock", "auto"), "deblock"),
        "light": light_values,
        "auto_light": bool(merged.get("auto_light", True)),
        "auto_wb": bool(merged.get("auto_wb", True)),
        "light_mode": str(merged.get("light_mode", "auto")),
        "tile": tile,
        "threads": threads,
        "keep_metadata": bool(merged.get("keep_metadata", False)),
        "auto_orient": bool(merged.get("auto_orient", True)),
        "preset": str(merged.get("preset", "")),
        "pattern": str(merged.get("pattern") or "{name}-{scale}"),
        "output_dir": str(merged.get("output_dir") or ""),
        "export": bool(merged.get("export", True)),
    }
    # حالت نور فقط وقتی اعمال می‌شود که صریح آمده باشد؛ وگرنه همان بولین‌های
    # auto_light/auto_wb تصمیم می‌گیرند (API برای اسکریپت‌نویس باید قابل پیش‌بینی باشد).
    mode = str(incoming.get("light_mode") or "").strip()
    if mode == "off":
        options["auto_light"] = False
        options["auto_wb"] = False
        options["light"] = {key: 0.0 for key in light_values}
        options["light_mode"] = "off"
    elif mode == "auto":
        options["auto_light"] = True
        options["auto_wb"] = True
        options["light_mode"] = "auto"
    elif mode == "manual":
        options["auto_light"] = False
        options["auto_wb"] = False
        options["light_mode"] = "manual"
    else:
        options["light_mode"] = "off" if not light_requested(options) else (
            "auto" if (options["auto_light"] or options["auto_wb"]) else "manual")
    return options


# ---------------------------------------------------------------------------
# قابلیت‌های build (یک‌بار خوانده و کش می‌شود)
# ---------------------------------------------------------------------------

CAPABILITIES: dict = {"version": None, "ncnn": False, "webp": False, "threads": os.cpu_count() or 1,
                      "formats": ["png", "jpg"]}
_caps_ready = threading.Event()


def capabilities(refresh: bool = False) -> dict:
    global CAPABILITIES
    if _caps_ready.is_set() and not refresh:
        return CAPABILITIES
    caps = {"version": None, "ncnn": False, "webp": False,
            "threads": os.cpu_count() or 1, "formats": ["png", "jpg"]}
    if CLI is not None:
        try:
            out = run_cli(["version", "--json"], timeout=30)
            payload = last_json(out.stdout) or {}
            caps["version"] = payload.get("version")
            caps["ncnn"] = bool(payload.get("ncnn"))
            caps["webp"] = bool(payload.get("webp"))
            caps["threads"] = int(payload.get("threads") or caps["threads"])
        except Exception:  # noqa: BLE001 — نسخهٔ قدیمی CLI ممکن است --json نداشته باشد
            try:
                out = run_cli(["version"], timeout=30)
                caps["version"] = (out.stdout or "").strip() or None
            except Exception:  # noqa: BLE001
                pass
    if caps["webp"]:
        caps["formats"] = ["png", "jpg", "webp"]
    CAPABILITIES = caps
    _caps_ready.set()
    return CAPABILITIES


# ---------------------------------------------------------------------------
# نام‌گذاری و ذخیرهٔ خروجی
# ---------------------------------------------------------------------------


def safe_stem(name: str) -> str:
    stem = Path(name).stem or "atlas"
    stem = re.sub(r'[\\/:*?"<>|\x00-\x1f]+', "_", stem).strip(" .")
    return stem[:80] or "atlas"


def render_pattern(pattern: str, item: dict, scale: int, ext: str, preset: str = "") -> str:
    tokens = {
        "name": safe_stem(item.get("name", "atlas")),
        "scale": "clean" if scale <= 1 else f"{scale}x",
        "preset": preset or "custom",
        "date": time.strftime("%Y%m%d"),
        "ext": ext,
    }
    text = pattern or "{name}-{scale}"
    for key, value in tokens.items():
        text = text.replace("{" + key + "}", value)
    text = re.sub(r'[\\/:*?"<>|\x00-\x1f]+', "_", text).strip(" .")
    return (text or tokens["name"]) + "." + ext


def unique_path(directory: Path, filename: str) -> Path:
    """هرگز فایل موجود را بازنویسی نمی‌کند: نام تازه با شماره ساخته می‌شود."""
    directory.mkdir(parents=True, exist_ok=True)
    candidate = directory / filename
    if not candidate.exists():
        return candidate
    stem, suffix = candidate.stem, candidate.suffix
    for index in range(1, 1000):
        candidate = directory / f"{stem}-{index}{suffix}"
        if not candidate.exists():
            return candidate
    return directory / f"{stem}-{uuid.uuid4().hex[:6]}{suffix}"


def export_item(item: dict, options: dict) -> dict:
    """نتیجهٔ PNG را با فرمت/نام/پوشهٔ خواسته‌شده روی دیسک می‌گذارد."""
    d = item_dir(item["id"])
    final = d / "result.png"
    if not final.is_file():
        raise ValueError("اول پردازش کنید؛ خروجی آماده‌ای وجود ندارد")

    fmt = options["format"]
    ext = "jpg" if fmt == "jpg" else fmt
    directory = Path(os.path.expanduser(os.path.expandvars(options.get("output_dir") or ""))) \
        if str(options.get("output_dir") or "").strip() else SETTINGS.output_dir()
    directory = directory.resolve()
    target = unique_path(directory, render_pattern(options.get("pattern") or "{name}-{scale}",
                                                   item, options["scale"], ext,
                                                   options.get("preset") or ""))
    if ext == "png":
        shutil.copyfile(final, target)
    else:
        args = ["convert", str(final), str(target), "--quality", str(options["quality"]), "--quiet"]
        out = run_cli(args, timeout=300)
        if out.returncode != 0 or not target.is_file():
            raise ValueError(f"تبدیل به {ext} شکست خورد: {out.stderr.strip()}")
    shutil.copyfile(final, d / "result_preview.png")
    return {
        "path": str(target),
        "dir": str(target.parent),
        "filename": target.name,
        "format": ext,
        "bytes": target.stat().st_size,
    }


# ---------------------------------------------------------------------------
# پایپ‌لاین پردازش یک تصویر
# ---------------------------------------------------------------------------


def process_item(item_id: str, options: dict, on_progress=None, should_cancel=None) -> dict:
    """پاک‌سازی → نور/رنگ → بزرگ‌نمایی → ذخیره. همهٔ مراحل با atlas-cli واقعی."""
    item = STATE.get(item_id)
    if item is None:
        raise ValueError("آیتم پیدا نشد")
    d = item_dir(item_id)
    source = d / str(item.get("source"))
    if not source.is_file():
        raise ValueError("فایل مبدأ روی دیسک نیست")

    restored = d / "restored.png"
    lit = d / "lit.png"
    final = d / "result.png"
    for stale in (restored, lit, final, d / "result_preview.png"):
        stale.unlink(missing_ok=True)

    def emit(percent: int, message: str) -> None:
        if on_progress is not None:
            on_progress(max(0, min(100, percent)), message)

    def cancelled() -> bool:
        return bool(should_cancel and should_cancel())

    started = time.perf_counter()
    steps: list[dict] = []

    # ---- ۱) پاک‌سازی ----
    emit(3, "پاک‌سازی نویز و بلوک JPEG")
    code, stdout, stderr = run_cli_progress(
        ["restore", str(source), str(restored), "--denoise", options["denoise"],
         "--deblock", options["deblock"], "--json"],
        should_cancel=should_cancel, timeout=CLI_TIMEOUT,
    )
    if code == -9 and cancelled():
        raise JobCancelled()
    if code != 0:
        raise ValueError(f"پاک‌سازی شکست خورد: {stderr.strip() or stdout.strip()}")
    metrics = last_json(stdout) or {}
    steps.append({
        "name": "restore",
        "ms": metrics.get("ms", 0.0),
        "deblock": metrics.get("deblock"),
        "denoise": metrics.get("denoise"),
        "denoise_strength": metrics.get("denoise_strength"),
        "deblock_strength": metrics.get("deblock_strength"),
    })
    if cancelled():
        raise JobCancelled()
    current = restored

    # ---- ۲) نور و رنگ (Light AI) ----
    light_stats = None
    if light_requested(options):
        emit(20, "اصلاح نور و رنگ")
        args = ["adjust", str(current), str(lit), *light_args(options), "--json"]
        code, stdout, stderr = run_cli_progress(args, should_cancel=should_cancel,
                                                timeout=CLI_TIMEOUT)
        if code == -9 and cancelled():
            raise JobCancelled()
        if code != 0:
            raise ValueError(f"اصلاح نور شکست خورد: {stderr.strip() or stdout.strip()}")
        light_stats = last_json(stdout) or {}
        steps.append({"name": "light", "ms": light_stats.get("ms", 0.0),
                      "auto_ev": light_stats.get("auto_ev"), "auto_contrast": light_stats.get("auto_contrast")})
        current = lit
        if cancelled():
            raise JobCancelled()

    # ---- ۳) بزرگ‌نمایی ----
    scale = options["scale"]
    if scale > 1:
        emit(30, "بزرگ‌نمایی")
        args = ["enhance", str(current), str(final), "--scale", str(scale),
                "--denoise", "off", "--deblock", "off", "--tile", str(options["tile"]),
                "--quality", "100", "--json"]
        if options["threads"]:
            args += ["--threads", str(options["threads"])]
        if not options["auto_orient"]:
            args.append("--no-auto-orient")
        if options["keep_metadata"]:
            args.append("--keep-metadata")

        code, stdout, stderr = run_cli_progress(
            args,
            on_progress=(lambda pct: emit(30 + int(pct * 0.65), "بزرگ‌نمایی")) if on_progress else None,
            should_cancel=should_cancel,
            timeout=CLI_TIMEOUT,
        )
        if code == -9 and cancelled():
            raise JobCancelled()
        if code != 0:
            raise ValueError(f"بزرگ‌نمایی شکست خورد: {stderr.strip() or stdout.strip()}")
        info = last_json(stdout) or {}
        steps.append({"name": "enhance", "scale": scale, "seconds": info.get("seconds")})
        if cancelled():
            raise JobCancelled()
    else:
        shutil.copyfile(current, final)

    if cancelled():
        raise JobCancelled()
    if not final.is_file():
        raise ValueError("خروجی ساخته نشد")

    # پیش‌نمای نتیجه از همان هسته (orientation/ICC یکسان با بقیهٔ مسیر)
    preview = d / "result_preview.png"
    out3 = run_cli(["convert", str(final), str(preview), "--quiet"], timeout=300)
    if out3.returncode != 0 or not preview.is_file():
        shutil.copyfile(final, preview)

    info = png_size(final)
    emit(96, "ذخیرهٔ خروجی")
    exported = None
    if options.get("export", True):
        try:
            exported = export_item(item, options)
        except (ValueError, OSError, subprocess.SubprocessError) as exc:
            exported = None
            steps.append({"name": "export", "error": str(exc)})

    item["result"] = {
        "width": info.get("width"),
        "height": info.get("height"),
        "scale": scale,
        "quality": options["quality"],
        "format": (exported or {}).get("format", "png"),
        "source_bytes": source.stat().st_size,
        "result_bytes": (exported or {}).get("bytes", final.stat().st_size),
        "path": (exported or {}).get("path"),
        "filename": (exported or {}).get("filename"),
        "dir": (exported or {}).get("dir"),
    }
    item["metrics"] = metrics
    item["light"] = light_stats
    item["seconds"] = round(time.perf_counter() - started, 2)
    item["steps"] = steps
    item["options"] = {key: options[key] for key in
                       ("scale", "quality", "format", "denoise", "deblock", "preset")}
    STATE.save()
    emit(100, "تمام")
    return item


class JobCancelled(Exception):
    """لغو توسط کاربر (نه خطا)."""


# ---------------------------------------------------------------------------
# صف کار
# ---------------------------------------------------------------------------

JOBS: dict[str, dict] = {}
JOB_ORDER: list[str] = []
JOB_LOCK = threading.Lock()
JOB_QUEUE: "queue.Queue[str]" = queue.Queue()


def job_public(job: dict) -> dict:
    return {
        "id": job["id"],
        "status": job["status"],
        "progress": job["progress"],
        "current": job.get("current"),
        "message": job.get("message"),
        "options": job.get("options"),
        "items": job["items"],
        "log": job["log"][-40:],
        "created": job.get("created"),
        "started": job.get("started"),
        "finished": job.get("finished"),
        "summary": job.get("summary"),
    }


def job_log(job: dict, message: str) -> None:
    job["log"].append({"at": time.time(), "message": message})


def enqueue_job(ids: list[str], options: dict) -> dict:
    if not ids:
        raise ValueError("هیچ تصویری انتخاب نشده")
    if len(ids) > MAX_BATCH:
        raise ValueError(f"حداکثر {MAX_BATCH} فایل در هر صف")
    items = []
    for item_id in ids:
        item = STATE.get(item_id)
        if item is None:
            raise ValueError(f"آیتم {item_id} پیدا نشد")
        items.append({"id": item_id, "name": item.get("name", item_id), "status": "queued",
                      "progress": 0, "error": None})
    job = {
        "id": uuid.uuid4().hex[:10],
        "status": "queued",
        "progress": 0,
        "current": None,
        "message": "در صف",
        "options": options,
        "items": items,
        "log": [],
        "cancel": threading.Event(),
        "created": time.time(),
    }
    with JOB_LOCK:
        JOBS[job["id"]] = job
        JOB_ORDER.append(job["id"])
        # فقط ۳۰ صف آخر نگه داشته می‌شود تا حافظه بی‌نهایت رشد نکند.
        while len(JOB_ORDER) > 30:
            JOBS.pop(JOB_ORDER.pop(0), None)
    job_log(job, f"صف ساخته شد: {len(items)} تصویر")
    JOB_QUEUE.put(job["id"])
    ensure_worker()
    return job


_worker_lock = threading.Lock()
_worker_started = False


def ensure_worker() -> None:
    global _worker_started
    with _worker_lock:
        if _worker_started:
            return
        thread = threading.Thread(target=job_worker, name="atlas-jobs", daemon=True)
        thread.start()
        _worker_started = True


def job_worker() -> None:
    while True:
        job_id = JOB_QUEUE.get()
        job = JOBS.get(job_id)
        if job is None:
            continue
        if job["cancel"].is_set():
            job["status"] = "cancelled"
            job["finished"] = time.time()
            continue
        job["status"] = "running"
        job["started"] = time.time()
        job["message"] = "در حال پردازش"
        total = len(job["items"])
        done = 0
        errors = 0
        for index, entry in enumerate(job["items"]):
            if job["cancel"].is_set():
                break
            entry["status"] = "running"
            job["current"] = entry["name"]
            job_log(job, f"شروع {entry['name']}")

            def progress(percent: int, message: str, index=index, entry=entry) -> None:
                entry["progress"] = percent
                entry["message"] = message
                job["message"] = message
                job["current"] = entry["name"]
                base = index / total * 100.0
                job["progress"] = round(base + percent / total, 1)

            try:
                item = process_item(entry["id"], job["options"], on_progress=progress,
                                    should_cancel=job["cancel"].is_set)
                entry["status"] = "done"
                entry["progress"] = 100
                entry["result"] = {
                    "width": item["result"].get("width"),
                    "height": item["result"].get("height"),
                    "path": item["result"].get("path"),
                    "filename": item["result"].get("filename"),
                    "seconds": item.get("seconds"),
                }
                done += 1
                job_log(job, f"تمام {entry['name']} → {entry['result'].get('filename') or '—'}")
            except JobCancelled:
                entry["status"] = "cancelled"
                job["status"] = "cancelled"
                job_log(job, f"لغو شد در {entry['name']}")
                break
            except Exception as exc:  # noqa: BLE001 — یک فایل خراب نباید کل صف را بکشد
                entry["status"] = "error"
                entry["error"] = str(exc)
                errors += 1
                job_log(job, f"خطا در {entry['name']}: {exc}")
        if job["cancel"].is_set() and job["status"] != "cancelled":
            job["status"] = "cancelled"
            for entry in job["items"]:
                if entry["status"] in ("queued", "running"):
                    entry["status"] = "cancelled"
        if job["status"] != "cancelled":
            job["status"] = "done" if errors == 0 else "error"
        job["progress"] = 100 if job["status"] == "done" else job["progress"]
        job["finished"] = time.time()
        job["message"] = {
            "done": f"{done} تصویر آماده شد",
            "error": f"{done} آماده، {errors} خطا",
            "cancelled": "لغو شد",
        }.get(job["status"], job["status"])
        job["current"] = None
        job["summary"] = {"done": done, "errors": errors, "total": total,
                          "seconds": round(job["finished"] - (job["started"] or job["finished"]), 1)}


def find_job(job_id: str) -> dict | None:
    with JOB_LOCK:
        return JOBS.get(job_id)


def cancel_job(job_id: str) -> dict | None:
    job = find_job(job_id)
    if job is None:
        return None
    job["cancel"].set()
    job["message"] = "در حال لغو…"
    return job


# ---------------------------------------------------------------------------
# HTTP
# ---------------------------------------------------------------------------


class Handler(http.server.SimpleHTTPRequestHandler):
    server_version = "AtlasDesktop/0.2"

    # ---------- کمکی‌ها ----------
    def send_json(self, payload: dict, status: int = 200) -> None:
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def send_error_json(self, message: str, status: int = 400) -> None:
        self.send_json({"error": message}, status)

    def read_body(self, limit: int = MAX_UPLOAD_BYTES) -> bytes:
        length = int(self.headers.get("Content-Length") or 0)
        if length <= 0:
            raise ValueError("بدنهٔ درخواست خالی است")
        if length > limit:
            raise ValueError(f"فایل بزرگ‌تر از حد مجاز ({limit // (1024 * 1024)} MB) است")
        return self.rfile.read(length)

    def read_json(self) -> dict:
        raw = self.read_body(limit=1024 * 1024)
        try:
            data = json.loads(raw.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise ValueError(f"JSON نامعتبر: {exc}") from exc
        if not isinstance(data, dict):
            raise ValueError("بدنه باید یک شیء JSON باشد")
        return data

    def serve_file(self, path: Path) -> None:
        if not path.is_file():
            self.send_error_json("فایل پیدا نشد", 404)
            return
        ctype, _ = mimetypes.guess_type(path.name)
        body = path.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", ctype or "application/octet-stream")
        self.send_header("Content-Length", str(len(body)))
        if path.suffix in (".html", ".js", ".css"):
            self.send_header("Cache-Control", "no-store")
        else:
            self.send_header("Cache-Control", "max-age=60")
        if path.suffix in (".jpg", ".png", ".webp"):
            self.send_header("Content-Disposition", f'inline; filename="{path.name}"')
        self.end_headers()
        self.wfile.write(body)

    # ---------- GET ----------
    def do_GET(self) -> None:  # noqa: N802 (API ثابت http.server)
        parsed = urlparse(self.path)
        path = parsed.path
        try:
            if path in ("/", "/index.html"):
                self.serve_file(WEB / "index.html")
                return
            if path in ("/styles.css", "/app.js"):
                self.serve_file(WEB / path.lstrip("/"))
                return
            if path == "/api/health":
                self.json_state()
                return
            if path == "/api/state":
                self.json_state()
                return
            if path == "/api/capabilities":
                self.send_json({"ok": True, "capabilities": capabilities(), "presets": preset_payload(),
                                "defaults": SETTINGS.data, "output_dir": str(SETTINGS.output_dir())})
                return
            if path == "/api/settings":
                self.send_json({"ok": True, "settings": SETTINGS.data,
                                "output_dir": str(SETTINGS.output_dir())})
                return
            if path == "/api/jobs":
                with JOB_LOCK:
                    jobs = [job_public(JOBS[j]) for j in reversed(JOB_ORDER) if j in JOBS]
                self.send_json({"ok": True, "jobs": jobs})
                return
            if path.startswith("/api/jobs/"):
                job = find_job(path.rsplit("/", 1)[-1])
                if job is None:
                    self.send_error_json("صف پیدا نشد", 404)
                    return
                self.send_json({"ok": True, "job": job_public(job)})
                return
            if path == "/api/devices":
                if CLI is None:
                    self.send_error_json("atlas-cli پیدا نشد", 500)
                    return
                out = run_cli(["devices"], timeout=60)
                data = last_json(out.stdout)
                self.send_json({"ok": out.returncode == 0, "devices": data or []})
                return
            if path == "/api/download":
                self.serve_download(parse_qs(parsed.query))
                return
            if path.startswith("/api/item/"):
                item = STATE.get(path.rsplit("/", 1)[-1])
                if item is None:
                    self.send_error_json("آیتم پیدا نشد", 404)
                    return
                self.send_json({"item": item})
                return
            if path.startswith("/img/"):
                self.serve_image(path, parse_qs(parsed.query))
                return
            self.send_error_json("مسیر ناشناخته", 404)
        except Exception as exc:  # noqa: BLE001 — مرز HTTP
            self.send_error_json(f"خطای داخلی: {exc}", 500)

    def json_state(self) -> None:
        items = sorted(STATE.items.values(), key=lambda x: x.get("created", 0), reverse=True)
        with JOB_LOCK:
            active = [job_public(JOBS[j]) for j in reversed(JOB_ORDER)
                      if j in JOBS and JOBS[j]["status"] in ("queued", "running")]
        self.send_json(
            {
                "ok": CLI is not None,
                "cli": str(CLI) if CLI else None,
                "capabilities": capabilities(),
                "settings": SETTINGS.data,
                "output_dir": str(SETTINGS.output_dir()),
                "data_dir": str(DATA),
                "items": items,
                "jobs": active,
            }
        )

    def serve_image(self, path: str, query: dict) -> None:
        rest = path[len("/img/"):]
        if "/" not in rest:
            self.send_error_json("مسیر تصویر نامعتبر", 400)
            return
        item_id, _, filename = rest.partition("/")
        variant = (query.get("v") or ["preview"])[0]
        allowed = {"source", "preview", "result"}
        if variant not in allowed:
            self.send_error_json("نوع تصویر نامعتبر", 400)
            return
        d = item_dir(item_id)
        if not d.is_dir():
            self.send_error_json("آیتم پیدا نشد", 404)
            return
        candidate = d / f"{variant}.png"
        if not candidate.is_file():
            candidate = d / filename
        self.serve_file(candidate)

    def serve_download(self, query: dict) -> None:
        """دانلود خروجی ذخیره‌شده — فقط از پوشهٔ داده یا پوشهٔ خروجی کاربر."""
        raw = (query.get("path") or [""])[0]
        if not raw:
            self.send_error_json("path لازم است", 400)
            return
        target = Path(raw).resolve()
        allowed_roots = [DATA.resolve(), SETTINGS.output_dir().resolve()]
        if not any(target == root or str(target).startswith(str(root) + os.sep)
                   for root in allowed_roots):
            self.send_error_json("دسترسی به این مسیر مجاز نیست", 403)
            return
        self.serve_file(target)

    # ---------- POST / DELETE ----------
    def do_POST(self) -> None:  # noqa: N802
        parsed = urlparse(self.path)
        try:
            if parsed.path == "/api/upload":
                self.handle_upload(parse_qs(parsed.query))
            elif parsed.path == "/api/process":
                self.handle_process()
            elif parsed.path == "/api/jobs":
                self.handle_job_create()
            elif parsed.path.startswith("/api/jobs/") and parsed.path.endswith("/cancel"):
                self.handle_job_cancel()
            elif parsed.path == "/api/export":
                self.handle_export()
            elif parsed.path == "/api/settings":
                self.handle_settings()
            else:
                self.send_error_json("مسیر ناشناخته", 404)
        except ValueError as exc:
            self.send_error_json(str(exc), 400)
        except subprocess.TimeoutExpired:
            self.send_error_json("پردازش بیش از حد طول کشید", 504)
        except RuntimeError as exc:
            self.send_error_json(str(exc), 500)
        except Exception as exc:  # noqa: BLE001 — مرز HTTP: هر خطایی باید JSON شود
            self.send_error_json(f"خطای داخلی: {exc}", 500)

    def do_DELETE(self) -> None:  # noqa: N802
        parsed = urlparse(self.path)
        try:
            if parsed.path.startswith("/api/item/"):
                item_id = parsed.path.rsplit("/", 1)[-1]
                item = STATE.remove(item_id)
                if item is None:
                    self.send_error_json("آیتم پیدا نشد", 404)
                    return
                shutil.rmtree(item_dir(item_id), ignore_errors=True)
                self.send_json({"ok": True, "removed": item_id})
                return
            self.send_error_json("مسیر ناشناخته", 404)
        except Exception as exc:  # noqa: BLE001
            self.send_error_json(f"خطای داخلی: {exc}", 500)

    def handle_upload(self, query: dict) -> None:
        name = (query.get("name") or ["image.png"])[0]
        name = os.path.basename(name) or "image.png"
        suffix = Path(name).suffix.lower()
        if suffix not in IMAGE_EXTS:
            raise ValueError("پسوند پشتیبانی‌نشده (PNG/JPEG/BMP/TGA/WebP)")
        if suffix == ".webp" and not CAPABILITIES.get("webp", False):
            raise ValueError("این build بدون کدک WebP ساخته شده است؛ PNG/JPEG بدهید")
        body = self.read_body()
        if not body:
            raise ValueError("فایل خالی است")

        item_id = uuid.uuid4().hex[:12]
        d = item_dir(item_id)
        d.mkdir(parents=True, exist_ok=True)
        source = d / f"source{Path(name).suffix.lower()}"
        source.write_bytes(body)

        # پیش‌نمای PNG: هم EXIF orientation و هم پروفایل ICC را درست اعمال می‌کند،
        # و مرورگر لازم نیست هیچ فرمت غیر PNG/JPEG را بشناسد.
        preview = d / "preview.png"
        out = run_cli(["convert", str(source), str(preview), "--quiet"])
        if out.returncode != 0 or not preview.is_file():
            shutil.rmtree(d, ignore_errors=True)
            raise ValueError(f"تصویر خوانده نشد: {out.stderr.strip() or 'خطای نامشخص'}")

        item = {
            "id": item_id,
            "name": name,
            "source": source.name,
            "created": time.time(),
            "result": None,
            "metrics": None,
            "light": None,
        }
        item.update(png_size(preview))
        STATE.add(item)
        self.send_json({"ok": True, "item": item})

    def handle_process(self) -> None:
        data = self.read_json()
        item_id = str(data.get("id") or "")
        if not STATE.get(item_id):
            raise ValueError("آیتم پیدا نشد")
        options = normalise_options(data.get("options") or data)
        item = process_item(item_id, options)
        self.send_json(
            {
                "ok": True,
                "item": item,
                "metrics": item.get("metrics"),
                "light": item.get("light"),
                "steps": item.get("steps"),
                "seconds": item.get("seconds"),
                "images": {
                    "source": f"/img/{item_id}/preview.png?v=preview",
                    "result": f"/img/{item_id}/result_preview.png?v=result",
                },
            }
        )

    def handle_job_create(self) -> None:
        data = self.read_json()
        ids = data.get("ids") or []
        if not isinstance(ids, list) or not all(isinstance(i, str) for i in ids):
            raise ValueError("ids باید فهرست شناسه باشد")
        options = normalise_options(data.get("options") or {})
        job = enqueue_job(ids, options)
        self.send_json({"ok": True, "job": job_public(job)}, status=202)

    def handle_job_cancel(self) -> None:
        job_id = self.path.rstrip("/").split("/")[-2]
        job = cancel_job(job_id)
        if job is None:
            self.send_error_json("صف پیدا نشد", 404)
            return
        self.send_json({"ok": True, "job": job_public(job)})

    def handle_export(self) -> None:
        data = self.read_json()
        ids = data.get("ids") or []
        if not isinstance(ids, list) or not ids:
            raise ValueError("ids لازم است")
        options = normalise_options(data.get("options") or {})
        saved = []
        errors = []
        for item_id in ids:
            item = STATE.get(item_id)
            if item is None:
                errors.append({"id": item_id, "error": "آیتم پیدا نشد"})
                continue
            try:
                result = export_item(item, options)
            except ValueError as exc:
                errors.append({"id": item_id, "error": str(exc)})
                continue
            item.setdefault("result", {})
            if isinstance(item["result"], dict):
                item["result"].update({"path": result["path"], "filename": result["filename"],
                                       "dir": result["dir"], "format": result["format"]})
            saved.append({"id": item_id, "name": item.get("name"), **result})
        STATE.save()
        self.send_json({"ok": not errors, "saved": saved, "errors": errors,
                        "output_dir": str(SETTINGS.output_dir())})

    def handle_settings(self) -> None:
        data = self.read_json()
        updated = SETTINGS.update(data if isinstance(data, dict) else {})
        self.send_json({"ok": True, "settings": updated, "output_dir": str(SETTINGS.output_dir())})

    def log_message(self, fmt: str, *args) -> None:  # noqa: A003
        sys.stderr.write("[atlas-ui] " + fmt % args + "\n")


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Atlas desktop shell (local, offline)")
    ap.add_argument("--port", type=int, default=8000)
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--open", action="store_true", help="باز کردن مرورگر پیش‌فرض")
    args = ap.parse_args(argv)

    DATA.mkdir(parents=True, exist_ok=True)
    SETTINGS.output_dir().mkdir(parents=True, exist_ok=True)
    if CLI is None:
        print("هشدار: atlas-cli پیدا نشد. اول `cmake -S core -B build && cmake --build build`.",
              file=sys.stderr)
    else:
        caps = capabilities()
        print(f"atlas-cli: {CLI} (version {caps.get('version') or '?'}, webp={'on' if caps.get('webp') else 'off'})")
        print(f"output  : {SETTINGS.output_dir()}")

    httpd = Server((args.host, args.port), Handler)
    url = f"http://localhost:{args.port}"
    print(f"Atlas desktop shell is running → {url}", flush=True)
    print("این پنجره را نبندید؛ برای توقف Ctrl+C.", flush=True)
    if args.open:
        threading.Timer(0.8, lambda: webbrowser.open(url)).start()
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        httpd.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

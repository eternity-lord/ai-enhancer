#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""سرور محلی پوستهٔ دسکتاپ Atlas — بدون هیچ وابستگی بیرونی (فقط stdlib).

این فایل سه کار می‌کند:
  ۱) فایل‌های استاتیک رابط کاربری (`desktop/web`) را سرو می‌کند،
  ۲) آپلود تصویر را در یک پوشهٔ موقت ذخیره می‌کند،
  ۳) پردازش واقعی را با `atlas-cli` (هستهٔ C++) اجرا می‌کند و اعداد را JSON برمی‌گرداند.

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
import re
import shutil
import socketserver
import subprocess
import sys
import threading
import time
import webbrowser
import uuid
from pathlib import Path
from urllib.parse import parse_qs, urlparse

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parent
WEB = ROOT / "web"
DATA = Path(os.environ.get("ATLAS_UI_DATA", ROOT / ".data"))

MAX_UPLOAD_BYTES = 80 * 1024 * 1024
CLI_TIMEOUT = 600


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


def run_cli(args: list[str], timeout: int = CLI_TIMEOUT) -> subprocess.CompletedProcess:
    if CLI is None:
        raise RuntimeError(
            "atlas-cli پیدا نشد؛ اول پروژه را بسازید "
            "(ویندوز: tools\\windows\\build.bat — لینوکس/مک: cmake --build build)."
        )
    with LOCK:
        return subprocess.run(
            [str(CLI), *args],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",  # کنسول ویندوز ممکن است cp1256 باشد
            timeout=timeout,
            check=False,
        )


def last_json(text: str) -> dict | None:
    for line in reversed(text.strip().splitlines()):
        line = line.strip()
        if line.startswith("{") and line.endswith("}"):
            try:
                return json.loads(line)
            except json.JSONDecodeError:
                continue
    return None


def parse_output_line(text: str) -> dict:
    """«output: 960x720 (0.69 MP) in 0.11s (6.26 MP/s) -> path»"""
    m = re.search(
        r"output:\s*(\d+)x(\d+)\s*\(([\d.]+) MP\)\s*in\s*([\d.]+)s\s*\(([\d.]+) MP/s\)",
        text,
    )
    if not m:
        return {}
    return {
        "width": int(m.group(1)),
        "height": int(m.group(2)),
        "megapixels": float(m.group(3)),
        "seconds": float(m.group(4)),
        "mp_per_s": float(m.group(5)),
    }


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


class State:
    """فرادادهٔ تصویرهای آپلودشده؛ روی دیسک نگه داشته می‌شود تا ری‌استارت سرور هم بماند."""

    def __init__(self) -> None:
        self.items: dict[str, dict] = {}
        self.path = DATA / "state.json"
        if self.path.is_file():
            try:
                self.items = json.loads(self.path.read_text("utf-8"))
            except json.JSONDecodeError:
                self.items = {}

    def save(self) -> None:
        DATA.mkdir(parents=True, exist_ok=True)
        self.path.write_text(json.dumps(self.items, ensure_ascii=False, indent=2), "utf-8")

    def add(self, item: dict) -> dict:
        self.items[item["id"]] = item
        self.save()
        return item

    def get(self, item_id: str) -> dict | None:
        return self.items.get(item_id)


STATE = State()


def item_dir(item_id: str) -> Path:
    return DATA / "items" / item_id


class Handler(http.server.SimpleHTTPRequestHandler):
    server_version = "AtlasDesktop/0.1"

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
        self.send_header("Cache-Control", "no-store" if path.suffix == ".html" else "max-age=60")
        self.end_headers()
        self.wfile.write(body)

    # ---------- GET ----------
    def do_GET(self) -> None:  # noqa: N802 (API ثابت http.server)
        parsed = urlparse(self.path)
        path = parsed.path

        if path in ("/", "/index.html"):
            self.serve_file(WEB / "index.html")
            return
        if path in ("/styles.css", "/app.js"):
            self.serve_file(WEB / path.lstrip("/"))
            return

        if path == "/api/health":
            version = ""
            if CLI is not None:
                out = run_cli(["version"], timeout=30)
                version = out.stdout.strip() or out.stderr.strip()
            self.json_state()
            return
        if path == "/api/state":
            self.json_state()
            return
        if path == "/api/devices":
            if CLI is None:
                self.send_error_json("atlas-cli پیدا نشد", 500)
                return
            out = run_cli(["devices"], timeout=60)
            data = last_json(out.stdout)
            self.send_json({"ok": out.returncode == 0, "devices": data or []})
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

    def json_state(self) -> None:
        items = sorted(STATE.items.values(), key=lambda x: x.get("created", 0), reverse=True)
        self.send_json(
            {
                "ok": CLI is not None,
                "cli": str(CLI) if CLI else None,
                "items": items,
            }
        )

    def serve_image(self, path: str, query: dict) -> None:
        rest = path[len("/img/") :]
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

    # ---------- POST ----------
    def do_POST(self) -> None:  # noqa: N802
        parsed = urlparse(self.path)
        try:
            if parsed.path == "/api/upload":
                self.handle_upload(parse_qs(parsed.query))
            elif parsed.path == "/api/process":
                self.handle_process()
            else:
                self.send_error_json("مسیر ناشناخته", 404)
        except ValueError as exc:
            self.send_error_json(str(exc), 400)
        except subprocess.TimeoutExpired:
            self.send_error_json("پردازش بیش از حد طول کشید", 504)
        except Exception as exc:  # noqa: BLE001 — مرز HTTP: هر خطایی باید JSON شود
            self.send_error_json(f"خطای داخلی: {exc}", 500)

    def handle_upload(self, query: dict) -> None:
        name = (query.get("name") or ["image.png"])[0]
        name = os.path.basename(name) or "image.png"
        if Path(name).suffix.lower() not in {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".webp"}:
            raise ValueError("پسوند پشتیبانی‌نشده (PNG/JPEG/BMP/TGA/WebP)")
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
        }
        item.update(png_size(preview))
        STATE.add(item)
        self.send_json({"ok": True, "item": item})


    def handle_process(self) -> None:
        data = self.read_json()
        item_id = str(data.get("id") or "")
        item = STATE.get(item_id)
        if item is None:
            raise ValueError("آیتم پیدا نشد")
        d = item_dir(item_id)
        source = d / item["source"]
        if not source.is_file():
            raise ValueError("فایل مبدأ روی دیسک نیست")

        denoise = str(data.get("denoise", "auto"))
        deblock = str(data.get("deblock", "auto"))
        scale = int(data.get("scale", 2))
        quality = int(data.get("quality", 92))
        if scale not in (1, 2, 3, 4):
            raise ValueError("scale باید ۱ تا ۴ باشد")
        if not (1 <= quality <= 100):
            raise ValueError("quality باید ۱ تا ۱۰۰ باشد")

        restored = d / "restored.png"
        final = d / "result.png"
        for stale in (restored, final):
            stale.unlink(missing_ok=True)

        started = time.perf_counter()
        # مرحلهٔ ۱: پاک‌سازی (نویز + JPEG) با گزارش JSON از خود هسته.
        out1 = run_cli(
            ["restore", str(source), str(restored), "--denoise", denoise,
             "--deblock", deblock, "--json"]
        )
        if out1.returncode != 0:
            raise ValueError(f"پاک‌سازی شکست خورد: {out1.stderr.strip()}")
        metrics = last_json(out1.stdout) or {}

        steps = [
            {
                "name": "restore",
                "ms": metrics.get("ms", 0.0),
                "deblock": metrics.get("deblock"),
                "denoise": metrics.get("denoise"),
                "denoise_strength": metrics.get("denoise_strength"),
                "deblock_strength": metrics.get("deblock_strength"),
            }
        ]

        # مرحلهٔ ۲ (اختیاری): بزرگ‌نمایی روی خروجی پاک‌شده.
        target = restored
        sr = {}
        if scale > 1:
            out2 = run_cli(
                ["enhance", str(restored), str(final), "--scale", str(scale),
                 "--denoise", "off", "--deblock", "off", "--quality", str(quality),
                 "--quiet"]
            )
            if out2.returncode != 0:
                raise ValueError(f"بزرگ‌نمایی شکست خورد: {out2.stderr.strip()}")
            target = final
            # با --quiet خط output چاپ نمی‌شود؛ زمان را خودمان اندازه می‌گیریم.
            sr = {"wall_seconds": round(time.perf_counter() - started, 2)}
            steps.append({"name": "enhance", "scale": scale, **sr})

        if not target.is_file():
            raise ValueError("خروجی ساخته نشد")

        # پیش‌نمای نتیجه از همان هسته (orientation/ICC یکسان با بقیهٔ مسیر).
        preview = d / "result_preview.png"

        # نام فایل نتیجه را با `result.png` هم‌نام می‌کنیم تا GET /img ساده بماند.
        if target != final:
            shutil.copyfile(target, final)

        result_preview = run_cli(["convert", str(final), str(preview), "--quiet"])
        if result_preview.returncode != 0 or not preview.is_file():
            shutil.copyfile(final, preview)

        # ابعاد نهایی از هدر PNG خود خروجی خوانده می‌شود (بدون حدس).
        info = png_size(final) if final.is_file() else {}

        item["result"] = {
            "width": info.get("width"),
            "height": info.get("height"),
            "scale": scale,
            "quality": quality,
            "format": "png",
            "source_bytes": source.stat().st_size,
            "result_bytes": final.stat().st_size,
        }
        item["metrics"] = metrics
        item["seconds"] = round(time.perf_counter() - started, 2)
        item["steps"] = steps
        STATE.save()

        self.send_json(
            {
                "ok": True,
                "item": item,
                "metrics": metrics,
                "steps": steps,
                "seconds": item["seconds"],
                "images": {
                    "source": f"/img/{item_id}/preview.png?v=preview",
                    "result": f"/img/{item_id}/result_preview.png?v=result",
                },
            }
        )

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
    if CLI is None:
        print("هشدار: atlas-cli پیدا نشد. اول `cmake -S core -B build && cmake --build build`.",
              file=sys.stderr)
    else:
        print(f"atlas-cli: {CLI}")

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

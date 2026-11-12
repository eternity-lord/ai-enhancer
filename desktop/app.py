#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Atlas — پنجرهٔ نیتیو (برنامهٔ واقعی، نه تب مرورگر).

این فایل همان رابط شیشه‌ای `desktop/web` را داخل یک پنجرهٔ سیستمی نشان می‌دهد:
  • بک‌اند همان `server.py` است که در یک thread روی 127.0.0.1 اجرا می‌شود،
    پس هیچ منطق پردازشی دوباره نوشته نشده و مسیر آزمایش‌شده همان است؛
  • موتور نمایش روی ویندوز WebView2 و روی مک WebKit است (از طریق pywebview)؛
  • خروجی نهایی با PyInstaller به `Atlas.exe` تبدیل می‌شود (build-exe.bat).

اجرا در حالت توسعه (بدون ساخت exe):
    python desktop/app.py                 # پنجرهٔ عادی با نوار عنوان سیستم
    python desktop/app.py --frameless     # پنجرهٔ بدون قاب (تم شیشه‌ای کامل)
    python desktop/app.py --browser       # فقط مرورگر (مثل قبل)
    python desktop/app.py --check         # تست سلامت بسته (برای CI)
"""

from __future__ import annotations

import argparse
import sys
import threading
import subprocess
import time
import traceback
import urllib.request
import webbrowser
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import server as backend  # noqa: E402  (مسیر بالا لازم است)

APP_NAME = "Atlas"


def log_path() -> Path:
    return backend.DATA / "atlas.log"


def write_log(message: str) -> None:
    """در حالت --noconsole هیچ کنسولی نیست؛ خطاها باید جایی بمانند."""
    try:
        backend.DATA.mkdir(parents=True, exist_ok=True)
        with log_path().open("a", encoding="utf-8") as handle:
            handle.write(f"{time.strftime('%Y-%m-%d %H:%M:%S')} {message}\n")
    except OSError:
        pass


def start_backend(host: str, port: int) -> tuple[backend.Server, str]:
    httpd = backend.Server((host, port), backend.Handler)
    actual_port = httpd.server_address[1]
    thread = threading.Thread(target=httpd.serve_forever, daemon=True)
    thread.start()
    return httpd, f"http://{host}:{actual_port}"


def wait_until_ready(url: str, timeout: float = 10.0) -> bool:
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with urllib.request.urlopen(f"{url}/api/health", timeout=1.0) as response:
                return response.status == 200
        except Exception:  # noqa: BLE001 — تا آماده شدن، هر خطایی طبیعی است
            time.sleep(0.1)
    return False


def _dialog():
    """ثابت‌های دیالوگ pywebview، با تحمل تفاوت نسخه‌ها."""
    try:
        import webview  # noqa: PLC0415

        return webview
    except Exception:  # noqa: BLE001
        class _Fallback:  # حداقل برای اینکه خطای واضح بدهیم
            FOLDER_DIALOG = 20
        return _Fallback()


class Shell:
    """پل کنترل پنجره برای حالت بدون قاب (چراغ‌های macOS و کشیدن پنجره)."""

    def __init__(self) -> None:
        self.window = None
        self.maximized = False

    def bind(self, window) -> None:
        self.window = window

    # این متدها از JavaScript صدا زده می‌شوند؛ هیچ‌کدام نباید استثنا پرتاب کند،
    # وگرنه رابط کاربری گیر می‌کند.
    def close_window(self) -> None:
        try:
            self.window.destroy()
        except Exception:  # noqa: BLE001
            pass

    def minimize_window(self) -> None:
        try:
            self.window.minimize()
        except Exception:  # noqa: BLE001
            pass

    def toggle_maximize(self) -> None:
        try:
            if self.maximized:
                self.window.restore()
            else:
                self.window.maximize()
            self.maximized = not self.maximized
        except Exception:  # noqa: BLE001
            pass

    # انتخاب پوشهٔ خروجی با دیالوگ بومی ویندوز/مک (به‌جای تایپ دستی مسیر).
    def choose_folder(self) -> dict | str:
        try:
            result = self.window.create_file_dialog(
                _dialog().FOLDER_DIALOG, allow_multiple=False
            )
            if result:
                return str(result[0]) if not isinstance(result, str) else result
            return ""
        except Exception as exc:  # noqa: BLE001
            return {"error": f"انتخاب پوشه ممکن نشد: {exc}"}

    # باز کردن پوشهٔ خروجی در فایل‌منیجر سیستم.
    def open_folder(self, path: str = "") -> dict:
        target = path or str(backend.SETTINGS.output_dir())
        try:
            folder = Path(target).expanduser()
            folder.mkdir(parents=True, exist_ok=True)
            if sys.platform.startswith("win"):
                subprocess.Popen(["explorer", str(folder)])
            elif sys.platform == "darwin":
                subprocess.Popen(["open", str(folder)])
            else:
                subprocess.Popen(["xdg-open", str(folder)])
            return {"ok": True, "path": str(folder)}
        except Exception as exc:  # noqa: BLE001
            return {"error": f"پوشه باز نشد: {exc}"}

    def move_window(self, dx: int, dy: int) -> None:
        try:
            x = self.window.x
            y = self.window.y
            if x is None or y is None:
                return
            self.window.move(int(x) + int(dx), int(y) + int(dy))
        except Exception:  # noqa: BLE001
            pass


def run_window(url: str, frameless: bool, title: str, debug: bool) -> int:
    try:
        import webview  # noqa: PLC0415 — وارد‌کردن دیرهنگام: --check نباید GUI بخواهد
    except Exception as exc:  # noqa: BLE001
        write_log(f"pywebview import failed: {exc}")
        print(
            "پنجرهٔ نیتیو در دسترس نیست (pywebview نصب/سازگار نیست).\n"
            "راه‌حل: python -m pip install --upgrade pywebview\n"
            f"یا مرورگر: {url}",
            file=sys.stderr,
        )
        return 3

    shell = Shell()
    window = webview.create_window(
        title,
        f"{url}/?shell={'glass' if frameless else 'native'}",
        width=1440,
        height=920,
        min_size=(1040, 680),
        background_color="#0b2f78",
        frameless=frameless,
        easy_drag=False,  # با easy_drag همهٔ کلیک‌ها کشیدن می‌شوند؛ خودمان مدیریت می‌کنیم
        js_api=shell,
    )
    shell.bind(window)
    webview.start(debug=debug)
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Atlas native shell")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=0)
    parser.add_argument("--frameless", action="store_true", help="پنجره بدون قاب (تم کامل)")
    parser.add_argument("--browser", action="store_true", help="فقط سرور محلی + مرورگر")
    parser.add_argument("--check", action="store_true", help="تست سلامت بسته و خروج")
    parser.add_argument("--debug", action="store_true", help="ابزار توسعهٔ WebView")
    args = parser.parse_args(argv)

    backend.DATA.mkdir(parents=True, exist_ok=True)
    httpd, url = start_backend(args.host, args.port)
    ready = wait_until_ready(url)
    write_log(f"start url={url} ready={ready} cli={backend.CLI} frozen={getattr(sys, 'frozen', False)}")

    if not ready:
        print("سرور محلی بالا نیامد.", file=sys.stderr)
        httpd.shutdown()
        return 4

    if args.check:
        with urllib.request.urlopen(f"{url}/api/health", timeout=5) as response:
            body = response.read().decode("utf-8")
        print(f"http {response.status} {body[:160]}")
        print(f"cli={backend.CLI}")
        print(f"data={backend.DATA}")
        print(f"web={backend.WEB}")
        httpd.shutdown()
        return 0 if response.status == 200 else 5

    if args.browser:
        print(f"Atlas → {url}  (Ctrl+C برای توقف)")
        try:
            webbrowser.open(url)
        except Exception:  # noqa: BLE001
            pass
        try:
            while True:
                time.sleep(0.5)
        except KeyboardInterrupt:
            pass
        httpd.shutdown()
        return 0

    try:
        code = run_window(url, args.frameless, APP_NAME, args.debug)
    finally:
        httpd.shutdown()
    return code


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except SystemExit:
        raise
    except Exception:  # noqa: BLE001 — در حالت windowed، هیچ کنسولی برای traceback نیست
        write_log(traceback.format_exc())
        raise

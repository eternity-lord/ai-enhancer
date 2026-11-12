#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""تست دودی سرور پوستهٔ دسکتاپ — بدون وابستگی بیرونی.

اجرا:
    python3 desktop/test_server.py
اگر `atlas-cli` ساخته نشده باشد، تست‌ها skip می‌شوند (نه fail) تا CI تازه هم سبز بماند.
"""

from __future__ import annotations

import json
import os
import struct
import sys
import tempfile
import threading
import unittest
import urllib.error
import urllib.request
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

os.environ.setdefault("ATLAS_UI_DATA", tempfile.mkdtemp(prefix="atlas-ui-test-"))

import server  # noqa: E402  (بعد از تنظیم مسیر و محیط)


def make_png(width: int = 48, height: int = 32) -> bytes:
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        for x in range(width):
            raw += bytes([int(255 * x / width), int(255 * y / height), 120])

    def chunk(tag: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) +
            chunk(b"IDAT", zlib.compress(bytes(raw))) + chunk(b"IEND", b""))


class ServerTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        if server.CLI is None:
            raise unittest.SkipTest("atlas-cli ساخته نشده است")
        cls.httpd = server.Server(("127.0.0.1", 0), server.Handler)
        cls.port = cls.httpd.server_address[1]
        cls.thread = threading.Thread(target=cls.httpd.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls) -> None:
        cls.httpd.shutdown()
        cls.httpd.server_close()

    # ---------- کمکی ----------
    def url(self, path: str) -> str:
        return f"http://127.0.0.1:{self.port}{path}"

    def get_json(self, path: str) -> dict:
        with urllib.request.urlopen(self.url(path), timeout=30) as response:
            self.assertEqual(response.status, 200)
            return json.loads(response.read().decode("utf-8"))

    def post(self, path: str, body: bytes, content_type: str) -> dict:
        request = urllib.request.Request(
            self.url(path), data=body, method="POST",
            headers={"Content-Type": content_type},
        )
        with urllib.request.urlopen(request, timeout=120) as response:
            return json.loads(response.read().decode("utf-8"))

    # ---------- تست‌ها ----------
    def test_health_reports_cli(self) -> None:
        payload = self.get_json("/api/health")
        self.assertTrue(payload["ok"])
        self.assertTrue(payload["cli"].endswith("atlas-cli"))

    def test_index_is_served(self) -> None:
        with urllib.request.urlopen(self.url("/"), timeout=30) as response:
            html = response.read().decode("utf-8")
        self.assertIn("Atlas", html)
        self.assertIn("app.js", html)

    def test_upload_and_process(self) -> None:
        png = make_png()
        uploaded = self.post("/api/upload?name=test.png", png, "application/octet-stream")
        item = uploaded["item"]
        self.assertEqual((item["width"], item["height"]), (48, 32))

        payload = self.post(
            "/api/process",
            json.dumps({"id": item["id"], "denoise": "auto", "deblock": "auto",
                        "scale": 2, "quality": 92}).encode("utf-8"),
            "application/json",
        )
        self.assertTrue(payload["ok"])
        self.assertEqual(payload["item"]["result"]["width"], 96)
        self.assertEqual(payload["item"]["result"]["height"], 64)
        self.assertIn("sigma_before", payload["metrics"])
        # خروجی واقعاً قابل دریافت است
        with urllib.request.urlopen(self.url(payload["images"]["result"]), timeout=30) as r:
            body = r.read()
        self.assertEqual(body[:8], b"\x89PNG\r\n\x1a\n")

    def test_process_scale_one_keeps_size(self) -> None:
        uploaded = self.post("/api/upload?name=same.png", make_png(64, 40),
                             "application/octet-stream")
        item = uploaded["item"]
        payload = self.post(
            "/api/process",
            json.dumps({"id": item["id"], "denoise": "off", "deblock": "off",
                        "scale": 1, "quality": 90}).encode("utf-8"),
            "application/json",
        )
        self.assertEqual(payload["item"]["result"]["width"], 64)
        self.assertEqual(payload["item"]["result"]["height"], 40)

    def test_upload_rejects_bad_extension(self) -> None:
        with self.assertRaises(urllib.error.HTTPError) as ctx:
            self.post("/api/upload?name=notes.txt", b"hello", "application/octet-stream")
        self.assertEqual(ctx.exception.code, 400)

    def test_cli_candidates_cover_windows_layout(self) -> None:
        # روی ویندوز فایل `.exe` است و با ژنراتور Visual Studio داخل Release/ می‌رود.
        candidates = [str(p) for p in server.cli_candidates(exe=".exe")]
        self.assertTrue(any(p.endswith("build\\Release\\atlas-cli.exe") or
                            p.endswith("build/Release/atlas-cli.exe") for p in candidates),
                        candidates)
        self.assertTrue(any(p.endswith("atlas-cli.exe") for p in candidates))
        unix = [str(p) for p in server.cli_candidates(exe="")]
        self.assertTrue(any(p.endswith("build/atlas-cli") for p in unix))

    def test_env_override_wins(self) -> None:
        old = os.environ.get("ATLAS_CLI")
        try:
            fake = Path(tempfile.mkdtemp()) / "atlas-cli"
            fake.write_text("#!/bin/sh\n", encoding="utf-8")
            fake.chmod(0o755)
            os.environ["ATLAS_CLI"] = str(fake)
            self.assertEqual(server.cli_candidates()[0], fake)
        finally:
            if old is None:
                os.environ.pop("ATLAS_CLI", None)
            else:
                os.environ["ATLAS_CLI"] = old

    def test_data_dir_honours_env_override(self) -> None:
        old = os.environ.get("ATLAS_UI_DATA")
        try:
            target = tempfile.mkdtemp(prefix="atlas-data-")
            os.environ["ATLAS_UI_DATA"] = target
            self.assertEqual(server.data_dir(), Path(target))
        finally:
            if old is None:
                os.environ.pop("ATLAS_UI_DATA", None)
            else:
                os.environ["ATLAS_UI_DATA"] = old

    def test_frozen_app_uses_meipass_and_user_data_dir(self) -> None:
        from unittest import mock

        old = os.environ.pop("ATLAS_UI_DATA", None)
        try:
            with mock.patch.object(server.sys, "frozen", True, create=True), \
                 mock.patch.object(server.sys, "_MEIPASS", "/tmp/atlas-meipass", create=True):
                self.assertEqual(server.bundle_root(), Path("/tmp/atlas-meipass"))
                data = server.data_dir()
                self.assertEqual((data.name, data.parent.name), ("data", "Atlas"))
                self.assertIn(Path("/tmp/atlas-meipass") / "atlas-cli",
                              server.cli_candidates())
        finally:
            if old is not None:
                os.environ["ATLAS_UI_DATA"] = old

    def test_cli_candidates_include_executable_folder(self) -> None:
        paths = [str(p) for p in server.cli_candidates()]
        exe_dir = str(Path(server.sys.executable).resolve().parent)
        self.assertTrue(any(path.startswith(exe_dir) for path in paths), paths[:3])

    def test_unknown_item_is_404(self) -> None:
        with self.assertRaises(urllib.error.HTTPError) as ctx:
            self.get_json("/api/item/doesnotexist")
        self.assertEqual(ctx.exception.code, 404)

    def test_process_missing_id_is_400(self) -> None:
        with self.assertRaises(urllib.error.HTTPError) as ctx:
            self.post("/api/process", json.dumps({"id": "nope"}).encode("utf-8"),
                      "application/json")
        self.assertEqual(ctx.exception.code, 400)


if __name__ == "__main__":
    unittest.main(verbosity=2)

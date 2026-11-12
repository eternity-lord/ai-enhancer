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
import time
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

    def test_webp_upload_is_rejected_when_codec_missing(self) -> None:
        if server.CAPABILITIES.get("webp"):
            self.skipTest("این build کدک WebP دارد")
        with self.assertRaises(urllib.error.HTTPError) as ctx:
            self.post("/api/upload?name=photo.webp", b"RIFF0000WEBP", "application/octet-stream")
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



    # ---------- قابلیت‌ها، تنظیمات، صف و ذخیره ----------
    def test_capabilities_report_build_and_presets(self) -> None:
        payload = self.get_json("/api/capabilities")
        caps = payload["capabilities"]
        self.assertTrue(caps["version"])
        self.assertIn("webp", caps)
        self.assertIn("png", caps["formats"])
        names = [preset["id"] for preset in payload["presets"]]
        self.assertIn("photo", names)
        self.assertIn("clean", names)
        # پریست‌ها باید گزینه‌های واقعی داشته باشند، نه فقط نام
        photo = next(preset for preset in payload["presets"] if preset["id"] == "photo")
        self.assertIn("scale", photo["options"])

    def test_settings_roundtrip(self) -> None:
        original = self.get_json("/api/settings")["settings"]
        target = tempfile.mkdtemp(prefix="atlas-out-")
        try:
            saved = self.post("/api/settings",
                              json.dumps({"quality": 88, "pattern": "{name}-{preset}",
                                          "output_dir": target}).encode("utf-8"),
                              "application/json")
            self.assertEqual(saved["settings"]["quality"], 88)
            self.assertEqual(saved["settings"]["pattern"], "{name}-{preset}")
            again = self.get_json("/api/settings")["settings"]
            self.assertEqual(again["quality"], 88)
        finally:
            self.post("/api/settings",
                      json.dumps({"quality": original["quality"],
                                  "pattern": original["pattern"],
                                  "output_dir": original["output_dir"]}).encode("utf-8"),
                      "application/json")

    def test_invalid_options_are_rejected(self) -> None:
        uploaded = self.post("/api/upload?name=bad.png", make_png(24, 24),
                             "application/octet-stream")
        item = uploaded["item"]
        for options in ({"scale": 9}, {"quality": 0}, {"format": "tiff"}, {"denoise": "hard"}):
            with self.assertRaises(urllib.error.HTTPError) as ctx:
                self.post("/api/process",
                          json.dumps({"id": item["id"], "options": options}).encode("utf-8"),
                          "application/json")
            self.assertEqual(ctx.exception.code, 400, options)

    def test_light_stage_runs_in_pipeline(self) -> None:
        uploaded = self.post("/api/upload?name=dark.png", make_png(64, 48),
                             "application/octet-stream")
        item = uploaded["item"]
        payload = self.post(
            "/api/process",
            json.dumps({"id": item["id"], "options": {
                "scale": 1, "denoise": "off", "deblock": "off",
                "auto_light": True, "auto_wb": True, "export": False,
            }}).encode("utf-8"),
            "application/json",
        )
        light = payload["light"]
        self.assertIsNotNone(light, "مرحلهٔ نور باید گزارش بدهد")
        self.assertIn("mean_before", light)
        self.assertIn("mean_after", light)
        self.assertGreater(light["mean_after"], 0.0)
        names = [step["name"] for step in payload["steps"]]
        self.assertIn("light", names)

    def test_clean_preset_skips_light_stage(self) -> None:
        item = self.post("/api/upload?name=clean.png", make_png(32, 32),
                         "application/octet-stream")["item"]
        payload = self.post(
            "/api/process",
            json.dumps({"id": item["id"], "options": {
                "scale": 1, "denoise": "auto", "deblock": "auto",
                "auto_light": False, "auto_wb": False, "export": False,
                "preset": "clean",
            }}).encode("utf-8"),
            "application/json",
        )
        # پریست «فقط پاک‌سازی» نباید نور را عوض کند (نه auto، نه WB).
        self.assertIsNone(payload["light"])
        self.assertEqual([step["name"] for step in payload["steps"]], ["restore"])

    def test_export_writes_chosen_format_and_pattern(self) -> None:
        uploaded = self.post("/api/upload?name=export-me.png", make_png(40, 30),
                             "application/octet-stream")
        item = uploaded["item"]
        self.post("/api/process",
                  json.dumps({"id": item["id"], "options": {
                      "scale": 2, "denoise": "off", "deblock": "off",
                      "auto_light": False, "auto_wb": False, "export": False,
                  }}).encode("utf-8"),
                  "application/json")
        out_dir = tempfile.mkdtemp(prefix="atlas-export-")
        payload = self.post(
            "/api/export",
            json.dumps({"ids": [item["id"]], "options": {
                "format": "jpg", "quality": 85, "pattern": "{name}-{scale}",
                "output_dir": out_dir, "scale": 2,
            }}).encode("utf-8"),
            "application/json",
        )
        self.assertTrue(payload["ok"], payload)
        saved = payload["saved"][0]
        self.assertTrue(saved["filename"].startswith("export-me-2x"))
        self.assertTrue(saved["filename"].endswith(".jpg"))
        written = Path(saved["path"])
        self.assertTrue(written.is_file())
        self.assertGreater(written.stat().st_size, 100)
        # نام تکراری نباید فایل قبلی را بازنویسی کند
        again = self.post(
            "/api/export",
            json.dumps({"ids": [item["id"]], "options": {
                "format": "jpg", "quality": 85, "pattern": "{name}-{scale}",
                "output_dir": out_dir, "scale": 2,
            }}).encode("utf-8"),
            "application/json",
        )
        self.assertNotEqual(again["saved"][0]["path"], saved["path"])
        self.assertTrue(Path(again["saved"][0]["path"]).is_file())

    def test_batch_job_processes_all_items(self) -> None:
        first = self.post("/api/upload?name=batch-a.png", make_png(48, 32),
                          "application/octet-stream")["item"]
        second = self.post("/api/upload?name=batch-b.png", make_png(40, 40),
                           "application/octet-stream")["item"]
        created = self.post(
            "/api/jobs",
            json.dumps({"ids": [first["id"], second["id"]], "options": {
                "scale": 2, "denoise": "auto", "deblock": "auto",
                "auto_light": True, "auto_wb": True, "format": "png",
            }}).encode("utf-8"),
            "application/json",
        )
        job_id = created["job"]["id"]
        job = created["job"]
        deadline = time.time() + 120
        while job["status"] in ("queued", "running") and time.time() < deadline:
            time.sleep(0.25)
            job = self.get_json(f"/api/jobs/{job_id}")["job"]
        self.assertEqual(job["status"], "done", job)
        self.assertEqual(job["progress"], 100)
        self.assertEqual(len(job["items"]), 2)
        for entry in job["items"]:
            self.assertEqual(entry["status"], "done", entry)
            self.assertTrue(entry["result"]["filename"])
            self.assertTrue(Path(entry["result"]["path"]).is_file())
        listed = self.get_json("/api/jobs")["jobs"]
        self.assertTrue(any(entry["id"] == job_id for entry in listed))

    def test_cancel_job_stops_before_finishing_or_reports_done(self) -> None:
        item = self.post("/api/upload?name=cancel.png", make_png(64, 48),
                         "application/octet-stream")["item"]
        created = self.post(
            "/api/jobs",
            json.dumps({"ids": [item["id"]], "options": {
                "scale": 4, "denoise": "auto", "deblock": "auto"}}).encode("utf-8"),
            "application/json",
        )
        job_id = created["job"]["id"]
        cancelled = self.post(f"/api/jobs/{job_id}/cancel", b"{}", "application/json")
        self.assertTrue(cancelled["ok"])
        deadline = time.time() + 60
        job = cancelled["job"]
        while job["status"] in ("queued", "running") and time.time() < deadline:
            time.sleep(0.2)
            job = self.get_json(f"/api/jobs/{job_id}")["job"]
        # روی فایل کوچک ممکن است پیش از رسیدن لغو تمام شود؛ هر دو نتیجه معتبر است.
        self.assertIn(job["status"], ("cancelled", "done"), job)

    def test_delete_removes_item_and_files(self) -> None:
        item = self.post("/api/upload?name=delete-me.png", make_png(32, 32),
                         "application/octet-stream")["item"]
        folder = server.item_dir(item["id"])
        self.assertTrue(folder.is_dir())
        request = urllib.request.Request(self.url(f"/api/item/{item['id']}"), method="DELETE")
        with urllib.request.urlopen(request, timeout=30) as response:
            payload = json.loads(response.read().decode("utf-8"))
        self.assertTrue(payload["ok"])
        self.assertFalse(folder.exists())
        with self.assertRaises(urllib.error.HTTPError) as ctx:
            self.get_json(f"/api/item/{item['id']}")
        self.assertEqual(ctx.exception.code, 404)

    def test_download_guard_blocks_outside_paths(self) -> None:
        with self.assertRaises(urllib.error.HTTPError) as ctx:
            self.get_json("/api/download?path=/etc/passwd")
        self.assertEqual(ctx.exception.code, 403)

if __name__ == "__main__":
    unittest.main(verbosity=2)

// SPDX-License-Identifier: Apache-2.0
// تست DOM رابط کاربری بدون مرورگر (jsdom). وابستگی اختیاری و فقط برای توسعه است:
// ساخت خود پروژه و تست‌های C++/Python به این فایل وابسته نیستند.
//
//   cd desktop && npm install && npm run test:dom
//
// چه‌چیزی را قفل می‌کند: رندر فهرست تصاویر، انتخاب آیتم، نمایش اعداد سنجش از
// پاسخ API، پریست‌ها، اسلایدرهای نور، صف دسته‌ای (+ پیشرفت و لغو)، ذخیرهٔ خروجی
// و حذف واقعی آیتم. هیچ پردازشی انجام نمی‌شود؛ فقط قرارداد HTTP بررسی می‌شود.

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { JSDOM } from "jsdom";

const here = path.dirname(fileURLToPath(import.meta.url));
const html = fs.readFileSync(path.join(here, "index.html"), "utf8");
const app = fs.readFileSync(path.join(here, "app.js"), "utf8");

const failures = [];
const check = (label, condition, detail = "") => {
  if (!condition) failures.push(`${label} ${detail}`.trim());
  console.log(`[${condition ? "ok" : "FAIL"}] ${label}${detail ? ` — ${detail}` : ""}`);
};

const metrics = {
  sigma_before: 0.0026,
  sigma_after: 0.0008,
  blockiness_before: 0.012,
  blockiness_after: 0.0001,
  deblock: true,
  denoise: true,
  denoise_strength: 0.25,
  deblock_strength: 1,
  ms: 45.2,
  width: 480,
  height: 360,
};

const lightReport = {
  mean_before: 0.3642,
  mean_after: 0.3806,
  contrast_before: 0.1282,
  contrast_after: 0.1665,
  saturation_before: 0.2252,
  saturation_after: 0.0203,
  auto_ev: 0.309,
  ms: 4.4,
};

const items = [
  {
    id: "aa",
    name: "غروب-ساحل.jpg",
    width: 480,
    height: 360,
    result: { width: 960, height: 720, scale: 2, path: "/data/output/sunset-2x.png", filename: "sunset-2x.png" },
    metrics,
    light: lightReport,
    seconds: 0.41,
  },
  { id: "bb", name: "پرتره.png", width: 320, height: 240, result: null, metrics: null, light: null },
];

const settings = {
  scale: 2,
  quality: 92,
  format: "png",
  restore_mode: "auto",
  denoise_on: true,
  deblock_on: true,
  denoise_pct: 35,
  deblock_pct: 55,
  light_mode: "auto",
  light: { exposure: 0, contrast: 0, saturation: 0, shadows: 0, highlights: 0, warmth: 0 },
  tile: 256,
  threads: 0,
  keep_metadata: false,
  pattern: "{name}-{scale}",
  output_dir: "",
  preset: "photo",
};

const capabilities = { version: "0.2.0-dev", ncnn: false, webp: false, threads: 8, formats: ["png", "jpg"] };

const presets = [
  {
    id: "photo",
    name: "عکس واقعی ۲×",
    note: "پیش‌فرض",
    options: { scale: 2, denoise: "auto", deblock: "auto", auto_light: true, auto_wb: true, format: "png", quality: 95 },
  },
  {
    id: "old",
    name: "عکس قدیمی",
    note: "سایه‌ها بالا",
    options: {
      scale: 2,
      denoise: "55",
      deblock: "65",
      auto_light: true,
      auto_wb: true,
      light: { shadows: 0.3, saturation: 0.2, contrast: 0.1 },
      format: "png",
      quality: 95,
    },
  },
  { id: "clean", name: "فقط پاک‌سازی", note: "بدون تغییر اندازه", options: { scale: 1, denoise: "auto", deblock: "auto", auto_light: false, auto_wb: false } },
];

const calls = [];
let jobPolls = 0;

const jobPayload = (status, progress) => ({
  ok: true,
  job: {
    id: "job1",
    status,
    progress,
    current: "غروب-ساحل.jpg",
    message: status === "running" ? "بزرگ‌نمایی" : "تمام",
    items: [
      { id: "aa", name: "غروب-ساحل.jpg", status, progress, error: null },
      { id: "bb", name: "پرتره.png", status: "queued", progress: 0, error: null },
    ],
    summary: { done: 1, errors: 0, total: 2, seconds: 1.2 },
  },
});

const dom = new JSDOM(html, {
  runScripts: "outside-only",
  url: "http://localhost:8000/",
  pretendToBeVisual: true,
});
const { window } = dom;

window.fetch = async (requestPath, options = {}) => {
  const method = options.method || "GET";
  calls.push({ path: requestPath, method, body: options.body });
  const json = (data) => ({ ok: true, status: 200, json: async () => data });
  if (requestPath === "/api/health") {
    return json({
      ok: true,
      cli: "/x/atlas-cli",
      capabilities,
      settings,
      output_dir: "/data/output",
      data_dir: "/data",
      items,
      jobs: [],
    });
  }
  if (requestPath === "/api/capabilities") {
    return json({ ok: true, capabilities, presets, defaults: settings, output_dir: "/data/output" });
  }
  if (requestPath === "/api/devices") return json({ ok: true, devices: [{ id: "classical-cpu" }] });
  if (requestPath === "/api/process") {
    return json({
      ok: true,
      item: { ...items[0], result: { width: 960, height: 720, scale: 2, path: "/data/output/x.png", filename: "x.png" }, metrics, light: lightReport },
      metrics,
      light: lightReport,
      steps: [],
      seconds: 0.41,
    });
  }
  if (requestPath === "/api/jobs" && method === "POST") return json({ ok: true, job: jobPayload("queued", 0).job });
  if (requestPath.startsWith("/api/jobs/job1")) {
    if (requestPath.endsWith("/cancel")) return json({ ok: true, job: jobPayload("cancelled", 42).job });
    jobPolls += 1;
    return json(jobPayload(jobPolls > 1 ? "done" : "running", jobPolls > 1 ? 100 : 42));
  }
  if (requestPath === "/api/export") {
    return json({ ok: true, saved: [{ path: "/data/output/غروب-ساحل-2x.png", filename: "غروب-ساحل-2x.png", dir: "/data/output", format: "png" }], errors: [] });
  }
  if (requestPath === "/api/settings") return json({ ok: true, settings, output_dir: "/data/output" });
  if (requestPath.startsWith("/api/item/") && method === "DELETE") return json({ ok: true, removed: "bb" });
  throw new Error(`unexpected path ${requestPath}`);
};

window.eval(app);
window.document.dispatchEvent(new window.Event("DOMContentLoaded"));
await new Promise((resolve) => setTimeout(resolve, 200));

const $ = (selector) => window.document.querySelector(selector);
const click = (element) => element.dispatchEvent(new window.Event("click"));
const change = (element) => element.dispatchEvent(new window.Event("change"));
const wait = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

check("فهرست تصاویر رندر شد", window.document.querySelectorAll(".item").length === 2);
check("شمارندهٔ فهرست فارسی است", $("#itemCount").textContent.includes("۲"), $("#itemCount").textContent);
check("نام فایل انتخاب‌شده", $("#fileName").textContent === "غروب-ساحل.jpg");
check("σ از API خوانده شد", $("#mSigma").textContent === "0.0026 → 0.0008", $("#mSigma").textContent);
check("زمان با ارقام فارسی", $("#mTime").textContent.includes("۰٫۴۱"), $("#mTime").textContent);
check("ابعاد خروجی", $("#mSize").textContent === "۹۶۰×۷۲۰", $("#mSize").textContent);
check("مسیر فایل ذخیره‌شده نمایش داده می‌شود", $("#mPath").textContent === "/data/output/sunset-2x.png");
check("روشنایی اندازه‌گیری‌شده", $("#mLight").textContent === "0.364 → 0.381", $("#mLight").textContent);
check("نشان دستگاه از API", $("#engineBadge").textContent === "classical-cpu");
check("دو پیش‌نما (پیش/پس)", window.document.querySelectorAll(".variant").length === 2);
check("دانلود برای آیتم پردازش‌شده فعال است", !$("#downloadBtn").disabled);

// پریست‌ها
check("پریست‌ها رندر شدند", window.document.querySelectorAll(".chip").length === 3);
check("پریست پیش‌فرض فعال است", $(".chip.active")?.dataset.preset === "photo");
check(
  "WebP در build بدون کدک غیرفعال است",
  $("#formatSelect option[value=webp]").disabled === true,
);

// آیتم دوم: بدون نتیجه
click(window.document.querySelectorAll(".item")[1]);
await wait(30);
check("آیتم بدون نتیجه انتخاب شد", $("#fileName").textContent === "پرتره.png");
check("سنجش برای آیتم خام پاک است", $("#mSigma").textContent === "—");
check("دانلود غیرفعال است", $("#downloadBtn").disabled === true);

// مقایسه بدون نتیجه نباید خطا بدهد و فقط پیام بدهد
click($("#compareBtn"));
check("مقایسه بدون نتیجه کرش نمی‌کند", $("#toast").hidden === false);

// پردازش آیتم دوم با تنظیمات پیش‌فرض
click($("#runBtn"));
await wait(200);
const processCall = calls.find((call) => call.path === "/api/process");
check("درخواست پردازش ارسال شد", Boolean(processCall));
if (processCall) {
  const payload = JSON.parse(processCall.body);
  check("پارامترهای پردازش درست‌اند", payload.options.denoise === "auto" && payload.options.deblock === "auto", processCall.body);
  check("مقیاس پیش‌فرض ۲ است", payload.options.scale === 2);
  check("نور خودکار پیش‌فرض روشن است", payload.options.auto_light === true && payload.options.auto_wb === true);
}

// حالت دستی پاک‌سازی
$("#restoreMode").value = "manual";
change($("#restoreMode"));
check("حالت دستی اسلایدر را فعال می‌کند", $("#denoiseRange").disabled === false);

$("#restoreMode").value = "off";
change($("#restoreMode"));
check("حالت خاموش مقدار را نشان می‌دهد", $("#denoiseValue").textContent === "خاموش");

// نور دستی: اسلایدر فعال می‌شود و مقدار در درخواست می‌رود
$("#lightMode").value = "manual";
change($("#lightMode"));
check("نور دستی اسلایدرها را فعال می‌کند", $("#shadowsRange").disabled === false);
$("#shadowsRange").value = "40";
$("#shadowsRange").dispatchEvent(new window.Event("input"));
check("مقدار سایه‌ها نمایش داده می‌شود", $("#shadowsValue").textContent === "۰٫۴", $("#shadowsValue").textContent);

click($("#runBtn"));
await wait(200);
const lightCall = calls.filter((call) => call.path === "/api/process").pop();
const lightPayload = lightCall ? JSON.parse(lightCall.body) : null;
check(
  "نور دستی در درخواست پردازش می‌رود",
  lightPayload && lightPayload.options.auto_light === false && Math.abs(lightPayload.options.light.shadows - 0.4) < 1e-6,
  lightCall?.body,
);

// پریست «عکس قدیمی»: تنظیمات را عوض می‌کند و ذخیره می‌شود
const oldChip = Array.from(window.document.querySelectorAll(".chip")).find((chip) => chip.dataset.preset === "old");
click(oldChip);
await wait(450); // debounce ذخیرهٔ تنظیمات ۳۵۰ms است
const activeChip = $(".chip.active");
check("پریست فعال شد", activeChip?.dataset.preset === "old", activeChip?.dataset.preset);
check("پریست نور خودکار را برمی‌گرداند", $("#lightMode").value === "auto");
check("پریست سایه‌ها را تنظیم می‌کند", $("#shadowsValue").textContent === "۰٫۳", $("#shadowsValue").textContent);
check(
  "تنظیمات به سرور ذخیره می‌شود",
  calls.some((call) => call.path === "/api/settings" && call.method === "POST"),
);

// صف دسته‌ای
click($("#processAllBtn"));
await wait(120);
const jobCall = calls.find((call) => call.path === "/api/jobs" && call.method === "POST");
check("درخواست صف ارسال شد", Boolean(jobCall));
if (jobCall) {
  const payload = JSON.parse(jobCall.body);
  check("هر دو تصویر در صف هستند", payload.ids.length === 2, jobCall.body);
  check("گزینه‌ها همراه صف می‌روند", Boolean(payload.options.scale >= 1));
}
await wait(700);
check("نوار صف دیده می‌شود", $("#jobBar").hidden === false);
check("درصد پیشرفت از سرور می‌آید", $("#jobPercent").textContent === "۴۲٪", $("#jobPercent").textContent);
check("دکمهٔ لغو فعال است", $("#cancelBtn").disabled === false);

// تب اطلاعات: هیچ کنترل بی‌کاری نمی‌ماند
click($("#tabInfo"));
check("تب اطلاعات باز می‌شود", $("#infoScroll").hidden === false && $("#inspectorScroll").hidden === true);
check("نسخهٔ هسته در تب اطلاعات", $("#infoVersion").textContent === "0.2.0-dev", $("#infoVersion").textContent);
check("وضعیت WebP/ncnn دیده می‌شود", $("#infoBuild").textContent.includes("webp"), $("#infoBuild").textContent);
check("مسیر atlas-cli نمایش داده می‌شود", $("#infoCli").textContent === "/x/atlas-cli", $("#infoCli").textContent);
click($("#tabSettings"));
check("بازگشت به تنظیمات", $("#infoScroll").hidden === true && $("#inspectorScroll").hidden === false);

// ذخیرهٔ نتیجه
click($("#downloadBtn"));
await wait(120);
const exportCall = calls.find((call) => call.path === "/api/export");
check("درخواست ذخیره ارسال شد", Boolean(exportCall));
if (exportCall) {
  const payload = JSON.parse(exportCall.body);
  check("شناسهٔ تصویر در ذخیره هست", Array.isArray(payload.ids) && payload.ids.length === 1, exportCall.body);
}
check("مسیر ذخیره در بازرس نوشته شد", $("#mPath").textContent.includes("/data/output/"), $("#mPath").textContent);

// حذف واقعی
click($("#removeBtn"));
await wait(120);
check(
  "حذف با DELETE به سرور می‌رود",
  calls.some((call) => call.method === "DELETE" && call.path.startsWith("/api/item/")),
);

if (failures.length) {
  console.error(`\n${failures.length} بررسی شکست خورد:`);
  for (const failure of failures) console.error(` - ${failure}`);
  process.exit(1);
}
console.log("\nهمهٔ بررسی‌های DOM سبز است.");

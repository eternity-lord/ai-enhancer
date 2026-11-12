// SPDX-License-Identifier: Apache-2.0
// تست DOM رابط کاربری بدون مرورگر (jsdom). وابستگی اختیاری و فقط برای توسعه است:
// ساخت خود پروژه و تست‌های C++/Python به این فایل وابسته نیستند.
//
//   cd desktop && npm install && npm run test:dom
//
// چه‌چیزی را قفل می‌کند: رندر فهرست تصاویر، انتخاب آیتم، نمایش اعداد سنجش از
// پاسخ API، فعال/غیرفعال شدن دانلود، و ارسال درخواست پردازش با پارامترهای درست.

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

const items = [
  {
    id: "aa",
    name: "غروب-ساحل.jpg",
    width: 480,
    height: 360,
    result: { width: 960, height: 720, scale: 2 },
    metrics,
    seconds: 0.41,
  },
  { id: "bb", name: "پرتره.png", width: 320, height: 240, result: null, metrics: null },
];

const calls = [];
const dom = new JSDOM(html, {
  runScripts: "outside-only",
  url: "http://localhost:8000/",
  pretendToBeVisual: true,
});
const { window } = dom;

window.fetch = async (requestPath, options = {}) => {
  calls.push({ path: requestPath, method: options.method || "GET", body: options.body });
  const json = (data) => ({ ok: true, status: 200, json: async () => data });
  if (requestPath === "/api/health") return json({ ok: true, cli: "/x/atlas-cli", items });
  if (requestPath === "/api/devices") return json({ ok: true, devices: [{ id: "classical-cpu" }] });
  if (requestPath === "/api/process") {
    return json({
      ok: true,
      item: { ...items[0], result: { width: 960, height: 720, scale: 2 }, metrics },
      metrics,
      steps: [],
      seconds: 0.41,
    });
  }
  throw new Error(`unexpected path ${requestPath}`);
};

window.eval(app);
window.document.dispatchEvent(new window.Event("DOMContentLoaded"));
await new Promise((resolve) => setTimeout(resolve, 120));

const $ = (selector) => window.document.querySelector(selector);
const click = (element) => element.dispatchEvent(new window.Event("click"));

check("فهرست تصاویر رندر شد", window.document.querySelectorAll(".item").length === 2);
check("شمارندهٔ فهرست فارسی است", $("#itemCount").textContent.includes("۲"), $("#itemCount").textContent);
check("نام فایل انتخاب‌شده", $("#fileName").textContent === "غروب-ساحل.jpg");
check("σ از API خوانده شد", $("#mSigma").textContent === "0.0026 → 0.0008", $("#mSigma").textContent);
check("زمان با ارقام فارسی", $("#mTime").textContent.includes("۰٫۴۱"), $("#mTime").textContent);
check("ابعاد خروجی", $("#mSize").textContent === "۹۶۰×۷۲۰", $("#mSize").textContent);
check("نشان دستگاه از API", $("#engineBadge").textContent === "classical-cpu");
check("دو پیش‌نما (پیش/پس)", window.document.querySelectorAll(".variant").length === 2);
check("دانلود برای آیتم پردازش‌شده فعال است", !$("#downloadBtn").disabled);

// آیتم دوم: بدون نتیجه
click(window.document.querySelectorAll(".item")[1]);
await new Promise((resolve) => setTimeout(resolve, 30));
check("آیتم بدون نتیجه انتخاب شد", $("#fileName").textContent === "پرتره.png");
check("سنجش برای آیتم خام پاک است", $("#mSigma").textContent === "—");
check("دانلود غیرفعال است", $("#downloadBtn").disabled === true);

// مقایسه بدون نتیجه نباید خطا بدهد و فقط پیام بدهد
click($("#compareBtn"));
check("مقایسه بدون نتیجه کرش نمی‌کند", $("#toast").hidden === false);

// پردازش آیتم دوم با تنظیمات پیش‌فرض
click($("#runBtn"));
await new Promise((resolve) => setTimeout(resolve, 120));
const processCall = calls.find((call) => call.path === "/api/process");
check("درخواست پردازش ارسال شد", Boolean(processCall));
if (processCall) {
  const payload = JSON.parse(processCall.body);
  check("پارامترهای پردازش درست‌اند", payload.denoise === "auto" && payload.deblock === "auto", processCall.body);
  check("مقیاس پیش‌فرض ۲ است", payload.scale === 2);
}

// حالت دستی: اسلایدرها فعال می‌شوند
$("#restoreMode").value = "manual";
$("#restoreMode").dispatchEvent(new window.Event("change"));
check("حالت دستی اسلایدر را فعال می‌کند", $("#denoiseRange").disabled === false);

$("#restoreMode").value = "off";
$("#restoreMode").dispatchEvent(new window.Event("change"));
check("حالت خاموش مقدار را نشان می‌دهد", $("#denoiseValue").textContent === "خاموش");

if (failures.length) {
  console.error(`\n${failures.length} بررسی شکست خورد:`);
  for (const failure of failures) console.error(` - ${failure}`);
  process.exit(1);
}
console.log("\nهمهٔ بررسی‌های DOM سبز است.");

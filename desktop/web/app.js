// SPDX-License-Identifier: Apache-2.0
// رابط کاربری Atlas — همهٔ پردازش واقعی است: هر «پردازش» یک یا چند فرایند
// atlas-cli را روی همین دستگاه اجرا می‌کند. هیچ عددی در UI هاردکد نشده؛
// اعداد سنجش (σ، بلوک، روشنایی، زمان) از خروجی JSON خود هسته می‌آیند و
// وضعیت صف از /api/jobs خوانده می‌شود، نه از حدس.

const $ = (selector) => document.querySelector(selector);
const $$ = (selector) => Array.from(document.querySelectorAll(selector));

// پوستهٔ اجرا: "web" (مرورگر)، "native" (پنجرهٔ سیستمی)، "glass" (پنجرهٔ بدون قاب).
const SHELL = new URLSearchParams(location.search).get("shell") || "web";
document.body.dataset.shell = SHELL;

const FA_DIGITS = "۰۱۲۳۴۵۶۷۸۹";
const fa = (value) =>
  String(value)
    .replace(/\d/g, (d) => FA_DIGITS[Number(d)])
    .replace(/\./g, "٫");

const state = {
  items: [],
  activeId: null,
  result: null, // { before, after, width, height, path }
  presets: [],
  caps: { formats: ["png", "jpg"], webp: false },
  outputDir: "",
  // تنظیمات پردازش
  scale: 2,
  quality: 92,
  format: "png",
  restoreMode: "auto", // auto | manual | off
  denoiseOn: true,
  deblockOn: true,
  denoisePct: 35,
  deblockPct: 55,
  lightMode: "auto", // auto | manual | off
  light: { exposure: 0, contrast: 0, saturation: 0, shadows: 0, highlights: 0, warmth: 0 },
  tile: 256,
  threads: 0,
  keepMetadata: false,
  pattern: "{name}-{scale}",
  preset: "",
  compare: true,
  split: 0.5,
  // صف
  job: null,
  jobTimer: 0,
  settingsTimer: 0,
};

/* ------------------------------------------------------------------ */
/* کمکی‌ها                                                             */
/* ------------------------------------------------------------------ */

function toast(message, isError = false) {
  const el = $("#toast");
  el.textContent = message;
  el.classList.toggle("error", isError);
  el.hidden = false;
  clearTimeout(el._timer);
  el._timer = setTimeout(() => (el.hidden = true), 5200);
}

async function api(path, options = {}) {
  const response = await fetch(path, options);
  let payload = null;
  try {
    payload = await response.json();
  } catch {
    /* پاسخ غیر JSON */
  }
  if (!response.ok) {
    throw new Error((payload && payload.error) || `خطای سرور (${response.status})`);
  }
  return payload;
}

function activeItem() {
  return state.items.find((item) => item.id === state.activeId) || null;
}

function setBusy(busy, text) {
  $("#busy").hidden = !busy;
  if (text) $("#busyText").textContent = text;
  $("#runBtn").disabled = busy || !state.activeId;
}

function shellApi() {
  return window.pywebview && window.pywebview.api ? window.pywebview.api : null;
}

/* ------------------------------------------------------------------ */
/* فهرست تصاویر                                                        */
/* ------------------------------------------------------------------ */

function jobEntryFor(id) {
  if (!state.job) return null;
  return (state.job.items || []).find((entry) => entry.id === id) || null;
}

function renderTree() {
  const tree = $("#tree");
  tree.innerHTML = "";
  if (state.items.length === 0) {
    tree.innerHTML =
      '<p class="empty-hint">هنوز تصویری اضافه نشده.<br />عکس را اینجا رها کنید یا «+» را بزنید.</p>';
    $("#itemCount").textContent = "۰ مورد";
    $("#processAllBtn").disabled = true;
    $("#exportAllBtn").disabled = true;
    return;
  }
  $("#itemCount").textContent = `${fa(state.items.length)} مورد`;
  $("#processAllBtn").disabled = false;
  $("#exportAllBtn").disabled = false;

  for (const item of state.items) {
    const entry = jobEntryFor(item.id);
    const row = document.createElement("div");
    row.className = "item" + (item.id === state.activeId ? " active" : "");
    row.tabIndex = 0;
    row.setAttribute("role", "button");
    row.dataset.id = item.id;
    if (entry && entry.status === "running") row.classList.add("running");

    const thumb = document.createElement("div");
    thumb.className = "thumb";
    thumb.style.backgroundImage = `url("/img/${item.id}/preview.png?v=preview")`;

    const meta = document.createElement("div");
    meta.className = "meta";
    const name = document.createElement("div");
    name.className = "name";
    name.textContent = item.name;
    const sub = document.createElement("div");
    sub.className = "sub";
    const dims = item.width && item.height ? `${fa(item.width)}×${fa(item.height)}` : "—";
    let statusText = item.result ? "پردازش‌شده" : "ویرایش‌شده";
    if (entry) {
      statusText =
        { queued: "در صف", running: `در حال پردازش ${fa(entry.progress || 0)}٪`,
          done: "ذخیره شد", error: "خطا", cancelled: "لغو شد" }[entry.status] || statusText;
    }
    sub.textContent = `${dims} · ${statusText}`;
    meta.append(name, sub);

    const dot = document.createElement("span");
    dot.className = "dot-state";
    if (entry) dot.classList.add(entry.status);
    else if (item.result) dot.classList.add("done");

    row.append(thumb, meta, dot);
    row.addEventListener("click", () => selectItem(item.id));
    row.addEventListener("keydown", (event) => {
      if (event.key === "Enter" || event.key === " ") {
        event.preventDefault();
        selectItem(item.id);
      }
    });
    tree.append(row);
  }
}

function selectItem(id) {
  const item = state.items.find((entry) => entry.id === id);
  if (!item) return;
  state.activeId = id;
  state.result = item.result
    ? {
        before: `/img/${id}/preview.png?v=preview`,
        after: `/img/${id}/result_preview.png?v=result&t=${Date.now()}`,
        width: item.result.width,
        height: item.result.height,
        path: item.result.path,
      }
    : { before: `/img/${id}/preview.png?v=preview` };

  $("#fileName").textContent = item.name;
  $("#fileSub").textContent = item.result ? "پردازش‌شده" : "ویرایش‌شده";
  $("#runBtn").disabled = false;

  $("#dropzone").hidden = true;
  $("#compare").hidden = false;
  $("#imgBefore").src = state.result.before;
  const after = $("#imgAfter");
  if (state.result.after) {
    after.src = state.result.after;
    after.hidden = false;
    enableCompare(true);
  } else {
    after.removeAttribute("src");
    after.hidden = true;
    enableCompare(false);
  }

  applyMetrics(item.metrics, item);
  renderTree();
  renderVariants();
  $("#downloadBtn").disabled = !state.result.after;
}

/* ------------------------------------------------------------------ */
/* سنجش                                                               */
/* ------------------------------------------------------------------ */

function applyMetrics(metrics, item) {
  const light = item && item.light ? item.light : null;
  if (!metrics) {
    $("#mSigma").textContent = "—";
    $("#mBlock").textContent = "—";
    $("#mLight").textContent = "—";
    $("#mTime").textContent = "—";
    $("#mSize").textContent = "—";
    $("#mPath").textContent = "—";
    $("#measureBadge").textContent = "پس از پردازش";
    return;
  }
  const f = (value) => (typeof value === "number" ? value.toFixed(4) : "—");
  $("#mSigma").textContent = `${f(metrics.sigma_before)} → ${f(metrics.sigma_after)}`;
  $("#mBlock").textContent = `${f(metrics.blockiness_before)} → ${f(metrics.blockiness_after)}`;
  $("#mLight").textContent = light
    ? `${(light.mean_before ?? 0).toFixed(3)} → ${(light.mean_after ?? 0).toFixed(3)}`
    : "بدون تغییر";
  const seconds = item?.seconds ?? (metrics.ms ? metrics.ms / 1000 : null);
  $("#mTime").textContent = seconds != null ? `${fa(seconds.toFixed(2))} ثانیه` : "—";
  const w = item?.result?.width;
  const h = item?.result?.height;
  $("#mSize").textContent = w && h ? `${fa(w)}×${fa(h)}` : "—";
  const path = item?.result?.path;
  $("#mPath").textContent = path || "—";
  $("#mPath").title = path || "";
  $("#measureBadge").textContent = "اندازه‌گیری‌شده";
}

/* ------------------------------------------------------------------ */
/* مقایسه                                                             */
/* ------------------------------------------------------------------ */

function setSplit(fraction) {
  const value = Math.min(1, Math.max(0, fraction));
  state.split = value;
  const percent = (value * 100).toFixed(2);
  document.documentElement.style.setProperty("--split", `${percent}%`);
  const after = $("#imgAfter");
  after.style.clipPath = `inset(0 0 0 ${percent}%)`;
  $("#handle").style.left = `${percent}%`;
  $("#handle").setAttribute("aria-valuenow", String(Math.round(value * 100)));
}

function enableCompare(enabled) {
  const handle = $("#handle");
  handle.hidden = !enabled || !state.compare;
  $("#imgAfter").hidden = !state.result?.after || !state.compare;
  $$(".tag").forEach((tag) => (tag.hidden = !enabled));
  $("#compareBtn").setAttribute("aria-pressed", String(state.compare));
  if (enabled) setSplit(state.split);
}

function bindCompareDrag() {
  const tray = $("#tray");
  const handle = $("#handle");
  let dragging = false;

  const updateFromEvent = (event) => {
    const rect = tray.getBoundingClientRect();
    const x = (event.touches ? event.touches[0].clientX : event.clientX) - rect.left;
    setSplit(x / rect.width);
  };

  handle.addEventListener("pointerdown", (event) => {
    dragging = true;
    handle.setPointerCapture(event.pointerId);
  });
  handle.addEventListener("pointermove", (event) => {
    if (dragging) updateFromEvent(event);
  });
  handle.addEventListener("pointerup", (event) => {
    dragging = false;
    handle.releasePointerCapture(event.pointerId);
  });
  handle.addEventListener("keydown", (event) => {
    if (event.key === "ArrowLeft") setSplit(state.split - 0.05);
    if (event.key === "ArrowRight") setSplit(state.split + 0.05);
  });
  tray.addEventListener("pointerdown", (event) => {
    if (event.target.closest(".handle")) return;
    if (!state.result?.after) return;
    updateFromEvent(event);
  });
}

/* ------------------------------------------------------------------ */
/* کنترل‌ها                                                           */
/* ------------------------------------------------------------------ */

function syncSliderFill(input) {
  const min = Number(input.min || 0);
  const max = Number(input.max || 100);
  const pct = ((Number(input.value) - min) / (max - min)) * 100;
  input.style.setProperty("--fill", `${pct}%`);
}

function restoreArg(kind) {
  if (state.restoreMode === "off") return "off";
  const on = kind === "denoise" ? state.denoiseOn : state.deblockOn;
  const pct = kind === "denoise" ? state.denoisePct : state.deblockPct;
  if (!on) return "off";
  if (state.restoreMode === "auto") return "auto";
  return String(pct);
}

function syncRestoreControls() {
  const manual = state.restoreMode === "manual";
  const off = state.restoreMode === "off";

  $("#denoiseRange").disabled = !manual || !state.denoiseOn;
  $("#deblockRange").disabled = !manual || !state.deblockOn;
  $("#denoiseSwitch").classList.toggle("on", state.denoiseOn && !off);
  $("#deblockSwitch").classList.toggle("on", state.deblockOn && !off);
  $("#denoiseSwitch").setAttribute("aria-checked", String(state.denoiseOn && !off));
  $("#deblockSwitch").setAttribute("aria-checked", String(state.deblockOn && !off));

  $("#denoiseValue").textContent =
    off || !state.denoiseOn ? "خاموش" : manual ? `${fa(state.denoisePct)}٪` : "خودکار";
  $("#deblockValue").textContent =
    off || !state.deblockOn ? "خاموش" : manual ? `${fa(state.deblockPct)}٪` : "خودکار";
}

// [کلید، اسلایدر، خروجی] — همهٔ اسلایدرها −۱۰۰..۱۰۰ هستند و در ۱/۱۰۰ ضرب می‌شوند.
const LIGHT_FIELDS = [
  ["exposure", "#exposureRange", "#exposureValue"],
  ["shadows", "#shadowsRange", "#shadowsValue"],
  ["highlights", "#highlightsRange", "#highlightsValue"],
  ["contrast", "#contrastRange", "#contrastValue"],
  ["saturation", "#saturationRange", "#saturationValue"],
  ["warmth", "#warmthRange", "#warmthValue"],
];

function syncLightControls() {
  const manual = state.lightMode === "manual";
  for (const [key, rangeSel, valueSel] of LIGHT_FIELDS) {
    const range = $(rangeSel);
    range.disabled = !manual;
    range.value = String(Math.round(state.light[key] * 100));
    syncSliderFill(range);
    $(valueSel).textContent = fa(state.light[key].toFixed(1));
  }
  $("#lightMode").value = state.lightMode;
}

function syncScaleControls() {
  $$("#scalePill .seg").forEach((button) => {
    button.classList.toggle("active", Number(button.dataset.scale) === state.scale);
  });
  $("#scaleSelect").value = String(state.scale);
}

function syncFormatControls() {
  const select = $("#formatSelect");
  const webp = select.querySelector('option[value="webp"]');
  if (webp) {
    webp.disabled = !state.caps.webp;
    webp.textContent = state.caps.webp ? "WebP" : "WebP (نیست)";
  }
  if (!state.caps.webp && state.format === "webp") state.format = "png";
  select.value = state.format;
  $("#formatBadge").textContent = state.format.toUpperCase();
}

function applySettingsToUi() {
  $("#qualitySelect").value = String(state.quality);
  $("#tileInput").value = String(state.tile);
  $("#threadsInput").value = String(state.threads);
  $("#patternInput").value = state.pattern;
  $("#outputDirInput").value = state.outputDir || "";
  $("#outputDirInput").placeholder = state.outputDirPath || "پوشهٔ پیش‌فرض برنامه";
  $("#restoreMode").value = state.restoreMode;
  syncRestoreControls();
  syncScaleControls();
  syncFormatControls();
  syncLightControls();
}

/* ------------------------------------------------------------------ */
/* پریست‌ها و تنظیمات                                                  */
/* ------------------------------------------------------------------ */

function renderPresets() {
  const host = $("#presetChips");
  host.innerHTML = "";
  for (const preset of state.presets) {
    const button = document.createElement("button");
    button.className = "chip" + (preset.id === state.preset ? " active" : "");
    button.textContent = preset.name;
    button.title = preset.note || "";
    button.dataset.preset = preset.id;
    button.addEventListener("click", () => applyPreset(preset));
    host.append(button);
  }
}

function applyPreset(preset) {
  const options = preset.options || {};
  state.preset = preset.id;
  if (options.scale) state.scale = Number(options.scale);
  if (options.quality) state.quality = Number(options.quality);
  if (options.format) state.format = options.format;
  if (options.denoise !== undefined) {
    const value = String(options.denoise);
    state.restoreMode = value === "off" ? "off" : value === "auto" ? "auto" : "manual";
    if (state.restoreMode === "manual") {
      state.denoisePct = Number(value);
      state.denoiseOn = true;
    }
    state.denoiseOn = value !== "off";
  }
  if (options.deblock !== undefined) {
    const value = String(options.deblock);
    if (value !== "auto" && value !== "off") state.deblockPct = Number(value);
    state.deblockOn = value !== "off";
  }
  state.lightMode = options.auto_light || options.auto_wb ? "auto" : "manual";
  if (options.light) {
    for (const key of Object.keys(state.light)) {
      if (typeof options.light[key] === "number") state.light[key] = options.light[key];
    }
  } else if (!options.auto_light) {
    for (const key of Object.keys(state.light)) state.light[key] = 0;
  }
  $("#presetBadge").textContent = preset.name;
  applySettingsToUi();
  renderPresets();
  saveSettings();
  toast(`پریست «${preset.name}» اعمال شد — ${preset.note || ""}`.trim());
}

function saveSettings() {
  clearTimeout(state.settingsTimer);
  state.settingsTimer = setTimeout(() => {
    const payload = {
      scale: state.scale,
      quality: state.quality,
      format: state.format,
      denoise: restoreArg("denoise"),
      deblock: restoreArg("deblock"),
      restore_mode: state.restoreMode,
      denoise_on: state.denoiseOn,
      deblock_on: state.deblockOn,
      denoise_pct: state.denoisePct,
      deblock_pct: state.deblockPct,
      light_mode: state.lightMode,
      light: state.light,
      auto_light: state.lightMode === "auto",
      auto_wb: state.lightMode === "auto",
      tile: state.tile,
      threads: state.threads,
      keep_metadata: state.keepMetadata,
      pattern: state.pattern,
      output_dir: state.outputDir,
      preset: state.preset,
    };
    api("/api/settings", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload),
    }).catch(() => {});
  }, 350);
}

function switchTab(which) {
  const info = which === "info";
  $("#infoScroll").hidden = !info;
  $("#inspectorScroll").hidden = info;
  $("#tabInfo").classList.toggle("active", info);
  $("#tabSettings").classList.toggle("active", !info);
  if (info) renderInfo();
}

function renderInfo() {
  const caps = state.caps || {};
  $("#infoVersion").textContent = caps.version || "—";
  $("#infoEngine").textContent = $("#engineBadge").textContent || "—";
  $("#infoCli").textContent = state.cliPath || "—";
  $("#infoCli").title = state.cliPath || "";
  $("#infoBuild").textContent = `webp ${caps.webp ? "روشن" : "خاموش"} · ncnn ${
    caps.ncnn ? "روشن" : "خاموش"
  }`;
  $("#infoThreads").textContent = fa(caps.threads ?? "—");
  $("#infoOutput").textContent = state.outputDir || state.outputDirPath || "—";
  $("#infoOutput").title = state.outputDir || state.outputDirPath || "";
  $("#infoData").textContent = state.dataDir || "—";
  $("#infoData").title = state.dataDir || "";
}

function applyStoredSettings(settings) {
  if (!settings) return;
  if (settings.scale) state.scale = Number(settings.scale);
  if (settings.quality) state.quality = Number(settings.quality);
  if (settings.format) state.format = settings.format;
  if (settings.restore_mode) state.restoreMode = settings.restore_mode;
  if (typeof settings.denoise_on === "boolean") state.denoiseOn = settings.denoise_on;
  if (typeof settings.deblock_on === "boolean") state.deblockOn = settings.deblock_on;
  if (settings.denoise_pct) state.denoisePct = Number(settings.denoise_pct);
  if (settings.deblock_pct) state.deblockPct = Number(settings.deblock_pct);
  if (settings.light_mode) state.lightMode = settings.light_mode;
  if (settings.light) {
    for (const key of Object.keys(state.light)) {
      if (typeof settings.light[key] === "number") state.light[key] = settings.light[key];
    }
  }
  if (settings.tile) state.tile = Number(settings.tile);
  if (typeof settings.threads === "number") state.threads = settings.threads;
  if (typeof settings.keep_metadata === "boolean") state.keepMetadata = settings.keep_metadata;
  if (settings.pattern) state.pattern = settings.pattern;
  if (typeof settings.output_dir === "string") state.outputDir = settings.output_dir;
  if (settings.preset) state.preset = settings.preset;
}

function collectOptions() {
  const auto = state.lightMode === "auto";
  const off = state.lightMode === "off";
  const light = {};
  for (const key of Object.keys(state.light)) {
    light[key] = off ? 0 : Number(state.light[key]);
  }
  return {
    scale: state.scale,
    quality: state.quality,
    format: state.format,
    denoise: restoreArg("denoise"),
    deblock: restoreArg("deblock"),
    light,
    auto_light: auto,
    auto_wb: auto,
    light_mode: state.lightMode,
    tile: state.tile,
    threads: state.threads,
    keep_metadata: state.keepMetadata,
    pattern: state.pattern,
    output_dir: state.outputDir,
    preset: state.preset,
    export: true,
  };
}

/* ------------------------------------------------------------------ */
/* آپلود                                                              */
/* ------------------------------------------------------------------ */

async function uploadFiles(files) {
  const list = Array.from(files || []).filter(Boolean);
  if (!list.length) return;
  let last = null;
  for (const file of list) {
    try {
      setBusy(true, `بارگذاری ${file.name}…`);
      const payload = await api(`/api/upload?name=${encodeURIComponent(file.name)}`, {
        method: "POST",
        body: file,
      });
      state.items = [payload.item, ...state.items];
      last = payload.item;
    } catch (error) {
      toast(`${file.name}: ${error.message}`, true);
    }
  }
  setBusy(false);
  renderTree();
  if (last) selectItem(last.id);
  if (list.length > 1) toast(`${fa(list.length)} تصویر اضافه شد`);
}

/* ------------------------------------------------------------------ */
/* پردازش                                                             */
/* ------------------------------------------------------------------ */

async function processCurrent() {
  const item = activeItem();
  if (!item) return;
  const started = performance.now();
  try {
    setBusy(true, "در حال پردازش…");
    const payload = await api("/api/process", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ id: item.id, options: collectOptions() }),
    });
    const updated = payload.item;
    state.items = state.items.map((entry) => (entry.id === updated.id ? updated : entry));
    selectItem(updated.id);
    const ms = Math.round(performance.now() - started);
    toast(
      `آماده شد · ${fa(updated.result.width)}×${fa(updated.result.height)} · ${fa(
        (ms / 1000).toFixed(2),
      )} ثانیه${updated.result.path ? ` · ${updated.result.filename}` : ""}`,
    );
  } catch (error) {
    toast(error.message, true);
  } finally {
    setBusy(false);
  }
}

async function processAll() {
  if (!state.items.length) return;
  try {
    const payload = await api("/api/jobs", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ ids: state.items.map((item) => item.id), options: collectOptions() }),
    });
    state.job = payload.job;
    renderJob();
    schedulePoll();
    toast(`صف با ${fa(state.job.items.length)} تصویر شروع شد`);
  } catch (error) {
    toast(error.message, true);
  }
}

function renderJob() {
  const job = state.job;
  const bar = $("#jobBar");
  if (!job) {
    bar.hidden = true;
    renderTree();
    return;
  }
  bar.hidden = false;
  const percent = Math.round(job.progress || 0);
  $("#jobPercent").textContent = `${fa(percent)}٪`;
  $("#jobFill").style.width = `${percent}%`;
  const running = job.status === "queued" || job.status === "running";
  const label = job.current ? `${job.current} — ${job.message || ""}` : job.message || job.status;
  $("#jobText").textContent = label;
  $("#cancelBtn").disabled = !running;
  $("#cancelBtn").hidden = !running;
  renderTree();
}

function schedulePoll() {
  clearTimeout(state.jobTimer);
  state.jobTimer = setTimeout(pollJob, 600);
}

async function pollJob() {
  if (!state.job) return;
  try {
    const payload = await api(`/api/jobs/${state.job.id}`);
    state.job = payload.job;
    renderJob();
    if (payload.job.status === "queued" || payload.job.status === "running") {
      schedulePoll();
    } else {
      const summary = payload.job.summary || {};
      const kind = payload.job.status;
      const message =
        kind === "done"
          ? `تمام شد · ${fa(summary.done || 0)} تصویر در ${fa(summary.seconds || 0)} ثانیه`
          : kind === "cancelled"
            ? "صف لغو شد"
            : `پایان با خطا · ${fa(summary.errors || 0)} فایل مشکل داشت`;
      toast(message, kind === "error");
      await refreshState();
    }
  } catch (error) {
    toast(`وضعیت صف خوانده نشد: ${error.message}`, true);
  }
}

async function cancelJob() {
  if (!state.job) return;
  try {
    await api(`/api/jobs/${state.job.id}/cancel`, { method: "POST", body: "{}" });
    toast("در حال لغو…");
  } catch (error) {
    toast(error.message, true);
  }
}

/* ------------------------------------------------------------------ */
/* ذخیره                                                              */
/* ------------------------------------------------------------------ */

function flashSaved(saved, errors) {
  if (errors && errors.length) {
    toast(errors[0].error || "ذخیره شکست خورد", true);
    return;
  }
  const first = saved && saved[0];
  if (!first) {
    toast("چیزی برای ذخیره نبود", true);
    return;
  }
  $("#mPath").textContent = first.path;
  $("#mPath").title = first.path;
  const extra = saved.length > 1 ? ` (+${fa(saved.length - 1)})` : "";
  toast(`ذخیره شد: ${first.filename}${extra}`);
}

async function exportItems(ids) {
  if (!ids.length) return toast("اول پردازش کنید", true);
  try {
    const payload = await api("/api/export", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ ids, options: collectOptions() }),
    });
    flashSaved(payload.saved, payload.errors);
  } catch (error) {
    toast(error.message, true);
  }
}

/* ------------------------------------------------------------------ */
/* حذف                                                                */
/* ------------------------------------------------------------------ */

async function removeActive() {
  const item = activeItem();
  if (!item) return;
  try {
    await api(`/api/item/${item.id}`, { method: "DELETE" });
    state.items = state.items.filter((entry) => entry.id !== item.id);
    state.activeId = state.items[0]?.id ?? null;
    if (state.activeId) selectItem(state.activeId);
    else resetView();
    toast(`«${item.name}» حذف شد`);
  } catch (error) {
    toast(error.message, true);
  }
}

/* ------------------------------------------------------------------ */
/* پوشه‌ها (فقط در پنجرهٔ نیتیو)                                       */
/* ------------------------------------------------------------------ */

async function chooseFolder() {
  const shell = shellApi();
  if (!shell || typeof shell.choose_folder !== "function") {
    toast("در حالت مرورگری مسیر پوشه را دستی بنویسید");
    return;
  }
  try {
    const folder = await shell.choose_folder();
    if (folder) {
      state.outputDir = folder;
      $("#outputDirInput").value = folder;
      saveSettings();
    }
  } catch (error) {
    toast(error.message, true);
  }
}

async function openFolder() {
  const shell = shellApi();
  const target = state.outputDir || state.outputDirPath;
  if (!shell || typeof shell.open_folder !== "function") {
    toast(target || "مسیر پوشه نامشخص است");
    return;
  }
  try {
    const result = await shell.open_folder(target || "");
    if (result && result.error) toast(result.error, true);
  } catch (error) {
    toast(error.message, true);
  }
}

/* ------------------------------------------------------------------ */
/* پیش‌نماها                                                           */
/* ------------------------------------------------------------------ */

function renderVariants() {
  const strip = $("#variants");
  strip.innerHTML = "";
  if (!state.activeId) return;
  const item = activeItem();
  const before = document.createElement("button");
  before.className = "variant" + (!state.result?.after ? " active" : "");
  before.title = "پیش از پردازش";
  before.style.backgroundImage = `url("/img/${item.id}/preview.png?v=preview")`;
  before.addEventListener("click", () => {
    state.compare = false;
    enableCompare(!!state.result?.after);
    setView("before");
  });

  strip.append(before);
  if (state.result?.after) {
    const after = document.createElement("button");
    after.className = "variant active";
    after.title = "پس از پردازش";
    after.style.backgroundImage = `url("${state.result.after}")`;
    after.addEventListener("click", () => setView("after"));
    strip.append(after);
  }
}

function setView(which) {
  const hasAfter = !!state.result?.after;
  if (!hasAfter) return;
  const after = $("#imgAfter");
  if (which === "before") {
    after.style.clipPath = "inset(0 0 0 100%)";
    $("#handle").hidden = true;
  } else if (which === "after") {
    after.style.clipPath = "none";
    $("#handle").hidden = true;
  } else {
    setSplit(state.split);
    $("#handle").hidden = false;
  }
}

/* ------------------------------------------------------------------ */
/* کنترل پنجرهٔ نیتیو (فقط در حالت بدون قاب)                          */
/* ------------------------------------------------------------------ */

function bindWindowControls() {
  const actions = { red: "close_window", yellow: "minimize_window", green: "toggle_maximize" };
  for (const [color, method] of Object.entries(actions)) {
    const dot = document.querySelector(`.dot.${color}`);
    if (!dot) continue;
    dot.setAttribute("role", "button");
    dot.setAttribute("tabindex", "0");
    dot.addEventListener("click", () => {
      const shell = shellApi();
      if (shell && typeof shell[method] === "function") shell[method]();
    });
  }

  // کشیدن پنجره از نوار بالای پنل‌ها (بدون تداخل با دکمه‌ها و ورودی‌ها).
  let dragging = false;
  let lastX = 0;
  let lastY = 0;
  const zones = [document.querySelector(".sidebar-head"), document.querySelector(".stage-head")];

  for (const zone of zones) {
    if (!zone) continue;
    zone.addEventListener("pointerdown", (event) => {
      if (event.target.closest("button, select, input, label, a")) return;
      dragging = true;
      lastX = event.screenX;
      lastY = event.screenY;
    });
  }

  window.addEventListener("pointermove", (event) => {
    if (!dragging) return;
    const shell = shellApi();
    if (!shell || typeof shell.move_window !== "function") return;
    const dx = event.screenX - lastX;
    const dy = event.screenY - lastY;
    if (dx === 0 && dy === 0) return;
    lastX = event.screenX;
    lastY = event.screenY;
    shell.move_window(dx, dy);
  });

  window.addEventListener("pointerup", () => (dragging = false));
  window.addEventListener("blur", () => (dragging = false));
}

/* ------------------------------------------------------------------ */
/* رویدادها                                                           */
/* ------------------------------------------------------------------ */

function bindEvents() {
  $("#pickBtn").addEventListener("click", () => $("#fileInput").click());
  $("#addBtn").addEventListener("click", () => $("#fileInput").click());
  $("#fileInput").addEventListener("change", (event) => {
    uploadFiles(event.target.files);
    event.target.value = "";
  });

  $("#removeBtn").addEventListener("click", removeActive);
  $("#collapseBtn").addEventListener("click", () => document.body.classList.toggle("compact"));

  $("#runBtn").addEventListener("click", processCurrent);
  $("#processAllBtn").addEventListener("click", processAll);
  $("#cancelBtn").addEventListener("click", cancelJob);
  $("#downloadBtn").addEventListener("click", () => {
    const item = activeItem();
    if (item) exportItems([item.id]);
  });
  $("#exportAllBtn").addEventListener("click", () =>
    exportItems(state.items.filter((item) => item.result).map((item) => item.id)),
  );

  $("#tabInfo").addEventListener("click", () => switchTab("info"));
  $("#tabSettings").addEventListener("click", () => switchTab("settings"));

  $("#pickFolderBtn").addEventListener("click", chooseFolder);
  $("#openFolderBtn").addEventListener("click", openFolder);

  $("#compareBtn").addEventListener("click", () => {
    if (!state.result?.after) {
      toast("اول «پردازش» را بزنید تا مقایسه ممکن شود");
      return;
    }
    state.compare = !state.compare;
    enableCompare(true);
    if (!state.compare) {
      $("#imgAfter").style.clipPath = "none";
      $("#handle").hidden = true;
    } else {
      setView("compare");
    }
  });

  $$("#scalePill .seg").forEach((button) => {
    button.addEventListener("click", () => {
      state.scale = Number(button.dataset.scale);
      syncScaleControls();
      saveSettings();
    });
  });

  $("#scaleSelect").addEventListener("change", (event) => {
    state.scale = Number(event.target.value);
    syncScaleControls();
    saveSettings();
  });

  $("#formatSelect").addEventListener("change", (event) => {
    state.format = event.target.value;
    syncFormatControls();
    saveSettings();
  });

  $("#qualitySelect").addEventListener("change", (event) => {
    state.quality = Number(event.target.value);
    saveSettings();
  });

  $("#tileInput").addEventListener("change", (event) => {
    state.tile = Math.max(32, Math.min(1024, Number(event.target.value) || 256));
    event.target.value = String(state.tile);
    saveSettings();
  });

  $("#threadsInput").addEventListener("change", (event) => {
    state.threads = Math.max(0, Math.min(128, Number(event.target.value) || 0));
    event.target.value = String(state.threads);
    saveSettings();
  });

  $("#patternInput").addEventListener("change", (event) => {
    state.pattern = event.target.value.trim() || "{name}-{scale}";
    event.target.value = state.pattern;
    saveSettings();
  });

  $("#outputDirInput").addEventListener("change", (event) => {
    state.outputDir = event.target.value.trim();
    saveSettings();
  });

  $("#restoreMode").addEventListener("change", (event) => {
    state.restoreMode = event.target.value;
    syncRestoreControls();
    saveSettings();
  });

  $("#lightMode").addEventListener("change", (event) => {
    state.lightMode = event.target.value;
    syncLightControls();
    saveSettings();
  });

  $("#denoiseSwitch").addEventListener("click", () => {
    state.denoiseOn = !state.denoiseOn;
    syncRestoreControls();
    saveSettings();
  });
  $("#deblockSwitch").addEventListener("click", () => {
    state.deblockOn = !state.deblockOn;
    syncRestoreControls();
    saveSettings();
  });

  const denoiseRange = $("#denoiseRange");
  denoiseRange.value = String(state.denoisePct);
  denoiseRange.addEventListener("input", () => {
    state.denoisePct = Number(denoiseRange.value);
    syncSliderFill(denoiseRange);
    syncRestoreControls();
    saveSettings();
  });
  const deblockRange = $("#deblockRange");
  deblockRange.value = String(state.deblockPct);
  deblockRange.addEventListener("input", () => {
    state.deblockPct = Number(deblockRange.value);
    syncSliderFill(deblockRange);
    syncRestoreControls();
    saveSettings();
  });

  for (const [key, rangeSel] of LIGHT_FIELDS) {
    const range = $(rangeSel);
    range.addEventListener("input", () => {
      state.light[key] = Number(range.value) / 100;
      syncSliderFill(range);
      syncLightControls();
      saveSettings();
    });
  }

  // کشیدن فایل روی کل پنجره
  const tray = $("#tray");
  ["dragenter", "dragover"].forEach((type) =>
    tray.addEventListener(type, (event) => {
      event.preventDefault();
      tray.classList.add("drag");
    }),
  );
  ["dragleave", "drop"].forEach((type) =>
    tray.addEventListener(type, (event) => {
      event.preventDefault();
      tray.classList.remove("drag");
    }),
  );
  tray.addEventListener("drop", (event) => uploadFiles(event.dataTransfer?.files));

  bindCompareDrag();

  document.addEventListener("keydown", (event) => {
    if ((event.ctrlKey || event.metaKey) && event.key === "Enter") {
      event.preventDefault();
      if (event.shiftKey) processAll();
      else processCurrent();
    }
    if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "s") {
      event.preventDefault();
      const item = activeItem();
      if (item) exportItems([item.id]);
    }
  });

  window.addEventListener("resize", () => {
    if (state.result?.after) setView(state.compare ? "compare" : "after");
  });
}

function resetView() {
  state.result = null;
  state.activeId = null;
  $("#fileName").textContent = "بدون تصویر";
  $("#fileSub").textContent = "ویرایش‌شده";
  $("#compare").hidden = true;
  $("#dropzone").hidden = false;
  $("#runBtn").disabled = true;
  $("#downloadBtn").disabled = true;
  applyMetrics(null, null);
  renderTree();
  renderVariants();
}

/* ------------------------------------------------------------------ */
/* وضعیت                                                              */
/* ------------------------------------------------------------------ */

async function refreshState() {
  const payload = await api("/api/health");
  state.items = payload.items || [];
  state.caps = payload.capabilities || state.caps;
  if (payload.output_dir) state.outputDirPath = payload.output_dir;
  const active = state.items.find((item) => item.id === state.activeId);
  if (active) selectItem(active.id);
  else if (state.items.length) selectItem(state.items[0].id);
  else resetView();
  if (payload.jobs && payload.jobs.length && !state.job) {
    state.job = payload.jobs[0];
    renderJob();
    if (state.job.status === "queued" || state.job.status === "running") schedulePoll();
  }
  return payload;
}

/* ------------------------------------------------------------------ */
/* راه‌اندازی                                                          */
/* ------------------------------------------------------------------ */

let initialized = false;

async function init() {
  if (initialized) return; // DOMContentLoaded در برخی محیط‌ها دوبار می‌رسد
  initialized = true;

  bindEvents();
  if (SHELL === "glass") bindWindowControls();

  try {
    const health = await api("/api/health");
    state.caps = health.capabilities || state.caps;
    state.outputDirPath = health.output_dir || "";
    state.cliPath = health.cli || "";
    state.dataDir = health.data_dir || "";
    applyStoredSettings(health.settings);

    const capabilities = await api("/api/capabilities");
    state.presets = capabilities.presets || [];
    state.caps = capabilities.capabilities || state.caps;
    state.outputDirPath = capabilities.output_dir || state.outputDirPath;

    state.items = health.items || [];
    applySettingsToUi();
    renderPresets();
    renderTree();

    api("/api/devices")
      .then((payload) => {
        const devices = payload.devices || [];
        if (devices.length) $("#engineBadge").textContent = devices[0].id || "classical";
      })
      .catch(() => {});

    if (!health.ok) {
      toast("atlas-cli پیدا نشد؛ پردازش کار نمی‌کند (راهنما: desktop/README.md)", true);
    }
    if (state.items.length) selectItem(state.items[0].id);
    if (health.jobs && health.jobs.length) {
      state.job = health.jobs[0];
      renderJob();
      if (state.job.status === "queued" || state.job.status === "running") schedulePoll();
    }
    $("#engineBadge").title = `atlas-cli ${state.caps.version || ""}`;
    renderInfo();
  } catch (error) {
    toast(`اتصال به سرور محلی برقرار نشد: ${error.message}`, true);
  }
}

document.addEventListener("DOMContentLoaded", init);

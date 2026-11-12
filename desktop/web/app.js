// SPDX-License-Identifier: Apache-2.0
// رابط کاربری Atlas — همهٔ پردازش واقعی است: هر کلیک «پردازش» یک فرایند
// atlas-cli را روی همین دستگاه اجرا می‌کند. هیچ عددی در UI هاردکد نشده؛
// اعداد سنجش (σ، بلوک، زمان) از خروجی JSON خود هسته می‌آیند.

const $ = (sel) => document.querySelector(sel);

// پوستهٔ اجرا: "web" (مرورگر)، "native" (پنجرهٔ سیستمی)، "glass" (پنجرهٔ بدون قاب).
const SHELL = new URLSearchParams(location.search).get("shell") || "web";
document.body.dataset.shell = SHELL;

const FA_DIGITS = "۰۱۲۳۴۵۶۷۸۹";
const fa = (value) =>
  String(value)
    .replace(/\d/g, (d) => FA_DIGITS[Number(d)])
    .replace(/\./g, "٫"); // جداکنندهٔ اعشار فارسی

const state = {
  items: [],
  activeId: null,
  result: null, // { before, after, width, height }
  scale: 2,
  quality: 92,
  restoreMode: "auto", // auto | manual | off
  denoiseOn: true,
  deblockOn: true,
  denoisePct: 35,
  deblockPct: 55,
  compare: true,
  split: 0.5,
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
  el._timer = setTimeout(() => (el.hidden = true), 4200);
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
  const overlay = $("#busy");
  overlay.hidden = !busy;
  if (text) $("#busyText").textContent = text;
  $("#runBtn").disabled = busy || !state.activeId;
}

/* ------------------------------------------------------------------ */
/* فهرست تصاویر                                                        */
/* ------------------------------------------------------------------ */

function renderTree() {
  const tree = $("#tree");
  tree.innerHTML = "";
  if (state.items.length === 0) {
    tree.innerHTML =
      '<p class="empty-hint">هنوز تصویری اضافه نشده.<br />عکس را اینجا رها کنید یا «+» را بزنید.</p>';
    $("#itemCount").textContent = "۰ مورد";
    return;
  }
  $("#itemCount").textContent = `${fa(state.items.length)} مورد`;

  for (const item of state.items) {
    const row = document.createElement("div");
    row.className = "item" + (item.id === state.activeId ? " active" : "");
    row.tabIndex = 0;
    row.setAttribute("role", "button");

    const thumb = document.createElement("div");
    thumb.className = "thumb";
    thumb.style.backgroundImage = `url("${item.id ? `/img/${item.id}/preview.png?v=preview` : ""}")`;

    const meta = document.createElement("div");
    meta.className = "meta";
    const name = document.createElement("div");
    name.className = "name";
    name.textContent = item.name;
    const sub = document.createElement("div");
    sub.className = "sub";
    const dims = item.width && item.height ? `${fa(item.width)}×${fa(item.height)}` : "—";
    sub.textContent = item.result ? `${dims} · پردازش‌شده` : dims;
    meta.append(name, sub);

    const dot = document.createElement("span");
    dot.className = "dot-state";

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
  if (!metrics) {
    $("#mSigma").textContent = "—";
    $("#mBlock").textContent = "—";
    $("#mTime").textContent = "—";
    $("#mSize").textContent = "—";
    $("#measureBadge").textContent = "پس از پردازش";
    return;
  }
  const f = (value) => (typeof value === "number" ? value.toFixed(4) : "—");
  $("#mSigma").textContent = `${f(metrics.sigma_before)} → ${f(metrics.sigma_after)}`;
  $("#mBlock").textContent = `${f(metrics.blockiness_before)} → ${f(metrics.blockiness_after)}`;
  const seconds = item?.seconds ?? (metrics.ms ? metrics.ms / 1000 : null);
  $("#mTime").textContent = seconds != null ? `${fa(seconds.toFixed(2))} ثانیه` : "—";
  const w = item?.result?.width;
  const h = item?.result?.height;
  $("#mSize").textContent = w && h ? `${fa(w)}×${fa(h)}` : "—";
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
  document.querySelectorAll(".tag").forEach((tag) => (tag.hidden = !enabled));
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

function syncRestoreControls() {
  const manual = state.restoreMode === "manual";
  const off = state.restoreMode === "off";

  $("#denoiseRange").disabled = !manual || !state.denoiseOn;
  $("#deblockRange").disabled = !manual || !state.deblockOn;
  $("#denoiseSwitch").classList.toggle("on", state.denoiseOn && !off);
  $("#deblockSwitch").classList.toggle("on", state.deblockOn && !off);
  $("#denoiseSwitch").setAttribute("aria-checked", String(state.denoiseOn && !off));
  $("#deblockSwitch").setAttribute("aria-checked", String(state.deblockOn && !off));

  $("#denoiseValue").textContent = off || !state.denoiseOn
    ? "خاموش"
    : manual
      ? `${fa(state.denoisePct)}٪`
      : "خودکار";
  $("#deblockValue").textContent = off || !state.deblockOn
    ? "خاموش"
    : manual
      ? `${fa(state.deblockPct)}٪`
      : "خودکار";
}

function currentRestoreArgs() {
  if (state.restoreMode === "off") return { denoise: "off", deblock: "off" };
  if (state.restoreMode === "auto") {
    return {
      denoise: state.denoiseOn ? "auto" : "off",
      deblock: state.deblockOn ? "auto" : "off",
    };
  }
  return {
    denoise: state.denoiseOn ? String(state.denoisePct) : "off",
    deblock: state.deblockOn ? String(state.deblockPct) : "off",
  };
}

/* ------------------------------------------------------------------ */
/* آپلود و پردازش                                                     */
/* ------------------------------------------------------------------ */

async function uploadFile(file) {
  if (!file) return;
  const MAX = 80 * 1024 * 1024;
  if (file.size > MAX) {
    toast("فایل بزرگ‌تر از ۸۰ مگابایت است", true);
    return;
  }
  setBusy(true, "در حال خواندن فایل…");
  try {
    const response = await fetch(`/api/upload?name=${encodeURIComponent(file.name)}`, {
      method: "POST",
      headers: { "Content-Type": "application/octet-stream" },
      body: file,
    });
    const payload = await response.json();
    if (!response.ok) throw new Error(payload.error || "آپلود ناموفق بود");
    state.items = [payload.item, ...state.items.filter((i) => i.id !== payload.item.id)];
    renderTree();
    selectItem(payload.item.id);
    toast(`«${payload.item.name}» اضافه شد`);
  } catch (error) {
    toast(error.message, true);
  } finally {
    setBusy(false);
  }
}

async function runProcess() {
  const item = activeItem();
  if (!item) return;
  const { denoise, deblock } = currentRestoreArgs();
  setBusy(true, "پاک‌سازی نویز و بلوک…");
  const started = performance.now();
  try {
    const payload = await api("/api/process", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        id: item.id,
        denoise,
        deblock,
        scale: state.scale,
        quality: state.quality,
      }),
    });

    item.result = payload.item.result;
    item.metrics = payload.metrics;
    item.seconds = payload.seconds;
    item.steps = payload.steps;

    state.result = {
      before: `/img/${item.id}/preview.png?v=preview`,
      after: `/img/${item.id}/result_preview.png?v=result&t=${Date.now()}`,
      width: payload.item.result.width,
      height: payload.item.result.height,
    };
    $("#imgBefore").src = state.result.before;
    const after = $("#imgAfter");
    after.src = state.result.after;
    after.hidden = false;
    enableCompare(true);
    $("#downloadBtn").disabled = false;
    $("#fileSub").textContent = "پردازش‌شده";
    applyMetrics(payload.metrics, item);
    renderTree();
    renderVariants();

    const ms = Math.round(performance.now() - started);
    toast(`آماده شد · ${fa(payload.item.result.width)}×${fa(payload.item.result.height)} · ${fa((ms / 1000).toFixed(2))} ثانیه`);
  } catch (error) {
    toast(error.message, true);
  } finally {
    setBusy(false);
  }
}

function downloadResult() {
  const item = activeItem();
  if (!item?.result) return;
  const suffix = item.result.scale > 1 ? `-${item.result.scale}x` : "-clean";
  const base = item.name.replace(/\.[^.]+$/, "") || "atlas";
  const link = document.createElement("a");
  link.href = `/img/${item.id}/result_preview.png?v=result&t=${Date.now()}`;
  link.download = `${base}${suffix}.png`;
  document.body.append(link);
  link.click();
  link.remove();
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

function shellApi() {
  return window.pywebview && window.pywebview.api ? window.pywebview.api : null;
}

function bindWindowControls() {
  const actions = { red: "close_window", yellow: "minimize_window", green: "toggle_maximize" };
  for (const [color, method] of Object.entries(actions)) {
    const dot = document.querySelector(`.dot.${color}`);
    if (!dot) continue;
    dot.setAttribute("role", "button");
    dot.setAttribute("tabindex", "0");
    dot.addEventListener("click", () => {
      const api = shellApi();
      if (api && typeof api[method] === "function") api[method]();
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
    const api = shellApi();
    if (!api || typeof api.move_window !== "function") return;
    const dx = event.screenX - lastX;
    const dy = event.screenY - lastY;
    if (dx === 0 && dy === 0) return;
    lastX = event.screenX;
    lastY = event.screenY;
    api.move_window(dx, dy);
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
    uploadFile(event.target.files?.[0]);
    event.target.value = "";
  });

  $("#removeBtn").addEventListener("click", () => {
    if (!state.activeId) return;
    const index = state.items.findIndex((i) => i.id === state.activeId);
    if (index === -1) return;
    const [removed] = state.items.splice(index, 1);
    state.activeId = state.items[0]?.id ?? null;
    if (state.activeId) {
      selectItem(state.activeId);
    } else {
      resetView();
    }
    toast(`«${removed.name}» از فهرست حذف شد`);
  });

  $("#collapseBtn").addEventListener("click", () => {
    document.body.classList.toggle("compact");
  });

  $("#runBtn").addEventListener("click", runProcess);
  $("#downloadBtn").addEventListener("click", downloadResult);

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

  document.querySelectorAll("#scalePill .seg").forEach((button) => {
    button.addEventListener("click", () => {
      document.querySelectorAll("#scalePill .seg").forEach((b) => b.classList.remove("active"));
      button.classList.add("active");
      state.scale = Number(button.dataset.scale);
      $("#scaleSelect").value = String(state.scale);
    });
  });

  $("#scaleSelect").addEventListener("change", (event) => {
    state.scale = Number(event.target.value);
    document.querySelectorAll("#scalePill .seg").forEach((b) => {
      b.classList.toggle("active", Number(b.dataset.scale) === state.scale);
    });
  });

  $("#qualitySelect").addEventListener("change", (event) => {
    state.quality = Number(event.target.value);
  });

  $("#restoreMode").addEventListener("change", (event) => {
    state.restoreMode = event.target.value;
    syncRestoreControls();
  });

  $("#denoiseSwitch").addEventListener("click", () => {
    state.denoiseOn = !state.denoiseOn;
    syncRestoreControls();
  });
  $("#deblockSwitch").addEventListener("click", () => {
    state.deblockOn = !state.deblockOn;
    syncRestoreControls();
  });

  const denoiseRange = $("#denoiseRange");
  denoiseRange.addEventListener("input", () => {
    state.denoisePct = Number(denoiseRange.value);
    syncSliderFill(denoiseRange);
    syncRestoreControls();
  });
  const deblockRange = $("#deblockRange");
  deblockRange.addEventListener("input", () => {
    state.deblockPct = Number(deblockRange.value);
    syncSliderFill(deblockRange);
    syncRestoreControls();
  });

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
  tray.addEventListener("drop", (event) => uploadFile(event.dataTransfer?.files?.[0]));

  bindCompareDrag();

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
/* راه‌اندازی                                                          */
/* ------------------------------------------------------------------ */

let initialized = false;

async function init() {
  if (initialized) return; // DOMContentLoaded در برخی محیط‌ها دوبار می‌رسد
  initialized = true;
  $("#threadValue").textContent = fa(navigator.hardwareConcurrency || "—");

  const denoiseSlider = $("#denoiseRange");
  denoiseSlider.value = String(state.denoisePct);
  syncSliderFill(denoiseSlider);
  const deblockSlider = $("#deblockRange");
  deblockSlider.value = String(state.deblockPct);
  syncSliderFill(deblockSlider);
  syncRestoreControls();
  bindEvents();
  if (SHELL === "glass") bindWindowControls();

  try {
    const health = await api("/api/health");
    if (!health.ok) {
      toast("atlas-cli پیدا نشد؛ پردازش کار نمی‌کند (راهنما: desktop/README.md)", true);
    }
    state.items = health.items || [];
    renderTree();
    api("/api/devices")
      .then((payload) => {
        const devices = payload.devices || [];
        if (devices.length) $("#engineBadge").textContent = devices[0].id || "classical";
      })
      .catch(() => {});
    if (state.items.length) selectItem(state.items[0].id);
  } catch (error) {
    toast(`اتصال به سرور محلی برقرار نشد: ${error.message}`, true);
  }
}

document.addEventListener("DOMContentLoaded", init);

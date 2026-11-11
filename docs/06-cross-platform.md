# ۰۶ — ماتریس چندسکویی و اجرا روی «هر سیستمی»

اصل: **هیچ دستگاه پشتیبانی‌شده‌ای بدون خروجی برنمی‌گردد.** اگر سخت‌افزار از بهترین مسیر پشتیبانی نکند، سلسله‌مراتب fallback اجرا می‌شود تا برسد به CPU (و در نهایت مسیر کلاسیک).

---

## ۱. سلسله‌مراتب Fallback

```
1) ncnn + Vulkan        (Windows/Linux/Android; Intel/AMD/NVIDIA/Adreno/Mali)
2) ONNX Runtime + EP    (DirectML | OpenVINO | CUDA | TensorRT | CoreML | QNN | XNNPACK)
3) ncnn + Metal         (macOS/iOS)
4) WebGPU / WASM        (مرورگر؛ PWA)
5) ncnn + CPU           (AVX2/AVX512/NEON، INT8)
6) مسیر کلاسیک پیشرفته  (Lanczos + NLM + deconvolution + guided filter) — همیشه در دسترس
```

هر مرحله با **timeout** (مثلاً ۳۰ ثانیه برای probe و ۵ دقیقه برای job کوچک) و **fallback خودکار** محافظت می‌شود؛ خطای درایور نباید کاربر را زمین‌گیر کند.

---

## ۲. ماتریس پشتیبانی

| سکو | نسخهٔ حداقل | معماری | مسیر اصلی | مسیر جایگزین | مسیر حداقلی | بسته‌بندی |
|---|---|---|---|---|---|---|
| Windows | ۱۰ (۱۸۰۹+) | x64, ARM64 | Vulkan (ncnn) | DirectML / OpenVINO | CPU AVX2 | NSIS/Portable + MSIX |
| macOS | ۱۲+ | arm64, x64 | Metal + CoreML | CPU | CPU | DMG (notarized) |
| Linux | glibc 2.28+ | x64, arm64 | Vulkan | CPU | CPU | AppImage + Flatpak |
| Android | ۸+ (API 26) | arm64-v8a (+x86_64 برای امولاتور) | Vulkan | XNNPACK CPU | CPU INT8 + حالت سریع | APK/AAB + Asset Delivery |
| iOS | ۱۵+ | arm64 | Metal + Core ML (ANE) | CPU | CPU | App Store |
| Web/PWA | Chrome/Edge 113+، Safari ۱۸+ | wasm32 | WebGPU | WASM SIMD | WASM + مدل سبک | PWA (بدون نصب) |

> **Android 15+**: الزام ۱۶KB page size برای کتابخانه‌های native — همهٔ `.so`ها باید با `-Wl,-z,max-page-size=16384` ساخته شوند.

---

## ۳. جزئیات هر سکو

### ۳.۱ ویندوز

- پوسته: **Tauri 2** (WebView2) + React/TS. حجم نصب ~۱۵MB بدون مدل.
- مسیر اصلی: **ncnn Vulkan** — تقریباً همهٔ GPUهای Intel HD 5xx/6xx/7xx، Iris Xe، AMD Vega/RDNA، NVIDIA GTX 9xx+ را پوشش می‌دهد.
- مسیر دوم: **ONNX Runtime + DirectML** برای مدل‌هایی که در ncnn نیستند (segmentation/face/diffusion).
- مسیر سوم: **OpenVINO** برای Intel iGPU/CPU (اغلب سریع‌تر از Vulkan روی Intel نسل جدید).
- نسخهٔ ARM64 (Snapdragon X): ONNX Runtime QNN + CPU؛ Vulkan در دسترس درایور.
- نصب: NSIS installer + نسخهٔ Portable ZIP + MSIX (برای Store). حالت «بدون نصب» برای فلش/سیستم‌های قفل‌شده.
- موارد ویندوزی: مسیرهای طولانی (`\\?\`)، فونت/DPI، High-DPI scaling، آنتی‌ویروس (امضا)، و WebView2 runtime.
- کدگذاری سخت‌افزاری اختیاری (Media Foundation) برای ویدیو در M6.

### ۳.۲ اندروید

- پوسته: **Kotlin + Jetpack Compose**، هستهٔ C++ از طریق **JNI**.
- مسیر اصلی: **ncnn Vulkan** (Adreno/Mali). نسخهٔ Vulkan 1.1+ و `VK_KHR_16bit_storage` بررسی می‌شود.
- مسیر جایگزین: **XNNPACK/CPU** با مدل INT8؛ برای دستگاه‌های قدیمی مدل سبک SPAN/Real-CUGAN.
- NPU: در ۲۰۲۶ **NNAPI منسوخ** است؛ مسیر درست **QNN (Snapdragon)** یا LiteRT GPU delegate است. در ۱.۰ اختیاری.
- ذخیره‌سازی: Scoped Storage + `MediaStore` برای ذخیره در گالری. صف در `filesDir`.
- مدل‌ها: **Play Asset Delivery** (install-time برای مدل پایه، fast-follow برای بقیه) یا دانلود در اولین اجرا (با Wi-Fi).
- حرارت/باتری: تشخیص thermal state → کاهش tile/thread و نمایش ETA واقع‌گرایانه.
- محدودیت‌ها: ۳–۴GB RAM رایج؛ پس‌زمینه محدود → foreground service برای job طولانی (با نوتیفیکیشن پیشرفت).
- پوشش تست: حداقل ۸ دستگاه از Adreno 5xx/6xx/7xx، Mali-G5x/G7x، Exynos، Dimensity، Snapdragon 8xx.

### ۳.۳ iOS

- پوسته: **SwiftUI**؛ هستهٔ C++ با bridging header.
- مسیر اصلی: **Metal** (ncnn Metal backend یا MPSGraph) + **Core ML** برای segmentation/face (با ANE).
- محدودیت حافظه: iOS به اپ‌های معمولی ~۲–۳GB می‌دهد (بسته به دستگاه)؛ باید tile و budget به‌صورت دقیق مدیریت شود و در حالت پس‌زمینه کار متوقف شود.
- توزیع: App Store؛ دانلود مدل‌های حجیم در اولین اجرا؛ On-Demand Resources.
- تست: iPhone SE (3rd) به‌عنوان کف، iPhone 13/15 به‌عنوان میان‌رده، iPad برای صفحهٔ بزرگ.

### ۳.۴ macOS

- یونیورسال باینری (arm64 + x86_64). Metal + Core ML.
- Apple Silicon بدون فن (MacBook Air) → throttling؛ نمایش ETA محافظه‌کارانه.
- notarization + Hardened Runtime؛ مدل‌ها در `~/Library/Application Support/Atlas/`.

### ۳.۵ لینوکس

- AppImage (بدون نصب) + Flatpak (برای توزیع).
- Vulkan روی Mesa (Intel/AMD) عالی است؛ NVIDIA با درایور اختصاصی.
- headless: `atlas-cli` برای سرور/NAS (بدون GUI، فقط CPU/Vulkan compute).

### ۳.۶ مرورگر / PWA

- هدف: «بدون نصب» برای کاربرانی که نمی‌توانند اپ نصب کنند (مخصوصاً iOS).
- **WebGPU** با ONNX Runtime Web یا اجرای مستقیم shader؛ fallback به **WASM SIMD** (XNNPACK).
- محدودیت واقع‌بینانه: مدل‌های سبک، حداکثر ~۱۲MP، بدون tiling سنگین؛ WebGPU محدودیت بافر دارد.
- مزیت: همان UI Tauri با یک بک‌اند `WebGpuBackend` → کد مشترک.
- محدودیت: حافظهٔ تب، سیاست iOS برای WebGPU (نسخهٔ ۱۸+)، و نبود دسترسی به فایل‌های بزرگ.

---

## ۴. تیرهای سخت‌افزاری (تعریف رسمی)

| تیر | معیار تشخیص | تجربهٔ هدف |
|---|---|---|
| **A+** | VRAM ≥ ۸GB و FP16 و (CUDA/DirectML قوی یا Apple M-Pro/Max) | Ultra/Diffusion، ۱۶x، دستهٔ بزرگ |
| **A** | Vulkan/DirectML/CoreML با VRAM ۴–۸GB | همهٔ قابلیت‌ها، ۴–۸x سریع |
| **B** | iGPU با VRAM اشتراکی ۲–۴GB | همهٔ قابلیت‌ها، سرعت متوسط |
| **C** | فقط CPU یا موبایل میان‌رده | مدل سبک، ۲–۴x، صبر لازم |
| **C-** | ۲ هسته/۴GB یا بدون Vulkan | حالت سریع + مسیر کلاسیک |

تشخیص: `vram + vulkan_device_type + micro-benchmark`. اگر بنچمارک < ۵MP/s باشد، خودکار به تیر پایین‌تر و مدل سبک‌تر می‌رود.

---

## ۵. حداقل نیازمندی‌ها (پیشنهادی برای انتشار)

| سکو | CPU | RAM | GPU | دیسک |
|---|---|---|---|---|
| Windows | ۲ هسته x64 با SSE4.2 | ۴GB (۸GB توصیه) | Vulkan 1.1 یا DirectX 12 | ۵۰۰MB (با مدل‌ها) |
| Android | arm64 ۴ هسته | ۳GB | Vulkan 1.1 (توصیه) | ۴۰۰MB |
| iOS | A12+ | ۳GB | — | ۴۰۰MB |
| macOS | M1 یا Intel ۲۰۱۷+ | ۸GB | Metal | ۵۰۰MB |
| Web | — | ۴GB | WebGPU توصیه | — |

---

## ۶. مواردی که «روی همه سیستم‌ها» را می‌شکنند (و راه‌حل)

| مشکل | نشانه | راه‌حل |
|---|---|---|
| درایور Vulkan قدیمی/باگ‌دار | کرش یا خروجی خراب | blocklist درایور + fallback به CPU/DirectML |
| VRAM اشتراکی کم | OOM هنگام tiling | tile تطبیقی + آزادسازی بافت‌ها + spill |
| نبود FP16 | خروجی سیاه/نویزی | تشخیص `shaderFloat16` و استفاده از FP32 |
| فایل بزرگ در مرورگر | تب کرش می‌کند | سقف MP + پردازش نوارِ کوچک |
| اندروید با RAM کم | kill شدن در پس‌زمینه | foreground service + ذخیرهٔ وضعیت |
| مسیر فارسی/یونیکد در ویندوز | خطای باز کردن فایل | `std::filesystem::path` + APIهای wide |
| سیستم بدون GPU | «برنامه باز نمی‌شود» | مسیر C- همیشه فعال، UI با پیام روشن |

---

## ۷. بسته‌بندی و امضا

| سکو | فرمت | امضا | به‌روزرسانی |
|---|---|---|---|
| Windows | NSIS + Portable + MSIX | Authenticode (EV) | Tauri updater / Store |
| macOS | DMG + ZIP | Developer ID + notarization | Sparkle/Tauri |
| Linux | AppImage + Flatpak | GPG (اختیاری) | Flatpak repo |
| Android | APK + AAB | keystore (Play App Signing) | Play / APK |
| iOS | IPA | Apple Distribution | App Store |
| Web | PWA | HTTPS + SRI | service worker |

مدل‌ها هم امضا/هش دارند (SHA-256 در کاتالوگ) و دانلودر هر بار صحت را بررسی می‌کند.

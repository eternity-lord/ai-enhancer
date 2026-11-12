# ۱۲ — ساخت، CI، بسته‌بندی و تست

> هیچ کدی هنوز وجود ندارد؛ این سند قرارداد ساخت (build contract) برای M1 است تا همهٔ پلتفرم‌ها از روز اول یکسان ساخته شوند.

---

## ۱. زنجیرهٔ ابزار

| ابزار | نسخهٔ هدف | استفاده |
|---|---|---|
| CMake | ≥ 3.28 | ساخت هسته |
| C++ | C++20 (GCC 13+/Clang 17+/MSVC 19.38+) | هسته |
| Ninja | آخرین | generator |
| vcpkg (manifest mode) | آخرین | وابستگی‌های C++ |
| Rust | stable | Tauri (فقط پوستهٔ دسکتاپ) |
| Node.js | 20 LTS / 22 | UI دسکتاپ |
| JDK | 17 | اندروید |
| Android NDK | r27+ | JNI |
| Xcode | 16+ | iOS/macOS |
| Swift | 5.10+ | پوستهٔ iOS |
| Python | 3.11 (فقط ابزار مدل) | تبدیل/کالیبراسیون |

### وابستگی‌های هسته

| کتابخانه | نقش | لایسنس |
|---|---|---|
| ncnn | استنتاج Vulkan/CPU | BSD-3 |
| ONNX Runtime | EPهای سخت‌افزاری | MIT |
| OpenCV (core/imgproc فقط) | تحلیل و عملیات کلاسیک | Apache-2.0 |
| libjpeg-turbo, libpng, libwebp, libavif, libjxl, libtiff | کدک | BSD-like |
| LittleCMS2 | مدیریت رنگ | MIT |
| {fmt}, spdlog | لاگ/قالب‌بندی | MIT |
| Catch2/GoogleTest | تست | BSL-1.0/MIT |
| stb_image (فقط ابزار) | کمکی | MIT |

> OpenCV را با ماژول‌های حداقلی بساز (`-DBUILD_LIST=core,imgproc,imgcodecs`) تا حجم باینری کم بماند.

---

## ۲. ساخت

```bash
# هسته (لینوکس/مک)
cmake -S core -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DATLAS_BACKEND_VULKAN=ON -DATLAS_BACKEND_ORT=ON -DATLAS_TESTS=ON
cmake --build build -j

# ویندوز — راه ساده: اسکریپت آماده (CMake و کامپایلر را خودش پیدا می‌کند)
tools\windows\build.bat
tools\windows\run-desktop.bat

# اگر کامپایلر C++ نبود یا نصب Visual Studio نیمه‌کاره بود:
powershell -NoProfile -ExecutionPolicy Bypass -File tools\windows\install-msvc.ps1
# راه سبک بدون Visual Studio (MinGW + Ninja):
powershell -NoProfile -ExecutionPolicy Bypass -File tools\windows\install-msvc.ps1 -MinGW

# ویندوز — دستی
cmake -S core -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release -j
.\build\Release\atlas-tests.exe
# خروجی ژنراتور VS داخل build\Release\ می‌افتد؛ سرور دسکتاپ هر دو مسیر را می‌شناسد.

# اندروید (arm64)
cmake -S core -B build-android -G Ninja ^
  -DCMAKE_TOOLCHAIN_FILE=%NDK%/build/cmake/android.toolchain.cmake ^
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 ^
  -DCMAKE_SHARED_LINKER_FLAGS="-Wl,-z,max-page-size=16384"

# پوستهٔ دسکتاپ
cd ui/desktop && npm ci && npm run tauri build
```

### پروفایل‌های CMake

| preset | Vulkan | ORT | تست | کاربرد |
|---|---|---|---|---|
| `dev` | ON | ON | ON | توسعه |
| `release` | ON | ON | OFF | انتشار |
| `cpu-only` | OFF | OFF | ON | سیستم‌های بدون GPU / CI |
| `android` | ON | ON | OFF | موبایل |
| `ios` | Metal | CoreML | OFF | اپل |

---

## ۳. CI (GitHub Actions)

| job | runner | کار |
|---|---|---|
| `build-linux` | ubuntu-24.04 | CPU + Vulkan (lavapipe) + تست |
| `build-windows` | windows-2022 | MSVC + Vulkan + DML + تست |
| `build-macos` | macos-14 | Metal + تست |
| `build-android` | ubuntu + NDK | AAR/APK اشکال‌زدایی |
| `build-ios` | macos-14 | xcodebuild |
| `golden-tests` | ubuntu (CPU) | تست تصویر طلایی با آستانه |
| `license-check` | ubuntu | مدل غیرتجاری در بستهٔ پیش‌فرض نباشد |
| `bench-smoke` | ubuntu | زمان‌های مرجع، شکست در صورت رگرسیون >۲۰٪ |

نکته: Vulkan در CI با `lavapipe` (Mesa) شبیه‌سازی می‌شود؛ تست واقعی GPU در دستگاه‌های self-hosted/دستگاه‌های فیزیکی.

---

## ۴. دانلودر مدل

```
tools/atlas-models fetch --set core        # مدل‌های پیش‌فرض
tools/atlas-models fetch --id gfpgan-v1.4
tools/atlas-models verify                  # SHA-256 همه
tools/atlas-models export bundle.zip       # برای نصب آفلاین
```

الزامات:

- resumable (HTTP Range)، retry با backoff، آینهٔ قابل‌تنظیم.
- بررسی SHA-256 پس از دانلود (از `catalog.json`).
- ذخیره در `%LOCALAPPDATA%/Atlas/models` (ویندوز)، `~/Library/Application Support/Atlas/models` (مک)، `/data/data/.../files/models` (اندروید).
- حالت هواپیما: اگر مدل موجود نبود، پیام روشن + گزینهٔ «ورود فایل مدل از فلش».

---

## ۵. استراتژی تست

### ۵.۱ هرم تست

| سطح | چه چیزی | ابزار |
|---|---|---|
| واحد | بافرها، tiling، blend، رنگ، کاتالوگ | GoogleTest |
| یکپارچه | گراف کامل روی تصویر کوچک + مقایسه با مرجع | GoogleTest + golden |
| تصویری (golden) | ۲۰ تصویر مرجع × ۵ مدل × ۳ بک‌اند | مقایسهٔ SSIM/LPIPS با آستانه |
| کارایی | زمان/حافظه، رگرسیون >۲۰٪ = fail | bench harness |
| E2E | UI: افزودن، اجرا، ذخیره، لغو | Playwright/Appium |
| دستگاه | ۸ اندروید + ۳ ویندوز + ۲ اپل | تست دستی/دستگاه‌های فیزیکی |

### ۵.۲ تصاویر طلایی

`tests/golden/` شامل: پرتره، گروهی، منظره، محصول با متن، اسکرین‌شات UI، انیمه، عکس قدیمی B&W، JPEG q30، نویز ISO 6400، تصویر ۱MP خیلی تاری.

آستانهٔ پذیرش: `SSIM ≥ 0.995` بین اجراهای همان بک‌اند (determinism) و `LPIPS` نسبت به مرجع نسخهٔ قبل بدون رگرسیون >۲٪.

### ۵.۳ تست کیفیت (ارزیابی انسانی)

- ۵۰ تصویر، ۳ داور، مقایسهٔ کور A/B با LetsEnhance و Topaz (به‌عنوان مرجع بازار).
- پرسش: «کدام طبیعی‌تر است؟ کدام متن را بهتر حفظ کرده؟ کدام آرتیفکت دارد؟»
- نتیجه در `bench/results/quality-YYYY-MM.md`.

---

## ۶. استانداردهای کد

- `clang-format` (Google style با ستون ۱۰۰)، `clang-tidy` (bugprone, performance).
- بدون `new/delete` خام در کد اپلیکیشن (RAII/`std::unique_ptr`).
- هر ماژول یک `README.md` کوتاه با قرارداد.
- هیچ مدل/مسیر مطلقی hard-code نشود.
- کامیت‌ها: Conventional Commits (`feat(core): tiled sr`).

---

## ۷. انتشار

| مرحله | کار |
|---|---|
| نسخه | SemVer (`0.1.0`, `1.0.0`)، مدل‌ها نسخهٔ جدا |
| CHANGELOG | Keep a Changelog |
| امضا | Authenticode/notarization/keystore |
| artifact | installer + portable + checksums + SBOM (CycloneDX) |
| مدل‌ها | release assets با SHA-256 |
| rollback | نسخهٔ قبلی همیشه قابل دانلود |

---

## ۸. کارهای باز

- [ ] ساخت مخزن کد و CI در M1.
- [ ] انتخاب نسخهٔ دقیق ncnn/ORT و پین کردن.
- [ ] ساخت اسکریپت تبدیل `pth→onnx→ncnn` و کالیبراسیون INT8.
- [ ] ساخت هارنس بنچمارک و مجموعهٔ تصاویر طلایی.

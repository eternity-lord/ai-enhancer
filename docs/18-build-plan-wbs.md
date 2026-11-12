# ۱۸ — برنامهٔ ساخت فایل‌به‌فایل (WBS)

> این سند «نقشهٔ کار» اجرایی است: هر فایلی که باید ساخته شود، هدفش، API کلیدی، تست پذیرش، وابستگی و فاز.
> تخمین‌ها به **نفر-روز (PD)** برای یک مهندس آشنا به حوزه است.

---

## ۱. ساختار نهایی مخزن

```
ai-enhancer/
├─ core/
│  ├─ include/atlas/           atlas.h, types.h, graph.h, face.h, version.h
│  ├─ src/
│  │  ├─ api/                  context.cpp, job.cpp, devices.cpp
│  │  ├─ image/                buffer.cpp, tiling.cpp, blend.cpp, color.cpp, metrics.cpp
│  │  ├─ io/                   decode.cpp, encode.cpp, metadata.cpp, icc.cpp
│  │  ├─ analyze/              noise.cpp, blur.cpp, jpeg.cpp, content.cpp
│  │  ├─ face/                 detect.cpp, align.cpp, restore.cpp, reference.cpp,
│  │  │                        identity.cpp, parse.cpp, composite.cpp, gate.cpp
│  │  ├─ stages/               sr.cpp, denoise.cpp, deblock.cpp, light.cpp, sharpen.cpp,
│  │  │                        background.cpp, inpaint.cpp, colorize.cpp, detail.cpp
│  │  ├─ pipeline/             graph.cpp, registry.cpp, preset.cpp, plan.cpp
│  │  ├─ backend/              backend.cpp, ncnn_backend.cpp, ort_backend.cpp,
│  │  │                        metal_backend.cpp, webgpu_backend.cpp, classical.cpp
│  │  ├─ sched/                scheduler.cpp, cache.cpp, memory.cpp
│  │  └─ util/                 log.cpp, errors.cpp, json.cpp, fs.cpp
│  └─ CMakeLists.txt
├─ cli/                        main.cpp, args.cpp
├─ ui/desktop/                  (Tauri + React)
├─ ui/android/                  (Kotlin)
├─ ui/ios/                      (SwiftUI)
├─ models/                      catalog.json, presets/*.json
├─ tools/                       convert_model.py, calibrate_int8.py, atlas-models
├─ bench/                       harness, cases/, results/
└─ tests/                       unit/, golden/, e2e/
```

---

## ۱.۵ وضعیت پیشرفت (به‌روزرسانی ۲۰۲۶-۱۱-۱۱، پایان P1)

| مورد | وضعیت | یادداشت |
|---|---|---|
| P0-01 CMake | ✅ انجام شد | بدون وابستگی سیستمی؛ گزینه‌های `ATLAS_WITH_NCNN`, `ATLAS_BUILD_CLI/TESTS` |
| P0-02 types | ✅ | `ImageF32`, `Rect`, `TilePlan`, `ErrorCode` |
| P0-03 buffer | ✅ | crop/pad_reflect/clamp01 (crop فعلاً کپی) |
| P0-04 errors/log | ✅ (حداقلی) | لاگ فایل چرخشی در P6 |
| P0-05/P0-06 codec | ✅ (stb) | JPEG/PNG/BMP/TGA خواندن+نوشتن، ۸/۱۶ بیت؛ **مسیر مهاجرت** به libjpeg-turbo/libpng باز است |
| P0-07 color | ✅ | `src/image/color.cpp`: sRGB↔خطی دقیق (IEC 61966-2-1)، آلفا دست‌نخورده |
| P0-08/P0-09 tiling/blend | ✅ / ✅ | کاشی با تست پوشش + دوخت raised-cosine با تست «تصویر ثابت» |
| P0-10 classical backend | ✅ | Lanczos+unsharp؛ ۹٫۱ MP/s (۲ هسته) |
| P0-11/12 context+CLI | ✅ | `version`/`devices`/`convert`/`enhance` + `--param/--weights/--tile/--overlap/--threads/--fp16` |
| P0-13 tests | ✅ | **۴۹ تست**، صفر شکست (با و بدون WebP) |
| P1-01 ncnn backend | ✅ (CPU) | `ATLAS_WITH_NCNN=ON`; pad-to-8 + crop؛ Vulkan فلگ بعدی |
| P1-02 model registry | ✅ (سبک) | `tools/fetch_models.py` با sha256 + مسیر آفلاین `--from-dir` |
| P1-03 SR کاشی‌ای واقعی | ✅ | x4plus و animevideov3-x2/x3/x4 روی عکس واقعی اجرا شد |
| P1-04 پلتفرم‌ها | ⏳ | Windows/Android در M1/M3 |
| P1-05 EXIF orientation | ✅ | ۸ حالت EXIF + پارسر APP1 (فقط ۱۲۸KB اول فایل)، پیش‌فرض روشن |
| P1-06 ICC | ✅ | پروفایل ماتریسی/TRC (sRGB/P3/AdobeRGB) + iCCP در PNG و APP2 در JPEG؛ LUT-based رد می‌شود |
| P1-07 آلفا | ✅ | PNG/TGA چهارکاناله، مسیر SR و بک‌اند ncnn آلفا را حفظ می‌کند |
| P1-08 WebP | ✅ | decoder/encoder اختیاری با libwebp؛ بدون آن build سالم است و پیام روشن می‌دهد |
| P1-09 متادیتا | ✅ | انتقال EXIF (opt-in) + برچسب sRGB در خروجی (iCCP/APP2) |
| P1-10 GPU plumbing | ✅ (کد) | `--gpu` + `vulkan_device_count()`؛ سندباکس دستگاه Vulkan ندارد |
| TIFF/AVIF/HEIC | ⏳ | P2 (نیاز به libtiff/libavif/libheif) |
| P3-04 متریک‌ها | 🟡 (شروع) | PSNR/SSIM/MAE/max-abs در `core/src/metrics` + `atlas-cli eval`؛ LPIPS/DISTS و ArcFace هنوز نه |

**Gate P0/P1 فعلی:** روی CPU سبز است. برای «روی iGPU» فقط باید ncnn با
`-DNCNN_VULKAN=ON` کامپایل و `--backend ncnn` روی دستگاه Vulkan اجرا شود (کد
آماده است، سندباکس Vulkan ندارد).

**کدک — چرا stb و بعدش چه؟** یا libjpeg-turbo/libpng در سندباکس هدر سیستمی
نداشتند و «بدون وابستگی» برای ویندوز/موبایل مهم‌تر بود؛ stb (MIT/PD) هر دو کار
را در دو هدر انجام می‌دهد. اما decode تک‌رشته‌ای stb برای فایل‌های ۵۰ مگابایتی
کند است، پس مسیر مهاجرت در P1-05 این است: **decode** به libjpeg-turbo/libpng
(با fallback به stb)، **encode** با کیفیت کنترل‌شده باقی می‌ماند.

## ۱.۶ اعداد واقعی P1 (سندباکس: ۲ هستهٔ CPU، بدون GPU، بدون Vulkan)

| مسیر | ورودی → خروجی | زمان | توان |
|---|---|---|---|
| classical (Lanczos+unsharp) | 3.00 MP → 12.00 MP | 1.32 s | 9.11 MP/s |
| ncnn `realesrgan-x4plus` | 1024×768 → 4096×3072 (12.58 MP) | 308.5 s | 0.041 MP/s |
| ncnn `realesr-animevideov3-x2` | 1024×768 → 2048×1536 (3.15 MP) | 6.6 s | 0.48 MP/s |

نتیجهٔ عملی: مدل ۳۳ مگابایتی x4plus روی CPU **۱۲ برابر** کندتر از مدل سبک
animevideov3 است؛ پس انتخاب خودکار مدل باید بر اساس «بودجهٔ زمان کاربر» و
«وجود GPU» باشد، نه یک مدل ثابت. روی Vulkan، x4plus در حد چند ثانیه برای همان
تصویر است (هدف P1-01).

### کیفیت (PSNR نسبت به مرجع، فقط به‌عنوان شاهد نه معیار)

| تصویر | بازسازی | classical | x4plus |
|---|---|---|---|
| `00003.png` (۵۱۲×۲۵۶، از ۴× box) | 4× | **26.69 dB** | 20.77 dB |
| پرترهٔ واقعی (۵۰۰×۷۵۰، از ۴× box) | 4× | **37.97 dB** | 29.85 dB |

PSNR مدل پایین‌تر است و این طبیعی است: GAN جزئیات را «می‌سازد» و خطای پیکسلی
بیشتر می‌شود. اما در پرتره، خروجی x4plus چشمان را اشباع و پوست را صاف می‌کند —
یعنی همان «زیباسازی/توهم» که در `docs/14` ممنوع شده است. نتیجه: **PSNR تنها
معیار نیست** و برای چهره باید دروازهٔ هویت (ArcFace) + بررسی آرتیفکت حاکم باشد.

## ۱.۷ P1.5 — «قابل اندازه‌گیری» و «درست دیده شدن»

دو چیز که بدون آن‌ها حتی بهترین مدل هم بی‌فایده است:

1. **جهت عکس (EXIF).** عکس موبایل معمولاً tag `0x0112` دارد؛ اگر نادیده گرفته
   شود، چهرهٔ کاربر ۹۰ درجه چرخیده تحویل داده می‌شود. پیاده‌سازی: پارسر APP1
   (فقط ۱۲۸ کیلوبایت اول فایل، بدون خواندن کل تصویر)، تبدیل ۸ حالت، و تست
   پیکسل‌به‌پیکسل برای هر حالت + تست رفت‌وبرگشت ۹۰/۲۷۰.
2. **متریک.** `atlas-cli eval a b` عدد می‌دهد: PSNR روی RGB، SSIM استاندارد
   (پنجرهٔ ۱۱×۱۱، σ=1.5، روی luma Rec.709)، MAE و بیشینهٔ خطا. با `--json` و
   `--min-psnr/--min-ssim` می‌شود از آن به‌عنوان دروازهٔ CI استفاده کرد.

```bash
./build/atlas-cli eval original.png restored.png --min-psnr 32 --min-ssim 0.90
./build/atlas-cli eval a.png b.png --json     # {"psnr_db":...,"ssim":...,"pass":true}
```

**نکتهٔ مهم:** این متریک‌ها «دروازهٔ کیفیت چهره» نیستند؛ برای چهره، ArcFace
(هویت) و بررسی آرتیفکت حاکم است (`docs/14`, `docs/17`). PSNR/SSIM ابزار
پیش‌غربال‌گری‌اند، نه حکم نهایی.

**رنگ خطی:** `to_linear`/`to_srgb` اضافه شد تا عملیات نویز/شارپ روی نور خطی
انجام شود. عمداً در مسیر مدل Real-ESRGAN وصل نشد، چون آن مدل با ورودی
sRGB-encoded آموزش دیده و خطی‌کردن، خروجی را بی‌صدا تغییر می‌دهد.

## ۱.۸ P1.6 — وفاداری رنگ و آلفا (ICC)

این مرحله همان چیزی است که بین «خروجی شارپ ولی رنگ‌پریده» و «خروجی درست» فرق
می‌گذارد.

**مشکل واقعی:** عکس آیفون/مک در فضای **Display P3** ذخیره می‌شود. اگر آن را sRGB
فرض کنیم، رنگ‌های اشباع (پوست، لباس، آسمان) کم‌جان می‌شوند، در حالی که هیچ
پیکسلی «خراب» نشده است. کاربر این را «کیفیت بد» می‌بیند.

**پیاده‌سازی:**

| بخش | جزئیات |
|---|---|
| پارسر ICC | هدر + جدول تگ؛ پشتیبانی `rXYZ/gXYZ/bXYZ` و `curv`/`para` (نوع ۰..۴) |
| تبدیل | LUT ۴۰۹۶ نمونه‌ای برای TRC + ماتریس ترکیبی «linear-device → linear-sRGB»؛ آلفا دست‌نخورده |
| استخراج | `iCCP` در PNG (inflate با stb) و `ICC_PROFILE` در APP2 جفت JPEG (چندتکه) |
| نقطهٔ سفید | `wtpt` + سازگاری Bradford → D65 (پروفایل‌های D50 مثل ProPhoto بدون این، رنگ‌افتادگی می‌دهند) |
| امتناع | پروفایل LUT-based (A2B0) یا خراب → **تبدیل نمی‌کنیم** و در CLI می‌گوییم `unsupported (assumed sRGB)` |
| کلید | `--no-color-manage` برای مقایسه/بازتولید دقیق بدون تبدیل |

**عدد واقعی (تست end-to-end):** PNG با پروفایل ساختگی «sRGB کامل» → خروجی
**بایت‌به‌بایت یکسان** (PSNR سقف ۹۹ dB). یعنی مسیر تبدیل، sRGB را دست‌نخورده
برمی‌گرداند و رنگ را «دور نمی‌زند».

**آلفا:** قبلاً `load_image` هر PNG را ۳ کاناله می‌کرد و شفافیت گم می‌شد. حالا
PNG/TGA چهارکاناله می‌مانند، `sr_tiled` آلفا را با همان وزن‌ها blend می‌کند، و
بک‌اند ncnn آلفا را **با Lanczos** (نه با مدل) بزرگ می‌کند تا لبه‌های شفافیت
هاله نگیرند. JPEG/BMP آلفا ندارند؛ آلفا کنار گذاشته می‌شود بدون اینکه رنگ در
آلفا ضرب شود (خطای رایج: خروجی سیاه).

## ۱.۹ P1.7 — WebP، متادیتا، و آمادگی GPU

**WebP.** وابستگی **اختیاری**: اگر libwebp باشد کامپایل و فعال می‌شود
(`find_package(WebP)`)، اگر نباشد build سالم می‌ماند و فقط پیام روشن می‌دهد
«this build has no WebP support». دلیل: ویندوز/موبایل ممکن است libwebp نداشته
باشند، ولی وب و اندروید پر از WebP است. آلفا حفظ می‌شود (آلفای کاملاً مات توسط
encoder حذف می‌شود — رفتار درست).

**متادیتا (وفاداری فایل).** دو کار، هر دو «برش و چسباندن» استاندارد بدون
کتابخانهٔ اضافه:
- انتقال EXIF از مبدأ به مقصد (`--keep-metadata`) — **پیش‌فرض خاموش**، چون EXIF
  شامل GPS است و انتشار تصویر با مکان، ریسک خصوصی است.
- برچسب sRGB در خروجی: اگر تبدیل رنگ انجام شده باشد، پروفایل sRGB جاسازی می‌شود
  (`iCCP` در PNG قبل از IDAT، `APP2` در JPEG با شکستن به تکه‌های ≤۶۵۵۱۹ بایت).

**آمادگی GPU.** `--gpu` به بک‌اند ncnn می‌رود؛ اگر ncnn با Vulkan ساخته شده و
دستگاه موجود باشد `name()` می‌شود `ncnn-vulkan`، وگرنه بی‌صدا `ncnn-cpu`.
`devices` تعداد دستگاه Vulkan را گزارش می‌کند (در این سندباکس: ۰).

## ۱.۱۰ جمع‌بندی فاز P1 (پایان P1)

| خروجی P1 | وضعیت |
|---|---|
| کدک واقعی (JPEG/PNG/BMP/TGA/WebP) + آلفا + ۱۶ بیت | ✅ |
| ICC (ماتریسی/TRC، P3/D50) → sRGB | ✅ |
| EXIF orientation (۸ حالت) | ✅ |
| بک‌اند ncnn (CPU؛ آمادهٔ Vulkan) + SR کاشی‌ای | ✅ |
| متریک PSNR/SSIM + `eval` با دروازهٔ CI | ✅ |
| انتقال EXIF + برچسب sRGB در خروجی | ✅ |
| ۴۹ تست، صفر شکست، دو پیکربندی build | ✅ |
| ویدیو، چهره، نویز/JPEG، دیفیوژن، UI | ❌ (P2..P6) |

## ۱.۱۱ P3-01/P3-04 — دروازهٔ هویت و مجموعهٔ طلایی

| خروجی | فایل | وضعیت |
|---|---|---|
| پروکسی هویت (SSIM + NCC چیدمان + HOG + رنگ خطی) | `src/eval/identity.cpp` | ✅ |
| آستانهٔ مستقل پروکسی + گزارش آستانهٔ ArcFace | `identity_threshold`, `proxy_threshold` | ✅ |
| قرارداد تعویض با ArcFace | `IIdentityEmbedder` در `identity.h` | ✅ (فقط قرارداد) |
| لودر مانیفست TSV + اجرای مجموعه | `src/eval/golden.cpp` | ✅ |
| پذیرش خودکار (≥۹۵٪، میانه ≥۰٫۶۵، `missing==0`) | `golden_meets_acceptance` | ✅ |
| CLI: `gate` / `eval-set` + `--json` + کد بازگشتی | `cli/main.cpp` | ✅ |
| نمونهٔ مصنوعی برای smoke-test | `tools/make_sample_golden.py` | ✅ |
| ArcFace واقعی + ۳۰۰ تصویر طلایی | — | ❌ P3-02 (نیاز به وزن مجاز و داده) |

**اندازه‌گیری واقعی (چهرهٔ مصنوعی ۱۱۲×۱۱۲، blind):** همان چهره ۱٫۰۰۰۰ PASS ·
جزئیات صاف‌شده ۰٫۹۹۸۷ PASS · چهرهٔ متفاوت ۰٫۸۰۹۵ FAIL (آستانه ۰٫۸۵).
یعنی دروازه در جهت درست کار می‌کند؛ اما هویت بیومتریک را ثابت نمی‌کند.

## ۱.۱۲ P2-01..P2-03 — عکس خراب: نویز و JPEG

| خروجی | فایل | وضعیت |
|---|---|---|
| برآورد σ نویز (لاپلاسین) + سنجش شبکهٔ ۸×۸ | `analyze_degradation` | ✅ |
| deblock شرطی (لبهٔ واقعی دست‌نخورده) + deringing | `deblock_jpeg` | ✅ |
| NLM با patch ۳×۳ / search ۵×۵، موازی | `denoise_nlmeans` | ✅ |
| شدت خودکار از روی σ و ratio | `restore_degraded` | ✅ |
| CLI: `restore` + `--denoise`/`--deblock` در `enhance` | `cli/main.cpp` | ✅ |
| مدل denoise (NAFNet/SCUNet) و ONNX | — | ❌ P4 |
| تشخیص آفست شبکه در تصاویر کراپ‌شده | — | ❌ کار باز |

**اندازه‌گیری (همین محیط، ۲ هستهٔ CPU):** JPEG q=15 روی ۴۸۰×۳۶۰: PSNR ۳۴٫۳۶ → ۳۵٫۷۹،
SSIM ۰٫۹۱۰ → ۰٫۹۶۹، blockiness ratio ۵٫۵۳ → ۱٫۳۰، ۸۴ ms · JPEG q=20 روی ۱۹۲۰×۱۰۸۰:
۳۷٫۹۵ → ۳۹٫۱۱ dB، زمان ۱٫۰۲ s (≈۲ MP/s) · نویز σ=۰٫۰۵: ۲۶٫۰۲ → ۳۸٫۵۲ dB ·
**ترکیبی، مقایسهٔ قبل/بعد از SR: ۳۱٫۲۳ dB → ۳۵٫۴۰ dB**.

**آنچه P1 عمداً انجام نداده:** هیچ مرحلهٔ «بازسازی محتوا» (چهره/نویز/دیفیوژن)
در P1 نیست. دلیل: تا وقتی مسیر رنگ/فرمت/متریک درست نشده، هر عدد کیفیتی
بی‌معنی است. P1 زیرساخت اندازه‌گیری را ساخت؛ P2 محتوا را.

## ۲. فاز P0 — اسکلت اجرایی (۱۵ نفر-روز)

| # | فایل | هدف | API/نکته | تست پذیرش | PD | وابسته |
|---|---|---|---|---|---|---|
| P0-01 | `core/CMakeLists.txt` | ساخت هسته با presetها | `ATLAS_BACKEND_*` | `cmake --build` سبز | ۱ | — |
| P0-02 | `include/atlas/types.h` | `ImageF32`, `Rect`, `Tensor`, `Error` | بدون وابستگی خارجی | unit | ۰٫۵ | — |
| P0-03 | `src/image/buffer.cpp` | تخصیص/کپی/برش، alignment | page-aligned، بدون copy در برش | unit + ASAN | ۱ | P0-02 |
| P0-04 | `src/util/errors.cpp`, `log.cpp` | کدهای خطا، لاگ چرخشی | `E_OOM`, `E_MODEL`, … | unit | ۰٫۵ | — |
| P0-05 | `src/io/image_io.cpp` | JPEG/PNG/BMP/TGA (+WebP بعدی) + EXIF | **stb** (vendored) → مهاجرت به libjpeg-turbo/libpng در P1-05 | تست ۲۰ فایل | ۲ | P0-03 |
| P0-06 | `src/io/image_io.cpp` | JPEG/PNG (BMP/TGA) + کیفیت | ۸/۱۶ بیت، کیفیت ۱–۱۰۰ | round-trip PSNR>۴۵ (PNG بی‌اتلاف) | ۲ | P0-05 |
| P0-07 | `src/image/color.cpp` | خطی‌سازی sRGB، LittleCMS | `to_linear/from_linear` | مقایسه با مرجع | ۱٫۵ | P0-03 |
| P0-08 | `src/image/tiling.cpp` | کاشی‌بندی + overlap + reflect | `plan_grid`, `iter_tiles` | تست مرزها | ۱٫۵ | P0-02 |
| P0-09 | `src/stages/sr.cpp` | دوخت raised-cosine | بدون seam | تست «تصویر ثابت» + مقایسهٔ کاشی‌ای/یک‌تکه | ۱ | P0-08 |
| P0-10 | `src/backend/backend.cpp` + `classical.cpp` | رابط `IBackend` + مسیر کلاسیک (Lanczos+unsharp) | fallback همیشه موجود | unit | ۲ | P0-02 |
| P0-11 | `src/api/context.cpp`, `devices.cpp` | ساخت context + probe دستگاه (stub) | C ABI | unit | ۱ | P0-10 |
| P0-12 | `cli/main.cpp`, `args.cpp` | `atlas-cli enhance --scale 4 --preset natural` | خروجی + کد خطا | e2e با PPM/PNG | ۱٫۵ | P0-11 |
| P0-13 | `tests/unit/*` | تست‌های پایه | GoogleTest | CI سبز | ۲ | همه |

**Gate P0:** یک PNG واقعی ۱۲MP روی CPU به ۴x برسد، بدون OOM، با زمان ثبت‌شده.

---

## ۳. فاز P1 — چهرهٔ پایه (۱۵ نفر-روز)

| # | فایل | هدف | نکته | تست | PD |
|---|---|---|---|---|---|
| P1-01 | `src/backend/ncnn_backend.cpp` | Vulkan/CPU، بارگذاری مدل، warmup | FP16 detection | اجرای x4plus | ۳ |
| P1-02 | `src/backend/ort_backend.cpp` | EPها (DirectML/CoreML/CPU) | fallback | اجرای ONNX | ۳ |
| P1-03 | `src/stages/sr.cpp` | SR کاشی‌ای + multi-pass | scale 2/4/8 | PSNR نسبت به مرجع | ۲ |
| P1-04 | `src/face/detect.cpp` | SCRFD + bbox + score | حداقل اندازه | تست ۵۰ چهره | ۱٫۵ |
| P1-05 | `src/face/align.cpp` | landmark + similarity transform + inverse | template 112/512 | خطای بازگشت <۰٫۵px | ۱٫۵ |
| P1-06 | `src/face/restore.cpp` | GFPGAN/CodeFormer adapter | شدت/fidelity | اجرا + خروجی | ۲ |
| P1-07 | `src/face/parse.cpp` | ماسک اجزای صورت | BiSeNet | IoU > ۰٫۸ | ۱ |
| P1-08 | `src/face/composite.cpp` | paste-back + feather + color match | Poisson/Laplacian | بدون خط دیده‌شده | ۲ |
| P1-09 | `src/face/identity.cpp` | ArcFace ONNX + cosine | دو مدل | unit | ۱ |
| P1-10 | `src/face/gate.cpp` | سیاست قبول/Soft/رد | آستانه‌ها | unit + سناریو | ۱ |
| P1-11 | `src/pipeline/registry.cpp`, `graph.cpp` | ثبت مراحل + اجرای گراف JSON | schema | اجرای preset | ۲ |
| P1-12 | `models/presets/natural.json` | پریست پیش‌فرض | — | e2e | ۰٫۵ |

**Gate P1:** عکس پرترهٔ ۱۲MP → خروجی ۴x با چهرهٔ بازسازی‌شده؛ Same-person ≥ ۸۵٪ در ۳۰ تصویر.

---

## ۴. فاز P2 — کیفیت عکس (۱۴ نفر-روز)

| # | فایل | هدف | تست | PD |
|---|---|---|---|---|
| P2-01 | `src/analyze/noise.cpp` | تخمین نویز | خطای <۱۵٪ | ۱ |
| P2-02 | `src/analyze/jpeg.cpp` | تخمین q و نقشهٔ بلوک | خطای ±۵ | ۱ |
| P2-03 | `src/analyze/blur.cpp` | تاری/کرنل | طبقه‌بندی ۸۰٪ | ۱٫۵ |
| P2-04 | `src/analyze/content.cpp` | کلاسیفایر محتوا | دقت ۹۰٪ | ۱٫۵ |
| P2-05 | `src/stages/deblock.cpp` | FBCNN/کلاسیک | NIQE بهتر | ۲ |
| P2-06 | `src/stages/denoise.cpp` | SCUNet/Real-CUGAN | حفظ بافت | ۲ |
| P2-07 | `src/stages/detail.cpp` | multi-band + grain + halo | halo <۵٪ | ۲ |
| P2-08 | `src/stages/light.cpp` | Light AI + skin curve + ΔE guard | ΔE <۳ | ۲ |
| P2-09 | `src/pipeline/plan.cpp` | برنامه‌ریز پاس/کاشی/مدل | ۲۰ سناریو | ۱ |

**Gate P2:** NIQE −۱۵٪؛ halo <۵٪؛ بدون banding.

---

## ۵. فاز P3 — دروازهٔ هویت و Natural (۱۰ نفر-روز)

| # | فایل | هدف | تست | PD |
|---|---|---|---|---|
| P3-01 | `src/face/gate.cpp` (تکمیل) | Soft-retry + fallback ladder | ۳۰ سناریو | ۲ |
| P3-02 | `src/face/composite.cpp` (تکمیل) | محافظت چشم/دندان + frequency split | ارزیابی انسانی | ۲ |
| P3-03 | `src/stages/sr.cpp` (face-aware tiling) | مرز کاشی از چهره عبور نکند | تست بصری | ۱٫۵ |
| P3-04 | `bench/harness` + متریک‌ها | IDS/LPIPS/NIQE/artifact | گزارش کامل | ۳ |
| P3-05 | `src/api/job.cpp` + `qa report` | گزارش JSON کنار خروجی | schema | ۱٫۵ |

**Gate P3:** IDS میانه ≥۰٫۶۵؛ <۵٪ زیر ۰٫۵؛ Same-person ≥۹۵٪.

---

## ۶. فاز P4 — Reference-locked (۲۰ نفر-روز)

| # | فایل | هدف | تست | PD |
|---|---|---|---|---|
| P4-01 | `src/face/reference.cpp` | بارگذاری/اعتبارسنجی ۱–۴ رفرنس | refs همان شخص | ۲ |
| P4-02 | `src/face/reference.cpp` (infer) | اجرای مدل Ref-FR (ONNX) | IDS-ref ≥۰٫۸ | ۵ |
| P4-03 | `src/face/cluster.cpp` | خوشه‌بندی هویت در batch | ۲۰ آلبوم | ۲ |
| P4-04 | `tools/convert_ref_fr.py` | تبدیل/دیستیل مدل | خروجی ONNX | ۴ |
| P4-05 | UI: انتخاب رفرنس + پیش‌نمایش | جریان کاربری | تست کاربردپذیری | ۳ |
| P4-06 | `tests/golden/ref-set` | ۵۰ هویت × ۵ عکس | — | ۲ |

**Gate P4:** IDS-ref میانه ≥۰٫۸۰ (۴ رفرنس) و ≥۰٫۷۲ (۱ رفرنس).

---

## ۷. فاز P5 — Max Detail و Old Photo (۱۸ نفر-روز)

| # | فایل | هدف | PD |
|---|---|---|---|
| P5-01 | `src/stages/sr.cpp` (multi-pass 8x/16x) | بافت پایدار | ۳ |
| P5-02 | `src/stages/inpaint.cpp` | LaMa/MI-GAN برای خش/شیء | ۳ |
| P5-03 | `src/stages/colorize.cpp` | DDColor | ۲ |
| P5-04 | `src/stages/oldphoto.cpp` | زنجیرهٔ مرمت | ۳ |
| P5-05 | تیر A+ (Diffusion/SupIR) | مدل اختیاری | ۵ |
| P5-06 | ارزیابی A/B | مقایسه با رقبا | ۲ |

---

## ۸. فاز P6 — تثبیت و انتشار (۱۵ نفر-روز)

| # | کار | PD |
|---|---|---|
| P6-01 | بنچمارک کامل ۵ پلتفرم | ۳ |
| P6-02 | بسته‌بندی/امضا (NSIS، AAB، DMG) | ۴ |
| P6-03 | مستند کاربر + صفحهٔ «چه تغییر می‌دهیم» | ۲ |
| P6-04 | تست انصاف و گزارش نهایی | ۲ |
| P6-05 | سخت‌سازی خطا/بازیابی job | ۲ |
| P6-06 | انتشار 1.0 | ۲ |

---

## ۹. UI (خلاصه — جزئیات در `08-ui-ux.md`)

| # | فایل | هدف | PD |
|---|---|---|---|
| UI-01 | `ui/desktop/src/App.tsx` | چیدمان سه‌ستونی | ۲ |
| UI-02 | `ui/desktop/src/Queue.tsx` | صف/پیشرفت/لغو | ۲ |
| UI-03 | `ui/desktop/src/Panel.tsx` | مدل/شدت/حالت | ۲ |
| UI-04 | `ui/desktop/src/Compare.tsx` | اسلایدر + زوم همگام | ۲ |
| UI-05 | `ui/desktop/src/Reference.tsx` | انتخاب رفرنس + هشدار | ۲ |
| UI-06 | `ui/desktop/src/api.ts` | IPC با هسته | ۱ |
| UI-07 | `ui/android/...` | صفحهٔ اصلی + تنظیمات + ذخیره | ۵ |
| UI-08 | `ui/ios/...` | معادل SwiftUI | ۵ |
| UI-09 | i18n `fa.json`/`en.json` | فارسی/انگلیسی | ۱ |

---

## ۱۰. ابزارها و مدل

| # | فایل | هدف | PD |
|---|---|---|---|
| T-01 | `tools/convert_model.py` | pth→onnx→ncnn | ۲ |
| T-02 | `tools/calibrate_int8.py` | کالیبراسیون | ۲ |
| T-03 | `tools/atlas-models` | دانلود/verify (SHA-256) | ۲ |
| T-04 | `models/catalog.json` (تکمیل) | لایسنس + دانلودها | ۱ |
| T-05 | `bench/harness` | اجرای ماتریسی + گزارش | ۳ |
| T-06 | `tests/golden` | ۳۰۰ تصویر + انتظارات | ۵ |

---

## ۱۱. ترتیب اجرا (۱۰ روز اول)

| روز | کار | خروجی |
|---|---|---|
| ۱ | P0-01..P0-03 | هسته می‌سازد، بافر/خطا کار می‌کند |
| ۲ | P0-05..P0-07 | decode/encode/رنگ |
| ۳ | P0-08..P0-09 | tiling + blend با تست |
| ۴ | P0-10..P0-11 | backend + context |
| ۵ | P0-12..P0-13 | CLI قابل اجرا روی PNG واقعی |
| ۶ | P1-01 | ncnn روی Vulkan/CPU + اجرای x4plus |
| ۷ | P1-03 | SR کاشی‌ای واقعی |
| ۸ | P1-04..P1-05 | detect/align |
| ۹ | P1-06, P1-08 | restore + composite |
| ۱۰ | P1-09..P1-12 | دروازه + گراف + پریست + دمو |

**خروجی روز ۱۰:** `atlas-cli enhance portrait.jpg out.png --preset natural` که چهره را جدا بازسازی می‌کند و در گزارش، امتیاز هویت می‌دهد.

---

## ۱۲. تعریف آماده/انجام برای هر فایل

**Ready:** هدف روشن، API امضاشده، تست نوشته‌شده (حتی خالی)، وابستگی‌ها موجود.

**Done:**
1. کد + تست واحد سبز.
2. تست روی ۳ بک‌اند (Vulkan/CPU/کلاسیک) یا دلیل مستند.
3. اندازه‌گیری زمان/حافظه ثبت‌شده.
4. خطاها با کد مشخص، نه کرش.
5. یک پاراگراف مستند در همان پوشه (`README.md`).
6. **یادداشت پایان فایل** در `docs/notes/JOURNAL.md`.

---

## ۱۳. بافر و ریسک زمانی

| فاز | تخمین | بافر |
|---|---|---|
| P0 | ۱۵ PD | +۳ |
| P1 | ۱۵ PD | +۵ (مدل‌ها) |
| P2 | ۱۴ PD | +۳ |
| P3 | ۱۰ PD | +۴ (کالیبراسیون) |
| P4 | ۲۰ PD | +۸ (لایسنس/مدل) |
| P5 | ۱۸ PD | +۵ |
| P6 | ۱۵ PD | +۳ |
| UI/ابزار/تست | ۲۷ PD | +۵ |
| **جمع** | **۱۳۴ PD** | **~۱۷۰ PD** |

با ۲ مهندس تمام‌وقت ≈ ۴ ماه کاری خالص؛ با ۳ نفر ≈ ۳ ماه + همپوشانی QA/طراحی.

---

## 📝 یادداشت من (پایان این فایل)

**تاریخ:** ۲۰۲۶-۱۱-۱۱ · **فایل:** `docs/18-build-plan-wbs.md`

- **آنچه مسلم شد:** مسیر P0→P1 در عمل یک دموی واقعی داد: خروجی ۴× از یک عکس JPEG واقعی، با کدک واقعی و مدل واقعی (نه PPM، نه Lanczos).
- **آنچه غافلگیر کرد:** روی CPU بدون GPU، مدل سنگین ۱۲ برابر کندتر از مدل سبک است؛ این عدد باید در UI به کاربر نشان داده شود، وگرنه «کند است» به‌عنوان باگ گزارش می‌شود.
- **تصمیم:** ارزیابی (P3-04) قبل از P4 ساخته می‌شود؛ و P3 حالا یک شاهد عینی دارد: مدل عمومی روی چهره، هویت را خراب می‌کند حتی وقتی «قشنگ‌تر» است.
- **ریسک:** تخمین PDها برای تیم آشناست؛ اگر تیم تازه‌کار باشد ۳۰–۵۰٪ بافر لازم است. همچنین مسیر stb→libjpeg-turbo یک بدهی فنی است که نباید تا M2 بماند.
- **قدم بعدی:** Vulkan (P1-01) و سپس pipeline چهره با دروازهٔ هویت.

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

## ۱.۵ وضعیت پیشرفت (به‌روزرسانی ۲۰۲۶-۱۱-۱۱)

| مورد | وضعیت | یادداشت |
|---|---|---|
| P0-01 CMake | ✅ انجام شد | بدون وابستگی خارجی، گزینه‌های CLI/TESTS |
| P0-02 types | ✅ | `ImageF32`, `Rect`, `TilePlan`, `ErrorCode` |
| P0-03 buffer | ✅ | crop/pad_reflect/clamp01 (crop فعلاً کپی) |
| P0-04 errors/log | ✅ (حداقلی) | لاگ فایل چرخشی در P6 |
| P0-05/P0-06 codec | ⏳ | موقتاً PPM؛ libjpeg-turbo/libpng بعدی |
| P0-07 color | ⏳ | تبدیل خطی در P1 |
| P0-08/P0-09 tiling/blend | ✅ / ⏳ | کاشی با تست پوشش؛ دوخت در P1 |
| P0-10 classical backend | ✅ | Lanczos+unsharp، ~۹ MP/s روی CPU تک‌رشته |
| P0-11/12 context+CLI | ✅ (حداقلی) | version/devices/enhance |
| P0-13 tests | ✅ | ۱۱ تست، صفر شکست |

**Gate P0 فعلاً:** روی CPU سبز است؛ برای «روی iGPU» باید P1-01 (ncnn) اضافه شود.

## ۲. فاز P0 — اسکلت اجرایی (۱۵ نفر-روز)

| # | فایل | هدف | API/نکته | تست پذیرش | PD | وابسته |
|---|---|---|---|---|---|---|
| P0-01 | `core/CMakeLists.txt` | ساخت هسته با presetها | `ATLAS_BACKEND_*` | `cmake --build` سبز | ۱ | — |
| P0-02 | `include/atlas/types.h` | `ImageF32`, `Rect`, `Tensor`, `Error` | بدون وابستگی خارجی | unit | ۰٫۵ | — |
| P0-03 | `src/image/buffer.cpp` | تخصیص/کپی/برش، alignment | page-aligned، بدون copy در برش | unit + ASAN | ۱ | P0-02 |
| P0-04 | `src/util/errors.cpp`, `log.cpp` | کدهای خطا، لاگ چرخشی | `E_OOM`, `E_MODEL`, … | unit | ۰٫۵ | — |
| P0-05 | `src/io/decode.cpp` | JPEG/PNG/WebP/BMP + EXIF orientation | libjpeg-turbo/libpng/libwebp | تست ۲۰ فایل | ۲ | P0-03 |
| P0-06 | `src/io/encode.cpp` | JPEG/PNG/WebP/TIFF + کیفیت | ۸/۱۶ بیت | round-trip PSNR>۴۵ | ۲ | P0-05 |
| P0-07 | `src/image/color.cpp` | خطی‌سازی sRGB، LittleCMS | `to_linear/from_linear` | مقایسه با مرجع | ۱٫۵ | P0-03 |
| P0-08 | `src/image/tiling.cpp` | کاشی‌بندی + overlap + reflect | `plan_grid`, `iter_tiles` | تست مرزها | ۱٫۵ | P0-02 |
| P0-09 | `src/image/blend.cpp` | دوخت raised-cosine | بدون seam | تست SSIM ناحیهٔ مرز | ۱ | P0-08 |
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

- **آنچه مسلم شد:** مسیر P0→P1 با ۱۰ روز کار، یک دموی واقعی می‌دهد؛ P4 (Reference) بزرگ‌ترین بلوک و بزرگ‌ترین ریسک است.
- **تصمیم:** ارزیابی (P3-04) قبل از P4 ساخته می‌شود تا بدون عدد وارد Reference نشویم.
- **ریسک:** تخمین PDها برای تیم آشناست؛ اگر تیم تازه‌کار باشد ۳۰–۵۰٪ بافر لازم است.
- **قدم بعدی:** شروع P0-01 و ساخت اسکلت واقعی کد + اولین CLI.

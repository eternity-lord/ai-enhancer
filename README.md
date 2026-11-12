# ai-enhancer — کدنام «Atlas»

> **مأموریت:** ساخت قوی‌ترین «بهبوددهنده و بزرگ‌نمای تصویر» (Image Enhancer / Upscaler) به‌صورت **کاملاً آفلاین و لوکال**، برای ویندوز، اندروید، iOS، مک و لینوکس — روی **هر سیستمی**: با کارت گرافیک مجزا، با گرافیک داخلی (iGPU)، و حتی **بدون هیچ GPU** (فقط CPU).

این مخزن در فاز **تحقیق + طراحی معماری** است. هدف این فاز این بود که دقیقاً بفهمیم
[LetsEnhance.io](https://letsenhance.io/nl/boost) چه قابلیت‌هایی دارد، هر قابلیت **از نظر فنی چطور کار می‌کند**،
و بعد یک نقشهٔ ساخت کامل برای نسخهٔ لوکال و چندسکویی بنویسیم.

---

## ۱) خلاصهٔ یک‌دقیقه‌ای: LetsEnhance چه می‌کند و چطور؟

- **۷ مدل بزرگ‌نمایی تخصصی**: `Prime` (پیش‌فرض، عکس واقعی)، `Balanced` (عکس خراب/کم‌کیفیت)، `Gentle` (متن/لوگو/UI)، `Strong` (خرابی شدید)، `Ultra` (حداکثر بازسازی، دو اسلایدر Intensity و Size of changes)، `Digital Art` (تصویرسازی/انیمه/AI art با اسلایدر Strength)، `Old Photo` (عکس قدیمی + رنگ‌آمیزی، خروجی ثابت ~۱ مگاپیکسل).
- **Light AI**: یک شبکهٔ عصبی برای اصلاح نور/رنگ/کنتراست/هایلایت-سایه با شدت قابل تنظیم (شبیه HDR نرم).
- **حذف نویز و آرتیفکت JPEG** به‌صورت خودکار در همهٔ مدل‌ها + عملیات مستقل `decompress` (moderate/strong/auto) و `sharpen` و `polish`.
- **حذف پس‌زمینه** (تا ۱۶ مگاپیکسل، خروجی PNG، دسته‌بندی General/Car)، **بزرگ‌نمایی ۱x تا ۱۶x**، تعیین عرض/ارتفاع/DPI (پرینت ۳۰۰ DPI)، **پردازش دسته‌ای تا ۲۰ فایل**، `Try all` برای مقایسهٔ چند مدل.
- **مولد تصویر ۲K**، **Chat Editor** (ویرایش با پرامپت زبان طبیعی)، **Image to Video**، و API جداگانه به‌نام **Claid.ai** با عملیات زنجیره‌ای (restorations/resizing/color/background/padding/privacy/generative).
- **زیرساخت:** سرورهای ابری با **GPU NVIDIA A100 (MIG) روی GKE**، سرو **NVIDIA Triton Inference Server**، صف/پردازش async، اتوسکیل تا **۱۵۰+ تصویر در ثانیه**، بیش از **۱۰۰ میلیون عکس** پردازش‌شده. یعنی همهٔ مدل‌ها سمت سرور اجرا می‌شوند و هیچ پردازش محلی وجود ندارد.

جزئیات کامل + منابع در [docs/01-letsenhance-teardown.md](docs/01-letsenhance-teardown.md) و
[docs/02-how-it-works.md](docs/02-how-it-works.md).

---

## ۲) نقشهٔ مستندات

| فایل | موضوع |
|---|---|
| [docs/README.md](docs/README.md) | فهرست و مسیر مطالعه |
| [docs/00-executive-summary.md](docs/00-executive-summary.md) | خلاصهٔ مدیریتی، تصمیم‌های کلیدی، ریسک‌ها |
| [docs/01-letsenhance-teardown.md](docs/01-letsenhance-teardown.md) | تحلیل کامل محصول LetsEnhance: همهٔ قابلیت‌ها، مدل‌ها، پارامترها، محدودیت‌ها، قیمت، API |
| [docs/02-how-it-works.md](docs/02-how-it-works.md) | «چطور این کار را می‌کنند»: علم Super-Resolution، پایپ‌لاین، معماری سرور |
| [docs/03-feature-spec.md](docs/03-feature-spec.md) | مشخصات محصول ما: برابری قابلیت‌ها + معیار پذیرش هر قابلیت |
| [docs/04-model-stack.md](docs/04-model-stack.md) | پشتهٔ مدل‌های اوپن‌سورس، لایسنس‌ها، انتخاب مدل، برنامهٔ مدل اختصاصی |
| [docs/05-architecture.md](docs/05-architecture.md) | معماری نرم‌افزار، هستهٔ مشترک، بک‌اندها، tiling، صف کار |
| [docs/06-cross-platform.md](docs/06-cross-platform.md) | ماتریس ویندوز/اندروید/iOS/مک/لینوکس/مرورگر؛ iGPU و بدون GPU |
| [docs/07-performance.md](docs/07-performance.md) | بودجهٔ زمان، کوانتیزه‌سازی، بهینه‌سازی و برنامهٔ بنچمارک |
| [docs/08-ui-ux.md](docs/08-ui-ux.md) | طراحی رابط کاربری، پریست‌ها، مقایسه، دسترس‌پذیری، RTL |
| [docs/09-formats-color.md](docs/09-formats-color.md) | فرمت‌ها، ۱۶ بیت، ICC، CMYK، DPI، EXIF، PDF/TIFF، AVIF/JXL |
| [docs/10-roadmap.md](docs/10-roadmap.md) | نقشهٔ راه اجرایی M0…M7 + Definition of Done |
| [docs/11-legal-licensing.md](docs/11-legal-licensing.md) | لایسنس مدل‌ها، استفادهٔ تجاری، حریم خصوصی، قوانین استور |
| [docs/12-build-test.md](docs/12-build-test.md) | ساخت، CI، بسته‌بندی، دانلودر مدل، تست‌ها |
| [docs/13-risks-open-questions.md](docs/13-risks-open-questions.md) | ریسک‌ها، سؤال‌های باز، لاگ تصمیم‌ها |
| [docs/14-masterplan-real-photo-face.md](docs/14-masterplan-real-photo-face.md) | **نقشهٔ مادر: قوی‌ترین برای عکس واقعی با چهرهٔ واقعی** |
| [docs/15-photo-face-pipeline.md](docs/15-photo-face-pipeline.md) | پایپ‌لاین چهره: تراز، بازسازی، دروازهٔ هویت، ترکیب |
| [docs/16-model-training-plan.md](docs/16-model-training-plan.md) | مدل‌های چهره، Reference-aware، آموزش و دیستیل |
| [docs/17-evaluation-protocol.md](docs/17-evaluation-protocol.md) | ارزیابی هویت/بافت/انصاف با آستانهٔ عبور |
| [docs/18-build-plan-wbs.md](docs/18-build-plan-wbs.md) | WBS فایل‌به‌فایل + ترتیب ۱۰ روز اول |
| [docs/19-identity-gate.md](docs/19-identity-gate.md) | دروازهٔ هویت + مجموعهٔ طلایی: کد، کالیبراسیون، محدودیت‌ها |
| [docs/20-degrade-restore.md](docs/20-degrade-restore.md) | حذف نویز و آرتیفکت JPEG پیش از بزرگ‌نمایی: اعداد و محدودیت‌ها |
| [docs/21-desktop-ui.md](docs/21-desktop-ui.md) | پوستهٔ دسکتاپ با تم Liquid Glass: معماری، کنترل‌ها، محدودیت‌ها |
| [docs/22-native-app.md](docs/22-native-app.md) | برنامهٔ واقعی ویندوز (Atlas.exe): پنجرهٔ نیتیو، بسته‌بندی، محدودیت‌ها |
| [docs/notes/JOURNAL.md](docs/notes/JOURNAL.md) | دفتر یادداشت‌های مهندسی (هر فایل یک ورودی) |
| [docs/research/sources.md](docs/research/sources.md) | منابع و تاریخ دسترسی (شفافیت پژوهشی) |
| [models/catalog.json](models/catalog.json) | کاتالوگ ماشین‌خوان مدل‌ها (وظیفه، فرمت، لایسنس، تیر) |

---

## ۳) اصول طراحی نسخهٔ لوکال

1. **آفلاین‌به‌شرط اول**: هیچ تصویری از دستگاه خارج نمی‌شود؛ بدون تلمتری؛ بدون نیاز به حساب.
2. **یک هسته، چند پوسته**: هستهٔ C++20 (پردازش تصویر + استنتاج + tiling) یک‌بار نوشته می‌شود و در ویندوز/اندروید/iOS/مک/لینوکس استفاده می‌شود.
3. **بک‌اند چندلایه با fallback خودکار**: `ncnn-Vulkan → ONNX Runtime (DirectML/OpenVINO/CUDA/TensorRT/CoreML/QNN) → ncnn-CPU → WebGPU/WASM`.
4. **همیشه جواب می‌دهد**: اگر GPU نبود، مسیر «CPU + مدل‌های سبک/کوانتیزه» و در بدترین حالت مسیر «کلاسیک پیشرفته» (Lanczos + NLM + deconvolution + guided filter) اجرا می‌شود.
5. **کیفیت قابل اندازه‌گیری**: هر مدل با تست طلایی (golden images) + معیارهای PSNR/SSIM/LPIPS/DISTS + آزمون انسانی A/B سنجیده می‌شود، نه با حس.
6. **شفافیت لایسنس**: هر مدل در کاتالوگ، فیلد `license` و `commercial_ok` دارد؛ مدل‌های غیرتجاری فقط به‌صورت پلاگین اختیاری و با هشدار نصب می‌شوند.

---

## ۴) وضعیت فعلی

- [x] **M0 — تحقیق و طراحی**: تحلیل کامل سایت، علم ماجرا، مشخصات، معماری، نقشهٔ راه.
- [x] **M0.5 — نقشهٔ کیفیت برای چهرهٔ واقعی**: نوار کیفیت عددی، دروازهٔ هویت (ArcFace)، حالت Reference-locked، پروتکل ارزیابی، WBS فایل‌به‌فایل.
- [x] **P0 — اسکلت اجرایی**: هستهٔ C++20 + کاشی‌بندی + مسیر کلاسیک + CLI + ۱۱ تست (بدون وابستگی خارجی).
- [x] **P1 — مسیر واقعی**: کدک واقعی JPEG/PNG/BMP/TGA (stb، vendored) + بک‌اند ncnn (CPU، آمادهٔ Vulkan) + موتور SR کاشی‌ای با دوخت raised-cosine + `tools/fetch_models.py` + ۱۹ تست. جزئیات و اعداد: [docs/18 §۱.۵–۱.۶](docs/18-build-plan-wbs.md).
- [x] **P1.5 — قابل اندازه‌گیری و درست دیده شدن**: EXIF orientation (۸ حالت) + رنگ خطی sRGB + متریک PSNR/SSIM + `atlas-cli eval` با JSON و دروازهٔ CI؛ ۳۵ تست. جزئیات: [docs/18 §۱.۷](docs/18-build-plan-wbs.md).
- [x] **P1.6 — وفاداری رنگ و آلفا**: تبدیل ICC (Display P3/AdobeRGB → sRGB) از `iCCP` و `APP2`، حفظ آلفای PNG/TGA در کل مسیر SR؛ ۴۵ تست. جزئیات: [docs/18 §۱.۸](docs/18-build-plan-wbs.md).
- [x] **P1.7 — پایان P1**: WebP (اختیاری)، انتقال EXIF، برچسب sRGB در خروجی، `--gpu`؛ **۴۹ تست**. جمع‌بندی: [docs/18 §۱.۱۰](docs/18-build-plan-wbs.md).
- [x] **P3-01/P3-04 — دروازهٔ هویت + مجموعهٔ طلایی**: پروکسی ساختاری (چیدمان + HOG + رنگ) با آستانهٔ مستقل از ArcFace، `atlas-cli gate` و `atlas-cli eval-set`، پذیرش خودکار مطابق docs/17؛ **۶۱ تست**. جزئیات و اعداد: [docs/19](docs/19-identity-gate.md).
- [x] **P2 — عکس خراب**: deblock شرطی ۸×۸ + NLM (patch ۳×۳/search ۵×۵) قبل از SR، با تشخیص خودکار نویز/شبکه؛ **+۴٫۱۷ dB PSNR** روی ورودی خراب و **۶۱ → ۷۱ تست**. جزئیات: [docs/20](docs/20-degrade-restore.md).
- [x] **M1-lite — پوستهٔ دسکتاپ**: پنجرهٔ شیشه‌ای (Liquid Glass) با کتابخانهٔ تصاویر، مقایسهٔ قبل/پس، پاک‌سازی و بزرگ‌نمایی واقعی روی `atlas-cli`؛ ۷ تست سرور + ۱۸ بررسی DOM. جزئیات: [docs/21](docs/21-desktop-ui.md).
- [x] **M1-lite.2 — برنامهٔ واقعی**: `Atlas.exe` تک‌فایل با پنجرهٔ نیتیو (WebView2 + pywebview) و تم شیشه‌ای؛ ساخت با `desktop\build-exe.bat`. جزئیات و محدودیت‌ها: [docs/22](docs/22-native-app.md).
- [ ] **M1 — MVP ویندوز** (نصب‌کننده، آیکن، صف دسته‌ای): Tauri + هستهٔ ncnn-Vulkan + بزرگ‌نمایی ۲x/۴x با tiling + صف دسته‌ای + پریست‌ها.

> **محدودیت صادقانه:** دروازهٔ فعلی پروکسی ساختاری است، نه ArcFace؛ برای گرفتن رگرسیون در CI ساخته شده و «همان شخص» را بیومتریک ثابت نمی‌کند. ArcFace در P3-02 وصل می‌شود.

> **تمرکز نسخهٔ ۱.۰:** «قوی‌ترین برای عکس واقعی با چهرهٔ واقعی» — یعنی هویت حفظ شود، بافت طبیعی بماند و هیچ زیباسازی/توهمی رخ ندهد. جزئیات: [docs/14](docs/14-masterplan-real-photo-face.md).
- [ ] **M2 — بازسازی کامل**: حذف نویز/JPEG، بازسازی چهره، Light AI، حذف پس‌زمینه.
- [ ] **M3 — اندروید**، **M4 — iOS/مک**، **M5 — تیر مولد (Diffusion)**، **M6 — ویدیو**، **M7 — PWA/WebGPU**.

جزئیات و معیار پذیرش هر مرحله: [docs/10-roadmap.md](docs/10-roadmap.md).

---

## ۵) اجرا

### ویندوز (سریع‌ترین راه)

```bat
:: فقط اگر «'cmake' is not recognized» گرفتید — یک‌بار:
winget install --id Kitware.CMake -e
winget install Microsoft.VisualStudio.2022.BuildTools --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools"

:: اگر کامپایلر C++ نبود، یا winget گفت «نصب است» ولی CMake پیدایش نکرد:
powershell -NoProfile -ExecutionPolicy Bypass -File tools\windows\install-msvc.ps1

:: بعد در یک پنجرهٔ تازه (CMD یا PowerShell):
tools\windows\build.bat        :: پیکربندی + ساخت + اجرای ۷۱ تست هسته
tools\windows\run-desktop.bat  :: باز کردن پوستهٔ دسکتاپ روی http://localhost:8000
```

خروجی موفق: `done. atlas-cli = build\Release\atlas-cli.exe`.
راهنمای گام‌به‌گام و رفع خطاها (کامپایلر پیدا نشد، MSVC قدیمی، …):
[tools/windows/README.md](tools/windows/README.md).

### لینوکس / مک

```bash
# ۱) مسیر کلاسیک (بدون هیچ وابستگی خارجی)
cmake -S core -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./build/atlas-tests                                 # ۷۱ تست
./build/atlas-cli enhance photo.jpg out2x.png --scale 2   # پاک‌سازی خودکار + بزرگ‌نمایی
./build/atlas-cli restore photo.jpg clean.png             # فقط حذف نویز/JPEG
./build/atlas-cli eval original.png out.png --min-psnr 32 --min-ssim 0.90
./build/atlas-cli gate original.png restored.png --face 120,90,256,256
./build/atlas-cli eval-set eval/golden/manifest.tsv

# ۲) پوستهٔ گرافیکی (محلی، آفلاین)
python3 desktop/server.py --port 8000             # → http://localhost:8000
python3 desktop/test_server.py                    # ۹ تست سرور

# ۳) مسیر مدل واقعی (ncnn؛ روی CPU هم اجرا می‌شود، با GPU چند برابر سریع‌تر)
python3 tools/fetch_models.py --from-wheel        # وزن‌ها هرگز در گیت نیستند
cmake -S core -B build-ncnn -DCMAKE_BUILD_TYPE=Release \
      -DATLAS_WITH_NCNN=ON -DCMAKE_PREFIX_PATH=/path/to/ncnn-install
cmake --build build-ncnn -j
./build-ncnn/atlas-cli enhance photo.jpg out4x.png \
      --param models/weights/realesrgan-x4plus.param \
      --weights models/weights/realesrgan-x4plus.bin --scale 4 --tile 256 --overlap 32
```

> کدک‌ها: stb (JPEG/PNG/BMP/TGA، ۸ و ۱۶ بیت) — vendored و بدون وابستگی سیستمی؛ مسیر مهاجرت به libjpeg-turbo/libpng در [docs/18](docs/18-build-plan-wbs.md) P0-05/06.
> جزئیات: [core/README.md](core/README.md) · دستورهای CI/بسته‌بندی: [docs/12](docs/12-build-test.md).

## ۶) لایسنس

لایسنس خود مخزن: **TBD** (پیشنهاد: `Apache-2.0` برای هسته و `MIT` برای پوسته‌ها).
لایسنس مدل‌های third-party جداگانه است؛ جدول کامل در [docs/11-legal-licensing.md](docs/11-legal-licensing.md).

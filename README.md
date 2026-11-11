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

- [x] **M0 — تحقیق و طراحی** (همین کامیت): تحلیل کامل سایت، علم ماجرا، مشخصات، معماری، نقشهٔ راه.
- [ ] **M1 — MVP ویندوز**: Tauri + هستهٔ ncnn-Vulkan + بزرگ‌نمایی ۲x/۴x با tiling + صف دسته‌ای + پریست‌ها.
- [ ] **M2 — بازسازی کامل**: حذف نویز/JPEG، بازسازی چهره، Light AI، حذف پس‌زمینه.
- [ ] **M3 — اندروید**، **M4 — iOS/مک**، **M5 — تیر مولد (Diffusion)**، **M6 — ویدیو**، **M7 — PWA/WebGPU**.

جزئیات و معیار پذیرش هر مرحله: [docs/10-roadmap.md](docs/10-roadmap.md).

---

## ۵) اجرا

هنوز کد اجرایی وجود ندارد (فاز طراحی). دستورهای ساخت، CI و بسته‌بندی در
[docs/12-build-test.md](docs/12-build-test.md) مشخص شده‌اند.

## ۶) لایسنس

لایسنس خود مخزن: **TBD** (پیشنهاد: `Apache-2.0` برای هسته و `MIT` برای پوسته‌ها).
لایسنس مدل‌های third-party جداگانه است؛ جدول کامل در [docs/11-legal-licensing.md](docs/11-legal-licensing.md).

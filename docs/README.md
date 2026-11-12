# مستندات پروژهٔ Atlas — فهرست

این پوشه نتیجهٔ فاز **M0 (تحقیق و طراحی)** است. برای این‌که گم نشوی، مسیر پیشنهادی مطالعه:

```
00 (چرا و چه) → 01 (آن‌ها چه دارند) → 02 (چطور کار می‌کند)
→ 03 (ما چه می‌سازیم) → 04 (با چه مدل‌هایی) → 05 (با چه معماری)
→ 06 (روی چه سیستم‌هایی) → 07 (با چه سرعتی) → 08/09 (تجربه و فایل‌ها)
→ 10 (با چه ترتیبی) → 11/12/13 (حقوقی، ساخت، ریسک)
```

| # | فایل | یک‌خطی |
|---|---|---|
| 00 | [executive-summary](00-executive-summary.md) | جمع‌بندی تصمیم‌ها برای کسی که وقت کم دارد |
| 01 | [letsenhance-teardown](01-letsenhance-teardown.md) | تحلیل کامل و مستند قابلیت‌های LetsEnhance |
| 02 | [how-it-works](02-how-it-works.md) | علم و مهندسی پشت این قابلیت‌ها |
| 03 | [feature-spec](03-feature-spec.md) | مشخصات کارکردی + معیار پذیرش |
| 04 | [model-stack](04-model-stack.md) | انتخاب مدل‌ها، لایسنس، استراتژی کیفیت |
| 05 | [architecture](05-architecture.md) | معماری هسته/پوسته، tiling، صف، API داخلی |
| 06 | [cross-platform](06-cross-platform.md) | ویندوز/اندروید/iOS/مک/لینوکس/وب + iGPU و CPU |
| 07 | [performance](07-performance.md) | سرعت، حافظه، کوانتیزه‌سازی، بنچمارک |
| 08 | [ui-ux](08-ui-ux.md) | رابط کاربری و جریان کار |
| 09 | [formats-color](09-formats-color.md) | فرمت/رنگ/متادیتا/چاپ |
| 10 | [roadmap](10-roadmap.md) | M0…M7 + DoD + تخمین زمان |
| 11 | [legal-licensing](11-legal-licensing.md) | لایسنس، تجاری‌سازی، حریم خصوصی |
| 12 | [build-test](12-build-test.md) | ساخت، CI، بسته‌بندی، تست |
| 13 | [risks-open-questions](13-risks-open-questions.md) | ریسک‌ها و سؤال‌های باز |
| **14** | [masterplan-real-photo-face](14-masterplan-real-photo-face.md) | **نقشهٔ مادر برای عکس واقعی + چهرهٔ واقعی (کیفیت عددی، فازها، دروازه‌ها)** |
| **15** | [photo-face-pipeline](15-photo-face-pipeline.md) | مشخصات پایپ‌لاین چهره: تراز، بازسازی، دروازهٔ هویت، ترکیب |
| **16** | [model-training-plan](16-model-training-plan.md) | مدل‌های چهره، Reference-aware، آموزش/دیستیل/کوانت |
| **17** | [evaluation-protocol](17-evaluation-protocol.md) | مجموعهٔ طلایی، متریک‌ها، آستانه‌ها، داوری انسانی، انصاف |
| **18** | [build-plan-wbs](18-build-plan-wbs.md) | **WBS فایل‌به‌فایل با تخمین و ترتیب اجرا** |
| **19** | [identity-gate](19-identity-gate.md) | **دروازهٔ هویت و مجموعهٔ طلایی: پیاده‌سازی، کالیبراسیون، محدودیت‌ها** |
| **20** | [degrade-restore](20-degrade-restore.md) | **حذف نویز و آرتیفکت JPEG: الگوریتم، اعداد، محدودیت‌ها (P2)** |
| **21** | [desktop-ui](21-desktop-ui.md) | **پوستهٔ دسکتاپ: متریال Liquid Glass، معماری وب+CLI، محدودیت‌ها** |
| — | [notes/JOURNAL](notes/JOURNAL.md) | دفتر یادداشت‌ها (تصمیم‌ها، فرض‌ها، سؤال‌ها) |
| — | [research/sources](research/sources.md) | منابع، تاریخ دسترسی، میزان اطمینان |

## قواعد نگارش این مستندات

- هر عدد یا ادعای مربوط به LetsEnhance، منبع دارد (پایان هر فایل + `research/sources.md`).
- هر چیزی که **استنباط** ماست (مثلاً معماری داخلی مدل‌ها) با برچسب «استنباط» یا «تأییدنشده» مشخص شده است.
- اعداد سرعت در این فاز **برآورد** هستند و در M1 با هارنس بنچمارک اندازه‌گیری و جایگزین می‌شوند.
- لایسنس هر مدل باید پیش از بسته‌بندی تجاری دوباره از مخزن اصلی تأیید شود (`license_verified` در `models/catalog.json`).

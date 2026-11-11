# ۰۴ — پشتهٔ مدل‌ها، لایسنس‌ها و استراتژی کیفیت

هدف: رسیدن به کیفیت هم‌ارز یا بهتر از LetsEnhance **بدون سرور**، با مدل‌هایی که روی CPU/iGPU هم اجرا شوند.

---

## ۱. معیار انتخاب مدل

| معیار | وزن | توضیح |
|---|---|---|
| کیفیت در دامنهٔ هدف | ۳۵٪ | نتیجه در تست طلایی + A/B انسانی، نه شهرت مدل |
| سازگاری سخت‌افزاری | ۲۰٪ | وجود نسخهٔ ncnn/ONNX، اجرا روی Vulkan/CPU |
| لایسنس تجاری | ۲۰٪ | `commercial_ok` برای بستهٔ پیش‌فرض |
| سرعت/حافظه | ۱۵٪ | ms/MP و اوج RAM |
| پایداری/نگهداری | ۱۰٪ | به‌روزرسانی، issues، determinism |

**قاعدهٔ طلایی:** هیچ مدلی بدون تست روی **سه** نوع تصویر (پرتره، متن/UI، آرت) و **سه** تیر سخت‌افزاری وارد پیش‌فرض نمی‌شود.

---

## ۲. مدل‌ها به تفکیک وظیفه

### ۲.۱ Super-Resolution (خط پایه)

| نقش | مدل پیشنهادی | معماری | مقیاس | حجم تقریبی | فرمت | لایسنس | تیر | یادداشت |
|---|---|---|---|---|---|---|---|---|
| سبک/موبایل | **SPAN 2x/4x** | SPAN | 2/4 | ۲–۱۰MB | ONNX/ncnn | بررسی شود (MIT/Apache ادعا شده) | C | سریع‌ترین کیفیت قابل قبول |
| سبک/CPU | **Real-ESRGAN general x4v3** | SRVGG | 4 | ~۵MB | ncnn/ONNX | BSD-3 | C | `denoise strength` قابل تنظیم |
| پیش‌فرض عکس | **Real-ESRGAN x4plus** | RRDB | 4 | ~۶۴MB | ncnn/ONNX | BSD-3 | B/A | استاندارد صنعتی |
| انیمه/آرت | **Real-ESRGAN anime6B** | RRDB | 4 | ~۱۸MB | ncnn/ONNX | BSD-3 | B/C | خطوط تمیز |
| انیمه سبک | **Real-CUGAN** | CUNet | 2/3/4 | ۱–۵MB | ncnn | MIT | C | عالی برای ویدیو/موبایل |
| شارپ تجاری | **4x-UltraSharp / 4x-Remacri** | ESRGAN | 4 | ~۶۴MB | ncnn/ONNX | ⚠️ غیرتجاری/نامشخص | A (پلاگین) | کیفیت چشم‌نواز، ریسک حقوقی |
| حداکثری (GPU) | **SwinIR / HAT / DAT** | Transformer | 2/4 | ۴۰–۱۵۰MB | ONNX | Apache/MIT (تأیید شود) | A | کند، PSNR بالا |
| حداکثری (مولد) | **SUPIR / SeedVR2** | Diffusion / DiT | 4 | ۱–۷GB | PyTorch→ONNX/GGUF | Apache-2.0 (تأیید شود) | A+ | «Ultra» واقعی؛ ۸GB+ |

> حجم‌ها تقریبی‌اند و برای برنامه‌ریزی بسته‌بندی ذکر شده‌اند؛ عدد قطعی در `models/catalog.json` بعد از انتخاب نسخه ثبت می‌شود.

### ۲.۲ بازسازی چهره

| مدل | لایسنس | تجاری | حجم | نقش |
|---|---|---|---|---|
| **GFPGAN v1.4** | Apache-2.0 (با استثنای اجزای ثالث) | ✅ (با بازبینی) | ~۳۳۲MB (pth) / ~۱۶۰MB ONNX | **پیش‌فرض** بازسازی چهره |
| CodeFormer | S-Lab 1.0 | ❌ غیرتجاری | ~۳۵۹MB | پلاگین اختیاری با هشدار (کیفیت + کنترل fidelity) |
| GPEN / RestoreFormer++ | بررسی شود | ⚠️ | — | جایگزین اگر لایسنس مناسب بود |
| تشخیص چهره: SCRFD / RetinaFace | MIT/Apache | ✅ | ۲–۱۰MB | دیتکتور سبک |

### ۲.۳ نویز، آرتیفکت، جزئیات

| مدل | وظیفه | لایسنس | حجم | تیر |
|---|---|---|---|---|
| FBCNN | تخمین/حذف آرتیفکت JPEG | Apache-2.0 (تأیید) | ~۲۵MB | B |
| SCUNet | حذف نویز طبیعی/فشردگی | BSD-3/Apache (تأیید) | ~۱۷MB | A |
| NAFNet | denoise/deblur عمومی | MIT (تأیید) | ~۶۷MB | A |
| fastNlMeans (OpenCV) | denoise کلاسیک | Apache-2.0 (OpenCV) | ۰ | C |
| unsharp/guided filter | شارپن و refine | — | ۰ | همه |

### ۲.۴ پس‌زمینه و inpaint

| مدل | وظیفه | لایسنس | حجم | تیر |
|---|---|---|---|---|
| **BiRefNet** (lite) | segmentation/matting | **MIT** | ۴۴–۲۰۰MB | B/C |
| U²-Net / U²-Net-lite | segmentation سبک | Apache-2.0 | ۴–۱۷۶MB | C |
| **LaMa** | inpaint عمومی | Apache-2.0 | ~۲۰۰MB | B |
| MI-GAN | inpaint سبک برای موبایل | Apache/MIT (تأیید) | ~۲۰MB | C |
| DDColor | رنگ‌آمیزی | Apache-2.0 | ~۱۰۰MB | B |

> ⚠️ **RMBG-1.4 / RMBG-2.0** کیفیت بالایی دارند اما لایسنس **غیرتجاری** (BRIA) دارند؛ فقط پلاگین اختیاری.

### ۲.۵ رنگ/نور

| روش | نوع | کیفیت | سرعت | تجاری |
|---|---|---|---|---|
| gray-world + white-patch + percentile stretch + curve + CLAHE + vibrance | کلاسیک | خوب | فوری | ✅ |
| مدل کوچک tone-mapping (HDRNet-lite/DCE) | شبکهٔ کوچک | بهتر در نور سخت | متوسط | ✅ (خودمان آموزش می‌دهیم) |
| 3D LUT قابل ساخت از پریست‌ها | جدول | قابل پیش‌بینی | فوری | ✅ |

### ۲.۶ ویدیو (M6)

| مورد | انتخاب |
|---|---|
| استخراج/انکود | FFmpeg (LGPL/GPL — نسخهٔ LGPL برای بسته‌بندی تجاری) |
| SR فریم | همان مدل‌های تصویر + کاشی |
| سازگاری زمانی | warp با optical flow (RAFT-lite) + blend فریم قبلی؛ یا RIFE برای درون‌یابی فریم |
| مدل اختصاصی ویدیو | SeedVR2/FlashVSR فقط در تیر A+ (نیازمند GPU قوی) |

---

## ۳. لایه‌بندی تیر سخت‌افزاری و مدل پیش‌فرض

| تیر | سخت‌افزار نمونه | مدل SR | کاشی | FP | زمان تقریبی ۱۲MP→۴x |
|---|---|---|---|---|---|
| **A+** | RTX 4070+ / M3 Pro+ / ۱۶GB+ VRAM | x4plus + face + (اختیاری SUPIR) | ۵۱۲–۷۶۸ | FP16 | ۱۰–۴۰s (+Diffusion: دقیقه) |
| **A** | GTX 1650 / M1 / iGPU Iris Xe | x4plus یا SPAN + face | ۲۵۶–۳۸۴ | FP16 | ۴۰–۱۲۰s |
| **B** | iGPU قدیمی / ۴–۸GB RAM | SPAN/x4v3 + face سبک | ۱۲۸–۲۵۶ | FP16 | ۲–۵ دقیقه |
| **C** | بدون GPU / ۴GB RAM / موبایل میان‌رده | SPAN/Real-CUGAN/مدل INT8 | ۹۶–۱۹۲ | INT8 | ۵–۲۰ دقیقه (یا حالت سریع) |

اعداد برآوردی‌اند؛ در M1 با بنچمارک جایگزین می‌شوند.

---

## ۴. روتر محتوا (Auto-Pilot)

```
ورودی
 ├─ دیتکت چهره (SCRFD) ──► تعداد/اندازهٔ چهره
 ├─ دیتکت متن (DBNet-lite یا heuristic لبه)
 ├─ متریک بلوک JPEG (FFT/8x8) ──► شدت فشردگی
 ├─ نویز (Laplacian variance در نواحی تخت)
 ├─ تاری (gradient histogram)
 ├─ کلاسیفایر محتوا (MobileNet/EfficientNet-lite: photo/anime/text/art/old)
 └─ ورن (Venn) تصمیم:
       عکس سالم + بزرگ‌نمایی کم → Prime-like (x4plus)
       عکس فشرده/نویزی → Balanced (x4v3 + FBCNN)
       متن/UI/لوگو → Gentle (مدل محافظه‌کار + ماسک لبه)
       خرابی شدید → Strong (SCUNet→x4plus)
       آرت/انیمه → Digital Art
       عکس قدیمی/B&W → Old Photo (inpaint + DDColor + face + SR)
```

خروجی روتر: `{model, scale_passes, denoise, deblock, face, light, tile, backend}` + **توضیح انسانی** برای UI.

---

## ۵. برنامهٔ مدل اختصاصی Atlas (تمایز واقعی)

هدف: مدلی که در دامنهٔ هدف از مدل‌های عمومی بهتر باشد و روی موبایل هم اجرا شود.

### ۵.۱ داده

- ۵۰–۲۰۰ هزار تصویر با کیفیت بالا (عکس، پرتره، محصول، آرت، اسکرین‌شات، سند).
- **پایپ‌لاین تخریب واقع‌گرا**: blur (isotropic/anisotropic) + noise (Gaussian/Poisson) + resize (bicubic/bilinear/area) + JPEG (q=20–۹۵) + ringing + chroma subsampling.
- برای متن/UI: رندر مصنوعی متن با فونت‌های متنوع + اسکرین‌شات‌های واقعی.
- مجموعهٔ اعتبارسنجی «چالش»: تصاویر واقعی کاربران (با رضایت) + تصاویر معیوب شناخته‌شده.

### ۵.۲ آموزش

1. **پیش‌آموزش** روی degradation عمومی با L1+LPIPS، سپس فاین‌تیون با GAN.
2. **سه اکسپرت**: `photo`, `text/ui`, `art/anime` + یک **روتر** سبک.
3. **دیستیل** از مدل بزرگ (مثلاً SUPIR یا HAT) به مدل کوچک SPAN با distillation روی خروجی و ویژگی‌های میانی → کیفیت نزدیک با ۱۰٪ محاسبه.
4. **کوانتیزه‌سازی آگاه به کیفیت**: FP16 → INT8 با calibration روی مجموعهٔ طلایی؛ شرط پذیرش: افت LPIPS < ۲٪.

### ۵.۳ ارزیابی و انتشار

- جدول نتایج در `bench/results/` با نسخهٔ مدل، seed، و دستگاه.
- هیچ مدل جدیدی بدون «تصویر قبل/بعد» در `docs/examples/` منتشر نمی‌شود.
- نسخه‌بندی SemVer برای مدل‌ها: `atlas-photo-v1.2.0`.

### ۵.۴ چرا این کار جواب می‌دهد

LetsEnhance هم دقیقاً همین کار را کرده: مدل عمومی نگرفته، بلکه روی دامنه‌های مشخص (عکس، آرت، متن، قدیمی) متخصص ساخته و روتر گذاشته. تفاوت ما این است که همان تخصص را در **۱۵۰MB** و روی **CPU** هم می‌خواهیم.

---

## ۶. بسته‌بندی و مدیریت مدل

```
models/
  catalog.json           # منبع حقیقت: id, task, files[], sha256, license, commercial_ok, tier
  weights/               # خارج از git (gitignore)
  presets/
    photo-standard.json
    text-safe.json
    anime.json
    old-photo.json
```

- هر ورودی کاتالوگ: `id`, `display_name`, `task`, `arch`, `scale`, `input_size`, `files[{url,sha256,size_mb,format}]`, `license`, `license_url`, `commercial_ok`, `tier`, `backend`, `notes`, `license_verified`.
- دانلودر: resumable، بررسی SHA-256، آینهٔ قابل‌تنظیم، حالت «آفلاین کامل» با بستهٔ zip امضاشده.
- مدل‌ها هرگز در گیت نگهداری نمی‌شوند (به `release assets` یا CDN/HF می‌روند).
- **قانون:** اگر `commercial_ok=false`، مدل در بستهٔ پیش‌فرض نیست و نصب آن نیازمند تأیید صریح کاربر است.

---

## ۷. نگاشت مدل‌های LetsEnhance به پشتهٔ ما

| LetsEnhance | معادل پیشنهادی ما | اختلاف هدف |
|---|---|---|
| Prime | Real-ESRGAN x4plus + face + Light کم | کیفیت برابر با کنترل بیشتر |
| Balanced | x4v3 (denoise) + FBCNN + Strong-ish | بهتر در JPEG |
| Gentle | مدل محافظه‌کار + ماسک لبه/متن | حفظ کامل هندسه |
| Strong | SCUNet + x4plus + پاس دوم | کنترل توهم |
| Ultra | SUPIR/SeedVR2 (تیر A+) | واقعی‌تر، آفلاین |
| Digital Art | anime6B / Real-CUGAN | سرعت روی موبایل |
| Old Photo | inpaint + DDColor + GFPGAN + SR | خروجی بزرگ‌تر از ۱MP |
| Light AI | پایپ‌لاین کلاسیک + مدل کوچک | Deterministic |
| Remove BG | BiRefNet-lite + matting | تجاری‌امن |
| Try all | اجرای موازی ۳–۴ مدل | محلی و رایگان |

---

## ۸. کارهای باز

- [ ] تأیید لایسنس تک‌تک مدل‌ها از مخزن اصلی (فیلد `license_verified`).
- [ ] انتخاب نسخهٔ دقیق SPAN و بررسی وجود نسخهٔ ncnn.
- [ ] ارزیابی SeedVR2 روی ONNX/GGUF برای تیر A+.
- [ ] ساخت مجموعهٔ طلایی ۲۰۰ تصویری با تنوع دامنه.
- [ ] تست INT8 روی Vulkan (ncnn) و XNNPACK.

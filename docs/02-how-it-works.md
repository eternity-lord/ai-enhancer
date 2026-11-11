# ۰۲ — این کار از نظر فنی چطور انجام می‌شود؟

این سند دو بخش دارد:
**(الف)** علمی که پشت این قابلیت‌هاست و تقریباً همهٔ سرویس‌های این حوزه (از جمله LetsEnhance) از آن استفاده می‌کنند.
**(ب)** بازسازی مهندسی همان چیزی که در LetsEnhance می‌بینیم، بر اساس شواهد + استنباط، و نقشهٔ پیاده‌سازی لوکال آن.

---

## بخش الف — علم ماجرا

### ۱. مسئله در یک خط

بزرگ‌نمایی معمولی (Bicubic/Lanczos) فقط **درون‌یابی** است: اطلاعات جدیدی تولید نمی‌کند.
Super-Resolution (SR) تلاش می‌کند تابع معکوسِ تخریب را یاد بگیرد و **جزئیات محتمل** را بازسازی کند:

```
تصویر کم‌کیفیت:  y = ( (x ⊗ k) ↓s + n ) → JPEG_q
هدف:            x̂ = f_θ(y)  ≈ x
```

که در آن `k` کرنل تاری (blur)، `s` ضریب کوچک‌سازی، `n` نویز، و `JPEG_q` فشردگی بلوکی است.
مدل روی میلیون‌ها جفت (کم‌کیفیت، سالم) آموزش می‌بیند و **پیشین (prior)** محتوا را یاد می‌گیرد: پوست، پارچه، برگ، متن، لبه.

> جملهٔ کلیدی: SR «پیکسل گم‌شده را پیدا نمی‌کند»؛ **حدس می‌زند**. کیفیت = کیفیت حدس. این دلیل وجود مدل‌های مختلف برای نوع محتواست.

### ۲. خانواده‌های معماری

| خانواده | نمونه‌ها | مزیت | عیب | مناسب برای |
|---|---|---|---|---|
| CNN سبک | FSRCNN, ESPCN, EDSR, **SPAN** | بسیار سریع، کم‌حافظه | جزئیات کمتر، «نرم» | موبایل، CPU، پیش‌نمایش |
| GAN / RRDB | ESRGAN, **Real-ESRGAN**, Real-CUGAN, 4x-UltraSharp | بافت واقعی، لبهٔ تیز، سرعت خوب | توهم جزئیات، گاهی بافت پلاستیکی | خط پایهٔ همه‌جا |
| Transformer | SwinIR, HAT, DAT, PLKSR | PSNR/جزئیات بالا | سنگین، روی CPU کند | کیفیت حداکثری روی GPU |
| Denoise/Restoration | SCUNet, NAFNet, Restormer, FBCNN | حذف نویز/آرتیفکت بدون از دست دادن بافت | نیاز به تنظیم شدت | پیش‌مرحلهٔ SR |
| Diffusion SR | StableSR, SeeSR, **SUPIR**, **SeedVR2** | واقع‌گرایی و بازسازی خارق‌العاده | کند، نیازمند VRAM، ریسک توهم | تیر «Ultra» برای دستگاه قوی |
| Face restoration | GFPGAN, CodeFormer, GPEN, RestoreFormer | بازسازی چشم/دهان/پوست | تغییر هویت در شدت بالا | پرتره، عکس قدیمی |
| Segmentation/Matting | U²-Net, **BiRefNet**, ISNet | حذف پس‌زمینه با آلفا نرم | مو و لبه‌های پیچیده | Background removal |
| Color/Tone | HDRNet, 3D LUT, Retinex, CLAHE | اصلاح نور/رنگ یک‌کلیکی | سلیقه‌ای | Light AI |

### ۳. تابع هزینه و متریک‌ها

```
L = λ_pix · L1 + λ_perc · LPIPS(VGG) + λ_adv · L_GAN + λ_tv · TV
```

- `L1` تنها → تصویر نرم و بیش‌ازحد صاف.
- `LPIPS + GAN` → جزئیات و بافت، ولی ریسک توهم.
- در محصول تجاری، وزن adversarial را برای مدل‌های «محافظه‌کار» (Gentle / Smart Resize) کم می‌کنند و برای «تهاجمی» (Strong / Ultra) زیاد.

متریک‌های ارزیابی ما:

| متریک | چه می‌گوید | محدودیت |
|---|---|---|
| PSNR | وفاداری پیکسلی | با GAN بدتر می‌شود ولی چشم بهتر می‌بیند |
| SSIM | ساختار | به بافت حساس نیست |
| LPIPS / DISTS | ادراک انسانی | به «توهم» پاداش می‌دهد |
| NIQE / BRISQUE | کیفیت بدون مرجع | کلی |
| OCR accuracy | خوانایی متن پس از SR | دامنه‌ای |
| Face embedding cosine | حفظ هویت چهره | دامنه‌ای |
| ΔE2000 | صحت رنگ | — |

### ۴. تکنیک حیاتی: Tiling (کاشی‌بندی)

هیچ کارت گرافیکی نمی‌تواند یک تصویر ۶۴ مگاپیکسلی را در یک پاس ۴x پردازش کند (۸ تریلیون عملیات و چند گیگابایت فعال‌سازی).
راه‌حل: تقسیم به کاشی‌های هم‌پوشان و دوخت با ماسک نرم:

```
tile = 256..512 px, overlap = 16..64 px
blend: weight = raised-cosine / linear feather در ناحیهٔ هم‌پوشانی
```

نکات ریز که کیفیت را تعیین می‌کند:

- کاشی خیلی کوچک → مرزهای تکرارشونده و از دست رفتن context.
- overlap ناکافی → خط‌های عمودی/افقی (seams).
- باید **reflect padding** در لبه‌های تصویر استفاده شود نه zero padding.
- برای مدل‌های transformer/diffusion، context بزرگ‌تر لازم است (tile ≥ ۵۱۲ یا رویکرد «tile با راهنمای تصویر کامل»).
- ترتیب پردازش و cache می‌تواند ۱۰–۳۰٪ سرعت بدهد (پردازش سطر به سطر برای locality).

### ۵. چرا مدل‌های مختلف برای نوع محتوا؟

| محتوا | شکست مدل عمومی | راه‌حل |
|---|---|---|
| عکس واقعی | صاف شدن پوست، بافت پلاستیکی | GAN ملایم + بافت واقعی |
| متن/لوگو/UI | تغییر شکل حروف و خطوط | مدل محافظه‌کار + ماسک لبه |
| انیمه/تصویرسازی | اضافه شدن بافت عکاسی روی رنگ‌های تخت | مدل دامنهٔ غیرعکاسی |
| عکس قدیمی | خط‌وخش و رنگ‌رفتگی | restoration + colorization + SR |
| عکس شدیداً فشرده | تشدید آرتیفکت JPEG | deblock قبل از SR |
| پرتره | چشم/دهان مخدوش | face model + paste-back |

### ۶. بازسازی چهره چطور کار می‌کند؟

```
تشخیص چهره (SCRFD/RetinaFace) → تراز و برش (align 512×512)
   → مدل بازسازی چهره (GFPGAN/CodeFormer) → ماسک نرم
   → paste-back روی تصویر اصلی (feather/Poisson) → شارپن ملایم
```

- `CodeFormer` یک پارامتر `fidelity weight` دارد: پایین = زیبایی بیشتر/هویت کمتر، بالا = وفاداری بیشتر. این همان منطق `Intensity` در محصولات تجاری است.
- برای چهره‌های خیلی کوچک (<۳۲px) بازسازی معتبر نیست؛ باید به‌عنوان «بهبود سلیقه‌ای» برچسب‌گذاری شود.

### ۷. حذف آرتیفکت JPEG

- تشخیص بلوک‌های ۸×۸ و تخمین quality factor (مدل **FBCNN** این را صریح مدل می‌کند).
- حذف با CNN (DnCNN/SCUNet/NAFNet) یا با مدل SR آموزش‌دیده روی فشردگی (Real-ESRGAN با `realesr-general-x4v3` و `denoise strength`).
- ترتیب درست: **decompress → denoise → SR**؛ اگر SR اول اجرا شود آرتیفکت‌ها «پخته» و بزرگ می‌شوند.

### ۸. «Light AI» از نظر فنی چه می‌تواند باشد؟

سه پیاده‌سازی محتمل (به ترتیب احتمال):

1. **شبکهٔ کوچک tone-mapping** (HDRNet/DCE-like) که روی جفت‌های (نور بد، نور خوب) آموزش دیده؛ سریع و قابل اجرا روی CPU. 🟡
2. **پایپ‌لاین کلاسیک هوشمند**: gray-world/white-patch برای وایت‌بالانس، percentile stretch برای levels، curve برای سایه/هایلایت، CLAHE موضعی، vibrance در LAB، clarity با unsharp mask. ✅ (قطعاً بخشی از آن همین است)
3. **ترکیب**: ماسک semantic (آسمان/پوست/گیاه) + اصلاح موضعی.

در محصول ما: مسیر ۲ به‌صورت پیش‌فرض (سریع، قابل پیش‌بینی، صفر توهم) + مدل کوچک اختیاری برای حالت «هوشمند».

### ۹. حذف پس‌زمینه

```
BiRefNet/U²-Net → ماسک آلفا → refine با guided filter/matting
→ خروجی RGBA (PNG/WebP) + لبهٔ نرم مو
```

محدودیت ۱۶MP در LetsEnhance احتمالاً به دلیل حافظهٔ مدل segmentation در رزولوشن بالا و نگهداری ماسک است.

### ۱۰. تفاوت GAN و Diffusion در «Ultra»

| ویژگی | GAN (Real-ESRGAN/… ) | Diffusion-based SR (SUPIR/SeeSR/SeedVR2) |
|---|---|---|
| سرعت | خیلی سریع (میلی‌ثانیه/مگاپیکسل) | کند (ثانیه تا دقیقه) |
| VRAM | ۱–۴GB | ۸–۲۴GB+ |
| بافت | تیز، گاهی پلاستیکی | طبیعی و غنی |
| کنترل | کم (شدت denoise) | زیاد (prompt، guidance، control scale) |
| توهم | متوسط | بالا |
| مناسب | موبایل/دسکتاپ معمولی | دسکتاپ قوی، «قهرمان» |

وجود دو اسلایدر در Ultra (`Intensity` = چقدر تهاجمی، `Size of changes`/`Similarity` = چقدر به اصل وفادار بماند) با پارامترهای `denoise strength` و `control/guidance scale` در مدل‌های diffusion SR هم‌خوانی دقیق دارد. 🟡

---

## بخش ب — آن‌ها عملاً چطور اجرا می‌کنند (بازسازی)

### ۱۱. پایپ‌لاین سرور (استنباط از شواهد)

```
Client (web) ──upload──▶ Object Storage ──▶ API/Job service ──▶ Queue
                                                              │
                                            ┌─────────────────┴──────────────┐
                                            ▼                                ▼
                                   GPU worker (Triton)                 Post-process
                                   - preprocess (decode, EXIF, sRGB)
                                   - analyze (blur/noise/JPEG/faces/content)
                                   - model(s)
                                   - tiling/blending
                                            │
                                            ▼
                                   Encode (JPG/PNG) + watermark (free)
                                            │
                                            ▼
                                   CDN → download link + DB record
```

- **Triton** مدل‌ها را با dynamic batching و نسخه‌بندی سرو می‌کند؛ هر مدل یک «model repository».
- **MIG** روی A100 اجازه می‌دهد چند نمونهٔ ۱g.5gb/2g.10gb اجرا شود؛ برای بار متغیر، ترکیب MIG + autoscaling بهترین اقتصاد است.
- **Throughput اعلامی ۱۰–۸۰ img/s و تا ۱۵۰+** یعنی برای هر تصویر ~۷–۱۰۰ms GPU-time بسته به اندازه/مدل.
- **مصرف اعتبار** تابع `f(megapixels_out, model, ops)` است؛ همان چیزی که در UI دیده می‌شود.

### ۱۲. ترتیب عملیات (همان چیزی که ما هم باید پیاده کنیم)

```
1. Decode + EXIF orientation + color space → sRGB (یا فضای کاری خطی)
2. تحلیل تصویر: رزولوشن، نویز، بلوک‌های JPEG، تاری، چهره، نوع محتوا (عکس/آرت/متن)
3. انتخاب خودکار مدل + پارامتر شدت (اگر کاربر Auto را انتخاب کرده باشد)
4. Decompress (در صورت نیاز)
5. Denoise (در صورت نیاز)
6. Face restore (اختیاری)
7. Super-Resolution (کاشی‌بندی + دوخت)
8. اصلاح رنگ/نور (Light AI)
9. Sharpen/Detail (مقدار کم، روی لبه‌های محافظت‌شده)
10. Background removal (اختیاری، بعد از SR برای بهترین لبه)
11. Resize نهایی به ابعاد/DPI هدف
12. Encode + متادیتا/ICC + نام‌گذاری
```

**چرا این ترتیب؟** هر مرحله فرض‌های مرحلهٔ بعد را بهتر می‌کند: حذف آرتیفکت قبل از SR جلوی «بزرگ شدن خرابی» را می‌گیرد؛ SR قبل از matting لبهٔ باکیفیت‌تر می‌دهد؛ رنگ‌بندی در فضای خطی از کلیپ شدن جلوگیری می‌کند.

### ۱۳. چرا سقف مگاپیکسل دارند؟

- حافظهٔ فعال‌سازی مدل با مربع ابعاد رشد می‌کند.
- زمان پردازش خطی با مگاپیکسل است؛ ۵۱۲MP با مدل GAN یعنی چند دقیقه GPU و هزینهٔ واقعی.
- encode/decode فایل‌های بزرگ در مرورگر کاربر نیز محدودیت دارد (حافظهٔ تب).
- سقف، ابزار کنترل هزینه و SLA است.

### ۱۴. پیاده‌سازی لوکال معادل (نقشهٔ ما)

| مرحله | ابزار/مدل لوکال | یادداشت |
|---|---|---|
| Decode/Encode | libjpeg-turbo, libpng, libwebp, libavif, libjxl, libtiff, LittleCMS | ۸/۱۶ بیت، ICC |
| تحلیل | توابع OpenCV + مدل‌های کوچک ONNX (classifier محتوا، face detector) | سبک، روی CPU |
| Decompress | FBCNN یا Real-ESRGAN general + denoise blend | کیفیت‌محور |
| Denoise | SCUNet / NAFNet (ONNX)، Real-CUGAN (ncnn) | قابل خاموش |
| SR | SPAN (سبک)، Real-ESRGAN x4plus / anime6B / UltraSharp / Remacri، SwinIR/HAT برای GPU قوی | ncnn برای Vulkan/CPU |
| Face | SCRFD + GFPGAN v1.4 (پیش‌فرض، تجاری‌امن) / CodeFormer (پلاگین) | — |
| Light | پایپ‌لاین کلاسیک + مدل کوچک اختیاری | Deterministic |
| Background | BiRefNet-lite (MIT) + matting | RGBA |
| Diffusion tier | SUPIR/SeedVR2 در ONNX/GGUF (اختیاری، GPU قوی) | «Ultra» |
| Video | FFmpeg + SR + temporal blend (flow warp) | M6 |

---

## ۱۵. نمونهٔ محاسبهٔ هزینه/زمان (برای برنامه‌ریزی)

اگر مدل ۴x به‌طور متوسط `T ms` برای هر مگاپیکسل **خروجی** روی یک دستگاه ببرد:

```
زمان ≈ MP_out × T × (1 + overhead_tiling(~15%)) × ops_count
MP_out = (W×H×scale²) / 1e6
```

مثال: عکس ۲MP با ۴x → خروجی ۳۲MP. با `T = 50ms/MP` (GPU مجزا) ≈ ۱.۸ ثانیه؛ با `T = 800ms/MP` (CPU) ≈ ۲۹ ثانیه.
اعداد واقعی در `07-performance.md` و با هارنس بنچمارک در M1 اندازه‌گیری می‌شوند.

---

## ۱۶. منابع علمی/فنی پیشنهادی

- Real-ESRGAN (Wang et al., ICCVW 2021) — degradation modeling
- ESRGAN (Wang et al., ECCVW 2018) — perceptual/GAN loss
- SwinIR (Liang et al., 2021) / HAT (2023) / DAT (2023)
- SCUNet (Zhang et al., CVPR 2023), NAFNet (Chen et al., ECCV 2022), FBCNN (Jiang et al., CVPR 2021)
- StableSR (Wang et al., 2023), SeeSR (Wu et al., CVPR 2024), SUPIR (Yu et al., CVPR 2024), SeedVR2 (2025)
- GFPGAN (Wang et al., CVPR 2021), CodeFormer (Zhou et al., NeurIPS 2022)
- BiRefNet (Zheng et al., CAAI AIR 2024)
- HDRNet (Gharbi et al., SIGGRAPH 2017)

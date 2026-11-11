# stb (vendored)

- `stb_image.h` — decoder برای JPEG/PNG/TGA/BMP/GIF/PSD/HDR
- `stb_image_write.h` — encoder برای PNG/JPG/BMP/TGA/HDR
- منبع: https://github.com/nothings/stb (شاخهٔ master، تاریخ دریافت ۲۰۲۶-۱۱-۱۱)
- لایسنس: **MIT یا Public Domain** (به انتخاب شما) — متن کامل در `LICENSE`

## چرا vendored؟

در P0 می‌خواستیم بدون هیچ وابستگی سیستمی (libjpeg/libpng نصب‌نشده) واقعاً JPEG/PNG بخوانیم و بنویسیم.
`stb` تک‌هدر و قابل حمل است و روی ویندوز/اندروید/iOS هم بدون تغییر کامپایل می‌شود.

## محدودیت‌ها (مستند)

- خروجی ۱۶ بیت پشتیبانی نمی‌شود (PNG 16-bit فقط خوانده می‌شود). برای ۱۶ بیت در P1 به libpng/libtiff مهاجرت می‌کنیم.
- سرعت کدک برای فایل‌های بزرگ کمتر از libjpeg-turbo است؛ در P1 جایگزین می‌شود.
- هیچ تغییری در فایل‌های هدر ندادیم (به‌روزرسانی آسان).

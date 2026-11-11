// SPDX-License-Identifier: Apache-2.0
// یادداشت: متریک‌های ارزیابی. این‌ها «دروازه» نیستند، ابزار اندازه‌گیری‌اند؛
// اما بدون آن‌ها هر تصمیمی دربارهٔ کیفیت حدس است. PSNR روی RGB و SSIM روی
// luma (Rec.709) محاسبه می‌شود تا با گزارش‌های مرسوم قابل مقایسه باشد.
#pragma once

#include "atlas/image.h"

namespace atlas {

struct QualityMetrics {
  bool valid = false;
  int width = 0;
  int height = 0;
  double psnr_db = 0.0;  // روی کانال‌های RGB در بازهٔ [0,1]، peak = 1
  double ssim = 0.0;     // ساختاری، پنجرهٔ گاوسی ۱۱×۱۱ با σ=1.5 روی luma
  double mae = 0.0;      // میانگین قدر مطلق خطا
  double max_abs = 0.0;  // بیشینهٔ خطای پیکسلی (برای پیدا کردن جهش‌های محلی)
};

// luma با ضرایب Rec.709؛ آلفا (کانال چهارم) نادیده گرفته می‌شود.
ImageF32 to_luma(const ImageF32& src);

// اگر ابعاد دو تصویر یکی نباشد، هر دو از مرکز به کوچک‌ترین ابعاد مشترک بریده
// می‌شوند (چون مقایسهٔ کراپ‌شده بهتر از خطای ابعاد است).
QualityMetrics compare_images(const ImageF32& a, const ImageF32& b);

double psnr_db(const ImageF32& a, const ImageF32& b);
double ssim(const ImageF32& a, const ImageF32& b);

}  // namespace atlas

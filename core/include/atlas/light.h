// SPDX-License-Identifier: Apache-2.0
// یادداشت: «Light AI» — اصلاح نور و رنگ، نه بازسازی محتوا. برخلاف SR، این مرحله
// هیچ پیکسلی نمی‌سازد: فقط روشنایی/کنتراست/اشباع هر پیکسل را با یک منحنی
// یکنوا تغییر می‌دهد. به همین دلیل بی‌خطر است و می‌تواند قبل از بزرگ‌نمایی اجرا
// شود (روی تصویر کوچک، ۴ برابر ارزان‌تر از اجرا روی خروجی ۴×).
//
// ترتیب دقیق (همان ترتیبی که در `adjust_light` اجرا می‌شود):
//   ۱) تعادل رنگ خودکار (اگر خواسته شود) — روی نور خطی، با گین‌های محدود
//   ۲) نوردهی (EV) + گرمی/سردی — روی نور خطی، ضربی و بی‌خطر برای رنگ‌ها
//   ۳) منحنی روشنایی: سایه‌ها (گاما) → هایلایت‌ها (ماسک y²) → کنتراست (S خطی)
//   ۴) اشباع — حول روشناییِ پس از منحنی، تا هیچ رنگی از بازهٔ [0,1] بیرون نزند
//
// چرا luma در فضای sRGB-encoded؟ چون «سایه/هایلایت» یک مفهوم ادراکی است و
// کاربر آن را روی تصویری که می‌بیند می‌سنجد، نه روی نور خطی. عملیات ضربی
// (نوردهی/تعادل رنگ/گرمی) اما در نور خطی انجام می‌شود که از نظر فیزیکی درست است.
#pragma once

#include "atlas/image.h"

namespace atlas {

// آماره‌های تصویر که هم برای گزارش و هم برای تنظیم خودکار استفاده می‌شوند.
struct LightStats {
  double mean = 0.0;       // میانگین luma (۰..۱)
  double contrast = 0.0;   // انحراف معیار luma
  double saturation = 0.0; // میانگین (max−min)/max روی کانال‌های رنگ
  double p01 = 0.0;        // صدک ۱٪ luma
  double p50 = 0.0;        // میانهٔ luma
  double p99 = 0.0;        // صدک ۹۹٪ luma
  double clip_low = 0.0;   // درصد پیکسل‌های کاملاً سیاه (luma ≤ ۰٫۰۱)
  double clip_high = 0.0;  // درصد پیکسل‌های کاملاً سفید (luma ≥ ۰٫۹۹)
};

LightStats measure_light(const ImageF32& img);

struct LightOptions {
  // همهٔ شدت‌ها −۱..+۱ هستند (۰ = بدون تغییر) تا UI بتواند مستقیم از اسلایدر
  // استفاده کند؛ نوردهی بر حسب EV است (−۲..+۲).
  float exposure = 0.0f;
  float contrast = 0.0f;
  float saturation = 0.0f;
  float shadows = 0.0f;
  float highlights = 0.0f;
  float warmth = 0.0f;  // + گرم‌تر (نارنجی)، − سردتر (آبی)
  bool auto_white_balance = false;
  bool auto_tone = false;  // نوردهی + کنتراست خودکار
};

// اگر هیچ کاری لازم نیست (همه صفر و خودکار خاموش)، خروجی باید *بیت‌به‌بیت*
// برابر ورودی باشد — این را تست قفل می‌کند تا مرحلهٔ خنثی، تصویر را دستکاری نکند.
bool light_is_neutral(const LightOptions& options);

struct LightReport {
  LightStats before;
  LightStats after;
  double wb_red_gain = 1.0;    // گین نهایی کانال قرمز (خطی)
  double wb_green_gain = 1.0;
  double wb_blue_gain = 1.0;
  double auto_exposure_ev = 0.0;  // EV اضافه‌شده توسط auto_tone
  double auto_contrast = 0.0;     // کنتراست اضافه‌شده توسط auto_tone
  bool white_balance_applied = false;
  bool tone_applied = false;
  double seconds = 0.0;
};

// ورودی و خروجی sRGB-encoded در [0,1]؛ آلفا بدون تغییر کپی می‌شود.
ImageF32 adjust_light(const ImageF32& src, const LightOptions& options,
                      LightReport* report = nullptr);

}  // namespace atlas

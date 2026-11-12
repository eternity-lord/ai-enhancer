// SPDX-License-Identifier: Apache-2.0
// یادداشت: P2 — بازسازی پیش از بزرگ‌نمایی. عکس خراب (نویز، JPEG کم‌کیفیت) اگر
// مستقیم به SR داده شود، مدل نویز را «بزرگ» می‌کند: نویز یک پیکسل، بعد از ۴×
// تبدیل به یک لکهٔ ۴×۴ با بافت ساختگی می‌شود. پس ترتیب درست این است:
// deblock → denoise → SR. این فایل مرحلهٔ اول و دوم را می‌دهد.
//
// هیچ‌کدام از این‌ها «توهم» نمی‌سازند: خروجی فقط از همسایگی خود پیکسل ساخته
// می‌شود (NLM/میانگین وزن‌دار)، نه از دانش قبلی مدل.
#pragma once

#include "atlas/image.h"

namespace atlas {

// --- سنجش خرابی ---
// sigma: برآورد انحراف معیار نویز (روش Immerkær: میانگین |لاپلاسین| روی luma).
// boundary_step/inside_step: میانگین |اختلاف| افقی-عمودی روی مرزهای ۸پیکسلی و
// داخل بلوک‌ها. اگر مرزها به‌طور معنادار پرش بیشتری داشته باشند، اثر JPEG است.
struct NoiseEstimate {
  double sigma = 0.0;
  double boundary_step = 0.0;
  double inside_step = 0.0;
  double blockiness = 0.0;        // max(0, boundary − inside)
  double blockiness_ratio = 1.0;  // boundary / inside (۱ = بدون شبکه)
  bool jpeg_like = false;
};

// آستانه‌ها بر اساس تجربهٔ JPEG: نویز محسوس از σ≈۰٫۰۰۴ و شبکهٔ دیده‌شدنی از
// نسبت ≈۱٫۵ شروع می‌شود. این اعداد آستانهٔ «تشخیص» هستند، نه آستانهٔ کیفیت.
NoiseEstimate analyze_degradation(const ImageF32& src);

// --- مراحل ---
// Non-Local Means روی luma؛ وزن‌ها روی هر سه کانال رنگ اعمال می‌شوند تا لکه‌های
// رنگی (chroma noise) هم بروند. strength در [0,1]؛ ۰ = بدون تغییر.
ImageF32 denoise_nlmeans(const ImageF32& src, float strength, int patch_radius = 1,
                         int search_radius = 2);

// حذف اثر بلوک‌های ۸×۸: فقط پله‌های کوچک روی مرز شبکه نرم می‌شوند (پلهٔ بزرگ =
// لبهٔ واقعی، دست‌نخورده می‌ماند) + کاهش ringing در نواحی تخت.
ImageF32 deblock_jpeg(const ImageF32& src, float strength, int block = 8);

struct RestoreOptions {
  // −۱ = خودکار (بر اساس analyze_degradation)، ۰ = خاموش، (۰,۱] = شدت دستی
  float denoise = -1.0f;
  float deblock = -1.0f;
  int patch_radius = 1;
  int search_radius = 2;
  bool chroma = true;  // اگر false باشد فقط luma نرم می‌شود (سریع‌تر، بی‌ریسک‌تر)
};

struct RestoreReport {
  NoiseEstimate before;
  NoiseEstimate after;
  bool denoise_applied = false;
  bool deblock_applied = false;
  float denoise_strength = 0.0f;
  float deblock_strength = 0.0f;
  double seconds = 0.0;
};

// ترتیب: deblock (پاک‌کردن شبکه) → denoise (پاک‌کردن نویز). عمداً برعکس
// نیست: NLM اگر مرز بلوک را «ساختار» ببیند، آن را حفظ می‌کند.
ImageF32 restore_degraded(const ImageF32& src, const RestoreOptions& options,
                          RestoreReport* report = nullptr);

}  // namespace atlas

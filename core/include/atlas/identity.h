// SPDX-License-Identifier: Apache-2.0
// یادداشت: «دروازهٔ هویت» یک ادعای مهم است، پس باید صادق باشیم:
// ArcFace (معیار صنعتی، docs/14) وزن‌هایش غیرتجاری است و در این ریپو نیست.
// بنابراین این فایل یک **پروکسی ساختاری** می‌سازد که می‌تواند هویت‌دریفت فاحش
// را بگیرد (تغییر هندسهٔ چشم/دهان/بینی، عوض شدن رنگ پوست، محو شدن جزئیات) و
// امتیازش با همان آستانه‌های docs/15 مقایسه می‌شود.
//
// قرارداد: این عدد «ArcFace نیست». برای انتشار تجاری، پلاگین ArcFace باید
// جایگزین یا مکمل این شود (رابط `IIdentityEmbedder`).
#pragma once

#include <array>
#include <string>

#include "atlas/image.h"
#include "atlas/types.h"

namespace atlas {

// کادر چهره به‌صورت **نرمال‌شده** (۰..۱) تا برای تصویر اصلی و بزرگ‌شده یکسان
// قابل استفاده باشد (چون ابعاد دو تصویر متفاوت است).
struct NormBox {
  float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;

  bool empty() const { return w <= 0.0f || h <= 0.0f; }
};

struct IdentityOptions {
  NormBox face;             // خالی = کل تصویر
  bool reference_mode = false;  // حالت Reference-locked (docs/14): آستانهٔ سخت‌تر
  int sample_size = 112;    // ابعاد نرمال‌سازی برای مقایسه
};

struct IdentityResult {
  bool valid = false;
  double structural = 0.0;  // SSIM روی luma چهرهٔ نرمال‌شده
  double shape = 0.0;       // شباهت کسینوسی HOG-lite (هندسهٔ خطوط)
  double color = 0.0;       // شباهت آمارهٔ رنگ (میانگین/انحراف معیار)
  double score = 0.0;       // ترکیب وزنی — همان عددی که با آستانه مقایسه می‌شود
  int face_px = 0;          // ضلع کوچک کادر چهره در تصویر اصلی (پیکسل)
  double threshold = 0.0;   // آستانهٔ مؤثر (هم‌مقیاس با score همین پروکسی)
  double arcface_threshold = 0.0;  // آستانهٔ docs/15 برای وقتی ArcFace وصل شود
  bool proxy_scale = true;  // score روی مقیاس پروکسی است، نه ArcFace
  bool pass = false;
  std::string note;         // توضیح انسانی (مثلاً «کادر خالی است»)
};

// آستانه‌های docs/15 (مقیاس ArcFace) — مرجع تصمیم محصول.
double identity_threshold(int face_px, bool reference_mode);

// آستانهٔ پروکسی: چون score این ماژول ArcFace نیست، نمی‌توان همان ۰٫۷۰ را
// استفاده کرد. این اعداد از کالیبراسیون روی جفت‌های ساختگی آمده‌اند
// (یکسان = ۱٫۰۰، صاف‌شده = ۰٫۹۹۹، «آدم دیگر» = ۰٫۸۴) و باید با دادهٔ واقعی
// بازبینی شوند. در گزارش هم اعلام می‌شود که مقیاس پروکسی است.
double proxy_threshold(int face_px, bool reference_mode);

IdentityResult identity_gate(const ImageF32& original, const ImageF32& restored,
                             const IdentityOptions& options);

// بردار شکل (HOG-lite) برای استفاده در گزارش/آزمون؛ طول = ۴×۴×۸.
std::array<double, 128> shape_signature(const ImageF32& image);

}  // namespace atlas

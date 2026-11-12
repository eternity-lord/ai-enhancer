// SPDX-License-Identifier: Apache-2.0
// یادداشت: IO تصویر با stb. مقادیر در [0,1] و در فضای sRGB-encoded نگه داشته
// می‌شوند (مدل‌های SR روی همین فضا آموزش دیده‌اند؛ خطی‌سازی در `color.h` جداست).
// `load_image_ex` علاوه بر پیکسل، متادیتای لازم برای وفاداری (EXIF/ICC/آلفا) را
// برمی‌گرداند و پروفایل ICC ماتریسی را به sRGB تبدیل می‌کند.
#pragma once

#include <string>

#include "atlas/types.h"

namespace atlas {

// متادیتای مفیدی که decode عملاً دور می‌ریزد ولی برای «وفاداری» لازم است.
struct ImageMeta {
  int width = 0;
  int height = 0;
  int channels = 0;
  int orientation = 0;         // ۰ = ندارد، ۱..۸ = EXIF
  bool has_icc_profile = false;
  bool icc_applied = false;    // تبدیل به sRGB انجام شد؟
  std::string icc_description; // نام خوانای پروفایل یا "none"
};

bool load_image(const std::string& path, ImageF32* out, std::string* error);

// مسیر کامل: decode + خواندن EXIF/ICC + تبدیل ICC به sRGB (اگر پروفایل ماتریسی
// بود). orientation اعمال **نمی‌شود**؛ در meta برگردانده می‌شود تا فراخوان
// تصمیم بگیرد (چون چرخش هندسه را عوض می‌کند).
bool load_image_ex(const std::string& path, ImageF32* out, ImageMeta* meta, std::string* error);
bool save_image(const std::string& path, const ImageF32& img, int jpeg_quality,
                std::string* error);

// --- WebP (وابستگی اختیاری) ---
// اگر build با libwebp ساخته نشده باشد، این توابع false و پیام روشن برمی‌گردانند
// (به‌جای crash یا خروجی خالی).
bool webp_compiled();
bool load_webp(const std::string& path, ImageF32* out, std::string* error);
bool save_webp(const std::string& path, const ImageF32& img, int quality, std::string* error);

}  // namespace atlas

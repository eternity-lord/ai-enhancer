// SPDX-License-Identifier: Apache-2.0
// یادداشت: چرا ICC؟ عکس آیفون/مک معمولاً Display P3 است. اگر آن را sRGB فرض
// کنیم، رنگ‌ها «کم‌جان» و پوست بی‌روح می‌شود — دقیقاً همان چیزی که کاربر به
// عنوان «کیفیت بد» گزارش می‌کند، در حالی که هیچ پیکسلی خراب نشده است.
//
// دامنهٔ پشتیبانی عمداً محدود است: پروفایل‌های ماتریسی/TRC (sRGB، Display P3،
// Adobe RGB، ProPhoto با TRC). پروفایل‌های LUT-based (A2B0) رد می‌شوند و
// رفتار پیش‌فرض «فرض sRGB» است، با گزارش در متادیتا.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "atlas/image.h"

namespace atlas {

struct IccProfile {
  bool valid = false;
  std::string description;          // از tag 'desc' یا 'mluc' (اختیاری)
  std::string color_space;          // "RGB " یا چیز دیگر
  std::array<float, 9> rgb_to_xyz{};  // row-major: XYZ = M * linear_rgb
  std::array<std::array<float, 4096>, 3> trc{};  // device → linear
  std::array<float, 3> gamma{{1.0f, 1.0f, 1.0f}};
  std::array<float, 3> white_point{{0.9505f, 1.0f, 1.089f}};  // D65 پیش‌فرض
  bool adapted_white = false;        // سازگاری رنگی Bradford اعمال شد؟
  bool used_parametric = false;      // 'para' در برابر 'curv'
};

// NSA-ish parse: هر چیزی که با استاندارد نخواند → valid=false (بدون استثنا).
IccProfile parse_icc(const uint8_t* data, size_t size);

// استخراج پروفایل از فایل PNG (chunk iCCP) یا JPEG (APP2 / ICC_PROFILE).
// رشتهٔ خالی یعنی «پروفایلی نبود یا خوانده نشد».
std::string read_png_icc_profile(const std::string& path);
std::string read_jpeg_icc_profile(const std::string& path);
std::string read_icc_profile(const std::string& path);

// تبدیل تصویر از فضای پروفایل به sRGB (خطی‌سازی، ماتریس، بازکدگذاری).
// اگر پروفایل معتبر نباشد، ورودی بدون تغییر برمی‌گردد.
ImageF32 apply_icc_to_srgb(const ImageF32& img, const IccProfile& profile);

// نام خوانا برای نمایش در CLI/متادیتا.
std::string icc_summary(const IccProfile& profile);

// ابزار داخلی (در تست‌ها هم استفاده می‌شود): ماتریس ترکیبی
// «linear-device → linear-sRGB».
std::array<float, 9> icc_to_srgb_matrix(const IccProfile& profile);

}  // namespace atlas

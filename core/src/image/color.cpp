// SPDX-License-Identifier: Apache-2.0
// یادداشت: از pow استفاده می‌کنیم نه LUT؛ برای ۱۲ مگاپیکسل حدود ۰٫۳ ثانیه است
// و دقت کامل دارد. اگر در pipeline داغ شد، LUT با درون‌یابی خطی جایگزین می‌شود
// (تفاوتش زیر ۱/۲۵۵ است و چشم نمی‌بیند).
#include <algorithm>
#include <cmath>

#include "atlas/color.h"
#include "atlas/image.h"

namespace atlas {

float srgb_to_linear(float v) {
  v = std::clamp(v, 0.0f, 1.0f);
  return v <= 0.04045f ? v / 12.92f
                       : std::pow((v + 0.055f) / 1.055f, 2.4f);
}

float linear_to_srgb(float v) {
  v = std::clamp(v, 0.0f, 1.0f);
  return v <= 0.0031308f ? v * 12.92f
                         : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}

namespace {

ImageF32 transfer(const ImageF32& src, bool to_lin) {
  if (src.empty()) return ImageF32();
  ImageF32 out(src.width(), src.height(), src.channels());
  const int c = src.channels();
  const int color_channels = c >= 4 ? c - 1 : c;  // کانال آخر اگر ۴ بود، آلفا
  for (size_t i = 0; i < src.pixel_count(); ++i) {
    const float* s = src.data() + i * c;
    float* d = out.data() + i * c;
    for (int ch = 0; ch < c; ++ch) {
      d[ch] = ch < color_channels
                  ? (to_lin ? srgb_to_linear(s[ch]) : linear_to_srgb(s[ch]))
                  : s[ch];
    }
  }
  return out;
}

}  // namespace

ImageF32 to_linear(const ImageF32& srgb) { return transfer(srgb, true); }
ImageF32 to_srgb(const ImageF32& linear) { return transfer(linear, false); }

}  // namespace atlas

// SPDX-License-Identifier: Apache-2.0
// یادداشت: چرا SSIM با گاوسی؟ چون نسخهٔ پنجرهٔ ۸×۸ (box) عدد را بالا می‌برد و
// خطاهای ساختاری را نرم می‌کند. نسخهٔ استاندارد Wang با پنجرهٔ ۱۱×۱۱ و σ=1.5
// با مقالات قابل مقایسه است. آلفا در محاسبات نمی‌آید، وگرنه PNGهای دارای آلفا
// بی‌دلیل عدد بدتری می‌گیرند.
#include <algorithm>
#include <cmath>

#include "atlas/image.h"
#include "atlas/metrics.h"

namespace atlas {
namespace {

constexpr double kC1 = 0.01 * 0.01;  // (0.01 * L)^2 با L=1
constexpr double kC2 = 0.03 * 0.03;  // (0.03 * L)^2

ImageF32 crop_centered(const ImageF32& src, int w, int h) {
  const int x0 = (src.width() - w) / 2;
  const int y0 = (src.height() - h) / 2;
  ImageF32 out(w, h, src.channels());
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const float* s = src.pixel(x0 + x, y0 + y);
      float* d = out.pixel(x, y);
      for (int c = 0; c < src.channels(); ++c) d[c] = s[c];
    }
  }
  return out;
}

// ابعاد مشترک از مرکز؛ اگر یکی از تصاویر خالی است، جفت خالی برمی‌گردد.
bool align_pair(const ImageF32& a, const ImageF32& b, ImageF32* out_a, ImageF32* out_b) {
  if (a.empty() || b.empty()) return false;
  const int w = std::min(a.width(), b.width());
  const int h = std::min(a.height(), b.height());
  if (w <= 0 || h <= 0) return false;
  *out_a = (a.width() == w && a.height() == h) ? a : crop_centered(a, w, h);
  *out_b = (b.width() == w && b.height() == h) ? b : crop_centered(b, w, h);
  return true;
}

int color_channels(const ImageF32& img) {
  return img.channels() >= 3 ? 3 : img.channels();
}

}  // namespace

ImageF32 to_luma(const ImageF32& src) {
  if (src.empty()) return ImageF32();
  ImageF32 out(src.width(), src.height(), 1);
  const int c = src.channels();
  for (size_t i = 0; i < src.pixel_count(); ++i) {
    const float* s = src.data() + i * c;
    if (c >= 3) {
      out.data()[i] = 0.2126f * s[0] + 0.7152f * s[1] + 0.0722f * s[2];
    } else {
      out.data()[i] = s[0];
    }
  }
  return out;
}

double psnr_db(const ImageF32& a_in, const ImageF32& b_in) {
  ImageF32 a;
  ImageF32 b;
  if (!align_pair(a_in, b_in, &a, &b)) return 0.0;

  const int c = color_channels(a);
  double mse = 0.0;
  size_t n = 0;
  for (size_t i = 0; i < a.pixel_count(); ++i) {
    for (int ch = 0; ch < c; ++ch) {
      const double d = static_cast<double>(a.data()[i * a.channels() + ch]) -
                       static_cast<double>(b.data()[i * b.channels() + ch]);
      mse += d * d;
      ++n;
    }
  }
  if (n == 0) return 0.0;
  mse /= static_cast<double>(n);
  if (mse <= 0.0) return 99.0;  // یکسان: سقف عملی گزارش
  return 10.0 * std::log10(1.0 / mse);
}

double ssim(const ImageF32& a_in, const ImageF32& b_in) {
  ImageF32 a;
  ImageF32 b;
  if (!align_pair(a_in, b_in, &a, &b)) return 0.0;
  if (a.width() < 11 || a.height() < 11) {
    // برای تصویرهای کوچک‌تر از پنجره، میانگین وزنی ساده استفاده می‌شود تا
    // به‌جای عدد بی‌معنی (۰)، یک مقدار محافظه‌کارانه بدهد.
    const double p = psnr_db(a, b);
    return std::clamp((p - 10.0) / 40.0, 0.0, 1.0);
  }

  const ImageF32 x = to_luma(a);
  const ImageF32 y = to_luma(b);

  ImageF32 x2(x.width(), x.height(), 1);
  ImageF32 y2(x.width(), x.height(), 1);
  ImageF32 xy(x.width(), x.height(), 1);
  for (size_t i = 0; i < x.pixel_count(); ++i) {
    const float xs = x.data()[i];
    const float ys = y.data()[i];
    x2.data()[i] = xs * xs;
    y2.data()[i] = ys * ys;
    xy.data()[i] = xs * ys;
  }

  constexpr float kSigma = 1.5f;  // پنجرهٔ ۱۱×۱۱ (radius = ceil(3σ) = 5)
  const ImageF32 mx = gaussian_blur(x, kSigma);
  const ImageF32 my = gaussian_blur(y, kSigma);
  const ImageF32 mx2 = gaussian_blur(x2, kSigma);
  const ImageF32 my2 = gaussian_blur(y2, kSigma);
  const ImageF32 mxy = gaussian_blur(xy, kSigma);

  double sum = 0.0;
  for (size_t i = 0; i < x.pixel_count(); ++i) {
    const double ux = mx.data()[i];
    const double uy = my.data()[i];
    const double vx = std::max(0.0, static_cast<double>(mx2.data()[i]) - ux * ux);
    const double vy = std::max(0.0, static_cast<double>(my2.data()[i]) - uy * uy);
    const double cxy = static_cast<double>(mxy.data()[i]) - ux * uy;
    const double num = (2.0 * ux * uy + kC1) * (2.0 * cxy + kC2);
    const double den = (ux * ux + uy * uy + kC1) * (vx + vy + kC2);
    sum += den > 0.0 ? num / den : 1.0;
  }
  return sum / static_cast<double>(x.pixel_count());
}

QualityMetrics compare_images(const ImageF32& a_in, const ImageF32& b_in) {
  QualityMetrics m;
  ImageF32 a;
  ImageF32 b;
  if (!align_pair(a_in, b_in, &a, &b)) return m;

  m.valid = true;
  m.width = a.width();
  m.height = a.height();
  m.psnr_db = psnr_db(a, b);
  m.ssim = ssim(a, b);

  const int c = color_channels(a);
  double sum = 0.0;
  double worst = 0.0;
  size_t n = 0;
  for (size_t i = 0; i < a.pixel_count(); ++i) {
    for (int ch = 0; ch < c; ++ch) {
      const double d = std::fabs(static_cast<double>(a.data()[i * a.channels() + ch]) -
                                 static_cast<double>(b.data()[i * b.channels() + ch]));
      sum += d;
      worst = std::max(worst, d);
      ++n;
    }
  }
  if (n > 0) {
    m.mae = sum / static_cast<double>(n);
    m.max_abs = worst;
  }
  return m;
}

}  // namespace atlas

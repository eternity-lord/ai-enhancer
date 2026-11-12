// SPDX-License-Identifier: Apache-2.0
// یادداشت: چرا HOG و نه یک شبکهٔ کوچک؟ چون هدف این مرحله «گرفتن دریفت فاحش با
// صفر وابستگی» است، نه تشخیص هویت. اگر یک مدل کوچک هم اضافه می‌کردیم، بدون
// آموزش درست، عددی می‌داد که قابل دفاع نبود. HOG-lite سه چیز را می‌گیرد:
//  ۱) محل و جهت خطوط اصلی صورت (چشم/ابرو/دهان) → تغییر هویت
//  ۲) کنتراست محلی → «صاف شدن» پوست توسط مدل‌های زیباساز
//  ۳) آمارهٔ رنگ → عوض شدن رنگ پوست/سفیدی چشم
#include <algorithm>
#include <cmath>
#include <vector>

#include "atlas/color.h"
#include "atlas/identity.h"
#include "atlas/image.h"
#include "atlas/metrics.h"

namespace atlas {
namespace {

constexpr int kCells = 4;
constexpr int kBins = 8;

ImageF32 crop_normalized(const ImageF32& img, const NormBox& box) {
  if (img.empty()) return ImageF32();
  const int w = img.width();
  const int h = img.height();
  const NormBox b = box.empty() ? NormBox{0.0f, 0.0f, 1.0f, 1.0f} : box;
  const int x0 = std::clamp(static_cast<int>(b.x * w), 0, w - 1);
  const int y0 = std::clamp(static_cast<int>(b.y * h), 0, h - 1);
  const int cw = std::clamp(static_cast<int>(b.w * w), 1, w - x0);
  const int chh = std::clamp(static_cast<int>(b.h * h), 1, h - y0);

  ImageF32 out(cw, chh, img.channels());
  for (int y = 0; y < chh; ++y) {
    for (int x = 0; x < cw; ++x) {
      const float* s = img.pixel(x0 + x, y0 + y);
      float* d = out.pixel(x, y);
      for (int c = 0; c < img.channels(); ++c) d[c] = s[c];
    }
  }
  return out;
}

// آمارهٔ رنگ در فضای خطی (میانگین و انحراف معیار هر کانال رنگ).
struct ColorStats {
  std::array<double, 3> mean{{0, 0, 0}};
  std::array<double, 3> stddev{{0, 0, 0}};
};

ColorStats color_stats(const ImageF32& img) {
  ColorStats s;
  if (img.empty() || img.channels() < 3) return s;
  const size_t n = img.pixel_count();
  for (int c = 0; c < 3; ++c) {
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i) {
      sum += srgb_to_linear(img.data()[i * img.channels() + c]);
    }
    const double mean = sum / static_cast<double>(n);
    double var = 0.0;
    for (size_t i = 0; i < n; ++i) {
      const double d = srgb_to_linear(img.data()[i * img.channels() + c]) - mean;
      var += d * d;
    }
    s.mean[static_cast<size_t>(c)] = mean;
    s.stddev[static_cast<size_t>(c)] = std::sqrt(var / static_cast<double>(n));
  }
  return s;
}

double color_similarity(const ImageF32& a, const ImageF32& b) {
  const ColorStats ca = color_stats(a);
  const ColorStats cb = color_stats(b);
  double dist = 0.0;
  for (size_t c = 0; c < 3; ++c) {
    dist += std::fabs(ca.mean[c] - cb.mean[c]) + 0.5 * std::fabs(ca.stddev[c] - cb.stddev[c]);
  }
  // ۰٫۳ اختلاف کل در سه کانال ≈ «رنگ کاملاً متفاوت». خطی نگه می‌داریم تا
  // عدد قابل توضیح باشد، نه یک تابع نمایی مرموز.
  return std::clamp(1.0 - dist / 0.3, 0.0, 1.0);
}

// همبستگی متقابل نرمال‌شده روی «تصویر کوچک خاکستری»: به چیدمان فضایی
// (فاصلهٔ چشم‌ها، جای دهان) حساس است، ولی به روشنایی کلی حساس نیست.
double layout_correlation(const ImageF32& a, const ImageF32& b, int size) {
  const ImageF32 ta = resize_lanczos(to_luma(a), size, size);
  const ImageF32 tb = resize_lanczos(to_luma(b), size, size);
  const size_t n = ta.pixel_count();
  if (n == 0) return 0.0;
  double ma = 0.0;
  double mb = 0.0;
  for (size_t i = 0; i < n; ++i) {
    ma += ta.data()[i];
    mb += tb.data()[i];
  }
  ma /= static_cast<double>(n);
  mb /= static_cast<double>(n);
  double num = 0.0;
  double da = 0.0;
  double db = 0.0;
  for (size_t i = 0; i < n; ++i) {
    const double x = ta.data()[i] - ma;
    const double y = tb.data()[i] - mb;
    num += x * y;
    da += x * x;
    db += y * y;
  }
  if (da < 1e-12 || db < 1e-12) return 0.0;
  return std::clamp(num / std::sqrt(da * db), 0.0, 1.0);
}

double cosine(const std::array<double, 128>& a, const std::array<double, 128>& b) {
  double dot = 0.0;
  double na = 0.0;
  double nb = 0.0;
  for (size_t i = 0; i < a.size(); ++i) {
    dot += a[i] * b[i];
    na += a[i] * a[i];
    nb += b[i] * b[i];
  }
  if (na <= 1e-12 || nb <= 1e-12) return 0.0;
  return std::clamp(dot / (std::sqrt(na) * std::sqrt(nb)), 0.0, 1.0);
}

}  // namespace

std::array<double, 128> shape_signature(const ImageF32& image) {
  std::array<double, 128> sig{};
  if (image.empty()) return sig;

  const ImageF32 luma = to_luma(image);
  const int w = luma.width();
  const int h = luma.height();
  if (w < 8 || h < 8) return sig;

  const int cell_w = std::max(1, w / kCells);
  const int cell_h = std::max(1, h / kCells);

  for (int y = 1; y < h - 1; ++y) {
    for (int x = 1; x < w - 1; ++x) {
      const double gx = (luma.pixel(x + 1, y)[0] - luma.pixel(x - 1, y)[0]) * 0.5;
      const double gy = (luma.pixel(x, y + 1)[0] - luma.pixel(x, y - 1)[0]) * 0.5;
      const double mag = std::sqrt(gx * gx + gy * gy);
      if (mag < 1e-5) continue;
      double angle = std::atan2(gy, gx);  // [-π, π]
      if (angle < 0) angle += M_PI;
      const int bin = std::min(kBins - 1, static_cast<int>(angle / M_PI * kBins));
      const int cx = std::min(kCells - 1, x / cell_w);
      const int cy = std::min(kCells - 1, y / cell_h);
      sig[static_cast<size_t>((cy * kCells + cx) * kBins + bin)] += mag;
    }
  }

  // نرمال‌سازی L2 در هر سلول: مقایسه به «الگوی جهت» حساس شود، نه روشنایی کلی.
  for (int cy = 0; cy < kCells; ++cy) {
    for (int cx = 0; cx < kCells; ++cx) {
      double sum = 0.0;
      for (int b = 0; b < kBins; ++b) {
        const double v = sig[static_cast<size_t>((cy * kCells + cx) * kBins + b)];
        sum += v * v;
      }
      const double norm = std::sqrt(sum);
      if (norm > 1e-9) {
        for (int b = 0; b < kBins; ++b) {
          sig[static_cast<size_t>((cy * kCells + cx) * kBins + b)] /= norm;
        }
      }
    }
  }
  return sig;
}

double identity_threshold(int face_px, bool reference_mode) {
  if (reference_mode) {
    if (face_px >= 192) return 0.80;
    if (face_px >= 96) return 0.78;
    return 0.75;
  }
  if (face_px >= 192) return 0.70;
  if (face_px >= 96) return 0.65;
  return 0.60;
}

double proxy_threshold(int face_px, bool reference_mode) {
  if (reference_mode) {
    if (face_px >= 192) return 0.92;
    if (face_px >= 96) return 0.90;
    return 0.88;
  }
  if (face_px >= 192) return 0.85;
  if (face_px >= 96) return 0.82;
  return 0.78;
}

IdentityResult identity_gate(const ImageF32& original, const ImageF32& restored,
                             const IdentityOptions& options) {
  IdentityResult result;
  if (original.empty() || restored.empty()) {
    result.note = "empty input";
    return result;
  }

  const ImageF32 a_crop = crop_normalized(original, options.face);
  const ImageF32 b_crop = crop_normalized(restored, options.face);
  if (a_crop.empty() || b_crop.empty()) {
    result.note = "empty face crop";
    return result;
  }

  // اندازهٔ چهره بر اساس تصویر **اصلی** (نه خروجی)، چون همان چیزی است که
  // کاربر داشت و آستانه‌ها برای آن تعریف شده‌اند.
  const int n = options.sample_size > 0 ? options.sample_size : 112;
  result.face_px = std::min(a_crop.width(), a_crop.height());

  const ImageF32 a = resize_lanczos(a_crop, n, n);
  const ImageF32 b = resize_lanczos(b_crop, n, n);

  result.structural = ssim(a, b);
  // دو جزء «شکل»: چیدمان فضایی (NCC روی تصویر کوچک) + جهت خطوط (HOG-lite).
  result.shape = 0.75 * layout_correlation(a, b, 32) + 0.25 * cosine(shape_signature(a), shape_signature(b));
  result.color = color_similarity(a, b);

  // وزن‌ها: ساختار مهم‌ترین است، شکل خطوط دوم، رنگ سوم. این وزن‌ها در
  // docs/19 توضیح داده شده‌اند و با تغییر آستانه‌ها تغییر نمی‌کنند.
  result.score = 0.35 * result.structural + 0.45 * result.shape + 0.20 * result.color;
  result.threshold = proxy_threshold(result.face_px, options.reference_mode);
  result.arcface_threshold = identity_threshold(result.face_px, options.reference_mode);
  result.proxy_scale = true;
  result.pass = result.score >= result.threshold;
  result.valid = true;
  return result;
}

}  // namespace atlas

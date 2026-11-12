// SPDX-License-Identifier: Apache-2.0
// یادداشت پیاده‌سازی: هدف این مرحله «قابل پیش‌بینی بودن» است، نه شعبده‌بازی.
// هر عملیات یک تابع ریاضی روشن دارد، خروجی هرگز از [0,1] بیرون نمی‌زند،
// آلفا دست نمی‌خورد، و حالت خنثی تصویر را بیت‌به‌بیت برمی‌گرداند.
#include "atlas/light.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

#include "atlas/color.h"

namespace atlas {
namespace {

constexpr double kLumaR = 0.2126;
constexpr double kLumaG = 0.7152;
constexpr double kLumaB = 0.0722;
constexpr int kLutSize = 1025;  // ۰..۱ با گام ۱/۱۰۲۴

inline double luma709(double r, double g, double b) {
  return kLumaR * r + kLumaG * g + kLumaB * b;
}

inline float clamp01(float v) {
  return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

// منحنی روشنایی: سایه‌ها (گاما) → هایلایت‌ها (ماسک y²) → کنتراست.
// چرا گاما برای سایه‌ها؟ چون y^g با g<1 سایه‌ها و میانه‌ها را بالا می‌آورد ولی
// سیاه و سفید مطلق را دقیقاً سر جای خودشان نگه می‌دارد؛ یک ضرب سادهٔ خطی سیاه
// را خاکستری می‌کرد.
double tone_curve(double y, double shadows, double highlights, double contrast) {
  y = std::clamp(y, 0.0, 1.0);
  if (shadows != 0.0) {
    const double gamma = 1.0 / (1.0 + 0.9 * shadows);
    y = std::pow(y, gamma);
  }
  if (highlights != 0.0) {
    // h<0 → گامای >۱ (بازپس‌گیری هایلایت)، h>0 → گامای <۱ (روشن‌تر کردن).
    const double gamma = highlights < 0.0 ? 1.0 + 0.9 * -highlights : 1.0 - 0.9 * highlights;
    const double target = std::pow(y, gamma);
    // ماسک y⁴: تقریباً هیچ اثری روی میانه‌ها ندارد و فقط نواحی روشن را می‌گیرد؛
    // ضریب ۱٫۵ اثر را در همان ناحیه به اندازهٔ کافی محسوس می‌کند.
    const double weight = 1.5 * y * y * y * y;
    y += weight * (target - y);
  }
  if (contrast != 0.0) {
    y = 0.5 + (y - 0.5) * (1.0 + 0.9 * contrast);
  }
  return std::clamp(y, 0.0, 1.0);
}

std::vector<float> build_lut(const LightOptions& opt) {
  std::vector<float> lut(kLutSize);
  for (int i = 0; i < kLutSize; ++i) {
    const double y = static_cast<double>(i) / (kLutSize - 1);
    lut[static_cast<size_t>(i)] = static_cast<float>(
        tone_curve(y, opt.shadows, opt.highlights, opt.contrast));
  }
  return lut;
}

inline float lut_sample(const std::vector<float>& lut, float y) {
  const float pos = clamp01(y) * static_cast<float>(kLutSize - 1);
  const int i = static_cast<int>(pos);
  if (i >= kLutSize - 1) return lut.back();
  const float t = pos - static_cast<float>(i);
  return lut[static_cast<size_t>(i)] * (1.0f - t) + lut[static_cast<size_t>(i) + 1] * t;
}

}  // namespace

LightStats measure_light(const ImageF32& img) {
  LightStats stats;
  if (img.empty() || img.channels() < 3) return stats;

  constexpr int kBins = 4096;
  std::vector<double> hist(kBins, 0.0);
  double sum = 0.0, sum_sq = 0.0, sat_sum = 0.0;
  double low = 0.0, high = 0.0;
  const size_t n = img.pixel_count();
  for (size_t i = 0; i < n; ++i) {
    const float* p = img.data() + i * static_cast<size_t>(img.channels());
    const double r = p[0], g = p[1], b = p[2];
    const double y = luma709(r, g, b);
    sum += y;
    sum_sq += y * y;
    const double maxc = std::max({r, g, b});
    const double minc = std::min({r, g, b});
    sat_sum += maxc > 1e-6 ? (maxc - minc) / maxc : 0.0;
    if (y <= 0.01) low += 1.0;
    if (y >= 0.99) high += 1.0;
    const int bin = std::min(kBins - 1, std::max(0, static_cast<int>(y * (kBins - 1))));
    hist[static_cast<size_t>(bin)] += 1.0;
  }

  stats.mean = sum / static_cast<double>(n);
  const double variance = std::max(0.0, sum_sq / static_cast<double>(n) - stats.mean * stats.mean);
  stats.contrast = std::sqrt(variance);
  stats.saturation = sat_sum / static_cast<double>(n);
  stats.clip_low = 100.0 * low / static_cast<double>(n);
  stats.clip_high = 100.0 * high / static_cast<double>(n);

  auto percentile = [&](double fraction) {
    const double target = fraction * static_cast<double>(n);
    double acc = 0.0;
    for (int i = 0; i < kBins; ++i) {
      acc += hist[static_cast<size_t>(i)];
      if (acc >= target) return static_cast<double>(i) / (kBins - 1);
    }
    return 1.0;
  };
  stats.p01 = percentile(0.01);
  stats.p50 = percentile(0.50);
  stats.p99 = percentile(0.99);
  return stats;
}

bool light_is_neutral(const LightOptions& o) {
  return !o.auto_white_balance && !o.auto_tone && o.exposure == 0.0f && o.contrast == 0.0f &&
         o.saturation == 0.0f && o.shadows == 0.0f && o.highlights == 0.0f && o.warmth == 0.0f;
}

ImageF32 adjust_light(const ImageF32& src, const LightOptions& options, LightReport* report) {
  const auto t0 = std::chrono::steady_clock::now();
  ImageF32 out(src.width(), src.height(), src.channels());
  out = src;  // کپی کامل: آلفا و هندسه هرگز تغییر نمی‌کنند

  LightReport local;
  LightReport* rep = report ? report : &local;
  if (src.empty() || src.channels() < 3) {
    if (report) {
      report->before = measure_light(src);
      report->after = report->before;
      report->seconds = 0.0;
    }
    return out;
  }

  rep->before = measure_light(src);
  if (light_is_neutral(options)) {
    // خاموش یعنی خاموش: هیچ رفت‌وبرگشتِ sRGB↔خطی، هیچ خطای گردکردن.
    rep->after = rep->before;
    rep->seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return out;
  }

  double red_gain = 1.0, green_gain = 1.0, blue_gain = 1.0;

  // ---------- ۱) تعادل رنگ خودکار (روی نور خطی، تنها روی پیکسل‌های میانه) ----------
  if (options.auto_white_balance) {
    double sr = 0.0, sg = 0.0, sb = 0.0;
    size_t counted = 0;
    const size_t n = src.pixel_count();
    for (size_t i = 0; i < n; ++i) {
      const float* p = src.data() + i * static_cast<size_t>(src.channels());
      const double y = luma709(p[0], p[1], p[2]);
      if (y < 0.05 || y > 0.95) continue;  // سیاه/سفیدِ اشباع اطلاعات رنگ ندارند
      sr += srgb_to_linear(p[0]);
      sg += srgb_to_linear(p[1]);
      sb += srgb_to_linear(p[2]);
      ++counted;
    }
    if (counted > 0 && sr > 1e-6 && sg > 1e-6 && sb > 1e-6) {
      const double mean =
          (sr + sg + sb) / (3.0 * static_cast<double>(counted));  // میانگین هر کانال
      const double cr = std::clamp(mean / (sr / static_cast<double>(counted)), 0.75, 1.35);
      const double cg = std::clamp(mean / (sg / static_cast<double>(counted)), 0.75, 1.35);
      const double cb = std::clamp(mean / (sb / static_cast<double>(counted)), 0.75, 1.35);
      // نرمال‌سازی روشنایی: گین‌ها روشنایی کلی تصویر را جابه‌جا نکنند.
      const double lum = kLumaR * cr + kLumaG * cg + kLumaB * cb;
      if (lum > 1e-6) {
        red_gain = cr / lum;
        green_gain = cg / lum;
        blue_gain = cb / lum;
        rep->white_balance_applied = true;
      }
    }
  }

  // ---------- ۲) نوردهی و کنتراست خودکار ----------
  double exposure_ev = options.exposure;
  double contrast = options.contrast;
  if (options.auto_tone) {
    const double target = 0.45;
    const double median = std::max(rep->before.p50, 1e-3);
    double limit = 1.2;
    // اگر از قبل هایلایت سوخته داریم، بالا بردن نور آن را بدتر می‌کند.
    if (rep->before.clip_high > 1.0) limit = 0.6;
    const double ev = std::clamp(std::log2(target / median), -limit, limit);
    rep->auto_exposure_ev = ev;
    exposure_ev += ev;

    // کنتراست خودکار فقط وقتی معنا دارد که تصویر *کم‌کنتراست* است، نه وقتی
    // فقط تاریک است: روی تصویر تاریک، افزودن کنتراست میانه را پایین‌تر می‌برد
    // و نتیجه برعکس می‌شود. پس اول نوردهی، بعد (فقط اگر نزدیک هدف بودیم) کنتراست.
    double add = 0.0;
    if (std::fabs(ev) < 0.35) {
      const double range = std::max(0.0, rep->before.p99 - rep->before.p01);
      add = std::clamp((0.85 - range) / 0.85 * 0.45, 0.0, 0.25);
      if (rep->before.clip_high > 2.0) add *= 0.5;
    }
    rep->auto_contrast = add;
    contrast = std::clamp(contrast + add, -0.6, 0.6);
    rep->tone_applied = true;
  }

  // ---------- ۳) گرمی/سردی + نوردهی (ضربی، در نور خطی) ----------
  const double exposure_gain = std::pow(2.0, exposure_ev);
  const double warm = std::clamp(static_cast<double>(options.warmth), -1.0, 1.0);
  if (warm != 0.0) {
    const double wr = std::pow(2.0, 0.10 * warm);
    const double wb = std::pow(2.0, -0.10 * warm);
    const double lum = kLumaR * wr + kLumaG + kLumaB * wb;
    red_gain *= wr / lum;
    blue_gain *= wb / lum;
  }
  red_gain *= exposure_gain;
  green_gain *= exposure_gain;
  blue_gain *= exposure_gain;
  rep->wb_red_gain = red_gain;
  rep->wb_green_gain = green_gain;
  rep->wb_blue_gain = blue_gain;

  LightOptions curve = options;
  curve.contrast = static_cast<float>(contrast);
  const std::vector<float> lut = build_lut(curve);
  const double saturation = std::clamp(static_cast<double>(options.saturation), -1.0, 1.0);

  const size_t n = src.pixel_count();
  const int channels = src.channels();
  for (size_t i = 0; i < n; ++i) {
    const float* s = src.data() + i * static_cast<size_t>(channels);
    float* d = out.data() + i * static_cast<size_t>(channels);

    const float r = linear_to_srgb(clamp01(static_cast<float>(srgb_to_linear(s[0]) * red_gain)));
    const float g = linear_to_srgb(clamp01(static_cast<float>(srgb_to_linear(s[1]) * green_gain)));
    const float b = linear_to_srgb(clamp01(static_cast<float>(srgb_to_linear(s[2]) * blue_gain)));

    const float y = static_cast<float>(luma709(r, g, b));
    const float y2 = lut_sample(lut, y);
    const float scale = y > 1e-4f ? y2 / y : 1.0f;

    float rr = r * scale;
    float gg = g * scale;
    float bb = b * scale;

    if (saturation != 0.0) {
      const double factor = 1.0 + saturation;
      rr = static_cast<float>(y2 + (rr - y2) * factor);
      gg = static_cast<float>(y2 + (gg - y2) * factor);
      bb = static_cast<float>(y2 + (bb - y2) * factor);
    }

    d[0] = clamp01(rr);
    d[1] = clamp01(gg);
    d[2] = clamp01(bb);
    if (channels == 4) d[3] = s[3];  // آلفا: نه رنگ است، نه روشنایی
  }

  rep->after = measure_light(out);
  rep->seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  return out;
}

}  // namespace atlas

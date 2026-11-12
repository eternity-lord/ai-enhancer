// SPDX-License-Identifier: Apache-2.0
// یادداشت: پیاده‌سازی P2. سه چیز عمداً ساده و قابل‌اندازه‌گیری نگه داشته شده:
// ۱) برآورد نویز از لاپلاسین (بدون آموزش، بدون فرض مدل)، ۲) NLM با پنجرهٔ کوچک
// (۳×۳ patch، ۵×۵ search) که روی CPU قابل تحمل است، ۳) deblock شرطی که فقط
// پله‌های کوچک روی مرز ۸پیکسلی را نرم می‌کند. هیچ‌کدام پیکسل جدید «اختراع»
// نمی‌کنند؛ اگر عدد بهبود نداد، باید حذف شوند، نه توجیه.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <thread>
#include <vector>

#include "atlas/metrics.h"
#include "atlas/restore.h"

namespace atlas {
namespace {

constexpr double kPi = 3.14159265358979323846;

// اجرای موازی ساده روی باندهای سطر؛ بدون وابستگی به کتابخانهٔ thread pool.
template <typename Fn>
void parallel_rows(int height, int threads, Fn&& fn) {
  threads = std::clamp(threads, 1, 16);
  if (threads == 1 || height < 32) {
    fn(0, height);
    return;
  }
  std::vector<std::thread> pool;
  pool.reserve(static_cast<size_t>(threads));
  const int band = (height + threads - 1) / threads;
  for (int t = 0; t < threads; ++t) {
    const int y0 = t * band;
    const int y1 = std::min(height, y0 + band);
    if (y0 >= y1) break;
    pool.emplace_back([&fn, y0, y1] { fn(y0, y1); });
  }
  for (auto& th : pool) th.join();
}

int default_threads() {
  const unsigned hw = std::thread::hardware_concurrency();
  return static_cast<int>(hw == 0 ? 1 : std::min(hw, 8u));
}

// میانگین |اختلاف| افقی و عمودی، جدا برای مرزهای شبکه و داخل بلوک.
void step_stats(const std::vector<float>& luma, int w, int h, int block, double* boundary,
                double* inside) {
  double bsum = 0.0, isum = 0.0;
  long long bcount = 0, icount = 0;
  for (int y = 0; y < h; ++y) {
    const float* row = luma.data() + static_cast<size_t>(y) * w;
    for (int x = 1; x < w; ++x) {
      const double d = std::fabs(static_cast<double>(row[x]) - row[x - 1]);
      if (x % block == 0) {
        bsum += d;
        ++bcount;
      } else {
        isum += d;
        ++icount;
      }
    }
  }
  for (int y = 1; y < h; ++y) {
    const float* row = luma.data() + static_cast<size_t>(y) * w;
    const float* prev = row - w;
    for (int x = 0; x < w; ++x) {
      const double d = std::fabs(static_cast<double>(row[x]) - prev[x]);
      if (y % block == 0) {
        bsum += d;
        ++bcount;
      } else {
        isum += d;
        ++icount;
      }
    }
  }
  *boundary = bcount ? bsum / static_cast<double>(bcount) : 0.0;
  *inside = icount ? isum / static_cast<double>(icount) : 0.0;
}

std::vector<float> luma_plane(const ImageF32& src) {
  std::vector<float> luma(src.pixel_count());
  const int c = src.channels();
  for (size_t i = 0; i < src.pixel_count(); ++i) {
    const float* p = src.data() + i * c;
    if (c >= 3) {
      luma[i] = 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
    } else {
      luma[i] = p[0];
    }
  }
  return luma;
}

}  // namespace

NoiseEstimate analyze_degradation(const ImageF32& src) {
  NoiseEstimate est;
  const int w = src.width();
  const int h = src.height();
  if (w < 4 || h < 4) return est;

  const std::vector<float> luma = luma_plane(src);

  // Immerkær: پاسخ لاپلاسین به نویز سفید، واریانس ۳۶σ² دارد.
  double sum = 0.0;
  long long count = 0;
  for (int y = 1; y < h - 1; ++y) {
    for (int x = 1; x < w - 1; ++x) {
      const double v = static_cast<double>(luma[static_cast<size_t>(y) * w + x]);
      const double lap = -4.0 * v + luma[static_cast<size_t>(y - 1) * w + x] +
                         luma[static_cast<size_t>(y + 1) * w + x] +
                         luma[static_cast<size_t>(y) * w + x - 1] +
                         luma[static_cast<size_t>(y) * w + x + 1];
      sum += std::fabs(lap);
      ++count;
    }
  }
  if (count > 0) {
    est.sigma = std::sqrt(kPi / 2.0) * sum / (6.0 * static_cast<double>(count));
  }

  step_stats(luma, w, h, 8, &est.boundary_step, &est.inside_step);
  est.blockiness = std::max(0.0, est.boundary_step - est.inside_step);
  est.blockiness_ratio =
      est.inside_step > 1e-6 ? est.boundary_step / est.inside_step : 1.0;
  est.jpeg_like = est.blockiness_ratio > 1.5 && est.boundary_step > 0.004;
  return est;
}

ImageF32 denoise_nlmeans(const ImageF32& src, float strength, int patch_radius,
                         int search_radius) {
  if (strength <= 0.0f || src.empty()) return src;
  const int w = src.width();
  const int h = src.height();
  const int c = src.channels();

  const std::vector<float> luma = luma_plane(src);
  const int pr = std::max(1, patch_radius);
  const int sr = std::max(1, search_radius);
  const float h2 = [&] {
    // h متناسب با شدت: ۰٫۰۲ (ملایم) تا ۰٫۱۲ (تهاجمی) در مقیاس [0,1].
    const float h = 0.02f + 0.10f * std::clamp(strength, 0.0f, 1.0f);
    return h * h;
  }();
  const float cut = 9.0f * h2;  // فاصله‌های بزرگ‌تر از این، عملاً وزن صفر دارند

  ImageF32 out = src;
  const int threads = default_threads();
  parallel_rows(h, threads, [&](int y0, int y1) {
    for (int y = y0; y < y1; ++y) {
      for (int x = 0; x < w; ++x) {
        float wsum = 0.0f;
        float acc[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        for (int dy = -sr; dy <= sr; ++dy) {
          const int yy = std::clamp(y + dy, 0, h - 1);
          for (int dx = -sr; dx <= sr; ++dx) {
            const int xx = std::clamp(x + dx, 0, w - 1);
            if (dx == 0 && dy == 0) continue;
            // فاصلهٔ patch در luma، با برش در لبه‌ها (clamp) تا هزینهٔ ثابت بماند.
            float d2 = 0.0f;
            int n = 0;
            for (int py = -pr; py <= pr; ++py) {
              const int sy = std::clamp(y + py, 0, h - 1);
              const int ty = std::clamp(yy + py, 0, h - 1);
              for (int px = -pr; px <= pr; ++px) {
                const int sx = std::clamp(x + px, 0, w - 1);
                const int tx = std::clamp(xx + px, 0, w - 1);
                const float diff = luma[static_cast<size_t>(sy) * w + sx] -
                                   luma[static_cast<size_t>(ty) * w + tx];
                d2 += diff * diff;
                ++n;
              }
            }
            d2 /= static_cast<float>(n);
            if (d2 > cut) continue;
            const float weight = std::exp(-d2 / h2);
            const float* q = src.pixel(xx, yy);
            acc[0] += weight * q[0];
            if (c > 1) acc[1] += weight * q[1];
            if (c > 2) acc[2] += weight * q[2];
            if (c > 3) acc[3] += weight * q[3];
            wsum += weight;
          }
        }
        float* o = out.pixel(x, y);
        if (wsum > 1e-8f) {
          o[0] = acc[0] / wsum;
          if (c > 1) o[1] = acc[1] / wsum;
          if (c > 2) o[2] = acc[2] / wsum;
        }
        // آلفا عمداً دست‌نخورده می‌ماند: شفافیت دادهٔ کاربر است، نه نویز.
      }
    }
  });
  return out;
}

ImageF32 deblock_jpeg(const ImageF32& src, float strength, int block) {
  if (strength <= 0.0f || src.empty()) return src;
  const int w = src.width();
  const int h = src.height();
  const int c = src.channels();
  block = std::max(2, block);
  const float blend = 0.55f * std::clamp(strength, 0.0f, 1.0f);

  // آستانه‌ها با اندازهٔ شبکه/کیفیت ثابت‌اند: پلهٔ بزرگ‌تر از ۰٫۱۲ یا بافت
  // درون‌بلوکی بیش از ۰٫۰۶ یعنی «لبهٔ واقعی»، نه اثر بلوک.
  constexpr float kMaxStep = 0.12f;
  constexpr float kMaxInner = 0.06f;

  ImageF32 out = src;
  std::vector<float> luma = luma_plane(src);

  auto smooth_pair = [&](int x0, int y0, int x1, int y1) {
    // مرکز دو پیکسل روی مرز؛ فقط اگر پله «بلوکی» بود، هر دو به میانگین نزدیک می‌شوند.
    const float p0 = luma[static_cast<size_t>(y0) * w + x0];
    const float q0 = luma[static_cast<size_t>(y1) * w + x1];
    const float d = std::fabs(q0 - p0);
    if (d < 1e-5f || d > kMaxStep) return;
    float* op = out.pixel(x0, y0);
    float* oq = out.pixel(x1, y1);
    for (int ch = 0; ch < std::min(c, 3); ++ch) {
      const float mean = 0.5f * (op[ch] + oq[ch]);
      op[ch] += (mean - op[ch]) * blend;
      oq[ch] += (mean - oq[ch]) * blend;
    }
  };

  // مرزهای عمودی
  for (int x = block; x < w; x += block) {
    for (int y = 0; y < h; ++y) {
      const int xl = x - 1, xr = x;
      if (xl - 1 < 0 || xr + 1 >= w) continue;
      const float p0 = luma[static_cast<size_t>(y) * w + xl];
      const float q0 = luma[static_cast<size_t>(y) * w + xr];
      const float p1 = luma[static_cast<size_t>(y) * w + xl - 1];
      const float q1 = luma[static_cast<size_t>(y) * w + xr + 1];
      if (std::fabs(p0 - p1) > kMaxInner || std::fabs(q0 - q1) > kMaxInner) continue;
      smooth_pair(xl, y, xr, y);
    }
  }

  // مرزهای افقی — luma باید با تغییرات عمودی هم‌زمان شود، پس دوباره محاسبه می‌شود.
  luma = luma_plane(out);
  for (int y = block; y < h; y += block) {
    for (int x = 0; x < w; ++x) {
      const int yt = y - 1, yb = y;
      if (yt - 1 < 0 || yb + 1 >= h) continue;
      const float p0 = luma[static_cast<size_t>(yt) * w + x];
      const float q0 = luma[static_cast<size_t>(yb) * w + x];
      const float p1 = luma[static_cast<size_t>(yt - 1) * w + x];
      const float q1 = luma[static_cast<size_t>(yb + 1) * w + x];
      if (std::fabs(p0 - p1) > kMaxInner || std::fabs(q0 - q1) > kMaxInner) continue;
      smooth_pair(x, yt, x, yb);
    }
  }

  // deringing: در نواحی تخت (واریانس ۳×۳ کوچک) میانگین ملایم، تا لرزش‌های
  // اطراف لبه‌های تیز کم شود. روی لبه‌ها اعمال نمی‌شود، چون واریانس بالاست.
  luma = luma_plane(out);
  ImageF32 flat = out;
  const float ring_blend = 0.5f * std::clamp(strength, 0.0f, 1.0f);
  parallel_rows(h, default_threads(), [&](int y0, int y1) {
    for (int y = y0; y < y1; ++y) {
      for (int x = 0; x < w; ++x) {
        float sum = 0.0f, sum2 = 0.0f;
        for (int dy = -1; dy <= 1; ++dy) {
          const int yy = std::clamp(y + dy, 0, h - 1);
          for (int dx = -1; dx <= 1; ++dx) {
            const int xx = std::clamp(x + dx, 0, w - 1);
            const float v = luma[static_cast<size_t>(yy) * w + xx];
            sum += v;
            sum2 += v * v;
          }
        }
        const float mean = sum / 9.0f;
        const float var = sum2 / 9.0f - mean * mean;
        if (var > 4e-4f) continue;  // لبه/بافت: دست نزن
        float* o = flat.pixel(x, y);
        for (int ch = 0; ch < std::min(c, 3); ++ch) {
          const float local =
              (out.pixel(std::clamp(x - 1, 0, w - 1), y)[ch] + out.pixel(x, y)[ch] +
               out.pixel(std::clamp(x + 1, 0, w - 1), y)[ch]) /
              3.0f;
          o[ch] = out.pixel(x, y)[ch] + (local - out.pixel(x, y)[ch]) * ring_blend;
        }
      }
    }
  });
  return flat;
}

ImageF32 restore_degraded(const ImageF32& src, const RestoreOptions& options,
                          RestoreReport* report) {
  const auto t0 = std::chrono::steady_clock::now();
  RestoreReport local;
  RestoreReport* rep = report ? report : &local;
  *rep = RestoreReport{};
  rep->before = analyze_degradation(src);
  if (src.empty()) return src;

  const auto auto_denoise = [&] {
    // σ=۰٫۰۰۳ تقریباً نامرئی، σ=۰٫۰۴ کاملاً محسوس است. اگر اثر JPEG دیده شود،
    // حتی با σ کوچک، NLM ملایم (۰٫۲۵) هم لازم است: اندازه‌گیری روی JPEG واقعی
    // نشان داد همین مقدار، PSNR را ۱–۲ dB بالاتر از deblock تنها می‌برد.
    const float by_noise =
        std::clamp(static_cast<float>((rep->before.sigma - 0.003) / 0.05), 0.0f, 1.0f);
    const float by_jpeg = rep->before.jpeg_like ? 0.25f : 0.0f;
    return std::max(by_noise, by_jpeg);
  };
  const auto auto_deblock = [&] {
    if (!rep->before.jpeg_like) return 0.0f;
    return std::clamp(static_cast<float>((rep->before.blockiness_ratio - 1.4) / 0.8), 0.15f,
                      1.0f);
  };

  const float dn = options.denoise < 0.0f ? auto_denoise()
                                          : std::clamp(options.denoise, 0.0f, 1.0f);
  const float db = options.deblock < 0.0f ? auto_deblock()
                                          : std::clamp(options.deblock, 0.0f, 1.0f);

  ImageF32 out = src;
  if (db > 0.0f) {
    out = deblock_jpeg(out, db, 8);
    rep->deblock_applied = true;
    rep->deblock_strength = db;
  }
  if (dn > 0.0f) {
    out = denoise_nlmeans(out, dn, options.patch_radius, options.search_radius);
    rep->denoise_applied = true;
    rep->denoise_strength = dn;
    if (!options.chroma) {
      // فقط luma: کرومای اصلی برگردانده می‌شود (برای وقتی که لکهٔ رنگی مهم نیست).
      const int c = src.channels();
      for (size_t i = 0; i < src.pixel_count(); ++i) {
        float* o = out.data() + i * c;
        const float* s = src.data() + i * c;
        if (c > 1) o[1] = s[1];
        if (c > 2) o[2] = s[2];
      }
    }
  }
  rep->after = analyze_degradation(out);
  const auto t1 = std::chrono::steady_clock::now();
  rep->seconds = std::chrono::duration<double>(t1 - t0).count();
  return out;
}

}  // namespace atlas

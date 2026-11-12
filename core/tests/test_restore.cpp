// SPDX-License-Identifier: Apache-2.0
// یادداشت: تست‌های P2 با دو نوع خرابیِ *واقعی* سنجیده می‌شوند، نه خرابی ساختگی:
// ۱) نویز گاوسی با σ مشخص (مولد LCG قطعی)، ۲) JPEG واقعی که خودِ کدک پروژه در
// کیفیت پایین می‌نویسد و دوباره می‌خواند. معیار پذیرش «بهتر شد» است، نه «عدد قشنگ»:
// σ کم شود، لبه بماند، PSNR/SSIM نسبت به تصویر پاک بالا برود.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>

#include "atlas/image.h"
#include "atlas/image_io.h"
#include "atlas/metrics.h"
#include "atlas/restore.h"
#include "test.h"

namespace {

// LCG قطعی: تست باید هر بار همان اعداد را بدهد.
struct Lcg {
  uint32_t state = 12345u;
  float next() {  // تقریباً نرمال در [-3,3] با مجموع سه عدد یکنواخت
    float sum = 0.0f;
    for (int i = 0; i < 3; ++i) {
      state = state * 1664525u + 1013904223u;
      sum += static_cast<float>((state >> 8) & 0xFFFF) / 65535.0f - 0.5f;
    }
    return sum * 2.0f;
  }
};

std::filesystem::path temp_path(const char* name) {
  return std::filesystem::temp_directory_path() / name;
}

atlas::ImageF32 flat_gray(int w, int h, float level) {
  atlas::ImageF32 img(w, h, 3);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      float* p = img.pixel(x, y);
      p[0] = p[1] = p[2] = level;
    }
  }
  return img;
}

atlas::ImageF32 gradient(int w, int h) {
  atlas::ImageF32 img(w, h, 3);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const float v = 0.25f + 0.5f * static_cast<float>(x + y) / static_cast<float>(w + h);
      float* p = img.pixel(x, y);
      p[0] = p[1] = p[2] = v;
    }
  }
  return img;
}

// تصویر «عکس‌مانند» مصنوعی: گرادیان + بافت ریز + یک مستطیل و یک دایره.
// بدون این بافت، JPEG فقط گرادیان را کوانتیزه می‌کند و تست واقع‌گرا نیست.
atlas::ImageF32 photo(int w, int h) {
  atlas::ImageF32 img(w, h, 3);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const float fx = static_cast<float>(x) / static_cast<float>(w);
      const float fy = static_cast<float>(y) / static_cast<float>(h);
      float v = 0.2f + 0.55f * (0.6f * fx + 0.4f * fy);
      v += 0.05f * std::sin(fx * 40.0f) * std::cos(fy * 33.0f);
      if (x >= w / 3 && x < 2 * w / 3 && y >= h / 4 && y < 3 * h / 4) v += 0.15f;
      const float dx = static_cast<float>(x) - w * 0.5f;
      const float dy = static_cast<float>(y) - h * 0.5f;
      if (dx * dx + dy * dy < (h / 7.0f) * (h / 7.0f)) v -= 0.18f;
      float* p = img.pixel(x, y);
      p[0] = std::min(1.0f, std::max(0.0f, v * 1.02f));
      p[1] = std::min(1.0f, std::max(0.0f, v));
      p[2] = std::min(1.0f, std::max(0.0f, v * 0.95f));
    }
  }
  return img;
}

// تصویر با لبه‌های تیز در جای دلخواه (نه لزوماً روی شبکهٔ ۸).
atlas::ImageF32 edges(int w, int h, int edge_x, int edge_y) {
  atlas::ImageF32 img(w, h, 3);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const bool inside = x >= edge_x && x < edge_x + 24 && y >= edge_y && y < edge_y + 24;
      const float v = inside ? 0.85f : 0.2f;
      float* p = img.pixel(x, y);
      p[0] = p[1] = p[2] = v;
    }
  }
  return img;
}

atlas::ImageF32 add_noise(const atlas::ImageF32& src, float sigma) {
  atlas::ImageF32 out = src;
  Lcg rng;
  for (size_t i = 0; i < out.pixel_count(); ++i) {
    float* p = out.data() + i * out.channels();
    for (int c = 0; c < std::min(3, out.channels()); ++c) {
      p[c] = std::min(1.0f, std::max(0.0f, p[c] + rng.next() * sigma));
    }
  }
  return out;
}

// JPEG واقعی از مسیر خودِ پروژه: نوشتن با کیفیت پایین و خواندن دوباره.
bool jpeg_roundtrip(const atlas::ImageF32& src, int quality, atlas::ImageF32* out) {
  const auto path = temp_path("atlas_restore_test.jpg");
  std::string error;
  if (!atlas::save_image(path.string(), src, quality, &error)) return false;
  return atlas::load_image(path.string(), out, &error);
}

double region_sigma(const atlas::ImageF32& img) {
  const auto luma = atlas::to_luma(img);
  double mean = 0.0;
  for (size_t i = 0; i < luma.pixel_count(); ++i) mean += luma.data()[i];
  mean /= static_cast<double>(luma.pixel_count());
  double var = 0.0;
  for (size_t i = 0; i < luma.pixel_count(); ++i) {
    const double d = luma.data()[i] - mean;
    var += d * d;
  }
  return std::sqrt(var / static_cast<double>(luma.pixel_count()));
}

}  // namespace

ATLAS_TEST(noise_estimate_reads_known_sigma) {
  const auto clean = flat_gray(96, 96, 0.5f);
  const auto noisy = add_noise(clean, 0.04f);
  const auto est = atlas::analyze_degradation(noisy);
  CHECK(est.sigma > 0.015);
  CHECK(est.sigma < 0.05);
  CHECK(!est.jpeg_like);
}

ATLAS_TEST(noise_estimate_clean_gradient_is_quiet) {
  const auto est = atlas::analyze_degradation(gradient(96, 96));
  CHECK(est.sigma < 0.005);
  CHECK(std::fabs(est.blockiness) < 0.01);
}

ATLAS_TEST(denoise_reduces_noise_on_flat_region) {
  const auto clean = flat_gray(96, 96, 0.5f);
  const auto noisy = add_noise(clean, 0.04f);
  const auto denoised = atlas::denoise_nlmeans(noisy, 0.9f);
  const double before = region_sigma(noisy);
  const double after = region_sigma(denoised);
  CHECK(before > 0.02);
  CHECK(after < before * 0.6);
}

ATLAS_TEST(denoise_keeps_edge_contrast) {
  const auto clean = edges(96, 96, 30, 30);
  const auto denoised = atlas::denoise_nlmeans(clean, 1.0f);
  const auto luma_a = atlas::to_luma(clean);
  const auto luma_b = atlas::to_luma(denoised);
  auto contrast = [](const atlas::ImageF32& luma) {
    double inside = 0.0, outside = 0.0;
    int ni = 0, no = 0;
    for (int y = 0; y < luma.height(); ++y) {
      for (int x = 0; x < luma.width(); ++x) {
        const bool is_inside = x >= 34 && x < 50 && y >= 34 && y < 50;
        const bool is_outside = (x < 26 || x >= 58) && (y < 26 || y >= 58);
        if (is_inside) {
          inside += luma.at(x, y, 0);
          ++ni;
        } else if (is_outside) {
          outside += luma.at(x, y, 0);
          ++no;
        }
      }
    }
    return (ni && no) ? inside / ni - outside / no : 0.0;
  };
  const double c0 = contrast(luma_a);
  const double c1 = contrast(luma_b);
  CHECK(c0 > 0.5);
  CHECK(c1 > c0 * 0.95);
}

ATLAS_TEST(deblock_on_real_jpeg_reduces_blockiness) {
  const auto clean = photo(320, 320);
  atlas::ImageF32 jpg;
  CHECK(jpeg_roundtrip(clean, 20, &jpg));

  const auto before = atlas::analyze_degradation(jpg);
  CHECK(before.jpeg_like);
  CHECK(before.blockiness_ratio > 1.5);

  const auto fixed = atlas::deblock_jpeg(jpg, 1.0f);
  const auto after = atlas::analyze_degradation(fixed);
  CHECK(after.blockiness_ratio < before.blockiness_ratio * 0.7);
  CHECK(after.blockiness < before.blockiness * 0.5);
  // PSNR باید نسبت به تصویر پاک بالا برود، وگرنه فیلتر فقط تار کرده است.
  CHECK(atlas::psnr_db(clean, fixed) > atlas::psnr_db(clean, jpg) + 0.3);
}

ATLAS_TEST(deblock_leaves_real_edges_alone) {
  // لبه‌ها روی ۳۰ هستند؛ شبکه روی مضارب ۸ است، پس این‌ها «اثر بلوک» نیستند.
  const auto clean = edges(96, 96, 30, 30);
  const auto fixed = atlas::deblock_jpeg(clean, 1.0f);
  CHECK(atlas::psnr_db(clean, fixed) > 30.0);
  CHECK(atlas::ssim(clean, fixed) > 0.98);
}

ATLAS_TEST(restore_is_noop_on_clean_image) {
  const auto clean = gradient(96, 96);
  atlas::RestoreReport rep;
  const auto out = atlas::restore_degraded(clean, atlas::RestoreOptions{}, &rep);
  CHECK(!rep.denoise_applied);
  CHECK(!rep.deblock_applied);
  CHECK(rep.before.sigma < 0.005);
  CHECK(atlas::compare_images(clean, out).max_abs < 1e-6);
}

ATLAS_TEST(restore_auto_cleans_real_jpeg) {
  const auto clean = photo(320, 320);
  atlas::ImageF32 jpg;
  CHECK(jpeg_roundtrip(clean, 20, &jpg));

  atlas::RestoreReport rep;
  const auto out = atlas::restore_degraded(jpg, atlas::RestoreOptions{}, &rep);
  CHECK(rep.deblock_applied);
  CHECK(rep.denoise_applied);
  CHECK(rep.denoise_strength <= 0.3f);  // برای JPEG کم‌نویز نباید تهاجمی شود
  CHECK(rep.after.blockiness < rep.before.blockiness * 0.4);
  CHECK(atlas::psnr_db(clean, out) > atlas::psnr_db(clean, jpg) + 1.0);
  CHECK(atlas::ssim(clean, out) > atlas::ssim(clean, jpg));
}

ATLAS_TEST(restore_auto_does_not_over_smooth_high_quality) {
  const auto clean = photo(320, 320);
  atlas::ImageF32 jpg;
  CHECK(jpeg_roundtrip(clean, 90, &jpg));

  atlas::RestoreReport rep;
  const auto out = atlas::restore_degraded(jpg, atlas::RestoreOptions{}, &rep);
  // در کیفیت بالا، بازسازی نباید کیفیت را پایین بیاورد (خطای مجاز ۰٫۲ dB).
  CHECK(atlas::psnr_db(clean, out) > atlas::psnr_db(clean, jpg) - 0.2);
  CHECK(atlas::ssim(clean, out) >= atlas::ssim(clean, jpg));
}

ATLAS_TEST(restore_keeps_alpha_untouched) {
  atlas::ImageF32 img(64, 64, 4);
  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 64; ++x) {
      float* p = img.pixel(x, y);
      p[0] = p[1] = p[2] = 0.4f;
      p[3] = (x < 32) ? 1.0f : 0.0f;
    }
  }
  atlas::RestoreOptions opt;
  opt.denoise = 0.8f;
  const auto out = atlas::restore_degraded(img, opt, nullptr);
  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 64; ++x) {
      CHECK(out.at(x, y, 3) == img.at(x, y, 3));
    }
  }
}

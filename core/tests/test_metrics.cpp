// SPDX-License-Identifier: Apache-2.0
// یادداشت: تست SSIM با مقدار مرجع: برای دو تصویر یکسان باید ۱ و برای نویز
// سنگین پایین باشد. مهم‌تر از عدد مطلق، «جهت درست» است: خروجی تارتر نباید
// SSIM بالاتری از خروجی شارپ‌تر با همان خطای پیکسلی بگیرد.
#include <cmath>

#include "atlas/image.h"
#include "atlas/metrics.h"
#include "test.h"

namespace {

atlas::ImageF32 gradient(int w, int h, float offset = 0.0f) {
  atlas::ImageF32 img(w, h, 3);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      float* p = img.pixel(x, y);
      for (int c = 0; c < 3; ++c) {
        p[c] = std::min(1.0f, std::max(0.0f, static_cast<float>(x + y + c) / (w + h) + offset));
      }
    }
  }
  return img;
}

}  // namespace

ATLAS_TEST(metrics_identical_images_are_perfect) {
  const auto a = gradient(64, 48);
  const auto m = atlas::compare_images(a, a);
  CHECK(m.valid);
  CHECK(m.psnr_db > 90.0);
  CHECK_NEAR(m.ssim, 1.0, 1e-6);
  CHECK_NEAR(m.mae, 0.0, 1e-9);
  CHECK_NEAR(m.max_abs, 0.0, 1e-9);
}

ATLAS_TEST(metrics_psnr_matches_formula) {
  auto a = gradient(32, 32);
  auto b = a;
  // همهٔ پیکسل‌ها +۰٫۱ (در بازهٔ مجاز بمانیم)
  for (size_t i = 0; i < b.pixel_count() * 3; ++i) b.data()[i] += 0.1f;

  const double expected = 10.0 * std::log10(1.0 / 0.01);  // ۲۰ dB
  CHECK_NEAR(atlas::psnr_db(a, b), expected, 0.05);
  const auto m = atlas::compare_images(a, b);
  CHECK_NEAR(m.mae, 0.1, 1e-4);
  CHECK(m.ssim > 0.9);  // اختلاف ثابت = ساختار حفظ شده
}

ATLAS_TEST(metrics_ssim_drops_with_noise) {
  auto a = gradient(64, 64);
  auto noisy = a;
  for (size_t i = 0; i < noisy.pixel_count(); ++i) {
    for (int c = 0; c < 3; ++c) {
      const float n = (static_cast<int>(i * 7 + c * 13) % 21 - 10) / 100.0f;
      float& v = noisy.data()[i * 3 + c];
      v = std::min(1.0f, std::max(0.0f, v + n));
    }
  }
  const auto m = atlas::compare_images(a, noisy);
  CHECK(m.valid);
  CHECK(m.ssim < 0.75);
  CHECK(m.psnr_db > 10.0);
  CHECK(m.psnr_db < 40.0);
}

ATLAS_TEST(metrics_handle_size_mismatch_by_center_crop) {
  const auto a = gradient(80, 60);
  const auto b = gradient(64, 60);
  const auto m = atlas::compare_images(a, b);
  CHECK(m.valid);
  CHECK(m.width == 64);
  CHECK(m.height == 60);
}

ATLAS_TEST(metrics_ignore_alpha_channel) {
  atlas::ImageF32 a(16, 16, 3);
  a.fill(0.4f);
  atlas::ImageF32 b(16, 16, 4);
  b.fill(0.4f);
  for (size_t i = 0; i < b.pixel_count(); ++i) b.data()[i * 4 + 3] = 0.0f;  // آلفا صفر
  const auto m = atlas::compare_images(a, b);
  CHECK(m.valid);
  CHECK(m.psnr_db > 90.0);
  CHECK_NEAR(m.mae, 0.0, 1e-9);
}

ATLAS_TEST(metrics_empty_input_is_invalid) {
  atlas::ImageF32 empty;
  const auto m = atlas::compare_images(empty, empty);
  CHECK(!m.valid);
  CHECK_NEAR(atlas::ssim(empty, empty), 0.0, 1e-12);
}

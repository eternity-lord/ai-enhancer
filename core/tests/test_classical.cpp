// SPDX-License-Identifier: Apache-2.0
// یادداشت: تست مسیر کلاسیک. هدف این نیست که «زیبا» باشد؛ هدف این است که
// قرارداد (ابعاد، محدودهٔ [0,1]، بدون تغییر در amount=0، حفظ میانگین در blur) قفل شود.
#include <cmath>

#include "atlas/buffer.h"
#include "atlas/image.h"
#include "test.h"

namespace {

atlas::ImageF32 make_gradient(int w, int h) {
  atlas::ImageF32 img(w, h, 3);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      float* p = img.pixel(x, y);
      p[0] = static_cast<float>(x) / (w - 1);
      p[1] = static_cast<float>(y) / (h - 1);
      p[2] = 0.5f;
    }
  }
  return img;
}

}  // namespace

ATLAS_TEST(lanczos_upscales_to_exact_size) {
  const auto src = make_gradient(9, 7);
  const auto up = atlas::resize_lanczos(src, 36, 28);
  CHECK(up.width() == 36);
  CHECK(up.height() == 28);
  CHECK(up.channels() == 3);
  CHECK_NEAR(up.pixel(0, 0)[0], 0.0f, 0.02);
  CHECK_NEAR(up.pixel(35, 27)[0], 1.0f, 0.02);
  CHECK_NEAR(up.pixel(35, 0)[1], 0.0f, 0.02);
  CHECK_NEAR(up.pixel(0, 27)[1], 1.0f, 0.02);
}

ATLAS_TEST(lanczos_is_monotonic_on_gradient) {
  const auto src = make_gradient(16, 4);
  const auto up = atlas::resize_lanczos(src, 64, 16);
  float prev = -1.0f;
  for (int x = 0; x < up.width(); ++x) {
    const float v = up.pixel(x, 8)[0];
    CHECK(v + 1e-4f >= prev);
    prev = v;
  }
}

ATLAS_TEST(unsharp_zero_amount_is_identity) {
  const auto src = make_gradient(12, 12);
  const auto out = atlas::unsharp(src, 0.0f, 1.0f);
  for (size_t i = 0; i < src.pixel_count() * 3; ++i) {
    CHECK_NEAR(out.data()[i], src.data()[i], 1e-6);
  }
}

ATLAS_TEST(gaussian_blur_keeps_mean_on_flat_area) {
  atlas::ImageF32 img(16, 16, 3);
  img.fill(0.4f);
  const auto blurred = atlas::gaussian_blur(img, 1.5f);
  for (size_t i = 0; i < blurred.pixel_count() * 3; ++i) {
    CHECK_NEAR(blurred.data()[i], 0.4f, 1e-3);
  }
}

ATLAS_TEST(clamp_limits_range) {
  atlas::ImageF32 img(2, 1, 3);
  img.data()[0] = -1.0f;
  img.data()[1] = 2.0f;
  img.data()[2] = 0.5f;
  atlas::clamp01(&img);
  CHECK(img.data()[0] == 0.0f);
  CHECK(img.data()[1] == 1.0f);
  CHECK(img.data()[2] == 0.5f);
}

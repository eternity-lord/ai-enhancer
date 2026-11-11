// SPDX-License-Identifier: Apache-2.0
// یادداشت: اعداد مرجع از تعریف sRGB: ۵۰٪ خاکستری sRGB معادل ۰٫۲۱۴۰۴۱ نور خطی
// است. اگر این عدد جابه‌جا شود، همهٔ محاسبات نویز/شارپ بی‌صدا اشتباه می‌شوند.
#include <cmath>

#include "atlas/color.h"
#include "atlas/image.h"
#include "test.h"

ATLAS_TEST(srgb_reference_values_are_exact) {
  CHECK_NEAR(atlas::srgb_to_linear(0.0f), 0.0f, 1e-7);
  CHECK_NEAR(atlas::srgb_to_linear(1.0f), 1.0f, 1e-6);
  CHECK_NEAR(atlas::srgb_to_linear(0.5f), 0.21404114f, 1e-5);
  CHECK_NEAR(atlas::srgb_to_linear(0.04045f), 0.0031308f, 1e-6);
  CHECK_NEAR(atlas::linear_to_srgb(0.21404114f), 0.5f, 1e-5);
  CHECK_NEAR(atlas::linear_to_srgb(0.0f), 0.0f, 1e-7);
  CHECK_NEAR(atlas::linear_to_srgb(1.0f), 1.0f, 1e-6);
}

ATLAS_TEST(color_roundtrip_is_stable) {
  for (int i = 0; i <= 255; ++i) {
    const float v = static_cast<float>(i) / 255.0f;
    CHECK_NEAR(atlas::linear_to_srgb(atlas::srgb_to_linear(v)), v, 2e-6);
  }
}

ATLAS_TEST(color_conversion_keeps_alpha_untouched) {
  atlas::ImageF32 img(4, 4, 4);
  img.fill(0.5f);
  for (size_t i = 0; i < img.pixel_count(); ++i) img.data()[i * 4 + 3] = 0.25f;

  const atlas::ImageF32 lin = atlas::to_linear(img);
  CHECK_NEAR(lin.data()[0], 0.21404114f, 1e-5);
  CHECK_NEAR(lin.data()[3], 0.25f, 1e-6);  // آلفا تغییر نکرد
  CHECK(lin.channels() == 4);

  const atlas::ImageF32 back = atlas::to_srgb(lin);
  CHECK_NEAR(back.data()[0], 0.5f, 1e-5);
  CHECK_NEAR(back.data()[3], 0.25f, 1e-6);
}

ATLAS_TEST(color_clamps_out_of_range_values) {
  CHECK_NEAR(atlas::srgb_to_linear(-1.0f), 0.0f, 1e-9);
  CHECK_NEAR(atlas::srgb_to_linear(2.0f), 1.0f, 1e-9);
  CHECK_NEAR(atlas::linear_to_srgb(-0.5f), 0.0f, 1e-9);
  CHECK_NEAR(atlas::linear_to_srgb(1.5f), 1.0f, 1e-6);
}

ATLAS_TEST(color_empty_image_stays_empty) {
  atlas::ImageF32 empty;
  CHECK(atlas::to_linear(empty).empty());
  CHECK(atlas::to_srgb(empty).empty());
}

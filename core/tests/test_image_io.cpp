// SPDX-License-Identifier: Apache-2.0
// یادداشت: تست کدک واقعی (stb). PNG باید تقریباً بی‌اتلاف باشد؛ JPEG با کیفیت ۹۵
// فقط «نزدیک» است. این تست‌ها از فایل‌های موقت سیستم استفاده می‌کنند و به نمونهٔ
// خارجی وابسته نیستند.
#include <filesystem>
#include <string>

#include "atlas/image.h"
#include "atlas/image_io.h"
#include "test.h"

namespace {

atlas::ImageF32 make_test_image(int w, int h) {
  atlas::ImageF32 img(w, h, 3);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      float* p = img.pixel(x, y);
      p[0] = static_cast<float>(x) / (w - 1);
      p[1] = static_cast<float>(y) / (h - 1);
      p[2] = (x + y) % 2 == 0 ? 0.25f : 0.75f;
    }
  }
  return img;
}

std::filesystem::path temp_path(const char* name) {
  return std::filesystem::temp_directory_path() / name;
}

}  // namespace

ATLAS_TEST(png_roundtrip_is_almost_lossless) {
  const auto img = make_test_image(8, 6);
  const auto path = temp_path("atlas_io_test.png");
  std::string error;
  CHECK(atlas::save_image(path.string(), img, 92, &error));

  atlas::ImageF32 loaded;
  CHECK(atlas::load_image(path.string(), &loaded, &error));
  CHECK(loaded.width() == 8);
  CHECK(loaded.height() == 6);
  if (!loaded.empty()) {
    for (size_t i = 0; i < img.pixel_count() * 3; ++i) {
      CHECK_NEAR(loaded.data()[i], img.data()[i], 1.0 / 255.0 + 1e-6);
    }
  }
  std::filesystem::remove(path);
}

ATLAS_TEST(jpeg_roundtrip_is_close) {
  const auto img = make_test_image(32, 24);
  const auto path = temp_path("atlas_io_test.jpg");
  std::string error;
  CHECK(atlas::save_image(path.string(), img, 95, &error));

  atlas::ImageF32 loaded;
  CHECK(atlas::load_image(path.string(), &loaded, &error));
  CHECK(loaded.width() == 32);
  CHECK(loaded.height() == 24);
  if (!loaded.empty()) {
    double sum = 0.0;
    for (size_t i = 0; i < img.pixel_count() * 3; ++i) {
      sum += std::fabs(static_cast<double>(loaded.data()[i]) - img.data()[i]);
    }
    const double mean_abs = sum / (img.pixel_count() * 3);
    CHECK(mean_abs < 0.05);
  }
  std::filesystem::remove(path);
}

ATLAS_TEST(unsupported_extension_is_rejected) {
  const auto img = make_test_image(4, 4);
  const auto path = temp_path("atlas_io_test.xyz");
  std::string error;
  CHECK(!atlas::save_image(path.string(), img, 92, &error));
  CHECK(!error.empty());
}

ATLAS_TEST(missing_file_returns_error) {
  atlas::ImageF32 loaded;
  std::string error;
  CHECK(!atlas::load_image(temp_path("atlas_does_not_exist.png").string(), &loaded, &error));
  CHECK(!error.empty());
}

// SPDX-License-Identifier: Apache-2.0
// یادداشت: تست round-trip کدک موقت P0. تحمل خطا ۱/۲۵۵ است چون کوانتیزه‌سازی ۸ بیتی
// برگشت‌ناپذیر است. اگر این تست شکست، یا هدر یا ترتیب بایت‌ها خراب شده.
#include <cstdio>
#include <filesystem>
#include <string>

#include "atlas/image.h"
#include "test.h"

ATLAS_TEST(ppm_roundtrip_preserves_pixels) {
  atlas::ImageF32 img(5, 3, 3);
  for (int y = 0; y < 3; ++y) {
    for (int x = 0; x < 5; ++x) {
      float* p = img.pixel(x, y);
      p[0] = static_cast<float>(x) / 4.0f;
      p[1] = static_cast<float>(y) / 2.0f;
      p[2] = 0.25f;
    }
  }

  const auto path = std::filesystem::temp_directory_path() / "atlas_ppm_test.ppm";
  std::string error;
  CHECK(atlas::write_ppm(path.string(), img, &error));

  atlas::ImageF32 loaded;
  CHECK(atlas::read_ppm(path.string(), &loaded, &error));
  CHECK(loaded.width() == 5);
  CHECK(loaded.height() == 3);
  CHECK(loaded.channels() == 3);

  if (!loaded.empty()) {
    for (size_t i = 0; i < img.pixel_count() * 3; ++i) {
      CHECK_NEAR(loaded.data()[i], img.data()[i], 1.0 / 255.0 + 1e-6);
    }
  }
  std::filesystem::remove(path);
}

ATLAS_TEST(ppm_rejects_non_ppm_file) {
  const auto path = std::filesystem::temp_directory_path() / "atlas_bad.txt";
  std::FILE* f = std::fopen(path.string().c_str(), "wb");
  CHECK(f != nullptr);
  if (f) {
    std::fputs("not a ppm", f);
    std::fclose(f);
  }
  atlas::ImageF32 loaded;
  std::string error;
  CHECK(!atlas::read_ppm(path.string(), &loaded, &error));
  CHECK(!error.empty());
  std::filesystem::remove(path);
}

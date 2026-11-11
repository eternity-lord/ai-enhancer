// SPDX-License-Identifier: Apache-2.0
// یادداشت: تست‌های SR کاشی‌ای. مهم‌ترین‌شان «تصویر ثابت باید ثابت بماند» است؛
// اگر دوخت وزن‌ها اشتباه باشد، همان‌جا خط‌های روشن/تاریک ظاهر می‌شود.
#include <algorithm>
#include <cmath>

#include "atlas/backend.h"
#include "atlas/image.h"
#include "atlas/sr.h"
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

ATLAS_TEST(sr_constant_image_stays_constant) {
  atlas::ImageF32 img(100, 80, 3);
  img.fill(0.5f);

  auto backend = atlas::make_classical_backend(2, 0.0f);
  CHECK(backend != nullptr);
  if (!backend) return;

  atlas::SrOptions opt;
  opt.tile = 32;
  opt.overlap = 8;
  const auto out = atlas::sr_tiled(img, *backend, opt);

  CHECK(out.width() == 200);
  CHECK(out.height() == 160);
  if (out.empty()) return;
  for (int y = 0; y < out.height(); y += 7) {
    for (int x = 0; x < out.width(); x += 5) {
      CHECK_NEAR(out.pixel(x, y)[0], 0.5f, 1e-5);
      CHECK_NEAR(out.pixel(x, y)[2], 0.5f, 1e-5);
    }
  }
}

ATLAS_TEST(sr_tiled_matches_single_tile_on_gradient) {
  const auto img = make_gradient(160, 120);

  auto tiled_backend = atlas::make_classical_backend(2, 0.0f);
  auto single_backend = atlas::make_classical_backend(2, 0.0f);
  CHECK(tiled_backend && single_backend);
  if (!tiled_backend || !single_backend) return;

  atlas::SrOptions tiled;
  tiled.tile = 64;
  tiled.overlap = 16;
  atlas::SrOptions single;
  single.tile = 4096;
  single.overlap = 0;

  const auto a = atlas::sr_tiled(img, *tiled_backend, tiled);
  const auto b = atlas::sr_tiled(img, *single_backend, single);
  CHECK(!a.empty() && !b.empty());
  if (a.empty() || b.empty()) return;
  CHECK(a.width() == b.width());
  CHECK(a.height() == b.height());

  double sum = 0.0;
  float worst = 0.0f;
  size_t count = 0;
  for (size_t i = 0; i < a.pixel_count(); ++i) {
    for (int c = 0; c < 3; ++c) {
      const float d = std::fabs(a.data()[i * 3 + c] - b.data()[i * 3 + c]);
      sum += d;
      worst = std::max(worst, d);
      ++count;
    }
  }
  const double mean = sum / count;
  CHECK(mean < 0.02);
  CHECK(worst < 0.15f);
}

ATLAS_TEST(sr_rejects_empty_input_and_bad_scale) {
  atlas::ImageF32 empty;
  auto backend = atlas::make_classical_backend(2, 0.0f);
  CHECK(backend != nullptr);
  if (backend) {
    const auto out = atlas::sr_tiled(empty, *backend, atlas::SrOptions{});
    CHECK(out.empty());
  }
  CHECK(atlas::make_classical_backend(0, 0.0f) == nullptr);
  CHECK(atlas::make_classical_backend(17, 0.0f) == nullptr);
}

ATLAS_TEST(sr_output_corners_are_sane) {
  const auto img = make_gradient(64, 48);
  auto backend = atlas::make_classical_backend(2, 0.0f);
  if (!backend) return;
  atlas::SrOptions opt;
  opt.tile = 32;
  opt.overlap = 8;
  const auto out = atlas::sr_tiled(img, *backend, opt);
  CHECK(!out.empty());
  if (out.empty()) return;
  CHECK_NEAR(out.pixel(0, 0)[0], 0.0f, 0.02);
  CHECK_NEAR(out.pixel(out.width() - 1, 0)[0], 1.0f, 0.02);
  CHECK_NEAR(out.pixel(0, out.height() - 1)[1], 1.0f, 0.02);
}

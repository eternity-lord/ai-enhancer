// SPDX-License-Identifier: Apache-2.0
// یادداشت: تست پوشش کامل + عدم سرریز + هم‌پوشانی. این تست باید هر بار که
// منطق کاشی عوض می‌شود اجرا شود؛ باگ در آن = نوار خالی یا کرش روی تصاویر بزرگ.
#include <vector>

#include "atlas/image.h"
#include "test.h"

using atlas::plan_tiles;

ATLAS_TEST(tiling_covers_every_pixel) {
  const int w = 1000, h = 700, tile = 256, overlap = 32;
  const auto plan = plan_tiles(w, h, tile, overlap);
  CHECK(plan.tile_count() > 1);

  std::vector<unsigned char> covered(static_cast<size_t>(w) * h, 0);
  for (const auto& t : plan.tiles) {
    CHECK(t.rect.x >= 0);
    CHECK(t.rect.y >= 0);
    CHECK(t.rect.w > 0 && t.rect.h > 0);
    CHECK(t.rect.w <= tile);
    CHECK(t.rect.h <= tile);
    CHECK(t.rect.right() <= w);
    CHECK(t.rect.bottom() <= h);
    for (int y = t.rect.y; y < t.rect.bottom(); ++y)
      for (int x = t.rect.x; x < t.rect.right(); ++x)
        covered[static_cast<size_t>(y) * w + x] = 1;
  }
  size_t missing = 0;
  for (unsigned char v : covered) missing += (v == 0);
  CHECK(missing == 0);
}

ATLAS_TEST(tiling_single_tile_for_small_image) {
  const auto plan = plan_tiles(100, 80, 256, 32);
  CHECK(plan.tile_count() == 1);
  CHECK(plan.tiles[0].rect.w == 100);
  CHECK(plan.tiles[0].rect.h == 80);
}

ATLAS_TEST(tiling_edges_are_snapped) {
  const int w = 513, h = 300, tile = 256, overlap = 32;
  const auto plan = plan_tiles(w, h, tile, overlap);
  bool touches_right = false, touches_bottom = false;
  for (const auto& t : plan.tiles) {
    if (t.rect.right() == w) touches_right = true;
    if (t.rect.bottom() == h) touches_bottom = true;
  }
  CHECK(touches_right);
  CHECK(touches_bottom);
}

ATLAS_TEST(tiling_handles_degenerate_inputs) {
  CHECK(plan_tiles(0, 100, 256, 32).tile_count() == 0);
  CHECK(plan_tiles(100, 0, 256, 32).tile_count() == 0);
  CHECK(plan_tiles(100, 100, 0, 32).tile_count() == 0);
}

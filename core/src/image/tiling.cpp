// SPDX-License-Identifier: Apache-2.0
// یادداشت: این تابع قلب کیفیت تصاویر بزرگ است. دو اشتباه رایج را عمداً حل می‌کند:
//  1) آخرین کاشی از لبه رد نشود (وگرنه crop بیرون از تصویر می‌شود)
//  2) هیچ پیکسلی بدون پوشش نماند (وگرنه نوارهای خالی در خروجی دیده می‌شود)
// در P3 نسخهٔ «آگاه به چهره» اضافه می‌شود که مرز کاشی از bbox چهره عبور نکند.
#include <algorithm>
#include <vector>

#include "atlas/image.h"

namespace atlas {
namespace {

std::vector<int> axis_positions(int size, int tile, int overlap) {
  std::vector<int> pos;
  if (size <= tile) {
    pos.push_back(0);
    return pos;
  }
  const int step = std::max(1, tile - overlap);
  for (int x = 0; x + tile < size; x += step) pos.push_back(x);
  pos.push_back(size - tile);  // چسباندن به لبه
  std::sort(pos.begin(), pos.end());
  pos.erase(std::unique(pos.begin(), pos.end()), pos.end());
  return pos;
}

}  // namespace

TilePlan plan_tiles(int width, int height, int tile, int overlap) {
  TilePlan plan;
  if (width <= 0 || height <= 0 || tile <= 0) return plan;

  tile = std::min(tile, std::max(width, height));
  overlap = std::clamp(overlap, 0, tile / 2);
  plan.tile = tile;
  plan.overlap = overlap;

  const auto xs = axis_positions(width, tile, overlap);
  const auto ys = axis_positions(height, tile, overlap);

  for (int y : ys) {
    for (int x : xs) {
      Tile t;
      t.rect.x = x;
      t.rect.y = y;
      t.rect.w = std::min(tile, width - x);
      t.rect.h = std::min(tile, height - y);
      plan.tiles.push_back(t);
    }
  }
  return plan;
}

}  // namespace atlas

// SPDX-License-Identifier: Apache-2.0
// یادداشت: موتور SR کاشی‌ای، مشترک بین بک‌اند کلاسیک و مدل. دوخت با وزن
// raised-cosine و تقسیم بر مجموع وزن‌ها؛ لبه‌های تصویر وزن کامل دارند تا
// هیچ پیکسلی با وزن صفر تقسیم نشود.
#pragma once

#include <functional>

#include "atlas/backend.h"
#include "atlas/types.h"

namespace atlas {

struct SrOptions {
  int tile = 256;
  int overlap = 32;
  bool verbose = false;
};

using SrProgress = std::function<void(int done, int total)>;

ImageF32 sr_tiled(const ImageF32& src, IBackend& backend, const SrOptions& options,
                  const SrProgress& progress = {});

}  // namespace atlas

// SPDX-License-Identifier: Apache-2.0
// یادداشت: عملیات بافر که pipeline به آن‌ها نیاز دارد (crop برای کاشی،
// padding بازتابی برای لبه‌ها، clamp برای خروجی مدل).
#pragma once

#include "atlas/types.h"

namespace atlas {

ImageF32 crop(const ImageF32& src, const Rect& rect);
ImageF32 pad_reflect(const ImageF32& src, int left, int top, int right, int bottom);
void clamp01(ImageF32* img);

}  // namespace atlas

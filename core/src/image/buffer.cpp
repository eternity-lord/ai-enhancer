// SPDX-License-Identifier: Apache-2.0
// یادداشت: crop بدون کپی اضافی نیست ولی برای P0 کافی است؛ در P1 با view/stride
// بازنویسی می‌شود تا کاشی‌ها zero-copy شوند.
#include <algorithm>

#include "atlas/buffer.h"

namespace atlas {
namespace {

int reflect_index(int i, int n) {
  if (n <= 1) return 0;
  while (i < 0 || i >= n) {
    if (i < 0) i = -i - 1;
    if (i >= n) i = 2 * n - i - 1;
  }
  return i;
}

}  // namespace

ImageF32 crop(const ImageF32& src, const Rect& rect) {
  const Rect r = rect;
  if (r.empty()) return ImageF32();
  ImageF32 out(r.w, r.h, src.channels());
  for (int y = 0; y < r.h; ++y) {
    for (int x = 0; x < r.w; ++x) {
      const float* s = src.pixel(r.x + x, r.y + y);
      float* d = out.pixel(x, y);
      for (int c = 0; c < src.channels(); ++c) d[c] = s[c];
    }
  }
  return out;
}

ImageF32 pad_reflect(const ImageF32& src, int left, int top, int right, int bottom) {
  const int w = src.width() + left + right;
  const int h = src.height() + top + bottom;
  ImageF32 out(w, h, src.channels());
  for (int y = 0; y < h; ++y) {
    const int sy = reflect_index(y - top, src.height());
    for (int x = 0; x < w; ++x) {
      const int sx = reflect_index(x - left, src.width());
      const float* s = src.pixel(sx, sy);
      float* d = out.pixel(x, y);
      for (int c = 0; c < src.channels(); ++c) d[c] = s[c];
    }
  }
  return out;
}

void clamp01(ImageF32* img) {
  if (!img) return;
  for (size_t i = 0; i < img->pixel_count() * img->channels(); ++i) {
    img->data()[i] = std::clamp(img->data()[i], 0.0f, 1.0f);
  }
}

}  // namespace atlas

// SPDX-License-Identifier: Apache-2.0
// یادداشت: این فایل جایی است که «کیفیت» و «سرعت» هم‌زمان تعیین می‌شوند.
// نکات کلیدی:
//  - لبهٔ خارجی تصویر نباید feather شود (وگرنه وزن صفر و پیکسل خالی می‌شود)
//  - کاشی‌های لبه با اندازهٔ کوچک‌تر به مدل می‌روند؛ خود بک‌اند آن‌ها را pad می‌کند
//  - جمع وزنی در float و نرمال‌سازی نهایی، دوخت را از «خط» به «طیف» تبدیل می‌کند
#include <algorithm>
#include <cmath>
#include <vector>

#include "atlas/buffer.h"
#include "atlas/image.h"
#include "atlas/sr.h"

namespace atlas {
namespace {

constexpr float kPi = 3.14159265358979323846f;

float ramp(int distance, int overlap) {
  if (overlap <= 0) return 1.0f;
  const float t = std::min(1.0f, (static_cast<float>(distance) + 0.5f) /
                                    static_cast<float>(overlap));
  return 0.5f - 0.5f * std::cos(kPi * t);
}

}  // namespace

ImageF32 sr_tiled(const ImageF32& src, IBackend& backend, const SrOptions& options,
                  const SrProgress& progress) {
  if (src.empty() || !backend.ok()) return ImageF32();

  const int scale = backend.scale();
  const int out_w = src.width() * scale;
  const int out_h = src.height() * scale;
  if (out_w <= 0 || out_h <= 0) return ImageF32();

  const TilePlan plan = plan_tiles(src.width(), src.height(), options.tile, options.overlap);
  if (plan.tile_count() == 0) return ImageF32();

  const int channels = src.channels();
  ImageF32 out(out_w, out_h, channels);
  std::vector<float> weight(static_cast<size_t>(out_w) * out_h, 0.0f);

  int done = 0;
  for (const Tile& tile : plan.tiles) {
    const Rect& r = tile.rect;
    const ImageF32 cropped = crop(src, r);
    const ImageF32 upscaled = backend.run(cropped);
    if (upscaled.empty()) return ImageF32();

    const int tile_out_w = upscaled.width();
    const int tile_out_h = upscaled.height();
    if (tile_out_w != r.w * scale || tile_out_h != r.h * scale) return ImageF32();

    for (int y = 0; y < tile_out_h; ++y) {
      const int gy = r.y * scale + y;
      if (gy < 0 || gy >= out_h) continue;
      for (int x = 0; x < tile_out_w; ++x) {
        const int gx = r.x * scale + x;
        if (gx < 0 || gx >= out_w) continue;

        float w = 1.0f;
        if (r.x > 0) w *= ramp(x, options.overlap * scale);
        if (r.right() < src.width()) w *= ramp(tile_out_w - 1 - x, options.overlap * scale);
        if (r.y > 0) w *= ramp(y, options.overlap * scale);
        if (r.bottom() < src.height()) w *= ramp(tile_out_h - 1 - y, options.overlap * scale);
        w = std::max(w, 1e-4f);

        const float* s = upscaled.pixel(x, y);
        float* d = out.pixel(gx, gy);
        for (int c = 0; c < channels; ++c) d[c] += s[c] * w;
        weight[static_cast<size_t>(gy) * out_w + gx] += w;
      }
    }

    ++done;
    if (progress) progress(done, static_cast<int>(plan.tile_count()));
  }

  for (int y = 0; y < out_h; ++y) {
    for (int x = 0; x < out_w; ++x) {
      const float w = weight[static_cast<size_t>(y) * out_w + x];
      float* d = out.pixel(x, y);
      if (w > 0.0f) {
        for (int c = 0; c < channels; ++c) d[c] = std::clamp(d[c] / w, 0.0f, 1.0f);
      }
    }
  }
  return out;
}

}  // namespace atlas

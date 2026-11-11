// SPDX-License-Identifier: Apache-2.0
// یادداشت: انواع پایه بدون وابستگی خارجی. تصویر همیشه float32 خطی است
// (تبدیل sRGB↔linear در image/color.cpp در P0-07 اضافه می‌شود).
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace atlas {

enum class ErrorCode : int {
  kOk = 0,
  kInvalidArgument = 1,
  kIo = 2,
  kUnsupported = 3,
  kOutOfMemory = 4,
  kInternal = 5,
};

struct Rect {
  int x = 0, y = 0, w = 0, h = 0;

  bool empty() const { return w <= 0 || h <= 0; }
  int right() const { return x + w; }
  int bottom() const { return y + h; }
  bool contains(int px, int py) const {
    return px >= x && py >= y && px < right() && py < bottom();
  }
};

// Planar interleaved image: data[(y * w + x) * c + ch], values nominally in [0,1].
class ImageF32 {
 public:
  ImageF32() = default;
  ImageF32(int width, int height, int channels)
      : width_(width), height_(height), channels_(channels),
        data_(static_cast<size_t>(width) * height * channels, 0.0f) {}

  int width() const { return width_; }
  int height() const { return height_; }
  int channels() const { return channels_; }
  bool empty() const { return data_.empty(); }
  size_t pixel_count() const {
    return static_cast<size_t>(width_) * static_cast<size_t>(height_);
  }

  float* data() { return data_.data(); }
  const float* data() const { return data_.data(); }

  float* pixel(int x, int y) {
    return data_.data() + (static_cast<size_t>(y) * width_ + x) * channels_;
  }
  const float* pixel(int x, int y) const {
    return data_.data() + (static_cast<size_t>(y) * width_ + x) * channels_;
  }

  float at(int x, int y, int ch) const { return pixel(x, y)[ch]; }

  void set(int x, int y, int ch, float v) { pixel(x, y)[ch] = v; }

  void fill(float v) {
    for (float& f : data_) f = v;
  }

 private:
  int width_ = 0, height_ = 0, channels_ = 0;
  std::vector<float> data_;
};

struct Tile {
  Rect rect;
  // Weight window for feathering; 1.0 everywhere until blend.cpp (P0-09).
};

struct TilePlan {
  int tile = 0;
  int overlap = 0;
  std::vector<Tile> tiles;
  size_t tile_count() const { return tiles.size(); }
};

}  // namespace atlas

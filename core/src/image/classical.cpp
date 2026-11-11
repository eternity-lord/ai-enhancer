// SPDX-License-Identifier: Apache-2.0
// یادداشت: مسیر «همیشه در دسترس». حتی وقتی هیچ GPU و هیچ مدلی نیست، کاربر باید
// خروجی بهتر از bicubic بگیرد. Lanczos + unsharp پایهٔ همان مسیر است؛ در P2
// با deconvolution و guided filter تقویت می‌شود.
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "atlas/image.h"

namespace atlas {
namespace {

constexpr float kPi = 3.14159265358979323846f;

float sinc(float x) {
  if (std::fabs(x) < 1e-6f) return 1.0f;
  const float px = kPi * x;
  return std::sin(px) / px;
}

float lanczos_kernel(float x, float a) {
  if (x <= -a || x >= a) return 0.0f;
  return sinc(x) * sinc(x / a);
}

using WeightList = std::vector<std::pair<int, float>>;

std::vector<WeightList> build_weights(int in_size, int out_size, float a) {
  std::vector<WeightList> table(static_cast<size_t>(out_size));
  const float scale = static_cast<float>(in_size) / static_cast<float>(out_size);
  const float support = a * std::max(1.0f, scale);

  for (int i = 0; i < out_size; ++i) {
    const float center = (static_cast<float>(i) + 0.5f) * scale - 0.5f;
    const int left = static_cast<int>(std::ceil(center - support));
    const int right = static_cast<int>(std::floor(center + support));

    WeightList w;
    float sum = 0.0f;
    for (int j = left; j <= right; ++j) {
      const float dist = (static_cast<float>(j) - center) / std::max(1.0f, scale);
      const float weight = lanczos_kernel(dist, a);
      if (weight == 0.0f) continue;
      const int clamped = std::clamp(j, 0, in_size - 1);
      w.emplace_back(clamped, weight);
      sum += weight;
    }
    if (sum != 0.0f) {
      for (auto& p : w) p.second /= sum;
    } else {
      const int nearest = std::clamp(static_cast<int>(std::lround(center)), 0, in_size - 1);
      w.assign(1, {nearest, 1.0f});
    }
    table[static_cast<size_t>(i)] = std::move(w);
  }
  return table;
}

ImageF32 resample_separable(const ImageF32& src, int out_w, int out_h, float a) {
  const int c = src.channels();
  ImageF32 tmp(out_w, src.height(), c);
  const auto wx = build_weights(src.width(), out_w, a);

  for (int y = 0; y < src.height(); ++y) {
    for (int x = 0; x < out_w; ++x) {
      float acc[4] = {0, 0, 0, 0};
      for (const auto& [sx, weight] : wx[static_cast<size_t>(x)]) {
        const float* s = src.pixel(sx, y);
        for (int ch = 0; ch < c; ++ch) acc[ch] += s[ch] * weight;
      }
      float* d = tmp.pixel(x, y);
      for (int ch = 0; ch < c; ++ch) d[ch] = acc[ch];
    }
  }

  ImageF32 out(out_w, out_h, c);
  const auto wy = build_weights(src.height(), out_h, a);
  for (int y = 0; y < out_h; ++y) {
    for (int x = 0; x < out_w; ++x) {
      float acc[4] = {0, 0, 0, 0};
      for (const auto& [sy, weight] : wy[static_cast<size_t>(y)]) {
        const float* s = tmp.pixel(x, sy);
        for (int ch = 0; ch < c; ++ch) acc[ch] += s[ch] * weight;
      }
      float* d = out.pixel(x, y);
      for (int ch = 0; ch < c; ++ch) d[ch] = acc[ch];
    }
  }
  return out;
}

std::vector<float> gaussian_kernel(float sigma, int* radius_out) {
  const int radius = std::max(1, static_cast<int>(std::ceil(3.0f * sigma)));
  std::vector<float> k(static_cast<size_t>(2 * radius + 1));
  float sum = 0.0f;
  for (int i = -radius; i <= radius; ++i) {
    const float v = std::exp(-(static_cast<float>(i) * i) / (2.0f * sigma * sigma));
    k[static_cast<size_t>(i + radius)] = v;
    sum += v;
  }
  for (float& v : k) v /= sum;
  *radius_out = radius;
  return k;
}

}  // namespace

ImageF32 resize_lanczos(const ImageF32& src, int out_width, int out_height, float a) {
  if (src.empty() || out_width <= 0 || out_height <= 0) return ImageF32();
  if (src.width() == out_width && src.height() == out_height) return src;
  return resample_separable(src, out_width, out_height, std::max(1.0f, a));
}

ImageF32 gaussian_blur(const ImageF32& src, float sigma) {
  if (src.empty()) return ImageF32();
  if (sigma <= 0.0f) return src;

  int radius = 0;
  const auto kernel = gaussian_kernel(sigma, &radius);
  const int c = src.channels();

  ImageF32 tmp(src.width(), src.height(), c);
  for (int y = 0; y < src.height(); ++y) {
    for (int x = 0; x < src.width(); ++x) {
      float acc[4] = {0, 0, 0, 0};
      for (int k = -radius; k <= radius; ++k) {
        const int sx = std::clamp(x + k, 0, src.width() - 1);
        const float w = kernel[static_cast<size_t>(k + radius)];
        const float* s = src.pixel(sx, y);
        for (int ch = 0; ch < c; ++ch) acc[ch] += s[ch] * w;
      }
      float* d = tmp.pixel(x, y);
      for (int ch = 0; ch < c; ++ch) d[ch] = acc[ch];
    }
  }

  ImageF32 out(src.width(), src.height(), c);
  for (int y = 0; y < src.height(); ++y) {
    for (int x = 0; x < src.width(); ++x) {
      float acc[4] = {0, 0, 0, 0};
      for (int k = -radius; k <= radius; ++k) {
        const int sy = std::clamp(y + k, 0, src.height() - 1);
        const float w = kernel[static_cast<size_t>(k + radius)];
        const float* s = tmp.pixel(x, sy);
        for (int ch = 0; ch < c; ++ch) acc[ch] += s[ch] * w;
      }
      float* d = out.pixel(x, y);
      for (int ch = 0; ch < c; ++ch) d[ch] = std::clamp(acc[ch], 0.0f, 1.0f);
    }
  }
  return out;
}

ImageF32 unsharp(const ImageF32& src, float amount, float sigma) {
  if (src.empty() || amount <= 0.0f) return src;
  const ImageF32 blurred = gaussian_blur(src, sigma);
  ImageF32 out(src.width(), src.height(), src.channels());
  const size_t n = src.pixel_count() * src.channels();
  for (size_t i = 0; i < n; ++i) {
    const float v = src.data()[i] + amount * (src.data()[i] - blurred.data()[i]);
    out.data()[i] = std::clamp(v, 0.0f, 1.0f);
  }
  return out;
}

}  // namespace atlas

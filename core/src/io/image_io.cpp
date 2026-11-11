// SPDX-License-Identifier: Apache-2.0
// یادداشت: انتخاب کدک بر اساس پسوند است، نه محتوا (تشخیص magic bytes در P1).
// ۱۶ بیت ورودی پشتیبانی می‌شود؛ خروجی فعلاً ۸ بیت (محدودیت stb).
#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

#include "atlas/image.h"
#include "atlas/image_io.h"
#include "stb_image.h"
#include "stb_image_write.h"

namespace atlas {
namespace {

std::string lower_extension(const std::string& path) {
  const size_t dot = path.find_last_of('.');
  if (dot == std::string::npos) return {};
  std::string ext = path.substr(dot);
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext;
}

bool append_error(std::string* error, const char* message) {
  if (error) *error = message;
  return false;
}

}  // namespace

bool load_image(const std::string& path, ImageF32* out, std::string* error) {
  if (!out) return append_error(error, "null output image");

  const std::string ext = lower_extension(path);
  if (ext == ".ppm" || ext == ".pgm") {
    return read_ppm(path, out, error);
  }

  int width = 0, height = 0, channels = 0;
  if (stbi_is_16_bit(path.c_str())) {
    stbi_us* pixels = stbi_load_16(path.c_str(), &width, &height, &channels, 3);
    if (!pixels) return append_error(error, stbi_failure_reason());
    ImageF32 img(width, height, 3);
    const float inv = 1.0f / 65535.0f;
    for (size_t i = 0; i < img.pixel_count() * 3; ++i) {
      img.data()[i] = static_cast<float>(pixels[i]) * inv;
    }
    stbi_image_free(pixels);
    *out = std::move(img);
    return true;
  }

  stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, 3);
  if (!pixels) return append_error(error, stbi_failure_reason());
  ImageF32 img(width, height, 3);
  const float inv = 1.0f / 255.0f;
  for (size_t i = 0; i < img.pixel_count() * 3; ++i) {
    img.data()[i] = static_cast<float>(pixels[i]) * inv;
  }
  stbi_image_free(pixels);
  *out = std::move(img);
  return true;
}

bool save_image(const std::string& path, const ImageF32& img, int jpeg_quality,
                std::string* error) {
  if (img.empty()) return append_error(error, "empty image");
  if (img.channels() != 3) return append_error(error, "only RGB output is supported");

  const std::string ext = lower_extension(path);
  if (ext == ".ppm" || ext == ".pgm") {
    return write_ppm(path, img, error);
  }

  std::vector<unsigned char> bytes(img.pixel_count() * 3);
  for (size_t i = 0; i < bytes.size(); ++i) {
    const float v = std::clamp(img.data()[i], 0.0f, 1.0f);
    bytes[i] = static_cast<unsigned char>(v * 255.0f + 0.5f);
  }

  const int w = img.width();
  const int h = img.height();
  const int stride = w * 3;
  int ok = 0;
  if (ext == ".png") {
    ok = stbi_write_png(path.c_str(), w, h, 3, bytes.data(), stride);
  } else if (ext == ".jpg" || ext == ".jpeg") {
    ok = stbi_write_jpg(path.c_str(), w, h, 3, bytes.data(),
                        std::clamp(jpeg_quality, 1, 100));
  } else if (ext == ".bmp") {
    ok = stbi_write_bmp(path.c_str(), w, h, 3, bytes.data());
  } else if (ext == ".tga") {
    ok = stbi_write_tga(path.c_str(), w, h, 3, bytes.data());
  } else {
    return append_error(error, "unsupported output extension (use .png/.jpg/.bmp/.tga/.ppm)");
  }
  if (!ok) return append_error(error, "encoder failed");
  return true;
}

}  // namespace atlas

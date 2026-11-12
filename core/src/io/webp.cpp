// SPDX-License-Identifier: Apache-2.0
// یادداشت: WebP عمداً «وابستگی اختیاری» است. دلیل: در ویندوز/موبایل معمولاً
// libwebp موجود نیست و ما نمی‌خواهیم build پیش‌فرض بشکند؛ ولی در وب و اندروید
// WebP پرکاربردترین فرمت است، پس وقتی کتابخانه هست، باید کار کند.
// قرارداد: پشتیبانی alpha (RGBA) و کیفیت ۰..۱۰۰؛ lossless فعلاً از CLI نمی‌آید.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "atlas/image.h"
#include "atlas/image_io.h"

#ifdef ATLAS_WITH_WEBP
#include <webp/decode.h>
#include <webp/encode.h>
#endif

namespace atlas {
namespace {

#ifdef ATLAS_WITH_WEBP
bool read_file(const std::string& path, std::vector<uint8_t>* out, std::string* error) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) {
    if (error) *error = "cannot open " + path;
    return false;
  }
  std::fseek(f, 0, SEEK_END);
  const long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (size <= 0) {
    std::fclose(f);
    if (error) *error = "empty file " + path;
    return false;
  }
  out->resize(static_cast<size_t>(size));
  const size_t got = std::fread(out->data(), 1, out->size(), f);
  std::fclose(f);
  if (got != out->size()) {
    if (error) *error = "short read " + path;
    return false;
  }
  return true;
}

#endif
}  // namespace

bool webp_compiled() {
#ifdef ATLAS_WITH_WEBP
  return true;
#else
  return false;
#endif
}

bool load_webp(const std::string& path, ImageF32* out, std::string* error) {
#ifdef ATLAS_WITH_WEBP
  std::vector<uint8_t> bytes;
  if (!read_file(path, &bytes, error)) return false;

  WebPBitstreamFeatures features;
  if (WebPGetFeatures(bytes.data(), bytes.size(), &features) != VP8_STATUS_OK) {
    if (error) *error = "not a valid WebP file";
    return false;
  }

  int w = 0;
  int h = 0;
  uint8_t* rgba = WebPDecodeRGBA(bytes.data(), bytes.size(), &w, &h);
  if (!rgba) {
    if (error) *error = "WebP decode failed";
    return false;
  }

  const int comp = features.has_alpha ? 4 : 3;
  ImageF32 img(w, h, comp);
  const float inv = 1.0f / 255.0f;
  for (size_t i = 0; i < img.pixel_count(); ++i) {
    for (int c = 0; c < comp; ++c) img.data()[i * comp + c] = rgba[i * 4 + c] * inv;
  }
  WebPFree(rgba);
  *out = std::move(img);
  return true;
#else
  (void)path;
  (void)out;
  if (error) *error = "this build has no WebP support (configure with libwebp available)";
  return false;
#endif
}

bool save_webp(const std::string& path, const ImageF32& img, int quality, std::string* error) {
#ifdef ATLAS_WITH_WEBP
  if (img.empty() || (img.channels() != 3 && img.channels() != 4)) {
    if (error) *error = "webp encoder needs RGB or RGBA";
    return false;
  }
  const int comp = img.channels();
  std::vector<uint8_t> bytes(img.pixel_count() * static_cast<size_t>(comp));
  for (size_t i = 0; i < img.pixel_count(); ++i) {
    for (int c = 0; c < comp; ++c) {
      const float v = std::min(1.0f, std::max(0.0f, img.data()[i * comp + c]));
      bytes[i * comp + c] = static_cast<uint8_t>(v * 255.0f + 0.5f);
    }
  }
  const int q = std::min(100, std::max(0, quality));
  uint8_t* encoded = nullptr;
  size_t size = comp == 4 ? WebPEncodeRGBA(bytes.data(), img.width(), img.height(),
                                           img.width() * comp, static_cast<float>(q), &encoded)
                          : WebPEncodeRGB(bytes.data(), img.width(), img.height(),
                                          img.width() * comp, static_cast<float>(q), &encoded);
  if (!encoded || size == 0) {
    if (encoded) WebPFree(encoded);
    if (error) *error = "WebP encode failed";
    return false;
  }
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) {
    WebPFree(encoded);
    if (error) *error = "cannot write " + path;
    return false;
  }
  const size_t wrote = std::fwrite(encoded, 1, size, f);
  std::fclose(f);
  WebPFree(encoded);
  if (wrote != size) {
    if (error) *error = "short write " + path;
    return false;
  }
  return true;
#else
  (void)path;
  (void)img;
  (void)quality;
  if (error) *error = "this build has no WebP support (configure with libwebp available)";
  return false;
#endif
}

}  // namespace atlas

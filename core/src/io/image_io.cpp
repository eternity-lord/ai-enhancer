// SPDX-License-Identifier: Apache-2.0
// یادداشت: انتخاب کدک بر اساس پسوند است، نه محتوا (تشخیص magic bytes در P1).
// ۱۶ بیت ورودی پشتیبانی می‌شود؛ خروجی فعلاً ۸ بیت (محدودیت stb).
#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

#include "atlas/exif.h"
#include "atlas/icc.h"
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
  if (ext == ".webp") return load_webp(path, out, error);

  // آلفا حفظ می‌شود: PNG/TGA با شفافیت نباید بی‌سروصدا RGB شوند، وگرنه
  // خروجی «پس‌زمینهٔ سیاه» می‌گیرد و کاربر فکر می‌کند فایل خراب شده است.
  int width = 0, height = 0, channels = 0;
  if (!stbi_info(path.c_str(), &width, &height, &channels)) {
    return append_error(error, stbi_failure_reason());
  }
  const int comp = (channels == 2 || channels == 4) ? 4 : 3;

  if (stbi_is_16_bit(path.c_str())) {
    stbi_us* pixels = stbi_load_16(path.c_str(), &width, &height, &channels, comp);
    if (!pixels) return append_error(error, stbi_failure_reason());
    ImageF32 img(width, height, comp);
    const float inv = 1.0f / 65535.0f;
    for (size_t i = 0; i < img.pixel_count() * static_cast<size_t>(comp); ++i) {
      img.data()[i] = static_cast<float>(pixels[i]) * inv;
    }
    stbi_image_free(pixels);
    *out = std::move(img);
    return true;
  }

  stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, comp);
  if (!pixels) return append_error(error, stbi_failure_reason());
  ImageF32 img(width, height, comp);
  const float inv = 1.0f / 255.0f;
  for (size_t i = 0; i < img.pixel_count() * static_cast<size_t>(comp); ++i) {
    img.data()[i] = static_cast<float>(pixels[i]) * inv;
  }
  stbi_image_free(pixels);
  *out = std::move(img);
  return true;
}

bool save_image(const std::string& path, const ImageF32& img, int jpeg_quality,
                std::string* error) {
  if (img.empty()) return append_error(error, "empty image");
  if (img.channels() != 3 && img.channels() != 4) {
    return append_error(error, "only RGB/RGBA output is supported");
  }

  const std::string ext = lower_extension(path);
  const bool has_alpha = img.channels() == 4;

  if (ext == ".webp") return save_webp(path, img, jpeg_quality, error);

  if (ext == ".ppm" || ext == ".pgm") {
    if (!has_alpha) return write_ppm(path, img, error);
    ImageF32 rgb(img.width(), img.height(), 3);
    for (size_t i = 0; i < img.pixel_count(); ++i) {
      const float* s = img.data() + i * 4;
      float* d = rgb.data() + i * 3;
      d[0] = s[0];
      d[1] = s[1];
      d[2] = s[2];
    }
    return write_ppm(path, rgb, error);
  }

  // JPEG/BMP آلفا ندارند؛ برای جلوگیری از «پس‌زمینهٔ سبز/سیاه» آلفا کنار گذاشته
  // می‌شود و در meta/CLI هم گزارش می‌دهیم.
  const bool can_alpha = ext == ".png" || ext == ".tga";
  const int comp = (has_alpha && can_alpha) ? 4 : 3;

  std::vector<unsigned char> bytes(img.pixel_count() * static_cast<size_t>(comp));
  const int src_channels = img.channels();
  for (size_t i = 0; i < img.pixel_count(); ++i) {
    for (int c = 0; c < comp; ++c) {
      const float v = std::clamp(img.data()[i * src_channels + c], 0.0f, 1.0f);
      bytes[i * comp + c] = static_cast<unsigned char>(v * 255.0f + 0.5f);
    }
  }

  const int w = img.width();
  const int h = img.height();
  const int stride = w * comp;
  int ok = 0;
  if (ext == ".png") {
    ok = stbi_write_png(path.c_str(), w, h, comp, bytes.data(), stride);
  } else if (ext == ".jpg" || ext == ".jpeg") {
    ok = stbi_write_jpg(path.c_str(), w, h, 3, bytes.data(),
                        std::clamp(jpeg_quality, 1, 100));
  } else if (ext == ".bmp") {
    ok = stbi_write_bmp(path.c_str(), w, h, 3, bytes.data());
  } else if (ext == ".tga") {
    ok = stbi_write_tga(path.c_str(), w, h, comp, bytes.data());
  } else {
    return append_error(error,
                        "unsupported output extension (use .png/.jpg/.webp/.bmp/.tga/.ppm)");
  }
  if (!ok) return append_error(error, "encoder failed");
  return true;
}

bool load_image_ex(const std::string& path, ImageF32* out, ImageMeta* meta, std::string* error) {
  if (!out) return append_error(error, "null output image");
  if (!load_image(path, out, error)) return false;

  ImageMeta info;
  info.width = out->width();
  info.height = out->height();
  info.channels = out->channels();
  info.orientation = read_exif_orientation(path);

  const std::string bytes = read_icc_profile(path);
  if (!bytes.empty()) {
    info.has_icc_profile = true;
    const IccProfile profile =
        parse_icc(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
    if (profile.valid) {
      *out = apply_icc_to_srgb(*out, profile);
      info.icc_applied = true;
      info.icc_description = icc_summary(profile);
    } else {
      // پروفایل LUT/ناشناخته: تبدیل نمی‌کنیم و صادقانه گزارش می‌دهیم.
      info.icc_description = "unsupported (assumed sRGB)";
    }
  } else {
    info.icc_description = "none";
  }

  if (meta) *meta = info;
  return true;
}

}  // namespace atlas

// SPDX-License-Identifier: Apache-2.0
// یادداشت: این تست‌ها «وفاداری فایل» را چک می‌کنند: پروفایل sRGB باید قابل
// بازخوانی باشد، EXIF باید جابه‌جا شود، و WebP باید رفت‌وبرگشت داشته باشد.
// اگر build بدون libwebp باشد، تست WebP خودش را skip می‌کند (نه اینکه بشکند).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "atlas/exif.h"
#include "atlas/icc.h"
#include "atlas/image.h"
#include "atlas/image_io.h"
#include "atlas/metadata.h"
#include "test.h"

namespace {

std::filesystem::path temp(const char* name) {
  return std::filesystem::temp_directory_path() / name;
}

// JPEG با یک APP1 ساده‌ی orientation=6 (برای تست انتقال EXIF).
std::vector<uint8_t> with_exif_orientation(const std::string& path, uint16_t orientation) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return {};
  std::fseek(f, 0, SEEK_END);
  const long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  std::vector<uint8_t> jpg(static_cast<size_t>(size));
  const size_t got = std::fread(jpg.data(), 1, jpg.size(), f);
  std::fclose(f);
  jpg.resize(got);

  const std::vector<uint8_t> tiff = {
      'I', 'I', 42, 0, 8, 0, 0, 0, 1, 0, 0x12, 0x01, 3, 0, 1, 0, 0, 0,
      static_cast<uint8_t>(orientation & 0xFF), static_cast<uint8_t>(orientation >> 8),
      0, 0, 0, 0, 0, 0};
  std::vector<uint8_t> seg = {0xFF, 0xE1};
  const uint16_t len = static_cast<uint16_t>(tiff.size() + 8);
  seg.push_back(static_cast<uint8_t>(len >> 8));
  seg.push_back(static_cast<uint8_t>(len & 0xFF));
  const char exif[] = "Exif\0\0";
  seg.insert(seg.end(), exif, exif + 6);
  seg.insert(seg.end(), tiff.begin(), tiff.end());

  std::vector<uint8_t> out;
  out.reserve(jpg.size() + seg.size());
  out.push_back(0xFF);
  out.push_back(0xD8);
  out.insert(out.end(), seg.begin(), seg.end());
  out.insert(out.end(), jpg.begin() + 2, jpg.end());
  return out;
}

void write_bytes(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
  std::FILE* f = std::fopen(path.string().c_str(), "wb");
  if (f) {
    std::fwrite(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
  }
}

atlas::ImageF32 rgba(int w, int h) {
  atlas::ImageF32 img(w, h, 4);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      float* p = img.pixel(x, y);
      p[0] = static_cast<float>(x) / (w - 1);
      p[1] = static_cast<float>(y) / (h - 1);
      p[2] = 0.3f;
      p[3] = 1.0f;
    }
  }
  return img;
}

}  // namespace

ATLAS_TEST(metadata_srgb_profile_is_valid_icc) {
  const std::string bytes = atlas::make_srgb_icc_profile();
  CHECK(bytes.size() > 200);
  const auto p = atlas::parse_icc(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
  CHECK(p.valid);
  CHECK(p.color_space == "RGB ");
  CHECK(p.description.find("sRGB") != std::string::npos);

  // پروفایل خودش باید تبدیل «sRGB → sRGB» را تقریباً بی‌اثر انجام دهد.
  atlas::ImageF32 img(2, 2, 3);
  img.fill(0.5f);
  const auto out = atlas::apply_icc_to_srgb(img, p);
  CHECK_NEAR(out.pixel(0, 0)[0], 0.5f, 2e-3);
}

ATLAS_TEST(metadata_exif_moves_from_jpeg_to_jpeg_and_png) {
  atlas::ImageF32 img(32, 24, 3);
  img.fill(0.6f);
  std::string error;

  const auto src = temp("atlas_meta_src.jpg");
  const auto dst_jpg = temp("atlas_meta_dst.jpg");
  const auto dst_png = temp("atlas_meta_dst.png");
  CHECK(atlas::save_image(src.string(), img, 95, &error));

  const auto with_exif = with_exif_orientation(src.string(), 6);
  write_bytes(src, with_exif);
  CHECK(atlas::read_exif_orientation(src.string()) == 6);
  CHECK(atlas::has_exif_metadata(src.string()));

  CHECK(atlas::save_image(dst_jpg.string(), img, 95, &error));
  CHECK(atlas::keep_exif_metadata(src.string(), dst_jpg.string(), &error));
  CHECK(atlas::read_exif_orientation(dst_jpg.string()) == 6);

  CHECK(atlas::save_image(dst_png.string(), img, 92, &error));
  CHECK(atlas::keep_exif_metadata(src.string(), dst_png.string(), &error));
  CHECK(atlas::has_exif_metadata(dst_png.string()));
  // eXIf در PNG توسط stb خوانده نمی‌شود، ولی خود پروفایل باید سالم باشد:
  CHECK(atlas::read_exif_orientation(dst_png.string()) == 0);

  // مبدأ بدون EXIF → false با پیام روشن، نه crash.
  const auto clean = temp("atlas_meta_clean.jpg");
  CHECK(atlas::save_image(clean.string(), img, 95, &error));
  CHECK(!atlas::has_exif_metadata(clean.string()));
  std::string meta_error;
  CHECK(!atlas::keep_exif_metadata(clean.string(), dst_jpg.string(), &meta_error));
  CHECK(!meta_error.empty());

  for (const auto& p : {src, dst_jpg, dst_png, clean}) std::filesystem::remove(p);
}

ATLAS_TEST(metadata_tags_srgb_profile_in_png_and_jpeg) {
  const auto img = rgba(16, 12);
  std::string error;

  const auto png = temp("atlas_srgb_tag.png");
  CHECK(atlas::save_image(png.string(), img, 92, &error));
  CHECK(atlas::tag_srgb_profile(png.string(), &error));
  const std::string png_icc = atlas::read_png_icc_profile(png.string());
  CHECK(!png_icc.empty());
  const auto p1 = atlas::parse_icc(reinterpret_cast<const uint8_t*>(png_icc.data()),
                                   png_icc.size());
  CHECK(p1.valid);
  // کل تصویر باید بعد از برچسب‌گذاری هنوز خوانده شود.
  atlas::ImageF32 back;
  CHECK(atlas::load_image(png.string(), &back, &error));
  CHECK(back.width() == 16);
  CHECK(back.channels() == 4);
  std::filesystem::remove(png);

  const auto jpg = temp("atlas_srgb_tag.jpg");
  CHECK(atlas::save_image(jpg.string(), img, 95, &error));
  CHECK(atlas::tag_srgb_profile(jpg.string(), &error));
  const std::string jpg_icc = atlas::read_jpeg_icc_profile(jpg.string());
  CHECK(!jpg_icc.empty());
  const auto p2 = atlas::parse_icc(reinterpret_cast<const uint8_t*>(jpg_icc.data()),
                                   jpg_icc.size());
  CHECK(p2.valid);
  atlas::ImageF32 back_jpg;
  CHECK(atlas::load_image(jpg.string(), &back_jpg, &error));
  CHECK(back_jpg.width() == 16);
  std::filesystem::remove(jpg);
}

ATLAS_TEST(webp_roundtrip_when_available) {
  const auto img = rgba(40, 30);
  const auto path = temp("atlas_webp.webp");
  std::string error;

  if (!atlas::webp_compiled()) {
    // build بدون libwebp: باید پیام روشن بدهد، نه crash.
    CHECK(!atlas::save_webp(path.string(), img, 90, &error));
    CHECK(!error.empty());
    return;
  }

  CHECK(atlas::save_webp(path.string(), img, 90, &error));
  atlas::ImageF32 back;
  CHECK(atlas::load_webp(path.string(), &back, &error));
  CHECK(back.width() == 40);
  CHECK(back.height() == 30);
  // آلفای کاملاً مات، توسط encoder حذف می‌شود (رفتار درست WebP): ۳ کانال.
  CHECK(back.channels() == 3);
  if (!back.empty()) {
    double sum = 0.0;
    size_t n = 0;
    for (size_t i = 0; i < img.pixel_count(); ++i) {
      for (int c = 0; c < 3; ++c) {
        sum += std::fabs(back.data()[i * back.channels() + c] - img.data()[i * img.channels() + c]);
        ++n;
      }
    }
    const double mean = sum / static_cast<double>(n);
    CHECK(mean < 0.05);
  }
  std::filesystem::remove(path);

  // آلفای غیرمات باید حفظ شود (کانال چهارم).
  atlas::ImageF32 alpha_img(24, 16, 4);
  for (int y = 0; y < 16; ++y) {
    for (int x = 0; x < 24; ++x) {
      float* p = alpha_img.pixel(x, y);
      p[0] = 0.2f;
      p[1] = 0.6f;
      p[2] = 0.9f;
      p[3] = static_cast<float>(x) / 23.0f;
    }
  }
  const auto alpha_path = temp("atlas_webp_alpha.webp");
  CHECK(atlas::save_webp(alpha_path.string(), alpha_img, 95, &error));
  atlas::ImageF32 alpha_back;
  CHECK(atlas::load_webp(alpha_path.string(), &alpha_back, &error));
  CHECK(alpha_back.channels() == 4);
  if (alpha_back.channels() == 4) {
    CHECK_NEAR(alpha_back.pixel(0, 0)[3], 0.0f, 2.0 / 255.0);
    CHECK_NEAR(alpha_back.pixel(23, 0)[3], 1.0f, 2.0 / 255.0);
  }
  std::filesystem::remove(alpha_path);

  // مسیر یکپارچه: save_image/load_image با پسوند .webp
  const auto via_io = temp("atlas_webp_io.webp");
  CHECK(atlas::save_image(via_io.string(), img, 90, &error));
  atlas::ImageF32 back2;
  CHECK(atlas::load_image(via_io.string(), &back2, &error));
  CHECK(back2.width() == 40);
  std::filesystem::remove(via_io);
}

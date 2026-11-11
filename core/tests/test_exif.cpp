// SPDX-License-Identifier: Apache-2.0
// یادداشت: عکس موبایل بدون EXIF یعنی چهرهٔ ۹۰ درجه چرخیده. تست‌ها هم خود
// تبدیل هندسی را چک می‌کنند و هم پارسر را با یک فایل ساختگی (بدون وابستگی به
// عکس واقعی) تا اگر روزی ساختار APP1 عوض شد، همان‌جا بشکند.
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "atlas/exif.h"
#include "atlas/image.h"
#include "atlas/image_io.h"
#include "test.h"

namespace {

// تصویر ۳×۲ که شناسهٔ هر پیکسل در کانال قرمز است: id = x + y*3
atlas::ImageF32 id_image() {
  atlas::ImageF32 img(3, 2, 3);
  for (int y = 0; y < 2; ++y) {
    for (int x = 0; x < 3; ++x) {
      float* p = img.pixel(x, y);
      p[0] = static_cast<float>(x + y * 3) / 10.0f;
      p[1] = p[2] = 0.0f;
    }
  }
  return img;
}

int id_at(const atlas::ImageF32& img, int x, int y) {
  return static_cast<int>(img.pixel(x, y)[0] * 10.0f + 0.5f);
}

std::vector<uint8_t> minimal_jpeg_with_orientation(uint16_t orientation) {
  std::vector<uint8_t> tiff = {
      'I', 'I', 42, 0,          // little-endian, magic
      8,   0,   0,   0,         // IFD0 offset = 8
      1,   0,                   // تعداد ورودی‌ها
      0x12, 0x01,               // tag 0x0112 (Orientation)
      3,    0,                  // type SHORT
      1,    0,   0,   0,        // count = 1
      static_cast<uint8_t>(orientation & 0xFF),
      static_cast<uint8_t>(orientation >> 8), 0, 0,
      0,    0,   0,   0,        // IFD بعدی: هیچ
  };
  std::vector<uint8_t> payload = {'E', 'x', 'i', 'f', 0, 0};
  payload.insert(payload.end(), tiff.begin(), tiff.end());

  const uint16_t seg_len = static_cast<uint16_t>(payload.size() + 2);
  std::vector<uint8_t> out;
  out.reserve(2 + 4 + payload.size() + 2);               // جلوگیری از هشدار کاذب GCC
  out = {0xFF, 0xD8};                                    // SOI
  out.push_back(0xFF);
  out.push_back(0xE1);                                   // APP1
  out.push_back(static_cast<uint8_t>(seg_len >> 8));     // طول big-endian
  out.push_back(static_cast<uint8_t>(seg_len & 0xFF));
  out.insert(out.end(), payload.begin(), payload.end());
  out.push_back(0xFF);
  out.push_back(0xD9);                                   // EOI
  return out;
}

std::filesystem::path write_temp(const std::string& name, const std::vector<uint8_t>& bytes) {
  const auto path = std::filesystem::temp_directory_path() / name;
  std::FILE* f = std::fopen(path.string().c_str(), "wb");
  if (f) {
    std::fwrite(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
  }
  return path;
}

}  // namespace

ATLAS_TEST(exif_orientation_transform_matches_exif_table) {
  const auto src = id_image();  // 3x2: [0 1 2 / 3 4 5]

  struct Expect {
    int orientation;
    int w, h;
    int top_left, top_right, bottom_left;
  };
  const Expect cases[] = {
      {2, 3, 2, 2, 0, 5},  // آینهٔ افقی
      {3, 3, 2, 5, 3, 2},  // ۱۸۰
      {4, 3, 2, 3, 5, 0},  // آینهٔ عمودی
      {5, 2, 3, 0, 3, 2},  // transpose
      {6, 2, 3, 3, 0, 5},  // ۹۰ ساعتگرد
      {7, 2, 3, 5, 2, 3},  // transverse
      {8, 2, 3, 2, 5, 0},  // ۲۷۰ ساعتگرد
  };

  for (const auto& e : cases) {
    const auto out = atlas::apply_orientation(src, e.orientation);
    CHECK(out.width() == e.w);
    CHECK(out.height() == e.h);
    if (out.width() != e.w || out.height() != e.h) continue;
    CHECK(id_at(out, 0, 0) == e.top_left);
    CHECK(id_at(out, out.width() - 1, 0) == e.top_right);
    CHECK(id_at(out, 0, out.height() - 1) == e.bottom_left);
  }
}

ATLAS_TEST(exif_orientation_one_and_invalid_are_identity) {
  const auto src = id_image();
  for (int o : {0, 1, 9, -3}) {
    const auto out = atlas::apply_orientation(src, o);
    CHECK(out.width() == src.width());
    CHECK(out.height() == src.height());
    CHECK(id_at(out, 0, 0) == 0);
    CHECK(id_at(out, 2, 1) == 5);
  }
}

ATLAS_TEST(exif_rotate_90_then_270_restores_image) {
  const auto src = id_image();
  const auto rotated = atlas::apply_orientation(src, 6);
  const auto back = atlas::apply_orientation(rotated, 8);
  CHECK(back.width() == src.width());
  CHECK(back.height() == src.height());
  for (int y = 0; y < 2; ++y) {
    for (int x = 0; x < 3; ++x) CHECK(id_at(back, x, y) == x + y * 3);
  }
}

ATLAS_TEST(exif_parser_reads_orientation_tag) {
  for (uint16_t o : {1, 3, 6, 8}) {
    const auto path = write_temp("atlas_exif_" + std::to_string(o) + ".jpg",
                                 minimal_jpeg_with_orientation(o));
    CHECK(atlas::read_exif_orientation(path.string()) == o);
    std::filesystem::remove(path);
  }
}

ATLAS_TEST(exif_parser_returns_zero_without_exif) {
  atlas::ImageF32 img(8, 8, 3);
  img.fill(0.5f);
  const auto path = std::filesystem::temp_directory_path() / "atlas_no_exif.jpg";
  std::string error;
  CHECK(atlas::save_image(path.string(), img, 90, &error));
  CHECK(atlas::read_exif_orientation(path.string()) == 0);
  std::filesystem::remove(path);

  CHECK(atlas::read_exif_orientation("/nonexistent/atlas.jpg") == 0);
}

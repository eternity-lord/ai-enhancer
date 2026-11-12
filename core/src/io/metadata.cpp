// SPDX-License-Identifier: Apache-2.0
// یادداشت: هیچ کتابخانهٔ EXIF/PNG لازم نیست؛ فقط «برش و چسباندن» بخش‌های
// استاندارد. سه دام مهم:
//  ۱) CRC هر chunk در PNG باید درست باشد، وگرنه فایل خراب می‌شود (سکوت کامل).
//  ۲) iCCP باید قبل از IDAT باشد (الزام PNG).
//  ۳) ICC در JPEG باید به APP2های ≤ ۶۵۵۱۹ بایت شکسته شود؛ گذاشتن یک APP2 بزرگ
//     باعث truncation بی‌صدا می‌شود.
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "atlas/metadata.h"

#include "stb_image_write.h"

// stb_image_write اعلان عمومی برای این تابع ندارد؛ خودش با STBIWDEF (یعنی
// extern "C" در C++) تعریف شده، پس اعلان ما هم باید C-linkage باشد.
extern "C" unsigned char* stbi_zlib_compress(unsigned char* data, int data_len, int* out_len,
                                              int quality);

namespace atlas {
namespace {

constexpr uint32_t kMaxAppSegment = 65519;  // 65535 منهای طول و نشانگر

bool read_all(const std::string& path, std::vector<uint8_t>* out, std::string* error) {
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

bool write_all(const std::string& path, const std::vector<uint8_t>& bytes, std::string* error) {
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) {
    if (error) *error = "cannot write " + path;
    return false;
  }
  const size_t wrote = std::fwrite(bytes.data(), 1, bytes.size(), f);
  const bool ok = std::fclose(f) == 0 && wrote == bytes.size();
  if (!ok && error) *error = "short write " + path;
  return ok;
}

uint32_t crc_table(uint32_t n) {
  uint32_t c = n;
  for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
  return c;
}

uint32_t crc32(const uint8_t* data, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; ++i) c = crc_table((c ^ data[i]) & 0xFF) ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

void put32(std::vector<uint8_t>* b, uint32_t v) {
  b->push_back(static_cast<uint8_t>(v >> 24));
  b->push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  b->push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  b->push_back(static_cast<uint8_t>(v & 0xFF));
}

bool is_png(const std::vector<uint8_t>& b) {
  static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  return b.size() >= 8 && std::memcmp(b.data(), sig, 8) == 0;
}

bool is_jpeg(const std::vector<uint8_t>& b) {
  return b.size() >= 2 && b[0] == 0xFF && b[1] == 0xD8;
}

std::vector<uint8_t> make_png_chunk(const char* type, const std::vector<uint8_t>& data) {
  std::vector<uint8_t> out;
  put32(&out, static_cast<uint32_t>(data.size()));
  const size_t start = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), data.begin(), data.end());
  put32(&out, crc32(out.data() + start, out.size() - start));
  return out;
}

// --- استخراج EXIF ---

// payload: بایت‌های بعد از «Exif\0\0» (TIFF خالص)
bool exif_tiff_from_jpeg(const std::vector<uint8_t>& jpg, std::vector<uint8_t>* tiff) {
  size_t i = 2;
  while (i + 4 <= jpg.size()) {
    if (jpg[i] != 0xFF) { ++i; continue; }
    const uint8_t marker = jpg[i + 1];
    if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) { i += 2; continue; }
    if (marker == 0xDA || marker == 0xD9) break;
    const uint16_t len = static_cast<uint16_t>((jpg[i + 2] << 8) | jpg[i + 3]);
    if (len < 2) break;
    const size_t payload = i + 4;
    const size_t payload_len = len - 2;
    if (payload + payload_len > jpg.size()) break;
    if (marker == 0xE1 && payload_len > 6 &&
        std::memcmp(jpg.data() + payload, "Exif\0\0", 6) == 0) {
      tiff->assign(jpg.begin() + static_cast<long>(payload + 6),
                   jpg.begin() + static_cast<long>(payload + payload_len));
      return true;
    }
    i = payload + payload_len;
  }
  return false;
}

bool exif_tiff_from_png(const std::vector<uint8_t>& png, std::vector<uint8_t>* tiff) {
  size_t i = 8;
  while (i + 12 <= png.size()) {
    const uint32_t len = (static_cast<uint32_t>(png[i]) << 24) |
                         (static_cast<uint32_t>(png[i + 1]) << 16) |
                         (static_cast<uint32_t>(png[i + 2]) << 8) | png[i + 3];
    if (i + 12 + static_cast<size_t>(len) > png.size()) break;
    if (std::memcmp(png.data() + i + 4, "eXIf", 4) == 0) {
      tiff->assign(png.begin() + static_cast<long>(i + 8),
                   png.begin() + static_cast<long>(i + 8 + len));
      return true;
    }
    if (std::memcmp(png.data() + i + 4, "IEND", 4) == 0) break;
    i += 12 + static_cast<size_t>(len);
  }
  return false;
}

std::vector<uint8_t> jpeg_app1_from_tiff(const std::vector<uint8_t>& tiff) {
  std::vector<uint8_t> seg;
  seg.reserve(4 + 6 + tiff.size());  // جلوگیری از هشدار کاذب GCC روی insert
  seg = {0xFF, 0xE1};
  const uint16_t len = static_cast<uint16_t>(tiff.size() + 8);  // len + "Exif\0\0"
  seg.push_back(static_cast<uint8_t>(len >> 8));
  seg.push_back(static_cast<uint8_t>(len & 0xFF));
  const char exif[] = "Exif\0\0";
  seg.insert(seg.end(), exif, exif + 6);
  seg.insert(seg.end(), tiff.begin(), tiff.end());
  return seg;
}

bool insert_after_soi(const std::string& path, const std::vector<uint8_t>& segment,
                      std::string* error) {
  std::vector<uint8_t> bytes;
  if (!read_all(path, &bytes, error)) return false;
  if (!is_jpeg(bytes)) {
    if (error) *error = "not a JPEG: " + path;
    return false;
  }
  std::vector<uint8_t> out;
  out.reserve(bytes.size() + segment.size());
  out.push_back(0xFF);
  out.push_back(0xD8);
  out.insert(out.end(), segment.begin(), segment.end());
  out.insert(out.end(), bytes.begin() + 2, bytes.end());
  return write_all(path, out, error);
}

// طبق مشخصات PNG، iCCP باید قبل از IDAT باشد؛ در غیر این صورت خواننده‌های
// استاندارد (و خوانندهٔ خودمان) آن را نمی‌بینند. برای eXIf سخت‌گیری نیست، ولی
// یکسان رفتار می‌کنیم.
bool insert_png_chunk_before_data(const std::string& path, const std::vector<uint8_t>& chunk,
                                  std::string* error) {
  std::vector<uint8_t> bytes;
  if (!read_all(path, &bytes, error)) return false;
  if (!is_png(bytes)) {
    if (error) *error = "not a PNG: " + path;
    return false;
  }
  // محل درج: اولین IDAT (و اگر نبود، IEND).
  size_t at = bytes.size();
  size_t i = 8;
  while (i + 12 <= bytes.size()) {
    const uint32_t len = (static_cast<uint32_t>(bytes[i]) << 24) |
                         (static_cast<uint32_t>(bytes[i + 1]) << 16) |
                         (static_cast<uint32_t>(bytes[i + 2]) << 8) | bytes[i + 3];
    if (std::memcmp(bytes.data() + i + 4, "IDAT", 4) == 0 ||
        std::memcmp(bytes.data() + i + 4, "IEND", 4) == 0) {
      at = i;
      break;
    }
    i += 12 + static_cast<size_t>(len);
  }
  std::vector<uint8_t> out;
  out.reserve(bytes.size() + chunk.size());
  out.insert(out.end(), bytes.begin(), bytes.begin() + static_cast<long>(at));
  out.insert(out.end(), chunk.begin(), chunk.end());
  out.insert(out.end(), bytes.begin() + static_cast<long>(at), bytes.end());
  return write_all(path, out, error);
}

void put16(std::vector<uint8_t>* b, uint16_t v) {
  b->push_back(static_cast<uint8_t>(v >> 8));
  b->push_back(static_cast<uint8_t>(v & 0xFF));
}

void put_s15f16(std::vector<uint8_t>* b, float v) {
  const int32_t fixed = static_cast<int32_t>(v * 65536.0f + (v >= 0 ? 0.5f : -0.5f));
  put32(b, static_cast<uint32_t>(fixed));
}

bool ends_with(const std::string& s, const char* suffix) {
  const size_t n = std::strlen(suffix);
  return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

}  // namespace

std::string make_srgb_icc_profile() {
  // ماتریس sRGB (D65) — همان اعدادی که در docs/09 آمده است.
  const float m[9] = {0.4124564f, 0.3575761f, 0.1804375f, 0.2126729f, 0.7151522f,
                      0.0721750f, 0.0193339f, 0.1191920f, 0.9503041f};

  std::vector<std::pair<const char*, std::vector<uint8_t>>> tags;
  auto xyz = [&](float x, float y, float z) {
    std::vector<uint8_t> t = {'X', 'Y', 'Z', ' ', 0, 0, 0, 0};
    put_s15f16(&t, x);
    put_s15f16(&t, y);
    put_s15f16(&t, z);
    return t;
  };
  tags.push_back({"rXYZ", xyz(m[0], m[1], m[2])});
  tags.push_back({"gXYZ", xyz(m[3], m[4], m[5])});
  tags.push_back({"bXYZ", xyz(m[6], m[7], m[8])});

  // منحنی sRGB با para type 4 (تنها نوعی که هر دو قطعه را دارد).
  auto trc = []() {
    std::vector<uint8_t> t = {'p', 'a', 'r', 'a', 0, 0, 0, 0};
    put16(&t, 4);
    put16(&t, 0);
    put_s15f16(&t, 2.4f);
    put_s15f16(&t, 1.0f / 1.055f);
    put_s15f16(&t, 0.055f / 1.055f);
    put_s15f16(&t, 0.0f);
    put_s15f16(&t, 0.04045f);
    put_s15f16(&t, 1.0f / 12.92f);
    put_s15f16(&t, 0.0f);
    return t;
  };
  tags.push_back({"rTRC", trc()});
  tags.push_back({"gTRC", trc()});
  tags.push_back({"bTRC", trc()});

  std::vector<uint8_t> desc = {'d', 'e', 's', 'c', 0, 0, 0, 0};
  const char* text = "Atlas sRGB";
  put32(&desc, static_cast<uint32_t>(std::strlen(text) + 1));
  desc.insert(desc.end(), text, text + std::strlen(text) + 1);
  tags.push_back({"desc", desc});

  std::vector<uint8_t> out(128, 0);
  out[8] = 0x02;                                   // version 2.1.0
  out[12] = 'm'; out[13] = 'n'; out[14] = 't'; out[15] = 'r';
  out[16] = 'R'; out[17] = 'G'; out[18] = 'B'; out[19] = ' ';
  out[20] = 'X'; out[21] = 'Y'; out[22] = 'Z'; out[23] = ' ';
  out[36] = 'a'; out[37] = 'c'; out[38] = 's'; out[39] = 'p';

  put32(&out, static_cast<uint32_t>(tags.size()));
  size_t offset = 128 + 4 + tags.size() * 12;
  std::vector<uint8_t> body;
  std::vector<std::pair<uint32_t, uint32_t>> refs;
  for (auto& tag : tags) {
    refs.emplace_back(static_cast<uint32_t>(offset), static_cast<uint32_t>(tag.second.size()));
    body.insert(body.end(), tag.second.begin(), tag.second.end());
    offset += tag.second.size();
    const size_t pad = (4 - (tag.second.size() % 4)) % 4;
    body.insert(body.end(), pad, 0);
    offset += pad;
  }
  for (size_t i = 0; i < tags.size(); ++i) {
    out.insert(out.end(), tags[i].first, tags[i].first + 4);
    put32(&out, refs[i].first);
    put32(&out, refs[i].second);
  }
  // طول کل در ۸ بایت اول
  const uint32_t total = static_cast<uint32_t>(out.size() + body.size());
  out[0] = static_cast<uint8_t>(total >> 24);
  out[1] = static_cast<uint8_t>((total >> 16) & 0xFF);
  out[2] = static_cast<uint8_t>((total >> 8) & 0xFF);
  out[3] = static_cast<uint8_t>(total & 0xFF);
  out.insert(out.end(), body.begin(), body.end());
  return std::string(out.begin(), out.end());
}

bool has_exif_metadata(const std::string& path) {
  std::vector<uint8_t> bytes;
  std::string error;
  if (!read_all(path, &bytes, &error)) return false;
  std::vector<uint8_t> tiff;
  if (is_jpeg(bytes)) return exif_tiff_from_jpeg(bytes, &tiff);
  if (is_png(bytes)) return exif_tiff_from_png(bytes, &tiff);
  return false;
}

bool keep_exif_metadata(const std::string& src_path, const std::string& dst_path,
                        std::string* error) {
  std::vector<uint8_t> src;
  if (!read_all(src_path, &src, error)) return false;

  std::vector<uint8_t> tiff;
  const bool found = is_jpeg(src) ? exif_tiff_from_jpeg(src, &tiff)
                                  : (is_png(src) ? exif_tiff_from_png(src, &tiff) : false);
  if (!found) {
    if (error) *error = "source has no EXIF metadata";
    return false;
  }

  if (ends_with(dst_path, ".jpg") || ends_with(dst_path, ".jpeg")) {
    return insert_after_soi(dst_path, jpeg_app1_from_tiff(tiff), error);
  }
  if (ends_with(dst_path, ".png")) {
    return insert_png_chunk_before_data(dst_path, make_png_chunk("eXIf", tiff), error);
  }
  if (error) *error = "metadata embedding supports .jpg and .png output";
  return false;
}

bool tag_srgb_profile(const std::string& dst_path, std::string* error) {
  const std::string profile = make_srgb_icc_profile();
  const std::vector<uint8_t> bytes(profile.begin(), profile.end());

  if (ends_with(dst_path, ".png")) {
    std::vector<uint8_t> data;
    const char name[] = "sRGB";
    data.insert(data.end(), name, name + 5);  // شامل \0
    data.push_back(0);                        // compression method = zlib
    int out_len = 0;
    unsigned char* packed = stbi_zlib_compress(const_cast<unsigned char*>(bytes.data()),
                                               static_cast<int>(bytes.size()), &out_len, 8);
    if (!packed || out_len <= 0) {
      if (packed) free(packed);
      if (error) *error = "zlib compression failed";
      return false;
    }
    data.insert(data.end(), packed, packed + out_len);
    free(packed);
    return insert_png_chunk_before_data(dst_path, make_png_chunk("iCCP", data), error);
  }

  if (ends_with(dst_path, ".jpg") || ends_with(dst_path, ".jpeg")) {
    const size_t total = (bytes.size() + kMaxAppSegment - 1) / kMaxAppSegment;
    std::vector<uint8_t> all;
    size_t offset = 0;
    int seq = 1;
    while (offset < bytes.size()) {
      const size_t n = std::min<size_t>(kMaxAppSegment, bytes.size() - offset);
      std::vector<uint8_t> payload = {'I', 'C', 'C', '_', 'P', 'R', 'O', 'F', 'I', 'L', 'E', 0,
                                      static_cast<uint8_t>(seq), static_cast<uint8_t>(total)};
      payload.insert(payload.end(), bytes.begin() + static_cast<long>(offset),
                     bytes.begin() + static_cast<long>(offset + n));
      const uint16_t len = static_cast<uint16_t>(payload.size() + 2);
      all.push_back(0xFF);
      all.push_back(0xE2);
      all.push_back(static_cast<uint8_t>(len >> 8));
      all.push_back(static_cast<uint8_t>(len & 0xFF));
      all.insert(all.end(), payload.begin(), payload.end());
      offset += n;
      ++seq;
    }
    return insert_after_soi(dst_path, all, error);
  }

  if (error) *error = "ICC embedding supports .png and .jpg output";
  return false;
}

}  // namespace atlas

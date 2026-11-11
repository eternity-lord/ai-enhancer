// SPDX-License-Identifier: Apache-2.0
// یادداشت: فقط سرِ فایل خوانده می‌شود (۱۲۸ کیلوبایت) چون APP1 در همان ابتداست
// و خواندن کل فایل ۵۰ مگابایتی فقط برای ۲ بایت اتلاف است. پارسر محافظه‌کار است:
// هر ساختار مشکوک → ۰ (رفتار پیش‌فرض = بدون چرخش) و هرگز استثنا پرتاب نمی‌کند.
#include <cstdint>
#include <cstdio>
#include <vector>

#include "atlas/exif.h"
#include "atlas/image.h"

namespace atlas {
namespace {

uint16_t read_u16(const uint8_t* p, bool little) {
  return little ? static_cast<uint16_t>(p[0] | (p[1] << 8))
                : static_cast<uint16_t>((p[0] << 8) | p[1]);
}

uint32_t read_u32(const uint8_t* p, bool little) {
  if (little) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
  }
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

// tiff: اشارهگر به ابتدای "II"/"MM" داخل APP1؛ size: بایت‌های باقی‌مانده.
int orientation_from_tiff(const uint8_t* tiff, size_t size) {
  if (size < 8) return 0;
  const bool little = tiff[0] == 'I' && tiff[1] == 'I';
  const bool big = tiff[0] == 'M' && tiff[1] == 'M';
  if (!little && !big) return 0;
  if (read_u16(tiff + 2, little) != 42) return 0;

  const uint32_t ifd = read_u32(tiff + 4, little);
  if (ifd + 2 > size) return 0;
  const uint16_t count = read_u16(tiff + ifd, little);
  // هر ورودی ۱۲ بایت است؛ محافظت در برابر count معیوب.
  if (ifd + 2 + static_cast<uint64_t>(count) * 12 > size) return 0;

  for (uint16_t i = 0; i < count; ++i) {
    const uint8_t* e = tiff + ifd + 2 + static_cast<size_t>(i) * 12;
    const uint16_t tag = read_u16(e, little);
    if (tag != 0x0112) continue;  // Orientation
    const uint16_t type = read_u16(e + 2, little);
    if (type != 3) return 0;  // SHORT
    const uint16_t value = read_u16(e + 8, little);
    return (value >= 1 && value <= 8) ? static_cast<int>(value) : 0;
  }
  return 0;
}

}  // namespace

int read_exif_orientation(const std::string& path) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return 0;
  std::vector<uint8_t> head(128 * 1024);
  const size_t n = std::fread(head.data(), 1, head.size(), f);
  std::fclose(f);
  head.resize(n);
  if (n < 4 || head[0] != 0xFF || head[1] != 0xD8) return 0;  // SOI (JPEG)

  size_t i = 2;
  while (i + 4 <= n) {
    if (head[i] != 0xFF) { ++i; continue; }
    const uint8_t marker = head[i + 1];
    if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
      i += 2;
      continue;
    }
    if (marker == 0xDA || marker == 0xD9) break;  // شروع داده/پایان: دیگر APP نداریم
    const uint16_t len = static_cast<uint16_t>((head[i + 2] << 8) | head[i + 3]);
    if (len < 2) break;
    const size_t payload = i + 4;
    const size_t payload_len = len - 2;
    if (payload + payload_len > n) break;
    if (marker == 0xE1 && payload_len > 6 && head[payload] == 'E' &&
        head[payload + 1] == 'x' && head[payload + 2] == 'i' &&
        head[payload + 3] == 'f' && head[payload + 4] == 0 && head[payload + 5] == 0) {
      return orientation_from_tiff(head.data() + payload + 6, payload_len - 6);
    }
    i = payload + payload_len;
  }
  return 0;
}

ImageF32 apply_orientation(const ImageF32& src, int orientation) {
  if (src.empty() || orientation <= 1 || orientation > 8) return src;

  const int w = src.width();
  const int h = src.height();
  const bool swapped = orientation >= 5;
  const int dw = swapped ? h : w;
  const int dh = swapped ? w : h;
  const int c = src.channels();

  ImageF32 out(dw, dh, c);
  for (int y = 0; y < dh; ++y) {
    for (int x = 0; x < dw; ++x) {
      int sx = x;
      int sy = y;
      switch (orientation) {
        case 2: sx = w - 1 - x; break;                          // آینهٔ افقی
        case 3: sx = w - 1 - x; sy = h - 1 - y; break;          // ۱۸۰ درجه
        case 4: sy = h - 1 - y; break;                          // آینهٔ عمودی
        case 5: sx = y; sy = x; break;                          // transpose
        case 6: sx = y; sy = h - 1 - x; break;                  // ۹۰ درجه ساعتگرد
        case 7: sx = w - 1 - y; sy = h - 1 - x; break;          // transverse
        case 8: sx = w - 1 - y; sy = x; break;                  // ۲۷۰ ساعتگرد
        default: break;
      }
      if (sx < 0) sx = 0;
      if (sy < 0) sy = 0;
      if (sx >= w) sx = w - 1;
      if (sy >= h) sy = h - 1;
      const float* s = src.pixel(sx, sy);
      float* d = out.pixel(x, y);
      for (int ch = 0; ch < c; ++ch) d[ch] = s[ch];
    }
  }
  return out;
}

}  // namespace atlas

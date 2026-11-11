// SPDX-License-Identifier: Apache-2.0
// یادداشت: این فایل یک «خوانندهٔ محافظه‌کار» است، نه یک موتور مدیریت رنگ کامل.
// تصمیم‌های مهم:
//  ۱) فقط پروفایل‌های ماتریسی/TRC (کلاس mntr/scnr با فضای 'RGB ').
//  ۲) پروفایل LUT (A2B0/B2A0) رد می‌شود؛ تبدیل کورکورانه از آن، رنگ را بدتر
//     خراب می‌کند تا وقتی اصلاً تبدیل نکنیم.
//  ۳) LUT سه‌کاناله با ۴۰۹۶ نمونه ساخته می‌شود: دقت ۱/۴۰۹۶ کافی است و
//     هزینهٔ pow در هر پیکسل حذف می‌شود.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "atlas/color.h"
#include "atlas/icc.h"
#include "atlas/image.h"

// فقط برای inflate کردن iCCP در PNG؛ خود استاتیک لینک شده است.
#include "stb_image.h"

namespace atlas {
namespace {

constexpr float kSrgbToXyz[9] = {0.4124564f, 0.3575761f, 0.1804375f,
                                 0.2126729f, 0.7151522f, 0.0721750f,
                                 0.0193339f, 0.1191920f, 0.9503041f};
constexpr float kXyzToSrgb[9] = {3.2404542f, -1.5371385f, -0.4985314f,
                                 -0.9692660f, 1.8760108f, 0.0415560f,
                                 0.0556434f, -0.2040259f, 1.0572252f};

uint16_t rd16(const uint8_t* p) {
  return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

uint32_t rd32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

float s15fixed16(const uint8_t* p) {
  const int32_t v = static_cast<int32_t>(rd32(p));
  return static_cast<float>(v) / 65536.0f;
}

bool tag_is(const uint8_t* data, size_t size, uint32_t off, const char* sig) {
  return off + 4 <= size && std::memcmp(data + off, sig, 4) == 0;
}

struct TagRef {
  uint32_t offset = 0;
  uint32_t size = 0;
  bool found = false;
};

TagRef find_tag(const uint8_t* data, size_t size, uint32_t count, const char* sig) {
  TagRef out;
  for (uint32_t i = 0; i < count; ++i) {
    const uint8_t* e = data + 132 + static_cast<size_t>(i) * 12;
    if (std::memcmp(e, sig, 4) != 0) continue;
    out.offset = rd32(e + 4);
    out.size = rd32(e + 8);
    out.found = out.offset + out.size <= size && out.size >= 8;
    return out;
  }
  return out;
}

float clamp01(float v) { return std::min(1.0f, std::max(0.0f, v)); }

// --- جبر ۳×۳ برای سازگاری رنگی (Bradford) ---
// پروفایل‌های D50 (ProPhoto، بسیاری از پروفایل‌های چاپی) اگر بدون سازگاری با
// ماتریس sRGB-D65 ترکیب شوند، یک رنگ‌افتادگی زرد/سبز می‌دهند. این ماتریس‌ها
// همان اعداد استاندارد Bradford هستند.
using Mat3 = std::array<float, 9>;

Mat3 mat_mul(const Mat3& a, const Mat3& b) {
  Mat3 out{};
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      out[r * 3 + c] = a[r * 3 + 0] * b[0 * 3 + c] + a[r * 3 + 1] * b[1 * 3 + c] +
                       a[r * 3 + 2] * b[2 * 3 + c];
    }
  }
  return out;
}

Mat3 mat_inverse(const Mat3& m) {
  const float a = m[0], b = m[1], c = m[2];
  const float d = m[3], e = m[4], f = m[5];
  const float g = m[6], h = m[7], i = m[8];
  const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
  Mat3 out{};
  if (std::fabs(det) < 1e-12f) return out;
  const float inv = 1.0f / det;
  out[0] = (e * i - f * h) * inv;
  out[1] = (c * h - b * i) * inv;
  out[2] = (b * f - c * e) * inv;
  out[3] = (f * g - d * i) * inv;
  out[4] = (a * i - c * g) * inv;
  out[5] = (c * d - a * f) * inv;
  out[6] = (d * h - e * g) * inv;
  out[7] = (b * g - a * h) * inv;
  out[8] = (a * e - b * d) * inv;
  return out;
}

Mat3 bradford(const std::array<float, 3>& src_wp, const std::array<float, 3>& dst_wp) {
  constexpr Mat3 kBrad = {0.8951f, 0.2664f, -0.1614f, -0.7502f, 1.7135f,
                          0.0367f, 0.0389f, -0.0685f, 1.0296f};
  const Mat3 brad_inv = mat_inverse(kBrad);
  auto cone = [&](const std::array<float, 3>& wp) {
    return std::array<float, 3>{kBrad[0] * wp[0] + kBrad[1] * wp[1] + kBrad[2] * wp[2],
                                kBrad[3] * wp[0] + kBrad[4] * wp[1] + kBrad[5] * wp[2],
                                kBrad[6] * wp[0] + kBrad[7] * wp[1] + kBrad[8] * wp[2]};
  };
  const auto s = cone(src_wp);
  const auto d = cone(dst_wp);
  const Mat3 scale = {d[0] / (s[0] + 1e-12f), 0, 0, 0, d[1] / (s[1] + 1e-12f), 0,
                      0, 0, d[2] / (s[2] + 1e-12f)};
  return mat_mul(brad_inv, mat_mul(scale, kBrad));
}

// پارامتریک curve types 0..4 (ICC v4 spec, 10.6)
float eval_parametric(int type, const float* p, float x) {
  x = clamp01(x);
  const float g = p[0];
  switch (type) {
    case 0:
      return std::pow(x, g);
    case 1: {
      const float a = p[1];
      const float b = p[2];
      return x >= -b / a ? clamp01(std::pow(a * x + b, g)) : 0.0f;
    }
    case 2: {
      const float a = p[1];
      const float b = p[2];
      const float c = p[3];
      return clamp01(std::pow(a * x + b, g) + c);
    }
    case 3: {
      const float a = p[1];
      const float b = p[2];
      const float c = p[3];
      const float d = p[4];
      return x >= d ? clamp01(std::pow(a * x + b, g) + c) : clamp01(c * x);
    }
    case 4: {
      const float a = p[1];
      const float b = p[2];
      const float c = p[3];
      const float d = p[4];
      const float e = p[5];
      const float f = p[6];
      return x >= d ? clamp01(std::pow(a * x + b, g) + c) : clamp01(e * x + f);
    }
    default:
      return x;
  }
}

bool build_trc(const uint8_t* data, const TagRef& tag, float gamma_out[1],
               std::array<float, 4096>* lut) {
  if (!tag.found) return false;
  const uint8_t* p = data + tag.offset;
  const uint32_t tag_size = tag.size;

  if (std::memcmp(p, "curv", 4) == 0 && tag_size >= 12) {
    const uint32_t count = rd32(p + 8);
    if (count == 0) {  // identity
      for (int i = 0; i < 4096; ++i) (*lut)[i] = static_cast<float>(i) / 4095.0f;
      *gamma_out = 1.0f;
      return true;
    }
    if (count == 1) {  // u8Fixed8 gamma
      const float g = static_cast<float>(rd16(p + 12)) / 256.0f;
      if (!(g > 0.01f && g < 10.0f)) return false;
      for (int i = 0; i < 4096; ++i) (*lut)[i] = std::pow(static_cast<float>(i) / 4095.0f, g);
      *gamma_out = g;
      return true;
    }
    if (tag_size < 12 + static_cast<size_t>(count) * 2) return false;
    for (int i = 0; i < 4096; ++i) {
      const float t = static_cast<float>(i) / 4095.0f * static_cast<float>(count - 1);
      const uint32_t i0 = static_cast<uint32_t>(t);
      const uint32_t i1 = std::min(i0 + 1, count - 1);
      const float f = t - static_cast<float>(i0);
      const float v0 = static_cast<float>(rd16(p + 12 + i0 * 2)) / 65535.0f;
      const float v1 = static_cast<float>(rd16(p + 12 + i1 * 2)) / 65535.0f;
      (*lut)[i] = v0 + (v1 - v0) * f;
    }
    *gamma_out = 0.0f;  // جدولی: گاما معنی ندارد
    return true;
  }

  if (std::memcmp(p, "para", 4) == 0 && tag_size >= 12) {
    const uint16_t type = rd16(p + 8);
    const int params = (type == 0) ? 1 : (type == 1 ? 3 : (type == 2 ? 4 : (type == 3 ? 5 : 7)));
    if (tag_size < 12 + static_cast<size_t>(params) * 4) return false;
    float v[7] = {1, 0, 0, 0, 0, 0, 0};
    for (int i = 0; i < params; ++i) v[i] = s15fixed16(p + 12 + i * 4);
    if (type >= 1 && std::fabs(v[1]) < 1e-6f) return false;  // a ~ 0 → تقسیم بر صفر
    for (int i = 0; i < 4096; ++i) {
      (*lut)[i] = eval_parametric(type, v, static_cast<float>(i) / 4095.0f);
    }
    *gamma_out = type == 0 ? v[0] : 0.0f;
    return true;
  }
  return false;
}

std::string read_text_tag(const uint8_t* data, const TagRef& tag) {
  if (!tag.found || tag.size < 12) return {};
  const uint8_t* p = data + tag.offset;
  if (std::memcmp(p, "desc", 4) == 0) {
    const uint32_t len = rd32(p + 8);
    if (len > 0 && 12 + len <= tag.size) {
      std::string s(reinterpret_cast<const char*>(p + 12), len);
      if (!s.empty() && s.back() == '\0') s.pop_back();
      return s;
    }
  }
  if (std::memcmp(p, "mluc", 4) == 0 && tag.size >= 28) {
    // فقط اولین رکورد کافی است: طول رشته بر حسب کاراکتر + offset.
    const uint32_t count = rd32(p + 8);
    if (count == 0) return {};
    const uint32_t len = rd32(p + 20);
    const uint32_t off = rd32(p + 24);
    if (off + len * 2 <= tag.size) {
      std::string s;
      s.reserve(len);
      for (uint32_t i = 0; i < len; ++i) {
        const uint16_t ch = rd16(p + off + i * 2);
        if (ch == 0) break;
        s.push_back(static_cast<char>(ch & 0x7F));
      }
      return s;
    }
  }
  return {};
}

std::vector<uint8_t> read_head(const std::string& path, size_t limit) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return {};
  std::vector<uint8_t> buf(limit);
  const size_t n = std::fread(buf.data(), 1, limit, f);
  std::fclose(f);
  buf.resize(n);
  return buf;
}

}  // namespace

IccProfile parse_icc(const uint8_t* data, size_t size) {
  IccProfile p;
  if (!data || size < 132) return p;
  if (std::memcmp(data + 36, "acsp", 4) != 0) return p;

  p.color_space.assign(reinterpret_cast<const char*>(data + 16), 4);
  if (p.color_space != "RGB ") return p;

  const uint32_t count = rd32(data + 128);
  if (count == 0 || count > 1024) return p;
  if (size < 132 + static_cast<size_t>(count) * 12) return p;

  const TagRef r_xyz = find_tag(data, size, count, "rXYZ");
  const TagRef g_xyz = find_tag(data, size, count, "gXYZ");
  const TagRef b_xyz = find_tag(data, size, count, "bXYZ");
  const TagRef r_trc = find_tag(data, size, count, "rTRC");
  const TagRef g_trc = find_tag(data, size, count, "gTRC");
  const TagRef b_trc = find_tag(data, size, count, "bTRC");
  if (!r_xyz.found || !g_xyz.found || !b_xyz.found) return p;  // پروفایل LUT: رد
  if (!tag_is(data, size, r_xyz.offset, "XYZ ") || !tag_is(data, size, g_xyz.offset, "XYZ ") ||
      !tag_is(data, size, b_xyz.offset, "XYZ ")) {
    return p;
  }

  p.rgb_to_xyz = {s15fixed16(data + r_xyz.offset + 8), s15fixed16(data + r_xyz.offset + 12),
                  s15fixed16(data + r_xyz.offset + 16), s15fixed16(data + g_xyz.offset + 8),
                  s15fixed16(data + g_xyz.offset + 12), s15fixed16(data + g_xyz.offset + 16),
                  s15fixed16(data + b_xyz.offset + 8), s15fixed16(data + b_xyz.offset + 12),
                  s15fixed16(data + b_xyz.offset + 16)};

  const TagRef trcs[3] = {r_trc, g_trc, b_trc};
  for (int c = 0; c < 3; ++c) {
    float gamma = 1.0f;
    if (!build_trc(data, trcs[c], &gamma, &p.trc[static_cast<size_t>(c)])) return p;
    p.gamma[static_cast<size_t>(c)] = gamma;
    p.used_parametric = p.used_parametric || (trcs[c].found && tag_is(data, size, trcs[c].offset, "para"));
  }

  // نقطهٔ سفید (wtpt) برای سازگاری رنگی؛ اگر نبود، D65 فرض می‌شود.
  const TagRef wtpt = find_tag(data, size, count, "wtpt");
  if (wtpt.found && tag_is(data, size, wtpt.offset, "XYZ ")) {
    p.white_point = {s15fixed16(data + wtpt.offset + 8), s15fixed16(data + wtpt.offset + 12),
                     s15fixed16(data + wtpt.offset + 16)};
  }

  TagRef desc = find_tag(data, size, count, "desc");
  if (!desc.found) desc = find_tag(data, size, count, "mluc");
  p.description = read_text_tag(data, desc);
  p.valid = true;
  return p;
}

std::array<float, 9> icc_to_srgb_matrix(const IccProfile& profile) {
  // اگر نقطهٔ سفید پروفایل به‌طور محسوسی از D65 دور باشد (مثلاً D50 در
  // ProPhoto)، اول با Bradford به D65 می‌رویم؛ وگرنه رنگ‌افتادگی می‌گیریم.
  Mat3 device_to_xyz = profile.rgb_to_xyz;
  constexpr std::array<float, 3> kD65 = {0.9505f, 1.0f, 1.089f};
  const float dx = std::fabs(profile.white_point[0] - kD65[0]);
  const float dy = std::fabs(profile.white_point[1] - kD65[1]);
  const float dz = std::fabs(profile.white_point[2] - kD65[2]);
  if (dx + dy + dz > 0.02f) {
    device_to_xyz = mat_mul(bradford(profile.white_point, kD65), device_to_xyz);
  }

  std::array<float, 9> out{};
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      float sum = 0.0f;
      for (int k = 0; k < 3; ++k) sum += kXyzToSrgb[r * 3 + k] * device_to_xyz[k * 3 + c];
      out[r * 3 + c] = sum;
    }
  }
  return out;
}

ImageF32 apply_icc_to_srgb(const ImageF32& img, const IccProfile& profile) {
  if (!profile.valid || img.empty() || img.channels() < 3) return img;

  const auto m = icc_to_srgb_matrix(profile);
  ImageF32 out(img.width(), img.height(), img.channels());
  const int ch = img.channels();
  for (size_t i = 0; i < img.pixel_count(); ++i) {
    const float* s = img.data() + i * ch;
    float lin[3];
    for (int c = 0; c < 3; ++c) {
      const float v = clamp01(s[c]);
      const size_t idx = static_cast<size_t>(v * 4095.0f + 0.5f);
      lin[c] = profile.trc[static_cast<size_t>(c)][std::min<size_t>(idx, 4095)];
    }
    float* d = out.data() + i * ch;
    for (int r = 0; r < 3; ++r) {
      const float v = m[r * 3 + 0] * lin[0] + m[r * 3 + 1] * lin[1] + m[r * 3 + 2] * lin[2];
      d[r] = linear_to_srgb(std::min(1.0f, std::max(0.0f, v)));
    }
    for (int c = 3; c < ch; ++c) d[c] = s[c];  // آلفا
  }
  return out;
}

std::string icc_summary(const IccProfile& profile) {
  if (!profile.valid) return "none";
  std::string out = profile.description.empty() ? "RGB profile" : profile.description;
  out += " (matrix/TRC";
  if (profile.gamma[0] > 0.0f && std::fabs(profile.gamma[0] - profile.gamma[1]) < 0.01f &&
      std::fabs(profile.gamma[1] - profile.gamma[2]) < 0.01f) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), ", gamma %.2f)", profile.gamma[0]);
    out += buf;
  } else {
    out += ")";
  }
  return out;
}

std::string read_png_icc_profile(const std::string& path) {
  const std::vector<uint8_t> head = read_head(path, 4 * 1024 * 1024);
  static const uint8_t kSig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  if (head.size() < 8 || std::memcmp(head.data(), kSig, 8) != 0) return {};

  size_t i = 8;
  while (i + 8 <= head.size()) {
    const uint32_t len = rd32(head.data() + i);
    if (i + 12 + static_cast<size_t>(len) > head.size()) break;
    const uint8_t* type = head.data() + i + 4;
    if (std::memcmp(type, "iCCP", 4) == 0 && len > 2) {
      const uint8_t* d = head.data() + i + 8;
      size_t name_len = 0;
      while (name_len < len && d[name_len] != 0) ++name_len;
      if (name_len + 2 > len) return {};
      const uint8_t method = d[name_len + 1];
      if (method != 0) return {};  // فقط zlib
      int out_len = 0;
      char* raw = stbi_zlib_decode_malloc(
          reinterpret_cast<const char*>(d + name_len + 2),
          static_cast<int>(len - name_len - 2), &out_len);
      if (!raw || out_len <= 0) {
        if (raw) stbi_image_free(raw);
        return {};
      }
      std::string profile(raw, static_cast<size_t>(out_len));
      stbi_image_free(raw);
      return profile;
    }
    if (std::memcmp(type, "IDAT", 4) == 0 || std::memcmp(type, "IEND", 4) == 0) break;
    i += 12 + static_cast<size_t>(len);  // len + type + data + crc
  }
  return {};
}

std::string read_jpeg_icc_profile(const std::string& path) {
  const std::vector<uint8_t> head = read_head(path, 8 * 1024 * 1024);
  if (head.size() < 4 || head[0] != 0xFF || head[1] != 0xD8) return {};

  std::vector<std::string> chunks;
  int total = 0;
  size_t i = 2;
  while (i + 4 <= head.size()) {
    if (head[i] != 0xFF) { ++i; continue; }
    const uint8_t marker = head[i + 1];
    if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) { i += 2; continue; }
    if (marker == 0xDA || marker == 0xD9) break;
    const uint16_t len = rd16(head.data() + i + 2);
    if (len < 2) break;
    const size_t payload = i + 4;
    const size_t payload_len = len - 2;
    if (payload + payload_len > head.size()) break;
    if (marker == 0xE2 && payload_len > 14 &&
        std::memcmp(head.data() + payload, "ICC_PROFILE\0", 12) == 0) {
      const int seq = head[payload + 12];
      const int cnt = head[payload + 13];
      if (total == 0 && cnt > 0) {
        total = cnt;
        chunks.assign(static_cast<size_t>(cnt), std::string());
      }
      if (seq >= 1 && seq <= total) {
        chunks[static_cast<size_t>(seq - 1)] =
            std::string(reinterpret_cast<const char*>(head.data() + payload + 14), payload_len - 14);
      }
    }
    i = payload + payload_len;
  }
  if (total <= 0) return {};
  std::string out;
  for (const auto& c : chunks) {
    if (c.empty()) return {};  // تکهٔ گم‌شده: پروفایل ناقص، استفاده نمی‌کنیم
    out += c;
  }
  return out;
}

std::string read_icc_profile(const std::string& path) {
  std::string png = read_png_icc_profile(path);
  if (!png.empty()) return png;
  return read_jpeg_icc_profile(path);
}

}  // namespace atlas

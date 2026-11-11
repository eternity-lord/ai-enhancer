// SPDX-License-Identifier: Apache-2.0
// یادداشت: پروفایل‌ها را در تست «می‌سازیم» تا هیچ فایل باینری مجوزدار در گیت
// نباشد. سه پروفایل: sRGB-مانند (باید تصویر را عملاً دست‌نخورده بگذارد)،
// خاکستریِ گاما ۲.۲ (عدد مشخص می‌دهد) و Profile RGB (ماتریس متفاوت + تگ desc).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "atlas/icc.h"
#include "atlas/image.h"
#include "test.h"

namespace {

void put16(std::vector<uint8_t>* b, uint16_t v) {
  b->push_back(static_cast<uint8_t>(v >> 8));
  b->push_back(static_cast<uint8_t>(v & 0xFF));
}

void put32(std::vector<uint8_t>* b, uint32_t v) {
  b->push_back(static_cast<uint8_t>(v >> 24));
  b->push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  b->push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  b->push_back(static_cast<uint8_t>(v & 0xFF));
}

void put_s15f16(std::vector<uint8_t>* b, float v) {
  put32(b, static_cast<uint32_t>(static_cast<int32_t>(std::lround(v * 65536.0f))));
}

std::vector<uint8_t> xyz_tag(float x, float y, float z) {
  std::vector<uint8_t> t = {'X', 'Y', 'Z', ' ', 0, 0, 0, 0};
  put_s15f16(&t, x);
  put_s15f16(&t, y);
  put_s15f16(&t, z);
  return t;
}

std::vector<uint8_t> gamma_curv(float gamma) {
  std::vector<uint8_t> t = {'c', 'u', 'r', 'v', 0, 0, 0, 0};
  put32(&t, 1);                                    // یک ورودی
  put16(&t, static_cast<uint16_t>(std::lround(gamma * 256.0f)));  // u8Fixed8
  put16(&t, 0);
  return t;
}

std::vector<uint8_t> identity_curv() {
  std::vector<uint8_t> t = {'c', 'u', 'r', 'v', 0, 0, 0, 0};
  put32(&t, 0);  // identity
  return t;
}

// منحنی پارامتریک sRGB (para type 4 — تنها نوعی که هر دو قطعه را دارد):
// Y = ((X+0.055)/1.055)^2.4 برای X >= 0.04045، وگرنه Y = X/12.92
std::vector<uint8_t> srgb_para() {
  std::vector<uint8_t> t = {'p', 'a', 'r', 'a', 0, 0, 0, 0};
  put16(&t, 4);   // نوع ۴
  put16(&t, 0);   // رزرو
  put_s15f16(&t, 2.4f);
  put_s15f16(&t, 1.0f / 1.055f);
  put_s15f16(&t, 0.055f / 1.055f);
  put_s15f16(&t, 0.0f);
  put_s15f16(&t, 0.04045f);
  put_s15f16(&t, 1.0f / 12.92f);
  put_s15f16(&t, 0.0f);
  return t;
}

std::vector<uint8_t> desc_tag(const std::string& text) {
  std::vector<uint8_t> t = {'d', 'e', 's', 'c', 0, 0, 0, 0};
  put32(&t, static_cast<uint32_t>(text.size() + 1));
  t.insert(t.end(), text.begin(), text.end());
  t.push_back(0);
  return t;
}

// ماتریس sRGB (D65) همان اعدادی که در docs آمده است.
std::vector<uint8_t> make_profile(float m[9], const std::vector<uint8_t>& trc,
                                  const std::string& description) {
  std::vector<std::pair<const char*, std::vector<uint8_t>>> tags = {
      {"rXYZ", xyz_tag(m[0], m[1], m[2])},
      {"gXYZ", xyz_tag(m[3], m[4], m[5])},
      {"bXYZ", xyz_tag(m[6], m[7], m[8])},
      {"rTRC", trc},
      {"gTRC", trc},
      {"bTRC", trc},
      {"desc", desc_tag(description)},
  };

  std::vector<uint8_t> out(128, 0);
  out[12] = 'm'; out[13] = 'n'; out[14] = 't'; out[15] = 'r';  // کلاس: display
  out[16] = 'R'; out[17] = 'G'; out[18] = 'B'; out[19] = ' ';
  out[36] = 'a'; out[37] = 'c'; out[38] = 's'; out[39] = 'p';  // امضا

  put32(&out, static_cast<uint32_t>(tags.size()));
  const size_t table_end = out.size() + tags.size() * 12;
  size_t offset = table_end;
  std::vector<uint8_t> body;
  std::vector<std::pair<uint32_t, uint32_t>> refs;  // offset,size
  for (auto& [sig, data] : tags) {
    refs.emplace_back(static_cast<uint32_t>(offset), static_cast<uint32_t>(data.size()));
    body.insert(body.end(), data.begin(), data.end());
    offset += data.size();
    if (offset % 4) {
      const size_t pad = 4 - (offset % 4);
      body.insert(body.end(), pad, 0);
      offset += pad;
    }
  }
  for (size_t i = 0; i < tags.size(); ++i) {
    out.push_back(static_cast<uint8_t>(tags[i].first[0]));
    out.push_back(static_cast<uint8_t>(tags[i].first[1]));
    out.push_back(static_cast<uint8_t>(tags[i].first[2]));
    out.push_back(static_cast<uint8_t>(tags[i].first[3]));
    put32(&out, refs[i].first);
    put32(&out, refs[i].second);
  }
  out.insert(out.end(), body.begin(), body.end());
  return out;
}

// --- جبر کوچک ۳×۳ برای ساخت پروفایل D50 در تست ---
struct M3 { float v[9]; };
M3 mul(const M3& a, const M3& b) {
  M3 o{};
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c)
      o.v[r * 3 + c] = a.v[r * 3] * b.v[c] + a.v[r * 3 + 1] * b.v[3 + c] + a.v[r * 3 + 2] * b.v[6 + c];
  return o;
}
M3 inverse(const M3& m) {
  const float a = m.v[0], b = m.v[1], c = m.v[2], d = m.v[3], e = m.v[4], f = m.v[5],
              g = m.v[6], h = m.v[7], i = m.v[8];
  const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
  M3 o{};
  o.v[0] = (e * i - f * h) / det; o.v[1] = (c * h - b * i) / det; o.v[2] = (b * f - c * e) / det;
  o.v[3] = (f * g - d * i) / det; o.v[4] = (a * i - c * g) / det; o.v[5] = (c * d - a * f) / det;
  o.v[6] = (d * h - e * g) / det; o.v[7] = (b * g - a * h) / det; o.v[8] = (a * e - b * d) / det;
  return o;
}
M3 bradford(float sx, float sy, float sz, float dx, float dy, float dz) {
  const M3 b = {{0.8951f, 0.2664f, -0.1614f, -0.7502f, 1.7135f, 0.0367f, 0.0389f, -0.0685f, 1.0296f}};
  auto cone = [&](float x, float y, float z) {
    return std::array<float, 3>{b.v[0] * x + b.v[1] * y + b.v[2] * z,
                                b.v[3] * x + b.v[4] * y + b.v[5] * z,
                                b.v[6] * x + b.v[7] * y + b.v[8] * z};
  };
  const auto s = cone(sx, sy, sz);
  const auto d = cone(dx, dy, dz);
  const M3 scale = {{d[0] / s[0], 0, 0, 0, d[1] / s[1], 0, 0, 0, d[2] / s[2]}};
  return mul(inverse(b), mul(scale, b));
}

void srgb_matrix(float m[9]) {
  const float v[9] = {0.4124564f, 0.3575761f, 0.1804375f, 0.2126729f, 0.7151522f,
                      0.0721750f, 0.0193339f, 0.1191920f, 0.9503041f};
  std::memcpy(m, v, sizeof(v));
}

// PNG با یک chunk iCCP؛ zlib «stored» (بدون فشرده‌سازی) دستی نوشته می‌شود تا
// تست به کتابخانهٔ zlib وابسته نباشد.
std::vector<uint8_t> stored_zlib(const std::string& data) {
  std::vector<uint8_t> z = {0x78, 0x01};
  size_t i = 0;
  while (i < data.size()) {
    const size_t n = std::min<size_t>(65535, data.size() - i);
    z.push_back(i + n >= data.size() ? 0x01 : 0x00);  // BFINAL
    z.push_back(static_cast<uint8_t>(n & 0xFF));
    z.push_back(static_cast<uint8_t>((n >> 8) & 0xFF));
    const uint16_t nlen = static_cast<uint16_t>(~n);
    z.push_back(static_cast<uint8_t>(nlen & 0xFF));
    z.push_back(static_cast<uint8_t>((nlen >> 8) & 0xFF));
    z.insert(z.end(), data.begin() + static_cast<long>(i),
             data.begin() + static_cast<long>(i + n));
    i += n;
  }
  // adler32
  uint32_t a = 1, b = 0;
  for (unsigned char c : data) {
    a = (a + c) % 65521;
    b = (b + a) % 65521;
  }
  put32(&z, (b << 16) | a);
  return z;
}

uint32_t crc_table_entry(uint32_t n) {
  uint32_t c = n;
  for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
  return c;
}

uint32_t crc32(const uint8_t* data, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; ++i) {
    c = crc_table_entry((c ^ data[i]) & 0xFF) ^ (c >> 8);
  }
  return c ^ 0xFFFFFFFFu;
}

void append_chunk(std::vector<uint8_t>* out, const char* type, const std::vector<uint8_t>& data) {
  put32(out, static_cast<uint32_t>(data.size()));
  const size_t start = out->size();
  out->insert(out->end(), type, type + 4);
  out->insert(out->end(), data.begin(), data.end());
  put32(out, crc32(out->data() + start, out->size() - start));
}

std::filesystem::path write_bytes(const std::string& name, const std::vector<uint8_t>& bytes) {
  const auto path = std::filesystem::temp_directory_path() / name;
  std::FILE* f = std::fopen(path.string().c_str(), "wb");
  if (f) {
    std::fwrite(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
  }
  return path;
}

atlas::ImageF32 gray_image(float v) {
  atlas::ImageF32 img(8, 8, 3);
  img.fill(v);
  return img;
}

}  // namespace

ATLAS_TEST(icc_srgb_like_profile_is_almost_identity) {
  float m[9];
  srgb_matrix(m);
  const auto bytes = make_profile(m, srgb_para(), "sRGB test");
  const atlas::IccProfile p = atlas::parse_icc(bytes.data(), bytes.size());
  CHECK(p.valid);
  CHECK(p.color_space == "RGB ");
  CHECK(p.description == "sRGB test");

  atlas::ImageF32 img(4, 4, 3);
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      float* q = img.pixel(x, y);
      q[0] = static_cast<float>(x) / 3.0f;
      q[1] = static_cast<float>(y) / 3.0f;
      q[2] = 0.5f;
    }
  }
  const auto out = atlas::apply_icc_to_srgb(img, p);
  for (size_t i = 0; i < img.pixel_count() * 3; ++i) {
    CHECK_NEAR(out.data()[i], img.data()[i], 2e-3);
  }
}

ATLAS_TEST(icc_gamma_profile_applies_known_curve) {
  float m[9];
  srgb_matrix(m);
  const auto bytes = make_profile(m, gamma_curv(2.2f), "gamma 2.2");
  const atlas::IccProfile p = atlas::parse_icc(bytes.data(), bytes.size());
  CHECK(p.valid);
  CHECK_NEAR(p.gamma[0], 2.2f, 0.01);

  // ورودی ۰٫۵ در فضای این پروفایل = 0.5^2.2 = 0.2176 نور خطی؛ تبدیل به sRGB
  // تقریباً همان ۰٫۵ را برمی‌گرداند (چون خروجی sRGB است).
  const auto out = atlas::apply_icc_to_srgb(gray_image(0.5f), p);
  CHECK_NEAR(out.pixel(0, 0)[0], 0.504f, 5e-3);

  // ۰٫۲۵ در پروفایل گاما ۲.۲ تیره‌تر از sRGB رمزگشایی می‌شود، پس خروجی sRGB
  // باید کمی تیره‌تر از ورودی باشد (≈۰٫۲۴۱). این «کمی» همان چیزی است که کاربر
  // به عنوان «رنگ کم‌جان» گزارش می‌کند.
  const auto out2 = atlas::apply_icc_to_srgb(gray_image(0.25f), p);
  CHECK_NEAR(out2.pixel(0, 0)[0], 0.241f, 6e-3);
}

ATLAS_TEST(icc_identity_trc_means_linear_data) {
  // اگر پروفایل bTRC=identity باشد، داده «خطی» است نه sRGB-encoded؛ پس ۰٫۵ باید
  // در خروجی روشن‌تر شود (≈۰٫۷۳۵). این تست جلوی یک اشتباه رایج را می‌گیرد:
  // فرض کردن اینکه identity یعنی «هیچ تبدیلی لازم نیست».
  float m[9];
  srgb_matrix(m);
  const auto bytes = make_profile(m, identity_curv(), "linear-ish");
  const atlas::IccProfile p = atlas::parse_icc(bytes.data(), bytes.size());
  CHECK(p.valid);
  const auto out = atlas::apply_icc_to_srgb(gray_image(0.5f), p);
  CHECK_NEAR(out.pixel(0, 0)[0], 0.7354f, 5e-3);
}

ATLAS_TEST(icc_d50_white_point_keeps_neutral_gray_neutral) {
  // پروفایل ProPhoto-مانند: ماتریس سازگاری‌یافته با D50 + نقطهٔ سفید D50.
  // اگر سازگاری Bradford اعمال نشود، خاکستری رنگ می‌گیرد (زرد/سبز) — باگی که
  // فقط با چشم دیده می‌شود.
  float m[9];
  srgb_matrix(m);
  const M3 srgb = {{m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8]}};
  const M3 d65_to_d50 = bradford(0.9505f, 1.0f, 1.089f, 0.9642f, 1.0f, 0.8249f);
  const M3 d50 = mul(d65_to_d50, srgb);

  float mm[9];
  for (int i = 0; i < 9; ++i) mm[i] = d50.v[i];

  auto bytes = make_profile(mm, srgb_para(), "D50 profile");
  // تگ wtpt را باید جدا اضافه کنیم؛ make_profile آن را ندارد.
  // ساده‌تر: نقطهٔ سفید را از ماتریس نمی‌شود حدس زد، پس wtpt را به بایت‌ها
  // تزریق می‌کنیم: یک تگ جدید در انتها و اصلاح تعداد تگ‌ها کار پرخطری است،
  // بنابراین wtpt را با «تغییر یک تگ موجود» شبیه‌سازی می‌کنیم: desc را
  // با wtpt جایگزین می‌کنیم.
  const size_t count_off = 128;
  const uint32_t count = (bytes[count_off] << 24) | (bytes[count_off + 1] << 16) |
                         (bytes[count_off + 2] << 8) | bytes[count_off + 3];
  size_t desc_entry = 0;
  for (uint32_t i = 0; i < count; ++i) {
    const size_t e = 132 + i * 12;
    if (std::memcmp(bytes.data() + e, "desc", 4) == 0) { desc_entry = e; break; }
  }
  CHECK(desc_entry != 0);
  if (desc_entry == 0) return;
  const uint32_t off = (bytes[desc_entry + 4] << 24) | (bytes[desc_entry + 5] << 16) |
                       (bytes[desc_entry + 6] << 8) | bytes[desc_entry + 7];
  std::memcpy(bytes.data() + desc_entry, "wtpt", 4);
  auto write_s15 = [&](size_t at, float v) {
    const int32_t fixed = static_cast<int32_t>(std::lround(v * 65536.0f));
    bytes[at] = static_cast<uint8_t>(fixed >> 24);
    bytes[at + 1] = static_cast<uint8_t>((fixed >> 16) & 0xFF);
    bytes[at + 2] = static_cast<uint8_t>((fixed >> 8) & 0xFF);
    bytes[at + 3] = static_cast<uint8_t>(fixed & 0xFF);
  };
  if (bytes.size() < off + 20) return;
  std::memcpy(bytes.data() + off, "XYZ ", 4);
  write_s15(off + 8, 0.9642f);
  write_s15(off + 12, 1.0f);
  write_s15(off + 16, 0.8249f);

  const atlas::IccProfile p = atlas::parse_icc(bytes.data(), bytes.size());
  CHECK(p.valid);
  CHECK_NEAR(p.white_point[0], 0.9642f, 0.002);

  const auto out = atlas::apply_icc_to_srgb(gray_image(0.5f), p);
  const float* q = out.pixel(0, 0);
  CHECK(std::fabs(q[0] - q[1]) < 0.01f);
  CHECK(std::fabs(q[1] - q[2]) < 0.01f);
  CHECK_NEAR(q[0], 0.5f, 0.01);
}

ATLAS_TEST(icc_invalid_or_lut_profile_is_rejected) {
  const std::vector<uint8_t> short_bytes(64, 0);
  CHECK(!atlas::parse_icc(short_bytes.data(), short_bytes.size()).valid);

  // هدر درست ولی بدون تگ rXYZ (شبیه پروفایل LUT) → باید رد شود.
  std::vector<uint8_t> lut_like(140, 0);
  lut_like[36] = 'a'; lut_like[37] = 'c'; lut_like[38] = 's'; lut_like[39] = 'p';
  lut_like[16] = 'R'; lut_like[17] = 'G'; lut_like[18] = 'B'; lut_like[19] = ' ';
  lut_like[131] = 1;  // یک تگ
  const char* a2b0 = "A2B0";
  std::memcpy(lut_like.data() + 132, a2b0, 4);
  CHECK(!atlas::parse_icc(lut_like.data(), lut_like.size()).valid);

  const atlas::IccProfile invalid;
  const auto img = gray_image(0.3f);
  const auto out = atlas::apply_icc_to_srgb(img, invalid);
  CHECK_NEAR(out.pixel(0, 0)[0], 0.3f, 1e-9);
}

ATLAS_TEST(icc_p3_matrix_changes_saturated_color_toward_srgb) {
  // ماتریس تقریبی Display P3 (D65) — فقط برای بررسی جهت تبدیل.
  float m[9] = {0.4865709f, 0.2656677f, 0.1982173f, 0.2289746f, 0.6917385f, 0.0792869f,
                0.0000000f, 0.0451134f, 1.0439444f};
  const auto bytes = make_profile(m, gamma_curv(2.2f), "P3-like");
  const atlas::IccProfile p = atlas::parse_icc(bytes.data(), bytes.size());
  CHECK(p.valid);

  atlas::ImageF32 red(2, 2, 3);
  for (size_t i = 0; i < red.pixel_count(); ++i) {
    red.data()[i * 3 + 0] = 1.0f;
    red.data()[i * 3 + 1] = 0.0f;
    red.data()[i * 3 + 2] = 0.0f;
  }
  const auto out = atlas::apply_icc_to_srgb(red, p);
  // قرمز خالص P3 در sRGB خارج از gamut است → قرمز می‌ماند و سبز/آبی کمی بالا می‌آید.
  CHECK(out.pixel(0, 0)[0] > 0.95f);
  CHECK(out.pixel(0, 0)[1] >= 0.0f);
  CHECK(out.pixel(0, 0)[1] < 0.2f);
  CHECK(out.pixel(0, 0)[2] <= 0.05f);
}

ATLAS_TEST(icc_png_chunk_and_jpeg_app2_are_read) {
  float m[9];
  srgb_matrix(m);
  const auto profile_bytes = make_profile(m, srgb_para(), "chunk test");
  const std::string profile_string(profile_bytes.begin(), profile_bytes.end());

  // --- PNG با iCCP ---
  std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  std::vector<uint8_t> ihdr;
  put32(&ihdr, 1); put32(&ihdr, 1);
  ihdr.push_back(8); ihdr.push_back(2); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
  append_chunk(&png, "IHDR", ihdr);

  std::vector<uint8_t> iccp = {'t', 'e', 's', 't', 0, 0};  // نام + null + متد
  const auto z = stored_zlib(profile_string);
  iccp.insert(iccp.end(), z.begin(), z.end());
  append_chunk(&png, "iCCP", iccp);
  append_chunk(&png, "IEND", {});
  const auto png_path = write_bytes("atlas_icc_test.png", png);
  CHECK(atlas::read_png_icc_profile(png_path.string()) == profile_string);
  CHECK(atlas::read_icc_profile(png_path.string()) == profile_string);
  std::filesystem::remove(png_path);

  // --- JPEG با APP2 در دو تکه ---
  const size_t half = profile_string.size() / 2;
  std::vector<uint8_t> jpg = {0xFF, 0xD8};
  auto append_app2 = [&](int seq, int total, const std::string& part) {
    std::vector<uint8_t> payload = {'I', 'C', 'C', '_', 'P', 'R', 'O', 'F', 'I', 'L', 'E', 0,
                                    static_cast<uint8_t>(seq), static_cast<uint8_t>(total)};
    payload.insert(payload.end(), part.begin(), part.end());
    jpg.push_back(0xFF);
    jpg.push_back(0xE2);
    const uint16_t len = static_cast<uint16_t>(payload.size() + 2);
    jpg.push_back(static_cast<uint8_t>(len >> 8));
    jpg.push_back(static_cast<uint8_t>(len & 0xFF));
    jpg.insert(jpg.end(), payload.begin(), payload.end());
  };
  append_app2(1, 2, profile_string.substr(0, half));
  append_app2(2, 2, profile_string.substr(half));
  jpg.push_back(0xFF);
  jpg.push_back(0xD9);
  const auto jpg_path = write_bytes("atlas_icc_test.jpg", jpg);
  CHECK(atlas::read_jpeg_icc_profile(jpg_path.string()) == profile_string);
  std::filesystem::remove(jpg_path);

  // تکهٔ گم‌شده → پروفایل ناقص، استفاده نمی‌شود.
  std::vector<uint8_t> broken = {0xFF, 0xD8};
  const uint16_t len = static_cast<uint16_t>(12 + 2 + half + 2);
  broken.push_back(0xFF); broken.push_back(0xE2);
  broken.push_back(static_cast<uint8_t>(len >> 8));
  broken.push_back(static_cast<uint8_t>(len & 0xFF));
  const char* sig = "ICC_PROFILE";
  broken.insert(broken.end(), sig, sig + 11);
  broken.push_back(0);
  broken.push_back(1);  // seq
  broken.push_back(3);  // total = 3 اما فقط یکی هست
  broken.insert(broken.end(), profile_string.begin(), profile_string.begin() + static_cast<long>(half));
  const auto broken_path = write_bytes("atlas_icc_broken.jpg", broken);
  CHECK(atlas::read_jpeg_icc_profile(broken_path.string()).empty());
  std::filesystem::remove(broken_path);
}

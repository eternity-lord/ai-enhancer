// SPDX-License-Identifier: Apache-2.0
// یادداشت: کدک موقت PPM فقط برای این است که P0 بدون وابستگی خارجی «قابل اجرا» باشد.
// در P0-05/P0-06 با libjpeg-turbo/libpng/libwebp جایگزین می‌شود و این فایل
// برای تست‌های سریع باقی می‌ماند (نه برای کاربر نهایی).
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "atlas/image.h"

namespace atlas {
namespace {

bool read_token(std::istream& in, std::string* token) {
  token->clear();
  char ch = 0;
  // skip whitespace and comments
  while (in.get(ch)) {
    if (ch == '#') {
      std::string ignored;
      std::getline(in, ignored);
      continue;
    }
    if (!std::isspace(static_cast<unsigned char>(ch))) break;
  }
  if (!in) return false;
  do {
    if (ch == '#' || std::isspace(static_cast<unsigned char>(ch))) {
      in.unget();  // delimiter is consumed by the caller
      break;
    }
    token->push_back(ch);
  } while (in.get(ch));
  return !token->empty();
}

}  // namespace

bool read_ppm(const std::string& path, ImageF32* out, std::string* error) {
  if (!out) {
    if (error) *error = "null output";
    return false;
  }
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    if (error) *error = "cannot open " + path;
    return false;
  }

  std::string magic, sw, sh, smax;
  if (!read_token(in, &magic) || magic != "P6") {
    if (error) *error = "not a binary PPM (P6)";
    return false;
  }
  if (!read_token(in, &sw) || !read_token(in, &sh) || !read_token(in, &smax)) {
    if (error) *error = "truncated PPM header";
    return false;
  }

  const int w = std::stoi(sw);
  const int h = std::stoi(sh);
  const int maxval = std::stoi(smax);
  if (w <= 0 || h <= 0 || maxval <= 0 || maxval > 255) {
    if (error) *error = "unsupported PPM dimensions/maxval";
    return false;
  }

  // exactly one whitespace follows maxval; handle CRLF explicitly
  if (in.get() == '\r' && in.peek() == '\n') in.get();
  std::vector<unsigned char> bytes(static_cast<size_t>(w) * h * 3);
  in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (in.gcount() != static_cast<std::streamsize>(bytes.size())) {
    if (error) *error = "truncated PPM payload";
    return false;
  }

  ImageF32 img(w, h, 3);
  const float inv = 1.0f / static_cast<float>(maxval);
  for (size_t i = 0; i < bytes.size(); ++i) {
    img.data()[i] = static_cast<float>(bytes[i]) * inv;
  }
  *out = std::move(img);
  return true;
}

bool write_ppm(const std::string& path, const ImageF32& img, std::string* error) {
  if (img.empty() || img.channels() < 3) {
    if (error) *error = "PPM writer needs RGB image";
    return false;
  }
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    if (error) *error = "cannot write " + path;
    return false;
  }
  out << "P6\n" << img.width() << " " << img.height() << "\n255\n";

  std::vector<unsigned char> bytes(static_cast<size_t>(img.width()) * img.height() * 3);
  for (size_t i = 0; i < img.pixel_count(); ++i) {
    for (int ch = 0; ch < 3; ++ch) {
      const float v = img.data()[i * img.channels() + ch];
      const float clamped = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
      bytes[i * 3 + ch] = static_cast<unsigned char>(clamped * 255.0f + 0.5f);
    }
  }
  out.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(out);
}

}  // namespace atlas

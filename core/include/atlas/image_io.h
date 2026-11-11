// SPDX-License-Identifier: Apache-2.0
// یادداشت: IO تصویر با stb. مقادیر در [0,1] و در فضای sRGB-encoded نگه داشته
// می‌شوند (مدل‌های SR روی همین فضا آموزش دیده‌اند؛ خطی‌سازی در P0-07 جدا خواهد شد).
#pragma once

#include <string>

#include "atlas/types.h"

namespace atlas {

bool load_image(const std::string& path, ImageF32* out, std::string* error);
bool save_image(const std::string& path, const ImageF32& img, int jpeg_quality,
                std::string* error);

}  // namespace atlas

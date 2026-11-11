// SPDX-License-Identifier: Apache-2.0
// یادداشت: چرا EXIF مهم است؟ عکس‌های موبایل تقریباً همیشه orientation ≠ 1 دارند.
// اگر نادیده گرفته شود، چهرهٔ کاربر ۹۰ درجه چرخیده تحویل داده می‌شود و کاربر
// فکر می‌کند برنامه خراب است. این ماژول فقط tag 0x0112 را می‌خواند؛ به کتابخانهٔ
// EXIF کامل نیاز نداریم.
#pragma once

#include <string>

#include "atlas/image.h"

namespace atlas {

// مقدار ۱..۸ (طبق EXIF)؛ ۰ یعنی «پیدا نشد / JPEG نیست / فایل خوانده نشد».
int read_exif_orientation(const std::string& path);

// تبدیل پیکسلی تصویر به جهت درست نمایش. برای ۵..۸ ابعاد جابه‌جا می‌شوند.
ImageF32 apply_orientation(const ImageF32& src, int orientation);

}  // namespace atlas

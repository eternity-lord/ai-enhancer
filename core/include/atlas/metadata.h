// SPDX-License-Identifier: Apache-2.0
// یادداشت: «وفاداری فایل» یعنی خروجی هم رنگ درست داشته باشد و هم بگوید در چه
// فضایی است. دو کار اینجا انجام می‌شود:
//  ۱) انتقال EXIF دوربین از مبدأ به مقصد (اختیاری، چون GPS دادهٔ خصوصی است).
//  ۲) جاسازی پروفایل sRGB در خروجی، چون ما پیکسل‌ها را به sRGB تبدیل کرده‌ایم
//     و فایل بدون برچسب، در نرم‌افزارهای مدیریت رنگ «نامعلوم» دیده می‌شود.
#pragma once

#include <string>

namespace atlas {

// پروفایل sRGB معتبر (v2، ماتریسی + para type 4 + desc) به‌صورت بایت‌های ICC.
std::string make_srgb_icc_profile();

// کپی EXIF از مبدأ به مقصد. اگر مبدأ EXIF نداشته باشد، false با پیام روشن
// («چیزی برای کپی نبود») — این خطا نیست، ولی نباید بی‌صدا موفق گزارش شود.
bool keep_exif_metadata(const std::string& src_path, const std::string& dst_path,
                        std::string* error);

// جاسازی پروفایل sRGB در خروجی PNG (iCCP) یا JPEG (APP2).
bool tag_srgb_profile(const std::string& dst_path, std::string* error);

// آیا فایل مبدأ EXIF قابل انتقال دارد؟
bool has_exif_metadata(const std::string& path);

}  // namespace atlas

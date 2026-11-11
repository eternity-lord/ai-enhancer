// SPDX-License-Identifier: Apache-2.0
// یادداشت: کدک‌ها (stb) مقدار sRGB-encoded می‌دهند، ولی مدل‌های SR و همهٔ
// عملیات نویز/شارپ روی نور خطی معنا دارند. این فایل مرز بین این دو دنیاست.
// آلفا هرگز تبدیل نمی‌شود (آلفا اصلاً رنگ نیست).
#pragma once

#include "atlas/image.h"

namespace atlas {

// پروفایل دقیق sRGB (IEC 61966-2-1): قطعهٔ خطی + پخش 2.4.
float srgb_to_linear(float v);
float linear_to_srgb(float v);

// روی کانال‌های رنگ اعمال می‌شود؛ کانال چهارم (آلفا) دست‌نخورده می‌ماند.
ImageF32 to_linear(const ImageF32& srgb);
ImageF32 to_srgb(const ImageF32& linear);

}  // namespace atlas

// SPDX-License-Identifier: Apache-2.0
// یادداشت: قرارداد مراحل تصویر. هر تابع ورودی را تغییر نمی‌دهد و خروجی نو می‌سازد
// تا pipeline بتواند نسخهٔ اصلی را برای مقایسه/QA نگه دارد.
#pragma once

#include <string>

#include "atlas/types.h"

namespace atlas {

// --- tiling (P0-08) ---
// تقسیم تصویر به کاشی‌های هم‌پوشان؛ آخرین کاشی در هر محور به لبه چسبانده می‌شود
// تا هیچ پیکسلی بیرون نماند و اندازهٔ هیچ کاشی از tile بیشتر نشود.
TilePlan plan_tiles(int width, int height, int tile, int overlap);

// --- classical fallback (P0-10) ---
// Lanczos (a=3) separable upscale; ورودی/خروجی float32 در [0,1].
ImageF32 resize_lanczos(const ImageF32& src, int out_width, int out_height,
                        float a = 3.0f);

// Unsharp mask with separable Gaussian; amount=0 => unchanged copy.
ImageF32 unsharp(const ImageF32& src, float amount, float sigma = 1.0f);

// Gaussian blur (exposed for tests / detail stage).
ImageF32 gaussian_blur(const ImageF32& src, float sigma);

// --- io/ppm (temporary until real codecs) ---
bool read_ppm(const std::string& path, ImageF32* out, std::string* error);
bool write_ppm(const std::string& path, const ImageF32& img, std::string* error);

}  // namespace atlas

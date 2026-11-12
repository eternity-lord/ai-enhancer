// SPDX-License-Identifier: Apache-2.0
// یادداشت: تست‌های Light AI «رفتار» را قفل می‌کنند، نه یک عدد ثابت: خاموشیِ
// کامل باید تصویر را دست‌نخورده برگرداند، هر اسلایدر باید در جهت خودش و
// *محدود به ناحیهٔ خودش* اثر بگذارد (سایه روی سایه، هایلایت روی روشن)، منحنی
// باید یکنوا بماند (هیچ روشنایی‌ای وارونه نشود) و آلفا هرگز تغییر نکند.
#include <algorithm>
#include <cmath>
#include <vector>

#include "atlas/light.h"
#include "test.h"

namespace {

atlas::ImageF32 solid(int w, int h, float r, float g, float b, float a = -1.0f) {
  const int channels = a >= 0.0f ? 4 : 3;
  atlas::ImageF32 img(w, h, channels);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      float* p = img.pixel(x, y);
      p[0] = r;
      p[1] = g;
      p[2] = b;
      if (channels == 4) p[3] = a;
    }
  }
  return img;
}

// نوار خاکستری ۰..۱ برای تست یکنوایی منحنی.
atlas::ImageF32 gray_ramp(int width = 256) {
  atlas::ImageF32 img(width, 1, 3);
  for (int x = 0; x < width; ++x) {
    const float v = static_cast<float>(x) / static_cast<float>(width - 1);
    float* p = img.pixel(x, 0);
    p[0] = p[1] = p[2] = v;
  }
  return img;
}

atlas::ImageF32 dark_gradient(int w = 64, int h = 64) {
  atlas::ImageF32 img(w, h, 3);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const float v = 0.02f + 0.25f * static_cast<float>(x) / static_cast<float>(w - 1);
      float* p = img.pixel(x, y);
      p[0] = v * 0.9f;
      p[1] = v;
      p[2] = v * 1.1f;
    }
  }
  return img;
}

atlas::ImageF32 blue_cast(int w = 48, int h = 48) {
  atlas::ImageF32 img(w, h, 3);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      float* p = img.pixel(x, y);
      p[0] = 0.25f;
      p[1] = 0.35f;
      p[2] = 0.62f;  // آبی غالب
    }
  }
  return img;
}

float max_abs_diff(const atlas::ImageF32& a, const atlas::ImageF32& b) {
  float worst = 0.0f;
  for (size_t i = 0; i < a.pixel_count() * static_cast<size_t>(a.channels()); ++i) {
    worst = std::max(worst, std::fabs(a.data()[i] - b.data()[i]));
  }
  return worst;
}

float luma_at(const atlas::ImageF32& img, int x, int y) {
  const float* p = img.pixel(x, y);
  return 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
}

}  // namespace

ATLAS_TEST(light_neutral_is_bit_exact) {
  const atlas::ImageF32 src = dark_gradient();
  atlas::LightOptions opt;  // همه صفر
  CHECK(atlas::light_is_neutral(opt));
  const atlas::ImageF32 out = atlas::adjust_light(src, opt);
  CHECK(max_abs_diff(src, out) == 0.0f);
}

ATLAS_TEST(light_neutral_flag_rejects_any_work) {
  atlas::LightOptions opt;
  opt.auto_tone = true;
  CHECK(!atlas::light_is_neutral(opt));
  atlas::LightOptions opt2;
  opt2.shadows = 0.01f;
  CHECK(!atlas::light_is_neutral(opt2));
}

ATLAS_TEST(light_alpha_is_untouched) {
  const atlas::ImageF32 src = solid(8, 8, 0.2f, 0.4f, 0.6f, 0.37f);
  atlas::LightOptions opt;
  opt.exposure = 0.8f;
  opt.saturation = 0.5f;
  opt.highlights = -0.4f;
  const atlas::ImageF32 out = atlas::adjust_light(src, opt);
  CHECK(out.channels() == 4);
  for (size_t i = 0; i < out.pixel_count(); ++i) {
    CHECK_NEAR(out.data()[i * 4 + 3], 0.37, 1e-7);
  }
}

ATLAS_TEST(light_exposure_raises_mean) {
  const atlas::ImageF32 src = dark_gradient();
  atlas::LightOptions opt;
  opt.exposure = 1.0f;
  atlas::LightReport rep;
  const atlas::ImageF32 out = atlas::adjust_light(src, opt, &rep);
  CHECK(rep.after.mean > rep.before.mean + 0.05);
  CHECK(rep.seconds >= 0.0);
}

ATLAS_TEST(light_shadows_affect_dark_more_than_bright) {
  const atlas::ImageF32 src = gray_ramp();
  atlas::LightOptions opt;
  opt.shadows = 0.6f;
  const atlas::ImageF32 out = atlas::adjust_light(src, opt);
  const float dark_delta = luma_at(out, 26, 0) - luma_at(src, 26, 0);    // ~۰٫۱
  const float bright_delta = luma_at(out, 230, 0) - luma_at(src, 230, 0); // ~۰٫۹
  CHECK(dark_delta > 0.01f);
  CHECK(dark_delta > bright_delta * 2.0f);
  // سیاه و سفید مطلق باید سر جای خودشان بمانند.
  CHECK_NEAR(luma_at(out, 0, 0), 0.0, 1e-6);
  CHECK_NEAR(luma_at(out, 255, 0), 1.0, 1e-6);
}

ATLAS_TEST(light_highlights_affect_bright_more_than_mid) {
  const atlas::ImageF32 src = gray_ramp();
  atlas::LightOptions opt;
  opt.highlights = -0.6f;
  const atlas::ImageF32 out = atlas::adjust_light(src, opt);
  const float bright_delta = luma_at(out, 220, 0) - luma_at(src, 220, 0);  // ~۰٫۸۶
  const float mid_delta = luma_at(out, 128, 0) - luma_at(src, 128, 0);     // ~۰٫۵
  CHECK(bright_delta < -0.02f);
  CHECK(std::fabs(bright_delta) > std::fabs(mid_delta) * 2.0f);
  CHECK_NEAR(luma_at(out, 255, 0), 1.0, 1e-6);
}

ATLAS_TEST(light_contrast_pushes_out_and_keeps_mid) {
  const atlas::ImageF32 src = gray_ramp();
  atlas::LightOptions opt;
  opt.contrast = 0.5f;
  const atlas::ImageF32 out = atlas::adjust_light(src, opt);
  CHECK(luma_at(out, 51, 0) < luma_at(src, 51, 0));    // ~۰٫۲
  CHECK(luma_at(out, 204, 0) > luma_at(src, 204, 0));  // ~۰٫۸
  CHECK_NEAR(luma_at(out, 128, 0), luma_at(src, 128, 0), 0.01);
}

ATLAS_TEST(light_tone_curve_stays_monotonic) {
  const atlas::ImageF32 src = gray_ramp();
  atlas::LightOptions opt;
  opt.shadows = 0.7f;
  opt.highlights = -0.7f;
  opt.contrast = 0.6f;
  const atlas::ImageF32 out = atlas::adjust_light(src, opt);
  float previous = -1.0f;
  for (int x = 0; x < out.width(); ++x) {
    const float value = luma_at(out, x, 0);
    CHECK(value >= previous - 1e-4f);
    previous = value;
  }
}

ATLAS_TEST(light_saturation_down_is_gray_up_is_stronger) {
  const atlas::ImageF32 src = solid(16, 16, 0.75f, 0.35f, 0.25f);
  atlas::LightOptions gray;
  gray.saturation = -1.0f;
  const atlas::ImageF32 out_gray = atlas::adjust_light(src, gray);
  const float* p = out_gray.pixel(4, 4);
  CHECK_NEAR(p[0], p[1], 1e-4);
  CHECK_NEAR(p[1], p[2], 1e-4);

  atlas::LightOptions vivid;
  vivid.saturation = 0.6f;
  const atlas::ImageF32 out_vivid = atlas::adjust_light(src, vivid);
  const float* q = out_vivid.pixel(4, 4);
  const float before_span = 0.75f - 0.25f;
  const float after_span = q[0] - q[2];
  CHECK(after_span > before_span);
  CHECK(q[0] <= 1.0f && q[2] >= 0.0f);
}

ATLAS_TEST(light_warmth_shifts_red_blue_ratio) {
  const atlas::ImageF32 src = solid(8, 8, 0.5f, 0.5f, 0.5f);
  atlas::LightOptions warm;
  warm.warmth = 1.0f;
  const atlas::ImageF32 out_warm = atlas::adjust_light(src, warm);
  const float* w = out_warm.pixel(2, 2);
  CHECK(w[0] > w[2] * 1.05f);

  atlas::LightOptions cold;
  cold.warmth = -1.0f;
  const atlas::ImageF32 out_cold = atlas::adjust_light(src, cold);
  const float* c = out_cold.pixel(2, 2);
  CHECK(c[2] > c[0] * 1.05f);
}

ATLAS_TEST(light_auto_tone_lifts_underexposed_image) {
  const atlas::ImageF32 src = dark_gradient();
  atlas::LightOptions opt;
  opt.auto_tone = true;
  atlas::LightReport rep;
  const atlas::ImageF32 out = atlas::adjust_light(src, opt, &rep);
  CHECK(rep.tone_applied);
  CHECK(rep.auto_exposure_ev > 0.0);
  CHECK(rep.after.p50 > rep.before.p50 + 0.05);
  CHECK(rep.after.mean > rep.before.mean);
}

ATLAS_TEST(light_auto_white_balance_reduces_cast) {
  const atlas::ImageF32 src = blue_cast();
  const atlas::LightStats before = atlas::measure_light(src);
  atlas::LightOptions opt;
  opt.auto_white_balance = true;
  atlas::LightReport rep;
  const atlas::ImageF32 out = atlas::adjust_light(src, opt, &rep);
  CHECK(rep.white_balance_applied);
  CHECK(std::fabs(rep.wb_red_gain - rep.wb_blue_gain) > 0.02);  // گین‌ها واقعاً فرق دارند
  const float* p = out.pixel(8, 8);
  const double spread_after = std::max({p[0], p[1], p[2]}) - std::min({p[0], p[1], p[2]});
  const double spread_before = 0.62 - 0.25;
  CHECK(spread_after < spread_before);
  CHECK(atlas::measure_light(out).mean > before.mean * 0.6);  // روشنایی فرو نریخت
}

ATLAS_TEST(light_coverage_report_is_populated) {
  const atlas::ImageF32 src = gray_ramp();
  const atlas::LightStats stats = atlas::measure_light(src);
  CHECK_NEAR(stats.p01, 0.01, 0.01);
  CHECK_NEAR(stats.p50, 0.5, 0.01);
  CHECK_NEAR(stats.p99, 0.99, 0.01);
  CHECK(stats.clip_low > 0.0);
  CHECK(stats.clip_high > 0.0);
  CHECK(stats.contrast > 0.2);
}

ATLAS_TEST(light_output_never_leaves_unit_range) {
  const atlas::ImageF32 src = solid(32, 32, 0.99f, 0.5f, 0.01f);
  atlas::LightOptions opt;
  opt.exposure = 2.0f;
  opt.contrast = 1.0f;
  opt.saturation = 1.0f;
  opt.shadows = 1.0f;
  opt.highlights = 1.0f;
  opt.warmth = 1.0f;
  const atlas::ImageF32 out = atlas::adjust_light(src, opt);
  for (size_t i = 0; i < out.pixel_count() * 3; ++i) {
    CHECK(out.data()[i] >= 0.0f && out.data()[i] <= 1.0f);
  }
}

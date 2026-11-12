// SPDX-License-Identifier: Apache-2.0
// یادداشت: این تست‌ها «جهت درست» دروازه را چک می‌کنند، نه دقت ArcFace.
// مهم‌ترین تست: تصویر یکسان باید راحت پاس شود، و تغییر فاحش (محو شدید یا
// رنگ‌عوض‌شده) باید بیفتد — وگرنه دروازه عملاً «همیشه سبز» است.
#include <cmath>

#include "atlas/identity.h"
#include "atlas/image.h"
#include "atlas/sr.h"
#include "test.h"

namespace {

// «چهرهٔ ساختگی»: بیضی + دو چشم + دهان، با مقدارهای مختلف. برای تستِ دروازه
// کافی است الگوی هندسی وجود داشته باشد.
atlas::ImageF32 fake_face_variant(int w, int h, float eye_offset, float mouth_offset,
                                  float tint) {
  atlas::ImageF32 img(w, h, 3);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      float* p = img.pixel(x, y);
      const float fx = (x - w * 0.5f) / (w * 0.5f);
      const float fy = (y - h * 0.5f) / (h * 0.5f);
      float v = 0.55f + 0.25f * std::exp(-(fx * fx * 3.0f + fy * fy * 2.0f));
      // چشم‌ها
      for (int s = -1; s <= 1; s += 2) {
        const float ex = fx - eye_offset * static_cast<float>(s);
        const float ey = fy + 0.2f;
        v -= 0.35f * std::exp(-(ex * ex * 90.0f + ey * ey * 120.0f));
      }
      // دهان
      const float mx = fx;
      const float my = fy - mouth_offset;
      v -= 0.25f * std::exp(-(mx * mx * 12.0f + my * my * 150.0f));
      p[0] = std::min(1.0f, std::max(0.0f, v * (1.0f + tint)));
      p[1] = std::min(1.0f, std::max(0.0f, v * 0.85f));
      p[2] = std::min(1.0f, std::max(0.0f, v * 0.75f));
    }
  }
  return img;
}

atlas::ImageF32 fake_face(int w, int h) { return fake_face_variant(w, h, 0.35f, 0.35f, 0.0f); }

// «آدم دیگر»: چشم‌ها دورتر، دهان پایین‌تر، پوست تیره‌تر.
atlas::ImageF32 other_face(int w, int h) { return fake_face_variant(w, h, 0.55f, 0.15f, -0.06f); }

atlas::ImageF32 blur3(const atlas::ImageF32& src) {
  atlas::ImageF32 out(src.width(), src.height(), src.channels());
  for (int y = 0; y < src.height(); ++y) {
    for (int x = 0; x < src.width(); ++x) {
      for (int c = 0; c < src.channels(); ++c) {
        float sum = 0.0f;
        int n = 0;
        for (int dy = -2; dy <= 2; ++dy) {
          for (int dx = -2; dx <= 2; ++dx) {
            const int sx = std::min(src.width() - 1, std::max(0, x + dx));
            const int sy = std::min(src.height() - 1, std::max(0, y + dy));
            sum += src.pixel(sx, sy)[c];
            ++n;
          }
        }
        out.pixel(x, y)[c] = sum / static_cast<float>(n);
      }
    }
  }
  return out;
}

}  // namespace

ATLAS_TEST(identity_identical_face_passes_easily) {
  const auto face = fake_face(240, 240);
  atlas::IdentityOptions opt;  // کل تصویر
  const auto r = atlas::identity_gate(face, face, opt);
  CHECK(r.valid);
  CHECK(r.score > 0.98);
  CHECK(r.face_px == 240);
  CHECK_NEAR(r.threshold, 0.85, 1e-9);         // آستانهٔ پروکسی، چهرهٔ بزرگ
  CHECK_NEAR(r.arcface_threshold, 0.70, 1e-9);  // آستانهٔ docs/15 برای ArcFace
  CHECK(r.pass);
}

ATLAS_TEST(identity_reference_mode_is_stricter) {
  CHECK_NEAR(atlas::proxy_threshold(200, false), 0.85, 1e-9);
  CHECK_NEAR(atlas::proxy_threshold(200, true), 0.92, 1e-9);
  CHECK_NEAR(atlas::proxy_threshold(120, false), 0.82, 1e-9);
  CHECK_NEAR(atlas::proxy_threshold(64, false), 0.78, 1e-9);
  CHECK_NEAR(atlas::identity_threshold(200, false), 0.70, 1e-9);
  CHECK_NEAR(atlas::identity_threshold(200, true), 0.80, 1e-9);
  CHECK_NEAR(atlas::identity_threshold(120, false), 0.65, 1e-9);
  CHECK_NEAR(atlas::identity_threshold(120, true), 0.78, 1e-9);
  CHECK_NEAR(atlas::identity_threshold(64, false), 0.60, 1e-9);
  CHECK_NEAR(atlas::identity_threshold(64, true), 0.75, 1e-9);
}

ATLAS_TEST(identity_detail_loss_lowers_score_but_keeps_identity) {
  // تفکیک مهم: «صاف شدن» جزئیات، هویت را عوض نمی‌کند. این دروازه دربارهٔ هویت
  // است، نه شارپنس؛ کیفیت/آرتیفکت با متریک جداگانه سنجیده می‌شود (docs/19).
  const auto face = fake_face(240, 240);
  const auto tiny = atlas::resize_lanczos(face, 24, 24);
  const auto soft = atlas::resize_lanczos(tiny, 240, 240);

  atlas::IdentityOptions opt;
  const auto r = atlas::identity_gate(face, soft, opt);
  const auto same = atlas::identity_gate(face, face, opt);
  CHECK(r.valid);
  CHECK(r.score < same.score);
  CHECK(r.score > 0.95);  // افت جزئی، نه سقوط
  CHECK(r.pass);          // صاف شدن جزئیات، هویت را عوض نمی‌کند
}

ATLAS_TEST(identity_different_face_fails_the_gate) {
  // جابه‌جایی چشم/دهان + رنگ پوست متفاوت = «آدم دیگر»؛ دروازه باید ببندد.
  const auto face = fake_face(240, 240);
  const auto other = other_face(240, 240);
  atlas::IdentityOptions opt;
  const auto r = atlas::identity_gate(face, other, opt);
  CHECK(r.valid);
  CHECK(r.score < r.threshold);
  CHECK(!r.pass);
}

ATLAS_TEST(identity_color_shift_lowers_score) {
  auto shifted = fake_face(240, 240);
  for (size_t i = 0; i < shifted.pixel_count(); ++i) {
    shifted.data()[i * 3 + 0] = std::min(1.0f, shifted.data()[i * 3 + 0] + 0.25f);
    shifted.data()[i * 3 + 1] *= 0.5f;
    shifted.data()[i * 3 + 2] *= 0.5f;
  }
  atlas::IdentityOptions opt;
  const auto base = atlas::identity_gate(fake_face(240, 240), fake_face(240, 240), opt);
  const auto r = atlas::identity_gate(fake_face(240, 240), shifted, opt);
  CHECK(r.valid);
  CHECK(r.color < base.color);
  CHECK(r.score < base.score);
}

ATLAS_TEST(identity_uses_face_box_for_small_faces) {
  // چهرهٔ ۴۰ پیکسلی داخل یک تصویر بزرگ: آستانه باید ۰٫۶۰ باشد و face_px ~۴۰.
  auto canvas = fake_face(400, 400);
  const atlas::NormBox box{0.4f, 0.4f, 0.1f, 0.1f};
  atlas::IdentityOptions opt;
  opt.face = box;
  const auto r = atlas::identity_gate(canvas, canvas, opt);
  CHECK(r.valid);
  CHECK(r.face_px >= 38 && r.face_px <= 40);
  CHECK_NEAR(r.threshold, 0.78, 1e-9);        // پروکسی، چهرهٔ کوچک
  CHECK_NEAR(r.arcface_threshold, 0.60, 1e-9);
}

ATLAS_TEST(identity_empty_input_is_invalid) {
  atlas::ImageF32 empty;
  const auto r = atlas::identity_gate(empty, empty, atlas::IdentityOptions{});
  CHECK(!r.valid);
  CHECK(!r.note.empty());
}

ATLAS_TEST(identity_shape_signature_is_normalized) {
  const auto sig = atlas::shape_signature(fake_face(112, 112));
  double norm = 0.0;
  for (double v : sig) norm += v * v;
  CHECK(norm > 0.0);
  // نرمال‌سازی در هر سلول: مجموع مربع‌ها ≤ ۴×۴ (سلول‌های خالی صفر می‌مانند)
  CHECK(norm <= 16.0 + 1e-6);
}

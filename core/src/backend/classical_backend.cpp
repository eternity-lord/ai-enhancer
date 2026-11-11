// SPDX-License-Identifier: Apache-2.0
// یادداشت: بک‌اند کلاسیک همان fallback همیشگی است؛ اما حالا از مسیر کاشی‌ای
// و دوخت نرم عبور می‌کند تا تفاوتش با مسیر مدل فقط در «کیفیت» باشد، نه در «صحت».
#include <memory>

#include "atlas/backend.h"
#include "atlas/image.h"

namespace atlas {
namespace {

class ClassicalBackend final : public IBackend {
 public:
  ClassicalBackend(int scale, float sharpen) : scale_(scale), sharpen_(sharpen) {}

  const char* name() const override { return "classical"; }
  int scale() const override { return scale_; }
  bool ok() const override { return scale_ >= 1; }

  ImageF32 run(const ImageF32& input) override {
    if (input.empty()) return ImageF32();
    ImageF32 out = resize_lanczos(input, input.width() * scale_, input.height() * scale_);
    if (sharpen_ > 0.0f) out = unsharp(out, sharpen_, 1.0f);
    return out;
  }

 private:
  int scale_;
  float sharpen_;
};

}  // namespace

std::unique_ptr<IBackend> make_classical_backend(int scale, float sharpen) {
  if (scale < 1 || scale > 16) return nullptr;
  return std::make_unique<ClassicalBackend>(scale, sharpen);
}

}  // namespace atlas

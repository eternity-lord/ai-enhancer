// SPDX-License-Identifier: Apache-2.0
// یادداشت: رابط بک‌اند استنتاج. هسته فقط این را می‌شناسد؛ ncnn/ORT/Metal پشت آن
// پنهان‌اند. هر بک‌اند باید در صورت خطا تصویر خالی برگرداند (نه exception).
#pragma once

#include <memory>
#include <string>

#include "atlas/types.h"

namespace atlas {

class IBackend {
 public:
  virtual ~IBackend() = default;
  virtual const char* name() const = 0;
  virtual int scale() const = 0;
  virtual bool ok() const = 0;
  virtual ImageF32 run(const ImageF32& input) = 0;
};

// مسیر کلاسیک: Lanczos + unsharp. همیشه در دسترس، بدون مدل.
std::unique_ptr<IBackend> make_classical_backend(int scale, float sharpen = 0.2f);

// مسیر مدل: اگر Atlas با ncnn ساخته نشده باشد، nullptr برمی‌گردد.
std::unique_ptr<IBackend> make_ncnn_backend(const std::string& param_path,
                                            const std::string& bin_path, int scale,
                                            int threads, bool fp16);
bool ncnn_compiled();

}  // namespace atlas

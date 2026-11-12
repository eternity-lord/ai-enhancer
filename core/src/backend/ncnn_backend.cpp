// SPDX-License-Identifier: Apache-2.0
// یادداشت: اولین بک‌اند واقعی. مدل‌های ncnn از Real-ESRGAN با ورودی/خروجی
// [0,1] و نام blobهای "data"/"output" کار می‌کنند. ورودی به مضرب ۸ پد می‌شود
// (محدودیت برخی لایه‌های downsample) و خروجی به ابعاد دقیق برش می‌خورد.
#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

#include "atlas/backend.h"

#ifdef ATLAS_WITH_NCNN
#include "net.h"
// ncnn با #cmakedefine01 یعنی NCNN_VULKAN همیشه تعریف شده است (۰ یا ۱)، پس
// #ifdef غلط است و باید مقدارش بررسی شود.
#if NCNN_VULKAN
#include "gpu.h"  // ncnn::get_gpu_count
#endif

#include "atlas/buffer.h"
#include "atlas/image.h"

namespace atlas {
namespace {

class NcnnBackend final : public IBackend {
 public:
  NcnnBackend(std::string param, std::string bin, int scale, int threads, bool fp16,
              bool use_gpu)
      : param_(std::move(param)), bin_(std::move(bin)), scale_(scale) {
    net_.opt.use_vulkan_compute = false;
#if NCNN_VULKAN
    // فقط اگر Vulkan در ncnn کامپایل شده و دستگاه واقعی وجود دارد.
    if (use_gpu && ncnn::get_gpu_count() > 0) {
      net_.opt.use_vulkan_compute = true;
      gpu_ = true;
    }
#else
    (void)use_gpu;
#endif
    net_.opt.use_packing_layout = false;
    net_.opt.use_fp16_packed = fp16;
    net_.opt.use_fp16_storage = fp16;
    net_.opt.use_fp16_arithmetic = fp16;
    net_.opt.num_threads = std::max(1, threads);
    ok_ = net_.load_param(param_.c_str()) == 0 && net_.load_model(bin_.c_str()) == 0;
  }

  const char* name() const override { return gpu_ ? "ncnn-vulkan" : "ncnn-cpu"; }
  int scale() const override { return scale_; }
  bool ok() const override { return ok_; }

  ImageF32 run(const ImageF32& input) override {
    if (!ok_ || input.empty()) return ImageF32();
    if (input.channels() != 3 && input.channels() != 4) return ImageF32();

    // مدل RGB می‌خواهد؛ آلفا جدا و با Lanczos (نه با مدل) بزرگ می‌شود تا
    // لبه‌های شفافیت «هالهٔ» مدل را نگیرند.
    const bool has_alpha = input.channels() == 4;
    ImageF32 rgb(input.width(), input.height(), 3);
    for (size_t i = 0; i < input.pixel_count(); ++i) {
      const float* s = input.data() + i * (has_alpha ? 4 : 3);
      float* d = rgb.data() + i * 3;
      d[0] = s[0];
      d[1] = s[1];
      d[2] = s[2];
    }

    constexpr int kAlign = 8;
    const int pad_w = (kAlign - input.width() % kAlign) % kAlign;
    const int pad_h = (kAlign - input.height() % kAlign) % kAlign;
    const ImageF32 padded =
        (pad_w || pad_h) ? pad_reflect(rgb, 0, 0, pad_w, pad_h) : rgb;

    ncnn::Mat in(padded.width(), padded.height(), 3);
    for (int c = 0; c < 3; ++c) {
      float* dst = in.channel(c);
      const float* src = padded.data() + c;
      for (size_t i = 0; i < padded.pixel_count(); ++i) {
        dst[i] = src[i * 3];
      }
    }

    ncnn::Extractor ex = net_.create_extractor();
    if (ex.input("data", in) != 0) return ImageF32();
    ncnn::Mat out;
    if (ex.extract("output", out) != 0) return ImageF32();
    if (out.elempack != 1 || out.elemsize != 4) return ImageF32();

    const int out_w = out.w;
    const int out_h = out.h;
    ImageF32 result(out_w, out_h, 3);
    for (int c = 0; c < 3; ++c) {
      const float* src = out.channel(c);
      float* dst = result.data() + c;
      for (size_t i = 0; i < result.pixel_count(); ++i) dst[i * 3] = src[i];
    }

    const int want_w = input.width() * scale_;
    const int want_h = input.height() * scale_;
    if (out_w < want_w || out_h < want_h) return ImageF32();

    ImageF32 cropped = result;
    if (out_w != want_w || out_h != want_h) {
      cropped = ImageF32(want_w, want_h, 3);
      for (int y = 0; y < want_h; ++y) {
        std::memcpy(cropped.pixel(0, y), result.pixel(0, y),
                    sizeof(float) * static_cast<size_t>(want_w) * 3);
      }
    }

    if (!has_alpha) return cropped;

    ImageF32 alpha_in(input.width(), input.height(), 1);
    for (size_t i = 0; i < input.pixel_count(); ++i) {
      alpha_in.data()[i] = input.data()[i * 4 + 3];
    }
    const ImageF32 alpha = resize_lanczos(alpha_in, want_w, want_h);
    ImageF32 rgba(want_w, want_h, 4);
    for (size_t i = 0; i < rgba.pixel_count(); ++i) {
      const float* s = cropped.data() + i * 3;
      float* d = rgba.data() + i * 4;
      d[0] = s[0];
      d[1] = s[1];
      d[2] = s[2];
      d[3] = alpha.data()[i];
    }
    return rgba;
  }

 private:
  std::string param_;
  std::string bin_;
  int scale_;
  bool ok_ = false;
  bool gpu_ = false;
  ncnn::Net net_;
};

}  // namespace

std::unique_ptr<IBackend> make_ncnn_backend(const std::string& param_path,
                                            const std::string& bin_path, int scale,
                                            int threads, bool fp16, bool use_gpu) {
  if (scale < 1 || scale > 16) return nullptr;
  auto backend = std::make_unique<NcnnBackend>(param_path, bin_path, scale, threads, fp16, use_gpu);
  if (!backend->ok()) return nullptr;
  return backend;
}

bool ncnn_compiled() { return true; }

int vulkan_device_count() {
#if NCNN_VULKAN
  return ncnn::get_gpu_count();
#else
  return 0;
#endif
}

}  // namespace atlas

#else  // !ATLAS_WITH_NCNN

namespace atlas {

std::unique_ptr<IBackend> make_ncnn_backend(const std::string&, const std::string&, int,
                                            int, bool, bool) {
  return nullptr;
}

bool ncnn_compiled() { return false; }

int vulkan_device_count() { return 0; }

}  // namespace atlas

#endif  // ATLAS_WITH_NCNN

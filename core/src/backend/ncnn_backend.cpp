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

#include "atlas/buffer.h"

namespace atlas {
namespace {

class NcnnBackend final : public IBackend {
 public:
  NcnnBackend(std::string param, std::string bin, int scale, int threads, bool fp16)
      : param_(std::move(param)), bin_(std::move(bin)), scale_(scale) {
    net_.opt.use_vulkan_compute = false;
    net_.opt.use_packing_layout = false;
    net_.opt.use_fp16_packed = fp16;
    net_.opt.use_fp16_storage = fp16;
    net_.opt.use_fp16_arithmetic = fp16;
    net_.opt.num_threads = std::max(1, threads);
    ok_ = net_.load_param(param_.c_str()) == 0 && net_.load_model(bin_.c_str()) == 0;
  }

  const char* name() const override { return "ncnn-cpu"; }
  int scale() const override { return scale_; }
  bool ok() const override { return ok_; }

  ImageF32 run(const ImageF32& input) override {
    if (!ok_ || input.empty() || input.channels() != 3) return ImageF32();

    constexpr int kAlign = 8;
    const int pad_w = (kAlign - input.width() % kAlign) % kAlign;
    const int pad_h = (kAlign - input.height() % kAlign) % kAlign;
    const ImageF32 padded =
        (pad_w || pad_h) ? pad_reflect(input, 0, 0, pad_w, pad_h) : input;

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
    if (out_w == want_w && out_h == want_h) return result;

    ImageF32 cropped(want_w, want_h, 3);
    for (int y = 0; y < want_h; ++y) {
      std::memcpy(cropped.pixel(0, y), result.pixel(0, y),
                  sizeof(float) * static_cast<size_t>(want_w) * 3);
    }
    return cropped;
  }

 private:
  std::string param_;
  std::string bin_;
  int scale_;
  bool ok_ = false;
  ncnn::Net net_;
};

}  // namespace

std::unique_ptr<IBackend> make_ncnn_backend(const std::string& param_path,
                                            const std::string& bin_path, int scale,
                                            int threads, bool fp16) {
  if (scale < 1 || scale > 16) return nullptr;
  auto backend = std::make_unique<NcnnBackend>(param_path, bin_path, scale, threads, fp16);
  if (!backend->ok()) return nullptr;
  return backend;
}

bool ncnn_compiled() { return true; }

}  // namespace atlas

#else  // !ATLAS_WITH_NCNN

namespace atlas {

std::unique_ptr<IBackend> make_ncnn_backend(const std::string&, const std::string&, int,
                                            int, bool) {
  return nullptr;
}

bool ncnn_compiled() { return false; }

}  // namespace atlas

#endif  // ATLAS_WITH_NCNN

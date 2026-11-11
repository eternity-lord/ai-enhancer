// SPDX-License-Identifier: Apache-2.0
// یادداشت: CLI حالا مسیر واقعی را اجرا می‌کند: JPEG/PNG → SR کاشی‌ای (مدل یا
// کلاسیک) → خروجی. مدل اختیاری است؛ اگر داده نشود، مسیر کلاسیک دقیقاً همان
// پایپ‌لاین (tiling+blend) را اجرا می‌کند تا رفتار یکسان بماند.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

#include "atlas/atlas.h"
#include "atlas/backend.h"
#include "atlas/image.h"
#include "atlas/exif.h"
#include "atlas/image_io.h"
#include "atlas/metrics.h"
#include "atlas/sr.h"
#include "atlas/version.h"

namespace {

struct Options {
  std::string in_path;
  std::string out_path;
  std::string param_path;
  std::string bin_path;
  std::string backend = "auto";
  int scale = 0;  // 0 = auto
  int tile = 256;
  int overlap = 32;
  int quality = 92;
  int sharpen_pct = 20;
  int threads = 0;
  bool fp16 = false;
  bool quiet = false;
  bool auto_orient = true;
  bool json = false;
  double min_psnr = -1.0;
  double min_ssim = -1.0;
};

void print_usage() {
  std::printf(
      "atlas-cli %s\n"
      "Usage:\n"
      "  atlas-cli version | devices\n"
      "  atlas-cli convert <in> <out> [--quality N] [--no-auto-orient]\n"
      "  atlas-cli eval <a> <b> [--json] [--min-psnr X] [--min-ssim Y]\n"
      "  atlas-cli enhance <in.(jpg|png|bmp|tga|ppm)> <out> [options]\n"
      "Options:\n"
      "  --scale N        upscale factor (default: model scale, else 2)\n"
      "  --tile N         tile size in pixels (default 256)\n"
      "  --overlap N      tile overlap (default 32)\n"
      "  --param PATH     ncnn .param file (enables model backend)\n"
      "  --weights PATH   ncnn .bin file\n"
      "  --backend NAME   auto | classical | ncnn\n"
      "  --threads N      CPU threads (default: hardware concurrency)\n"
      "  --fp16           request fp16 (GPU backends; CPU ignores)\n"
      "  --sharpen P      classical unsharp amount 0..100 (default 20)\n"
      "  --quality N      JPEG quality 1..100 (default 92)\n"
      "  --quiet          no progress output\n"
      "  --no-auto-orient ignore EXIF orientation (default: apply it)\n"
      "  --json           (eval) machine-readable JSON output\n"
      "  --min-psnr X     (eval) fail if PSNR < X dB\n"
      "  --min-ssim Y     (eval) fail if SSIM < Y\n",
      ATLAS_VERSION_STRING);
}

bool parse_int(const char* s, int* out) {
  if (!s || !*s) return false;
  char* end = nullptr;
  const long v = std::strtol(s, &end, 10);
  if (end == s || *end != '\0') return false;
  *out = static_cast<int>(v);
  return true;
}

bool parse_args(int argc, char** argv, Options* opt) {
  for (int i = 4; i < argc; ++i) {
    const std::string arg = argv[i];
    const bool has_next = (i + 1) < argc;
    auto need = [&](int* target) {
      if (!has_next || !parse_int(argv[++i], target)) return false;
      return true;
    };
    if (arg == "--no-auto-orient") {
      opt->auto_orient = false;
    } else if (arg == "--json") {
      opt->json = true;
    } else if (arg == "--min-psnr") {
      if (!has_next) return false;
      char* end = nullptr;
      const double v = std::strtod(argv[++i], &end);
      if (end == argv[i] || *end != '\0') return false;
      opt->min_psnr = v;
    } else if (arg == "--min-ssim") {
      if (!has_next) return false;
      char* end = nullptr;
      const double v = std::strtod(argv[++i], &end);
      if (end == argv[i] || *end != '\0') return false;
      opt->min_ssim = v;
    } else if (arg == "--quiet") {
      opt->quiet = true;
    } else if (arg == "--fp16") {
      opt->fp16 = true;
    } else if (arg == "--scale") {
      if (!need(&opt->scale)) return false;
    } else if (arg == "--tile") {
      if (!need(&opt->tile)) return false;
    } else if (arg == "--overlap") {
      if (!need(&opt->overlap)) return false;
    } else if (arg == "--quality") {
      if (!need(&opt->quality)) return false;
    } else if (arg == "--sharpen") {
      if (!need(&opt->sharpen_pct)) return false;
    } else if (arg == "--threads") {
      if (!need(&opt->threads)) return false;
    } else if (arg == "--param" && has_next) {
      opt->param_path = argv[++i];
    } else if (arg == "--weights" && has_next) {
      opt->bin_path = argv[++i];
    } else if (arg == "--backend" && has_next) {
      opt->backend = argv[++i];
    } else {
      std::fprintf(stderr, "unknown or incomplete option: %s\n", arg.c_str());
      return false;
    }
  }
  return true;
}

// خواندن با اعمال EXIF orientation؛ هر جای دیگری که تصویر را از دیسک می‌خوانیم
// باید از این تابع استفاده کند، وگرنه یک مسیر بی‌سروصدا چرخش را از دست می‌دهد.
bool load_oriented(const std::string& path, bool auto_orient, atlas::ImageF32* img,
                   std::string* error, int* orientation_out = nullptr) {
  if (!atlas::load_image(path, img, error)) return false;
  const int o = auto_orient ? atlas::read_exif_orientation(path) : 0;
  if (orientation_out) *orientation_out = o;
  if (o > 1) *img = atlas::apply_orientation(*img, o);
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    print_usage();
    return 2;
  }

  const std::string cmd = argv[1];
  if (cmd == "version" || cmd == "--version" || cmd == "-v") {
    std::printf("atlas-cli %s (ncnn=%s)\n", atlas_version(),
                atlas::ncnn_compiled() ? "on" : "off");
    return 0;
  }

  AtlasContext* ctx = atlas_create();
  if (!ctx) {
    std::fprintf(stderr, "failed to create context\n");
    return 1;
  }

  if (cmd == "devices") {
    std::printf("%s\n", atlas_devices_json(ctx));
    atlas_destroy(ctx);
    return 0;
  }

  // eval: اندازه‌گیری کیفیت دو تصویر. خروجی JSON برای CI و پنل ارزیابی
  // (`docs/17`) و خروجی انسانی برای کار روزمره.
  if (cmd == "eval" && argc >= 4) {
    Options eopt;
    eopt.in_path = argv[2];
    eopt.out_path = argv[3];
    if (!parse_args(argc, argv, &eopt)) {
      atlas_destroy(ctx);
      return 2;
    }
    atlas::ImageF32 a;
    atlas::ImageF32 b;
    std::string error;
    if (!load_oriented(eopt.in_path, eopt.auto_orient, &a, &error)) {
      std::fprintf(stderr, "cannot load %s: %s\n", eopt.in_path.c_str(), error.c_str());
      atlas_destroy(ctx);
      return 1;
    }
    if (!load_oriented(eopt.out_path, eopt.auto_orient, &b, &error)) {
      std::fprintf(stderr, "cannot load %s: %s\n", eopt.out_path.c_str(), error.c_str());
      atlas_destroy(ctx);
      return 1;
    }
    const atlas::QualityMetrics m = atlas::compare_images(a, b);
    if (!m.valid) {
      std::fprintf(stderr, "cannot compare empty images\n");
      atlas_destroy(ctx);
      return 1;
    }
    const bool pass = (eopt.min_psnr < 0.0 || m.psnr_db >= eopt.min_psnr) &&
                      (eopt.min_ssim < 0.0 || m.ssim >= eopt.min_ssim);
    if (eopt.json) {
      std::printf(
          "{\"psnr_db\":%.4f,\"ssim\":%.6f,\"mae\":%.6f,\"max_abs\":%.6f,"
          "\"width\":%d,\"height\":%d,\"pass\":%s}\n",
          m.psnr_db, m.ssim, m.mae, m.max_abs, m.width, m.height,
          pass ? "true" : "false");
    } else {
      std::printf("psnr   : %.2f dB\nssim   : %.4f\nmae    : %.4f\nmax abs: %.4f\n",
                  m.psnr_db, m.ssim, m.mae, m.max_abs);
      std::printf("size   : %dx%d (مقایسه روی ناحیهٔ مشترک از مرکز)\n", m.width, m.height);
      if (!pass) std::printf("gate   : FAILED\n");
    }
    atlas_destroy(ctx);
    return pass ? 0 : 1;
  }

  // convert: مسیر decode→encode بدون تغییر هندسه؛ برای QA و ساخت جفت‌های
  // ارزیابی (HR↔LR) لازم است تا مجبور نباشیم ابزار بیرونی نصب کنیم.
  if (cmd == "convert" && argc >= 4) {
    Options copt;
    copt.in_path = argv[2];
    copt.out_path = argv[3];
    if (!parse_args(argc, argv, &copt)) {
      atlas_destroy(ctx);
      return 2;
    }
    atlas::ImageF32 img;
    std::string error;
    int orientation = 0;
    if (!load_oriented(copt.in_path, copt.auto_orient, &img, &error, &orientation)) {
      std::fprintf(stderr, "cannot load %s: %s\n", copt.in_path.c_str(), error.c_str());
      atlas_destroy(ctx);
      return 1;
    }
    if (!atlas::save_image(copt.out_path, img, copt.quality, &error)) {
      std::fprintf(stderr, "cannot save %s: %s\n", copt.out_path.c_str(), error.c_str());
      atlas_destroy(ctx);
      return 1;
    }
    if (!copt.quiet) {
      std::printf("%dx%d (%d ch, exif=%s) -> %s\n", img.width(), img.height(), img.channels(),
                  orientation > 0 ? std::to_string(orientation).c_str() : "none",
                  copt.out_path.c_str());
    }
    atlas_destroy(ctx);
    return 0;
  }

  if (cmd != "enhance" || argc < 4) {
    print_usage();
    atlas_destroy(ctx);
    return 2;
  }

  Options opt;
  opt.in_path = argv[2];
  opt.out_path = argv[3];
  if (!parse_args(argc, argv, &opt)) {
    atlas_destroy(ctx);
    return 2;
  }

  const bool has_model = !opt.param_path.empty() && !opt.bin_path.empty();
  if (!has_model && (!opt.param_path.empty() || !opt.bin_path.empty())) {
    std::fprintf(stderr, "both --param and --weights are required\n");
    atlas_destroy(ctx);
    return 2;
  }

  const bool want_ncnn = (opt.backend == "ncnn") || (opt.backend == "auto" && has_model);
  if (want_ncnn && !atlas::ncnn_compiled()) {
    std::fprintf(stderr,
                 "ncnn backend is not compiled in this build; rebuild with "
                 "-DATLAS_WITH_NCNN=ON or use --backend classical\n");
    atlas_destroy(ctx);
    return 2;
  }
  if (want_ncnn && !has_model) {
    std::fprintf(stderr, "--backend ncnn needs --param and --weights\n");
    atlas_destroy(ctx);
    return 2;
  }

  const int threads = opt.threads > 0 ? opt.threads
                                      : static_cast<int>(std::thread::hardware_concurrency());
  std::unique_ptr<atlas::IBackend> backend;
  if (want_ncnn) {
    const int model_scale = opt.scale > 0 ? opt.scale : 4;
    backend = atlas::make_ncnn_backend(opt.param_path, opt.bin_path, model_scale, threads,
                                       opt.fp16);
    if (!backend) {
      std::fprintf(stderr, "failed to load model: %s / %s\n", opt.param_path.c_str(),
                   opt.bin_path.c_str());
      atlas_destroy(ctx);
      return 2;
    }
  } else {
    const int scale = opt.scale > 0 ? opt.scale : 2;
    backend = atlas::make_classical_backend(scale, opt.sharpen_pct / 100.0f);
    if (!backend) {
      std::fprintf(stderr, "invalid scale %d\n", scale);
      atlas_destroy(ctx);
      return 2;
    }
  }

  if (opt.scale > 0 && opt.scale != backend->scale()) {
    std::fprintf(stderr, "requested scale %d does not match model scale %d\n", opt.scale,
                 backend->scale());
    atlas_destroy(ctx);
    return 2;
  }

  atlas::ImageF32 src;
  std::string error;
  int orientation = 0;
  if (!load_oriented(opt.in_path, opt.auto_orient, &src, &error, &orientation)) {
    std::fprintf(stderr, "cannot load %s: %s\n", opt.in_path.c_str(), error.c_str());
    atlas_destroy(ctx);
    return 1;
  }

  atlas::SrOptions sr;
  sr.tile = opt.tile;
  sr.overlap = opt.overlap;
  sr.verbose = !opt.quiet;

  if (!opt.quiet) {
    std::printf("input : %dx%d (%.2f MP)%s\n", src.width(), src.height(),
                src.pixel_count() / 1e6,
                orientation > 1 ? (std::string(" [exif orientation ") +
                                  std::to_string(orientation) + "]").c_str()
                                : "");
    std::printf("model : %s (scale %d, threads %d)\n", backend->name(), backend->scale(),
                threads);
    std::printf("plan  : tile=%d overlap=%d\n", sr.tile, sr.overlap);
    std::fflush(stdout);
  }

  const auto t0 = std::chrono::steady_clock::now();
  int last_printed = -1;
  atlas::ImageF32 upscaled = atlas::sr_tiled(src, *backend, sr, [&](int done, int total) {
    if (opt.quiet || total <= 0) return;
    const int pct = done * 100 / total;
    if (pct / 10 != last_printed / 10 || done == total) {
      std::printf("\rtiles : %d/%d (%d%%)", done, total, pct);
      std::fflush(stdout);
      last_printed = pct;
    }
  });
  if (!opt.quiet) std::printf("\n");
  const auto t1 = std::chrono::steady_clock::now();
  const double seconds = std::chrono::duration<double>(t1 - t0).count();

  if (upscaled.empty()) {
    std::fprintf(stderr, "inference failed or returned empty image\n");
    atlas_destroy(ctx);
    return 1;
  }

  if (!atlas::save_image(opt.out_path, upscaled, opt.quality, &error)) {
    std::fprintf(stderr, "cannot save %s: %s\n", opt.out_path.c_str(), error.c_str());
    atlas_destroy(ctx);
    return 1;
  }

  if (!opt.quiet) {
    std::printf("output: %dx%d (%.2f MP) in %.2fs (%.2f MP/s) -> %s\n", upscaled.width(),
                upscaled.height(), upscaled.pixel_count() / 1e6, seconds,
                upscaled.pixel_count() / 1e6 / (seconds > 0 ? seconds : 1e-9),
                opt.out_path.c_str());
  }

  atlas_destroy(ctx);
  return 0;
}

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
#include "atlas/golden.h"
#include "atlas/identity.h"
#include "atlas/metadata.h"
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
  std::string face_box;  // "x,y,w,h" نرمال‌شده برای gate
  bool reference_mode = false;
  int scale = 0;  // 0 = auto
  int tile = 256;
  int overlap = 32;
  int quality = 92;
  int sharpen_pct = 20;
  int threads = 0;
  bool fp16 = false;
  bool quiet = false;
  bool auto_orient = true;
  bool color_manage = true;
  bool keep_metadata = false;
  bool use_gpu = false;
  bool allow_missing = false;
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
      "  atlas-cli gate <original> <restored> [--face x,y,w,h] [--ref] [--json]\n"
      "  atlas-cli eval-set <manifest.tsv> [--json] [--allow-missing]\n"
      "  atlas-cli enhance <in.(jpg|png|bmp|tga|ppm)> <out> [options]\n"
      "Options:\n"
      "  --scale N        upscale factor (default: model scale, else 2)\n"
      "  --tile N         tile size in pixels (default 256)\n"
      "  --overlap N      tile overlap (default 32)\n"
      "  --param PATH     ncnn .param file (enables model backend)\n"
      "  --weights PATH   ncnn .bin file\n"
      "  --backend NAME   auto | classical | ncnn\n"
      "  --threads N      CPU threads (default: hardware concurrency)\n"
      "  --gpu            use Vulkan when ncnn was built with it (falls back to CPU)\n"
      "  --fp16           request fp16 (GPU backends; CPU ignores)\n"
      "  --sharpen P      classical unsharp amount 0..100 (default 20)\n"
      "  --quality N      JPEG quality 1..100 (default 92)\n"
      "  --quiet          no progress output\n"
      "  --no-auto-orient ignore EXIF orientation (default: apply it)\n"
      "  --no-color-manage ignore the ICC profile (default: convert to sRGB)\n"
      "  --keep-metadata  copy source EXIF into the output (off: GPS/privacy)\n"
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

bool parse_face_box(const std::string& text, atlas::NormBox* box) {
  float v[4] = {0, 0, 0, 0};
  int found = 0;
  size_t start = 0;
  while (found < 4 && start <= text.size()) {
    const size_t comma = text.find(',', start);
    const std::string cell = text.substr(start, comma == std::string::npos ? std::string::npos
                                                                          : comma - start);
    char* end = nullptr;
    const double value = std::strtod(cell.c_str(), &end);
    if (cell.empty() || end == cell.c_str() || *end != '\0') return false;
    v[found++] = static_cast<float>(value);
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  if (found != 4) return false;
  *box = atlas::NormBox{v[0], v[1], v[2], v[3]};
  return box->w > 0.0f && box->h > 0.0f;
}

// first: اولین آرگومان اختیاری؛ برای دستورهایی که تعداد پارامتر positional
// متفاوتی دارند (مثل eval-set با یک پارامتر) وگرنه فلگ‌ها بی‌صدا نادیده می‌مانند.
bool parse_args(int argc, char** argv, Options* opt, int first = 4) {
  for (int i = first; i < argc; ++i) {
    const std::string arg = argv[i];
    const bool has_next = (i + 1) < argc;
    auto need = [&](int* target) {
      if (!has_next || !parse_int(argv[++i], target)) return false;
      return true;
    };
    if (arg == "--no-auto-orient") {
      opt->auto_orient = false;
    } else if (arg == "--no-color-manage") {
      opt->color_manage = false;
    } else if (arg == "--keep-metadata") {
      opt->keep_metadata = true;
    } else if (arg == "--allow-missing") {
      opt->allow_missing = true;
    } else if (arg == "--ref") {
      opt->reference_mode = true;
    } else if (arg == "--face" && has_next) {
      opt->face_box = argv[++i];
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
    } else if (arg == "--gpu") {
      opt->use_gpu = true;
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

// خواندن + مدیریت رنگ + جهت. هر جای دیگری که تصویر را از دیسک می‌خوانیم باید
// از این تابع استفاده کند، وگرنه یک مسیر بی‌سروصدا رنگ یا چرخش را از دست می‌دهد.
bool load_ready(const std::string& path, bool auto_orient, bool color_manage,
                atlas::ImageF32* img, atlas::ImageMeta* meta, std::string* error) {
  if (!atlas::load_image_ex(path, img, meta, error)) return false;
  if (!color_manage) {
    // خواستهٔ کاربر: بدون تبدیل پروفایل. باید دوباره از دیسک بخوانیم چون
    // load_image_ex تبدیل را انجام داده است.
    if (!atlas::load_image(path, img, error)) return false;
    if (meta) {
      meta->icc_applied = false;
      meta->icc_description = meta->has_icc_profile ? "ignored (ICC bypassed)" : "none";
    }
  }
  if (meta && auto_orient && meta->orientation > 1) {
    *img = atlas::apply_orientation(*img, meta->orientation);
  }
  return true;
}

// بعد از نوشتن فایل: برچسب sRGB (اگر تبدیل رنگ انجام شده) و انتقال EXIF.
void finalize_output(const std::string& in_path, const std::string& out_path,
                     const atlas::ImageMeta& meta, const Options& opt) {
  std::string error;
  const std::string ext = out_path.substr(out_path.find_last_of('.') + 1);
  const bool raster = ext == "png" || ext == "jpg" || ext == "jpeg";

  if (raster && (meta.icc_applied || opt.keep_metadata)) {
    if (atlas::tag_srgb_profile(out_path, &error)) {
      if (!opt.quiet) std::printf("icc   : embedded sRGB profile\n");
    } else if (!opt.quiet) {
      std::printf("icc   : not embedded (%s)\n", error.c_str());
    }
  }

  if (opt.keep_metadata) {
    if (atlas::keep_exif_metadata(in_path, out_path, &error)) {
      if (!opt.quiet) std::printf("exif  : copied from source\n");
    } else if (!opt.quiet) {
      std::printf("exif  : not copied (%s)\n", error.c_str());
    }
  }
}

void print_color_line(const atlas::ImageMeta& meta) {
  if (meta.icc_applied) {
    std::printf("color : %s -> sRGB\n", meta.icc_description.c_str());
  } else if (meta.has_icc_profile) {
    std::printf("color : %s\n", meta.icc_description.c_str());
  }
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
    const int vulkan = atlas::vulkan_device_count();
    if (vulkan > 0) std::printf("vulkan: %d device(s)\n", vulkan);
    atlas_destroy(ctx);
    return 0;
  }

  // gate: دروازهٔ هویت روی یک جفت (اصلی، بازسازی‌شده). توجه: score روی مقیاس
  // پروکسی است (نه ArcFace)؛ در خروجی صریح گفته می‌شود.
  if (cmd == "gate" && argc >= 4) {
    Options gopt;
    gopt.in_path = argv[2];
    gopt.out_path = argv[3];
    if (!parse_args(argc, argv, &gopt)) {
      atlas_destroy(ctx);
      return 2;
    }
    atlas::ImageF32 original;
    atlas::ImageF32 restored;
    atlas::ImageMeta meta_a;
    atlas::ImageMeta meta_b;
    std::string error;
    if (!load_ready(gopt.in_path, gopt.auto_orient, gopt.color_manage, &original, &meta_a,
                    &error)) {
      std::fprintf(stderr, "cannot load %s: %s\n", gopt.in_path.c_str(), error.c_str());
      atlas_destroy(ctx);
      return 1;
    }
    if (!load_ready(gopt.out_path, gopt.auto_orient, gopt.color_manage, &restored, &meta_b,
                    &error)) {
      std::fprintf(stderr, "cannot load %s: %s\n", gopt.out_path.c_str(), error.c_str());
      atlas_destroy(ctx);
      return 1;
    }

    atlas::IdentityOptions opt;
    opt.reference_mode = gopt.reference_mode;
    if (!gopt.face_box.empty() && !parse_face_box(gopt.face_box, &opt.face)) {
      std::fprintf(stderr, "--face needs x,y,w,h in 0..1 (example: 0.3,0.2,0.4,0.5)\n");
      atlas_destroy(ctx);
      return 2;
    }

    const atlas::IdentityResult r = atlas::identity_gate(original, restored, opt);
    if (!r.valid) {
      std::fprintf(stderr, "gate failed: %s\n", r.note.c_str());
      atlas_destroy(ctx);
      return 1;
    }
    if (gopt.json) {
      std::printf(
          "{\"id_score\":%.4f,\"threshold\":%.4f,\"pass\":%s,\"scale\":\"%s\","
          "\"arcface_threshold\":%.4f,\"face_px\":%d,\"structural\":%.4f,"
          "\"shape\":%.4f,\"color\":%.4f}\n",
          r.score, r.threshold, r.pass ? "true" : "false", r.proxy_scale ? "proxy" : "arcface",
          r.arcface_threshold, r.face_px, r.structural, r.shape, r.color);
    } else {
      std::printf("id score : %.3f (threshold %.2f, %s scale)\n", r.score, r.threshold,
                  r.proxy_scale ? "proxy" : "ArcFace");
      std::printf("parts    : structural %.3f | shape %.3f | color %.3f\n", r.structural,
                  r.shape, r.color);
      std::printf("face     : %d px (%s mode)\n", r.face_px,
                  gopt.reference_mode ? "reference-locked" : "blind");
      std::printf("verdict  : %s\n", r.pass ? "PASS" : "FAIL");
      if (r.proxy_scale) {
        std::printf(
            "note     : پروکسی ساختاری است، نه ArcFace؛ عدد آن با آستانهٔ %.2f (docs/15) "
            "قابل مقایسه نیست.\n",
            r.arcface_threshold);
      }
    }
    atlas_destroy(ctx);
    return r.pass ? 0 : 1;
  }

  // eval-set: دروازهٔ هویت روی یک مانیفست TSV (docs/17).
  if (cmd == "eval-set" && argc >= 3) {
    Options sopt;
    sopt.in_path = argv[2];  // مسیر مانیفست
    if (!parse_args(argc, argv, &sopt, 3)) {
      atlas_destroy(ctx);
      return 2;
    }
    std::vector<atlas::GoldenRow> rows;
    std::string error;
    if (!atlas::load_manifest(sopt.in_path, &rows, &error)) {
      std::fprintf(stderr, "%s\n", error.c_str());
      atlas_destroy(ctx);
      return 2;
    }
    atlas::GoldenRunOptions run;
    run.allow_missing = sopt.allow_missing;
    const size_t slash = sopt.in_path.find_last_of('/');
    run.base_dir = slash == std::string::npos ? std::string() : sopt.in_path.substr(0, slash);

    const atlas::GoldenReport report = atlas::run_golden(rows, run);
    if (sopt.json) {
      std::printf("{\"total\":%d,\"ran\":%d,\"passed\":%d,\"missing\":%d,"
                  "\"pass_rate\":%.4f,\"median_score\":%.4f,\"min_score\":%.4f,\"cases\":[",
                  report.total, report.ran, report.passed, report.missing, report.pass_rate(),
                  report.median_score, report.min_score);
      bool first = true;
      for (const auto& c : report.cases) {
        if (!first) std::printf(",");
        first = false;
        if (c.identity.valid) {
          std::printf("{\"id\":\"%s\",\"score\":%.4f,\"pass\":%s,\"face_px\":%d}",
                      c.id.c_str(), c.identity.score, c.identity.pass ? "true" : "false",
                      c.identity.face_px);
        } else {
          std::printf("{\"id\":\"%s\",\"error\":\"%s\"}", c.id.c_str(), c.error.c_str());
        }
      }
      std::printf("]}\n");
    } else {
      for (const auto& c : report.cases) {
        if (c.identity.valid) {
          std::printf("%-24s score %.3f  face %4d px  %s\n", c.id.c_str(), c.identity.score,
                      c.identity.face_px, c.identity.pass ? "PASS" : "FAIL");
        } else {
          std::printf("%-24s skipped (%s)\n", c.id.c_str(), c.error.c_str());
        }
      }
      std::printf("\n%d/%d ran, %d passed (%.1f%%), median %.3f, min %.3f, missing %d\n",
                  report.ran, report.total, report.passed, report.pass_rate() * 100.0,
                  report.median_score, report.min_score, report.missing);
      std::printf("acceptance (docs/17: >=95%% pass, median >=0.65): %s\n",
                  atlas::golden_meets_acceptance(report) ? "PASS" : "FAIL");
    }
    atlas_destroy(ctx);
    return atlas::golden_meets_acceptance(report) ? 0 : 1;
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
    atlas::ImageMeta meta_a;
    atlas::ImageMeta meta_b;
    std::string error;
    if (!load_ready(eopt.in_path, eopt.auto_orient, eopt.color_manage, &a, &meta_a, &error)) {
      std::fprintf(stderr, "cannot load %s: %s\n", eopt.in_path.c_str(), error.c_str());
      atlas_destroy(ctx);
      return 1;
    }
    if (!load_ready(eopt.out_path, eopt.auto_orient, eopt.color_manage, &b, &meta_b, &error)) {
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
    atlas::ImageMeta meta;
    if (!load_ready(copt.in_path, copt.auto_orient, copt.color_manage, &img, &meta, &error)) {
      std::fprintf(stderr, "cannot load %s: %s\n", copt.in_path.c_str(), error.c_str());
      atlas_destroy(ctx);
      return 1;
    }
    if (!atlas::save_image(copt.out_path, img, copt.quality, &error)) {
      std::fprintf(stderr, "cannot save %s: %s\n", copt.out_path.c_str(), error.c_str());
      atlas_destroy(ctx);
      return 1;
    }
    finalize_output(copt.in_path, copt.out_path, meta, copt);
    if (!copt.quiet) {
      std::printf("%dx%d (%d ch, exif=%s) -> %s\n", img.width(), img.height(), img.channels(),
                  meta.orientation > 0 ? std::to_string(meta.orientation).c_str() : "none",
                  copt.out_path.c_str());
      print_color_line(meta);
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
                                       opt.fp16, opt.use_gpu);
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
  atlas::ImageMeta meta;
  if (!load_ready(opt.in_path, opt.auto_orient, opt.color_manage, &src, &meta, &error)) {
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
                meta.orientation > 1 ? (std::string(" [exif orientation ") +
                                        std::to_string(meta.orientation) + "]").c_str()
                                     : "");
    print_color_line(meta);
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
  finalize_output(opt.in_path, opt.out_path, meta, opt);

  if (!opt.quiet) {
    std::printf("output: %dx%d (%.2f MP) in %.2fs (%.2f MP/s) -> %s\n", upscaled.width(),
                upscaled.height(), upscaled.pixel_count() / 1e6, seconds,
                upscaled.pixel_count() / 1e6 / (seconds > 0 ? seconds : 1e-9),
                opt.out_path.c_str());
  }

  atlas_destroy(ctx);
  return 0;
}

// SPDX-License-Identifier: Apache-2.0
// یادداشت: CLI اولین «محصول قابل استفاده» است: بدون UI، بدون GPU، ولی واقعی.
// در P0 مسیر کلاسیک (Lanczos + unsharp) اجرا می‌شود تا قرارداد آرگومان‌ها و
// گزارش‌ها قفل شود؛ در P1 همین دستور با ncnn مدل واقعی را صدا می‌زند.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "atlas/atlas.h"
#include "atlas/buffer.h"
#include "atlas/image.h"
#include "atlas/version.h"

namespace {

void print_usage() {
  std::printf(
      "atlas-cli %s\n"
      "Usage:\n"
      "  atlas-cli version\n"
      "  atlas-cli devices\n"
      "  atlas-cli enhance <in.ppm> <out.ppm> [options]\n"
      "Options:\n"
      "  --scale N      upscale factor (default 2)\n"
      "  --sharpen P    unsharp amount in percent 0..100 (default 20)\n"
      "  --tile N       tile size for planning/report (default 256)\n"
      "  --overlap N    tile overlap (default 32)\n"
      "  --lanczos A    Lanczos a (default 3)\n"
      "  --quiet        no progress output\n",
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

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    print_usage();
    return 2;
  }

  const std::string cmd = argv[1];
  if (cmd == "version" || cmd == "--version" || cmd == "-v") {
    std::printf("atlas-cli %s\n", atlas_version());
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

  if (cmd != "enhance") {
    print_usage();
    atlas_destroy(ctx);
    return 2;
  }

  if (argc < 4) {
    print_usage();
    atlas_destroy(ctx);
    return 2;
  }

  const std::string in_path = argv[2];
  const std::string out_path = argv[3];

  int scale = 2;
  int sharpen_pct = 20;
  int tile = 256;
  int overlap = 32;
  int lanczos_a = 3;
  bool quiet = false;

  for (int i = 4; i < argc; ++i) {
    const std::string arg = argv[i];
    const bool has_next = (i + 1) < argc;
    if (arg == "--quiet") {
      quiet = true;
    } else if (arg == "--scale" && has_next) {
      if (!parse_int(argv[++i], &scale) || scale < 1 || scale > 16) {
        std::fprintf(stderr, "invalid --scale\n");
        atlas_destroy(ctx);
        return 2;
      }
    } else if (arg == "--sharpen" && has_next) {
      if (!parse_int(argv[++i], &sharpen_pct) || sharpen_pct < 0 || sharpen_pct > 100) {
        std::fprintf(stderr, "invalid --sharpen\n");
        atlas_destroy(ctx);
        return 2;
      }
    } else if (arg == "--tile" && has_next) {
      parse_int(argv[++i], &tile);
    } else if (arg == "--overlap" && has_next) {
      parse_int(argv[++i], &overlap);
    } else if (arg == "--lanczos" && has_next) {
      parse_int(argv[++i], &lanczos_a);
    } else {
      std::fprintf(stderr, "unknown option: %s\n", arg.c_str());
      atlas_destroy(ctx);
      return 2;
    }
  }

  atlas::ImageF32 src;
  std::string error;
  if (!atlas::read_ppm(in_path, &src, &error)) {
    std::fprintf(stderr, "%s\n", error.c_str());
    atlas_destroy(ctx);
    return 1;
  }

  const int out_w = src.width() * scale;
  const int out_h = src.height() * scale;
  const auto plan = atlas::plan_tiles(src.width(), src.height(), tile, overlap);

  if (!quiet) {
    std::printf("input: %dx%d (%.2f MP)\n", src.width(), src.height(),
                src.pixel_count() / 1e6);
    std::printf("output: %dx%d (%.2f MP), scale=%d\n", out_w, out_h,
                (double)out_w * out_h / 1e6, scale);
    std::printf("tiles: %zu (tile=%d overlap=%d)\n", plan.tile_count(), plan.tile, plan.overlap);
    std::printf("backend: classical (no GPU/model yet)\n");
  }

  const auto t0 = std::chrono::steady_clock::now();
  atlas::ImageF32 upscaled = atlas::resize_lanczos(src, out_w, out_h, (float)lanczos_a);
  if (sharpen_pct > 0) {
    upscaled = atlas::unsharp(upscaled, (float)sharpen_pct / 100.0f, 1.0f);
  }
  atlas::clamp01(&upscaled);
  const auto t1 = std::chrono::steady_clock::now();
  const double seconds = std::chrono::duration<double>(t1 - t0).count();

  if (!atlas::write_ppm(out_path, upscaled, &error)) {
    std::fprintf(stderr, "%s\n", error.c_str());
    atlas_destroy(ctx);
    return 1;
  }

  if (!quiet) {
    std::printf("wrote %s in %.2fs (%.2f MP/s)\n", out_path.c_str(), seconds,
                (double)upscaled.pixel_count() / 1e6 / (seconds > 0 ? seconds : 1e-9));
  }

  atlas_destroy(ctx);
  return 0;
}

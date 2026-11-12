// SPDX-License-Identifier: Apache-2.0
// یادداشت: تست مانیفست باید هم مسیر خوش‌بینانه (همه پاس) و هم مسیر بدبینانه
// (فایل گم‌شده، سطر ناقص) را بگیرد. ابزار سنجش اگر خودش اشتباه بشمارد، بدتر از
// نداشتن ابزار است.
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "atlas/golden.h"
#include "atlas/image.h"
#include "atlas/image_io.h"
#include "test.h"

namespace {

std::filesystem::path dir() {
  const auto d = std::filesystem::temp_directory_path() / "atlas_golden_test";
  std::filesystem::create_directories(d);
  return d;
}

std::string write_manifest(const std::string& name, const std::string& body) {
  const auto path = dir() / name;
  std::ofstream out(path);
  out << body;
  return path.string();
}

atlas::ImageF32 pattern(int seed) {
  atlas::ImageF32 img(200, 200, 3);
  for (int y = 0; y < 200; ++y) {
    for (int x = 0; x < 200; ++x) {
      float* p = img.pixel(x, y);
      const float v = 0.5f + 0.25f * std::sin((x + seed) * 0.15f) * std::cos(y * 0.11f);
      p[0] = v;
      p[1] = v * 0.9f;
      p[2] = v * 0.8f;
    }
  }
  return img;
}

}  // namespace

ATLAS_TEST(golden_manifest_parses_header_comments_and_face_box) {
  const auto path = write_manifest("ok.tsv",
                                   "# comment\n"
                                   "id\toriginal\trestored\tx\ty\tyw\th\tmode\n"
                                   "case1\ta.png\tb.png\t0.25\t0.25\t0.5\t0.5\tblind\n"
                                   "case2\ta.png\tb.png\t-\t-\t-\t-\tref\n");
  std::vector<atlas::GoldenRow> rows;
  std::string error;
  CHECK(atlas::load_manifest(path, &rows, &error));
  CHECK(rows.size() == 2);
  if (rows.size() == 2) {
    CHECK(rows[0].id == "case1");
    CHECK_NEAR(rows[0].face.w, 0.5f, 1e-6);
    CHECK(!rows[0].reference_mode);
    CHECK(rows[1].face.empty());
    CHECK(rows[1].reference_mode);
  }
}

ATLAS_TEST(golden_manifest_rejects_broken_rows) {
  const auto bad = write_manifest("bad.tsv", "id\toriginal\nonly-two\tcolumns\n");
  std::vector<atlas::GoldenRow> rows;
  std::string error;
  CHECK(!atlas::load_manifest(bad, &rows, &error));
  CHECK(!error.empty());

  const auto empty = write_manifest("empty.tsv", "# nothing\n");
  CHECK(!atlas::load_manifest(empty, &rows, &error));
}

ATLAS_TEST(golden_run_counts_pass_and_missing) {
  const auto d = dir();
  std::string error;
  const auto a = d / "a.png";
  const auto b = d / "b.png";
  CHECK(atlas::save_image(a.string(), pattern(0), 95, &error));
  CHECK(atlas::save_image(b.string(), pattern(0), 95, &error));  // یکسان → پاس

  const auto manifest = write_manifest(
      "run.tsv",
      std::string("id\toriginal\trestored\tx\ty\tw\th\tmode\n") +
          "same\ta.png\tb.png\t-\t-\t-\t-\tblind\n" +
          "missing\ta.png\tnope.png\t-\t-\t-\t-\tblind\n");

  std::vector<atlas::GoldenRow> rows;
  CHECK(atlas::load_manifest(manifest, &rows, &error));
  atlas::GoldenRunOptions opt;
  opt.base_dir = d.string();
  const auto report = atlas::run_golden(rows, opt);

  CHECK(report.total == 2);
  CHECK(report.ran == 1);
  CHECK(report.passed == 1);
  CHECK(report.missing == 1);
  CHECK(report.pass_rate() > 0.99);
  CHECK(report.median_score > 0.95);
  // دادهٔ ناقص یعنی قبول نیست، حتی اگر موارد موجود پاس شوند.
  CHECK(!atlas::golden_meets_acceptance(report));

  std::filesystem::remove_all(d);
}

ATLAS_TEST(golden_missing_files_can_be_skipped_locally) {
  const auto d = dir();
  std::filesystem::create_directories(d);
  std::string error;
  CHECK(atlas::save_image((d / "a.png").string(), pattern(1), 95, &error));
  CHECK(atlas::save_image((d / "b.png").string(), pattern(1), 95, &error));

  const auto manifest = write_manifest(
      "skip.tsv", std::string("id\toriginal\trestored\n") + "same\ta.png\tb.png\n");
  std::vector<atlas::GoldenRow> rows;
  CHECK(atlas::load_manifest(manifest, &rows, &error));

  atlas::GoldenRunOptions opt;
  opt.base_dir = d.string();
  opt.allow_missing = true;
  const auto report = atlas::run_golden(rows, opt);
  CHECK(report.missing == 0);
  CHECK(report.ran == 1);
  CHECK(atlas::golden_meets_acceptance(report, 0.95, 0.65));

  std::filesystem::remove_all(d);
}

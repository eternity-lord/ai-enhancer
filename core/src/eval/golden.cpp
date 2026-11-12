// SPDX-License-Identifier: Apache-2.0
// یادداشت: این فایل «کد محصول» نیست، «ابزار سنجش» است. برای همین خطاها را
// نرم می‌گیرد: فایل گم‌شده با `allow_missing` فقط شمرده می‌شود، ولی در حالت
// سخت‌گیرانه (CI) شکست است. هیچ‌وقت روی داده‌های ناقص «قبول» اعلام نمی‌کند.
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "atlas/golden.h"
#include "atlas/image_io.h"
#include "atlas/identity.h"

namespace atlas {
namespace {

std::string trim(const std::string& s) {
  const size_t first = s.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  const size_t last = s.find_last_not_of(" \t\r\n");
  return s.substr(first, last - first + 1);
}

std::vector<std::string> split_tsv(const std::string& line) {
  std::vector<std::string> out;
  std::string cell;
  for (char c : line) {
    if (c == '\t') {
      out.push_back(trim(cell));
      cell.clear();
    } else {
      cell.push_back(c);
    }
  }
  out.push_back(trim(cell));
  return out;
}

std::string resolve(const std::string& base_dir, const std::string& path) {
  if (path.empty() || path[0] == '/' || base_dir.empty()) return path;
  return base_dir + "/" + path;
}

bool parse_float(const std::string& s, float* out) {
  if (s.empty() || s == "-") return false;
  char* end = nullptr;
  const double v = std::strtod(s.c_str(), &end);
  if (end == s.c_str() || *end != '\0') return false;
  *out = static_cast<float>(v);
  return true;
}

}  // namespace

double GoldenReport::pass_rate() const {
  return ran > 0 ? static_cast<double>(passed) / static_cast<double>(ran) : 0.0;
}

bool load_manifest(const std::string& path, std::vector<GoldenRow>* rows, std::string* error) {
  if (!rows) {
    if (error) *error = "null rows";
    return false;
  }
  std::ifstream in(path);
  if (!in) {
    if (error) *error = "cannot open manifest: " + path;
    return false;
  }

  std::string line;
  int line_no = 0;
  bool saw_header = false;
  while (std::getline(in, line)) {
    ++line_no;
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    const auto cells = split_tsv(line);
    if (!saw_header) {
      saw_header = true;
      if (!cells.empty() && trim(cells[0]) == "id") continue;  // سرستون
    }
    if (cells.size() < 3) {
      if (error) {
        *error = "line " + std::to_string(line_no) + ": need at least id, original, restored";
      }
      return false;
    }
    GoldenRow row;
    row.id = cells[0];
    row.original = cells[1];
    row.restored = cells[2];
    if (cells.size() > 3) {
      float x = 0, y = 0, w = 0, h = 0;
      if (parse_float(cells[3], &x) && parse_float(cells[4], &y) && parse_float(cells[5], &w) &&
          parse_float(cells[6], &h)) {
        row.face = NormBox{x, y, w, h};
      }
    }
    if (cells.size() > 7 && (cells[7] == "ref" || cells[7] == "reference")) {
      row.reference_mode = true;
    }
    rows->push_back(std::move(row));
  }
  if (rows->empty()) {
    if (error) *error = "manifest has no rows: " + path;
    return false;
  }
  return true;
}

GoldenReport run_golden(const std::vector<GoldenRow>& rows, const GoldenRunOptions& options) {
  GoldenReport report;
  report.total = static_cast<int>(rows.size());
  std::vector<double> scores;

  for (const auto& row : rows) {
    GoldenCaseResult result;
    result.id = row.id;

    ImageF32 original;
    ImageF32 restored;
    std::string error;
    ImageMeta meta;
    if (!load_image_ex(resolve(options.base_dir, row.original), &original, &meta, &error) ||
        !load_image_ex(resolve(options.base_dir, row.restored), &restored, &meta, &error)) {
      result.error = error.empty() ? "cannot load pair" : error;
      ++report.missing;
      report.cases.push_back(std::move(result));
      continue;
    }

    IdentityOptions opt;
    opt.face = row.face;
    opt.reference_mode = row.reference_mode;
    result.identity = identity_gate(original, restored, opt);
    result.ran = result.identity.valid;
    if (!result.ran) {
      result.error = result.identity.note.empty() ? "gate failed" : result.identity.note;
      report.cases.push_back(std::move(result));
      continue;
    }

    ++report.ran;
    if (result.identity.pass) ++report.passed;
    scores.push_back(result.identity.score);
    report.cases.push_back(std::move(result));
  }

  if (!scores.empty()) {
    std::sort(scores.begin(), scores.end());
    const size_t n = scores.size();
    report.median_score = (n % 2 == 1) ? scores[n / 2]
                                       : 0.5 * (scores[n / 2 - 1] + scores[n / 2]);
    report.min_score = scores.front();
  }
  return report;
}

bool golden_meets_acceptance(const GoldenReport& report, double min_pass_rate,
                             double min_median) {
  if (report.ran == 0) return false;
  if (report.missing > 0) return false;  // دادهٔ ناقص = قبول نیست
  return report.pass_rate() >= min_pass_rate && report.median_score >= min_median;
}

}  // namespace atlas

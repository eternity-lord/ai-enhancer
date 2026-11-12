// SPDX-License-Identifier: Apache-2.0
// یادداشت: مجموعهٔ طلایی (docs/17) ۳۰۰ تصویر است که در گیت نیست. قرارداد این
// فایل، «مانیفست» آن است: یک فایل TSV که هر سطر یک جفت (اصلی، بازسازی‌شده) و
// کادر نرمال‌شدهٔ چهره را توصیف می‌کند. TSV به‌جای JSON انتخاب شد چون:
//  ۱) بدون پارسر JSON کار می‌کند (ما پارسر نداریم و نوشتنش هدر است)
//  ۲) با Excel/Sheets هم قابل ساخت است (کار تیم QA)
#pragma once

#include <string>
#include <vector>

#include "atlas/identity.h"

namespace atlas {

struct GoldenRow {
  std::string id;
  std::string original;
  std::string restored;
  NormBox face;              // خالی = کل تصویر
  bool reference_mode = false;
};

struct GoldenCaseResult {
  std::string id;
  bool ran = false;
  std::string error;
  IdentityResult identity;
};

struct GoldenReport {
  std::vector<GoldenCaseResult> cases;
  int total = 0;
  int ran = 0;
  int passed = 0;
  int missing = 0;
  double median_score = 0.0;
  double min_score = 0.0;

  double pass_rate() const;
};

struct GoldenRunOptions {
  bool allow_missing = false;      // برای اجرای محلی با زیرمجموعه‌ای از تصاویر
  std::string base_dir;            // مسیرهای نسبی مانیفست از اینجا حل می‌شوند
};

// مانیفست TSV با سرستون: id, original, restored, x, y, w, h, mode
// (کادر می‌تواند "-" باشد؛ mode: blind|ref). خطوط # و خط خالی نادیده گرفته می‌شوند.
bool load_manifest(const std::string& path, std::vector<GoldenRow>* rows, std::string* error);

GoldenReport run_golden(const std::vector<GoldenRow>& rows, const GoldenRunOptions& options);

// معیار پذیرش docs/17: نرخ عبور ≥ ۹۵٪ و میانهٔ امتیاز ≥ ۰٫۶۵.
bool golden_meets_acceptance(const GoldenReport& report, double min_pass_rate = 0.95,
                             double min_median = 0.65);

}  // namespace atlas

// SPDX-License-Identifier: Apache-2.0
// یادداشت: در P0 دستگاه واقعی probe نمی‌شود؛ فقط مسیر کلاسیک/CPU اعلام می‌شود تا
// قرارداد ABI قفل شود. در P1، ncnn/ORT اینجا دستگاه‌ها را enumerate می‌کنند و
// انتخاب backend با micro-benchmark انجام می‌شود.
#include <cstdio>
#include <string>
#include <thread>

#include "atlas/atlas.h"
#include "atlas/errors.h"
#include "atlas/version.h"

#include "atlas/backend.h"

namespace {

// فهرست دستگاه‌ها باید همان چیزی باشد که واقعاً می‌توانیم اجرا کنیم؛ دستگاه
// خیالی اعلام کردن، بعداً به «مدل لود نشد» در دستگاه کاربر تبدیل می‌شود.
std::string build_devices_json() {
  const unsigned hw = std::thread::hardware_concurrency();
  char ncnn[256];
  std::snprintf(ncnn, sizeof(ncnn),
                R"(,{"id":"ncnn-cpu","backend":"ncnn","tier":"B","fp16":false,"threads":%u})",
                hw > 0 ? hw : 1u);
  std::string json = R"([{"id":"classical-cpu","backend":"classical","tier":"C-","fp16":false,"threads":)";
  json += std::to_string(hw > 0 ? hw : 1u);
  json += "}";
  if (atlas::ncnn_compiled()) json += ncnn;
  json += "]";
  return json;
}

}  // namespace

struct AtlasContext {
  std::string last_error;
  std::string devices_json = build_devices_json();
};

extern "C" {

const char* atlas_version(void) { return ATLAS_VERSION_STRING; }

AtlasContext* atlas_create(void) { return new AtlasContext(); }

void atlas_destroy(AtlasContext* context) { delete context; }

const char* atlas_devices_json(AtlasContext* context) {
  if (!context) return "[]";
  return context->devices_json.c_str();
}

const char* atlas_last_error(AtlasContext* context) {
  if (!context) return "E_INVALID_ARGUMENT";
  if (context->last_error.empty()) return "ok";
  return context->last_error.c_str();
}

}  // extern "C"

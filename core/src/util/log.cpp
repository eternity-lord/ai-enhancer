// SPDX-License-Identifier: Apache-2.0
// یادداشت: لاگ حداقلی و بی‌طرف. در P0-04 به فایل چرخشی + سطوح گسترش می‌یابد.
// عمداً هیچ اطلاعاتی از محتوای تصویر یا مسیر کاربر لاگ نمی‌شود (حریم خصوصی).
#include <cstdio>
#include <cstdarg>

#include "atlas/log.h"

namespace atlas {

void log_info(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  std::fputs("[atlas] ", stdout);
  std::vfprintf(stdout, fmt, args);
  std::fputc('\n', stdout);
  va_end(args);
}

void log_warn(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  std::fputs("[atlas][warn] ", stderr);
  std::vfprintf(stderr, fmt, args);
  std::fputc('\n', stderr);
  va_end(args);
}

}  // namespace atlas

// SPDX-License-Identifier: Apache-2.0
// یادداشت: نگاشت کد خطا به رشتهٔ پایدار؛ UI و CLI هر دو از همین استفاده می‌کنند
// تا پیام‌ها واگرا نشوند.
#include "atlas/errors.h"

namespace atlas {

const char* error_to_string(ErrorCode code) {
  switch (code) {
    case ErrorCode::kOk: return "ok";
    case ErrorCode::kInvalidArgument: return "E_INVALID_ARGUMENT";
    case ErrorCode::kIo: return "E_IO";
    case ErrorCode::kUnsupported: return "E_UNSUPPORTED";
    case ErrorCode::kOutOfMemory: return "E_OOM";
    case ErrorCode::kInternal: return "E_INTERNAL";
  }
  return "E_UNKNOWN";
}

}  // namespace atlas

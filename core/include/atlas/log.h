// SPDX-License-Identifier: Apache-2.0
// یادداشت: رابط لاگ، بدون fmt/spdlog در P0 تا هیچ وابستگی خارجی نداشته باشیم.
#pragma once

namespace atlas {

void log_info(const char* fmt, ...);
void log_warn(const char* fmt, ...);

}  // namespace atlas

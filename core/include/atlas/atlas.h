// SPDX-License-Identifier: Apache-2.0
// یادداشت: تنها سطح عمومی C ABI. JNI (اندروید) و Swift (iOS) از همین استفاده می‌کنند.
// در P0 فقط version/create/destroy/devices پیاده شده است؛ run_graph در P1.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AtlasContext AtlasContext;

const char* atlas_version(void);

AtlasContext* atlas_create(void);
void atlas_destroy(AtlasContext* context);

// JSON: [{"id":"classical-cpu","backend":"classical","tier":"C-"}]
const char* atlas_devices_json(AtlasContext* context);

// آخرین خطا (رشتهٔ پایدار، مثل E_OOM). تا فراخوانی بعدی معتبر است.
const char* atlas_last_error(AtlasContext* context);

#ifdef __cplusplus
}
#endif

# atlas-core — هستهٔ C++20

این پوشه اولین برش اجرایی (P0) از نقشهٔ `docs/18-build-plan-wbs.md` است: یک کتابخانهٔ کوچک، بدون وابستگی خارجی، که همین حالا build می‌شود و کار می‌کند.

## وضعیت فعلی

| بخش | وضعیت |
|---|---|
| ساخت با CMake + C++20 | ✅ |
| بافر تصویر float32 + crop/pad/clamp | ✅ |
| کاشی‌بندی با پوشش کامل و چسبیدن به لبه | ✅ |
| مسیر کلاسیک: Lanczos + Gaussian + unsharp | ✅ |
| کدک موقت PPM (برای تست/دمو) | ✅ |
| C ABI (`atlas_create`, `atlas_devices_json`, …) | ✅ (حداقلی) |
| CLI: `version`, `devices`, `enhance` | ✅ |
| تست واحد (۱۱ تست) | ✅ |
| ncnn/Vulkan + مدل SR واقعی | ⏳ P1 |
| کدک JPEG/PNG/WebP/TIFF | ⏳ P0-05/P0-06 |

## ساخت و اجرا

```bash
cmake -S core -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

./build/atlas-cli version
./build/atlas-cli devices
./build/atlas-cli enhance in.ppm out.ppm --scale 2 --sharpen 20
./build/atlas-tests
```

> ورودی فعلاً فقط `PPM (P6)` است. این عمدی است: تا وقتی libjpeg-turbo/libpng اضافه نشده‌اند، هیچ وابستگی خارجی نداریم و build روی هر پلتفرمی سبز است.

### نمونهٔ اجرا (سندباکس، CPU تک‌رشته)

```
input: 2000x1500 (3.00 MP)
output: 4000x3000 (12.00 MP), scale=2
tiles: 63 (tile=256 overlap=32)
backend: classical (no GPU/model yet)
wrote out.ppm in 1.32s (9.11 MP/s)
```

## ساختار

```
include/atlas/   atlas.h (C ABI), types.h, image.h, buffer.h, errors.h, log.h, version.h
src/api/         context.cpp
src/image/       buffer.cpp, tiling.cpp, classical.cpp
src/io/          ppm.cpp (موقت)
src/util/        log.cpp, errors.cpp
tests/           test.h, test_main.cpp, test_tiling.cpp, test_classical.cpp, test_ppm.cpp
../cli/main.cpp  atlas-cli
```

## یادداشت فایل‌به‌فایل (طبق قاعدهٔ `docs/notes/JOURNAL.md`)

| فایل | یادداشت |
|---|---|
| `CMakeLists.txt` | بدون وابستگی خارجی؛ گزینه‌های `ATLAS_BUILD_CLI/TESTS` تا در موبایل فقط lib ساخته شود. |
| `include/atlas/types.h` | تصویر planar interleaved float32؛ `Tile/Rect` ساده تا سریال‌پذیر بماند. |
| `include/atlas/image.h` | قرارداد «ورودی دست‌نخورده، خروجی نو» تا QA و مقایسهٔ قبل/بعد ممکن باشد. |
| `src/image/buffer.cpp` | crop فعلاً کپی می‌کند؛ در P1 با view/stride صفر-کپی می‌شود. |
| `src/image/tiling.cpp` | دو باگ کلاسیک (سرریز لبه، پیکسل بدون پوشش) با تست پوشش بسته شده‌اند. |
| `src/image/classical.cpp` | Lanczos جداپذیر + گاوس + unsharp؛ مسیر «همه‌جا جواب می‌دهد». |
| `src/io/ppm.cpp` | باگ delimiter در هدر PPM در اولین اجرا پیدا و رفع شد (تست round-trip). |
| `src/api/context.cpp` | ABI قفل شد؛ probe واقعی دستگاه در P1. |
| `cli/main.cpp` | `include atlas/buffer.h` در اولین build جا افتاده بود؛ اکنون کامل است. |
| `tests/*` | ۱۱ تست، صفر وابستگی، کد خروج ۱ در شکست برای CI. |

## کار بعدی (P1)

1. افزودن ncnn به‌عنوان submodule و بک‌اند Vulkan/CPU (`P1-01`).
2. بارگذاری `realesrgan-x4plus` و اجرای SR کاشی‌ای واقعی (`P1-03`).
3. جایگزینی PPM با JPEG/PNG (`P0-05`، `P0-06`).

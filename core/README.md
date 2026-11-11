# atlas-core — هستهٔ C++20

این پوشه برش اجرایی نقشهٔ `docs/18-build-plan-wbs.md` است: یک کتابخانهٔ کوچک که
همین حالا build می‌شود، روی هر سه پلتفرم (ویندوز/لینوکس/مک و بعد موبایل) کار
می‌کند و مسیر «مدل واقعی» را هم دارد، بدون اینکه وزن‌ها وارد گیت شوند.

## وضعیت فعلی (P1)

| بخش | وضعیت |
|---|---|
| ساخت با CMake + C++20 | ✅ بدون وابستگی سیستمی |
| بافر تصویر float32 + crop/pad/clamp | ✅ |
| کاشی‌بندی با پوشش کامل و چسبیدن به لبه | ✅ |
| موتور SR کاشی‌ای + دوخت raised-cosine | ✅ (`src/stages/sr.cpp`) |
| مسیر کلاسیک: Lanczos + Gaussian + unsharp | ✅ ~۹ MP/s روی ۲ هستهٔ CPU |
| کدک واقعی JPEG/PNG/BMP/TGA/`@8/16bit` | ✅ stb (vendored، MIT/PD) |
| بک‌اند ncnn (CPU؛ Vulkan با فلگ) | ✅ `ATLAS_WITH_NCNN=ON` |
| C ABI (`atlas_create`, `atlas_devices_json`, …) | ✅ |
| CLI: `version`, `devices`, `convert`, `enhance` | ✅ |
| تست واحد | ✅ ۱۹ تست، صفر شکست |
| WebP/TIFF + EXIF orientation | ⏳ P1-05 |
| خطی‌سازی رنگ (sRGB → linear) | ⏳ P0-07 |

## ساخت و اجرا

```bash
# مسیر کلاسیک — هیچ وابستگی خارجی ندارد
cmake -S core -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

./build/atlas-cli version
./build/atlas-cli devices
./build/atlas-cli convert photo.jpg out.png          # decode/encode بدون تغییر هندسه
./build/atlas-cli enhance photo.jpg out2x.png --scale 2 --sharpen 20
./build/atlas-tests                                   # ۱۹ تست
```

### مسیر مدل واقعی (ncnn)

```bash
# ۱) وزن‌ها (هرگز در گیت نیستند)
python3 tools/fetch_models.py --from-wheel      # یا --from-dir /path/to/models
python3 tools/fetch_models.py --check

# ۲) ncnn را بسازید (CPU کافی است؛ Vulkan برای GPU)
cmake -S ncnn -B ncnn-build -DNCNN_VULKAN=OFF -DNCNN_BUILD_TOOLS=OFF \
      -DNCNN_BUILD_EXAMPLES=OFF -DCMAKE_INSTALL_PREFIX=$PWD/ncnn-install
cmake --build ncnn-build -j && cmake --install ncnn-build

# ۳) هسته را با بک‌اند مدل بسازید
cmake -S core -B build-ncnn -DCMAKE_BUILD_TYPE=Release \
      -DATLAS_WITH_NCNN=ON -DCMAKE_PREFIX_PATH=$PWD/ncnn-install
cmake --build build-ncnn -j

./build-ncnn/atlas-cli enhance photo.jpg out4x.png \
      --param models/weights/realesrgan-x4plus.param \
      --weights models/weights/realesrgan-x4plus.bin \
      --scale 4 --tile 256 --overlap 32
```

> بدون `-DATLAS_WITH_NCNN=ON` هم همه‌چیز build می‌شود؛ فقط `enhance` با
> `--param/--weights` خطای روشن می‌دهد. این عمدی است تا موبایل/CI سبک بماند.

### نمونهٔ اجرا و اعداد واقعی (سندباکس، ۲ هستهٔ CPU، بدون GPU)

```
classical  : 3.00 MP -> 12.00 MP   در 1.32s  (9.11 MP/s)
x4plus     : 1024x768 -> 4096x3072 (12.58 MP) در 308.5s (0.041 MP/s)
animevideo : 1024x768 -> 2048x1536 ( 3.15 MP) در   6.6s (0.48 MP/s)
```

نتیجهٔ مهم: `realesrgan-x4plus` روی CPU بدون GPU عملاً برای عکس ۱۲ مگاپیکسلی
«شب تا صبح» است؛ برای همین کاشی‌بندی + مدل سبک `animevideov3` و در نهایت مسیر
GPU (Vulkan) پیش‌فرض می‌شود و CPU فقط fallback است. اعداد کیفیت در
`docs/18-build-plan-wbs.md` §۱.۶ آمده است.

## ساختار

```
include/atlas/   atlas.h (C ABI), types.h, image.h, buffer.h, image_io.h, backend.h, sr.h, …
src/api/         context.cpp (ABI + فهرست واقعی دستگاه‌ها)
src/image/       buffer.cpp, tiling.cpp, classical.cpp
src/io/          stb_impl.cpp (فقط implementation), image_io.cpp (codec)، ppm.cpp (قدیمی)
src/backend/     classical_backend.cpp, ncnn_backend.cpp
src/stages/      sr.cpp (کاشی + دوخت)
src/util/        log.cpp, errors.cpp
third_party/stb/ stb_image.h, stb_image_write.h + LICENSE
tests/           test.h, test_main.cpp, test_{tiling,classical,ppm,image_io,sr}.cpp
../cli/main.cpp  atlas-cli
```

## یادداشت فایل‌به‌فایل (طبق قاعدهٔ `docs/notes/JOURNAL.md`)

| فایل | یادداشت |
|---|---|
| `CMakeLists.txt` | گزینه‌های `ATLAS_WITH_NCNN`, `ATLAS_BUILD_CLI/TESTS`؛ هدرهای stb فقط در TU خودشان با `-w` کامپایل می‌شوند تا هشدارهایش بقیه را شلوغ نکند. |
| `include/atlas/types.h` | تصویر interleaved float32؛ `Tile/Rect` ساده تا سریال‌پذیر بماند. |
| `include/atlas/image.h` | قرارداد «ورودی دست‌نخورده، خروجی نو» تا QA و مقایسهٔ قبل/بعد ممکن باشد. |
| `include/atlas/image_io.h` | یک API برای load/save با پیام خطای متنی؛ پسوند تعیین‌کنندهٔ فرمت است. |
| `include/atlas/backend.h` | `IBackend` کوچک: `scale()`, `name()`, `ok()`, `run()`؛ هر بک‌اند باید بدون حالت (stateless) باشد تا موتور SR بتواند کاشی‌ها را هر ترتیبی بدهد. |
| `include/atlas/sr.h` | قرارداد موتور: کاشی، overlap، callback پیشرفت؛ خروجی همیشه `w*scale × h*scale`. |
| `src/io/stb_impl.cpp` | اشتباه اول: `#define STBI_NO_STDIO 0` باعث حذف `stbi_load` شد (stb فقط «تعریف‌نشدن» را می‌فهمد)؛ حذف شد. |
| `src/io/image_io.cpp` | ۱۶ بیت به float32 نرمال می‌شود؛ کانال آلفا حفظ می‌شود اما در SR فعلاً استفاده نمی‌شود. |
| `src/backend/classical_backend.cpp` | Lanczos + unsharp؛ مرجع «همه‌جا جواب می‌دهد» و مبنای مقایسه. |
| `src/backend/ncnn_backend.cpp` | ورودی به مضرب ۸ pad می‌شود (الزام convهای ncnn) و بعد به `w*scale` بریده می‌شود؛ نرمال‌سازی فقط `/255` و ترتیب RGB — همان چیزی که وزن‌های Real-ESRGAN انتظار دارند. |
| `src/stages/sr.cpp` | اولین build شکست خورد (`crop` در `buffer.h` بود، نه `image.h`). لبهٔ بیرونی تصویر feather نمی‌شود وگرنه وزن صفر و پیکسل خالی می‌ماند. |
| `src/api/context.cpp` | `devices` حالا فقط چیزهایی را اعلام می‌کند که واقعاً اجرا می‌شوند (`classical-cpu` و در صورت کامپایل `ncnn-cpu`). |
| `cli/main.cpp` | `convert` اضافه شد چون برای ساخت جفت HR/LR و QA به decode→encode بدون ابزار بیرونی نیاز داشتیم. |
| `tests/test_image_io.cpp` | PNG باید تقریباً بی‌اتلاف باشد؛ JPEG q95 فقط «نزدیک». |
| `tests/test_sr.cpp` | تست کلیدی: تصویر ثابت باید ثابت بماند؛ اگر وزن‌های دوخت غلط باشند همان‌جا خط ظاهر می‌شود. |
| `../tools/fetch_models.py` | چون assetهای Release گیت‌هاب در محیط بسته دانلود نمی‌شوند، مسیر «استخراج از wheel پایتون PyPI» اضافه شد؛ sha256 هر فایل چک می‌شود. |

## کار بعدی (P1 ادامه)

1. WebP/TIFF + EXIF orientation (`P1-05`) و سپس مسیر libjpeg-turbo/libpng برای سرعت و ۱۶ بیت واقعی.
2. Vulkan backend در ncnn (`P1-01`) + انتخاب خودکار دستگاه با micro-benchmark.
3. اتصال `sr.cpp` به pipeline چهره (`docs/15`): تشخیص چهره، دروازهٔ هویت، کامپوزیت.
4. متریک‌ها (PSNR/SSIM/LPIPS) به‌عنوان ابزار ارزیابی، نه معیار تنها.

---

## 📝 یادداشت من (پایان این فایل)

**تاریخ:** ۲۰۲۶-۱۱-۱۱ · **فایل:** `core/README.md`

- **مسلم شد:** «چند تست سبز» با «مسیر واقعی» یکی نیست؛ اولین اجرای مدل روی یک
  چهرهٔ واقعی نشان داد خروجی x4plus چشم‌ها را اشباع و پوست را پلاستیکی می‌کند —
  همان چیزی که در `docs/14` پیش‌بینی شده بود. این تجربه، دروازهٔ هویت را از
  «نظریه» به «الزام» تبدیل کرد.
- **فرض:** خوانندهٔ این فایل کسی است که می‌خواهد در ۵ دقیقه بفهمد چه چیزی الان
  کار می‌کند؛ بنابراین اعداد واقعی را کنار دستورها گذاشتم، نه در فایل جدا.
- **سؤال باز:** آیا مسیر libjpeg-turbo باید پیش‌فرض ویندوز شود و stb فقط برای
  موبایل بماند؟ (سرعت decode برای فایل‌های ۵۰ مگابایتی مهم می‌شود.)
- **قدم بعدی:** Vulkan + pipeline چهره، با همان اعداد به‌عنوان خط پایه.

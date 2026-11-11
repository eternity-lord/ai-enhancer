# P1 skeleton — یادداشت فایل (2026-11-11، به‌روزرسانی P1.5)
# تغییرات نسبت به P0:
#  - کدک واقعی JPEG/PNG/BMP/TGA با stb (vendored، بدون وابستگی سیستمی)
#  - بک‌اند مدل با ncnn (اختیاری، ATLAS_WITH_NCNN=ON) + بک‌اند کلاسیک
#  - موتور SR کاشی‌ای با دوخت raised-cosine
#  - مدل‌ها/وزن‌ها در گیت نیستند؛ tools/fetch_models.py آن‌ها را می‌آورد.
#  - P1.5: EXIF orientation، تبدیل sRGB↔خطی، متریک‌های PSNR/SSIM برای ارزیابی.
cmake_minimum_required(VERSION 3.20)
project(atlas_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ATLAS_BUILD_CLI "Build atlas-cli" ON)
option(ATLAS_BUILD_TESTS "Build unit tests" ON)
option(ATLAS_WITH_NCNN "Build the ncnn inference backend" OFF)

add_library(atlas-core STATIC
  src/util/log.cpp
  src/util/errors.cpp
  src/image/buffer.cpp
  src/image/color.cpp
  src/image/tiling.cpp
  src/image/classical.cpp
  src/io/ppm.cpp
  src/io/stb_impl.cpp
  src/io/image_io.cpp
  src/io/exif.cpp
  src/backend/classical_backend.cpp
  src/backend/ncnn_backend.cpp
  src/stages/sr.cpp
  src/metrics/metrics.cpp
  src/api/context.cpp
)

target_include_directories(atlas-core PUBLIC include)
target_include_directories(atlas-core PRIVATE third_party/stb)
target_compile_options(atlas-core PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
  $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  # هدرهای stb هشدارهای زیادی می‌دهند؛ فقط برای همان TU خاموش می‌کنیم.
  set_source_files_properties(src/io/stb_impl.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()

if (ATLAS_WITH_NCNN)
  find_package(ncnn REQUIRED)
  target_link_libraries(atlas-core PUBLIC ncnn)
  target_compile_definitions(atlas-core PUBLIC ATLAS_WITH_NCNN=1)
  message(STATUS "Atlas: ncnn backend enabled (${ncnn_INCLUDE_DIRS})")
else()
  message(STATUS "Atlas: ncnn backend disabled (classical only)")
endif()

if (ATLAS_BUILD_CLI)
  add_executable(atlas-cli ${CMAKE_CURRENT_SOURCE_DIR}/../cli/main.cpp)
  target_link_libraries(atlas-cli PRIVATE atlas-core)
endif()

if (ATLAS_BUILD_TESTS)
  enable_testing()
  add_executable(atlas-tests
    tests/test_main.cpp
    tests/test_tiling.cpp
    tests/test_classical.cpp
    tests/test_ppm.cpp
    tests/test_image_io.cpp
    tests/test_sr.cpp
    tests/test_metrics.cpp
    tests/test_color.cpp
    tests/test_exif.cpp
  )
  target_link_libraries(atlas-tests PRIVATE atlas-core)
  add_test(NAME atlas-unit COMMAND atlas-tests)
endif()

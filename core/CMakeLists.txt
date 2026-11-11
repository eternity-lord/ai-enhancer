# P0 skeleton — یادداشت فایل
# تاریخ: 2026-11-11
# هدف: ساخت هفتگیِ هستهٔ Atlas (C++20) + CLI حداقلی که همین حالا اجرا می‌شود.
# تصمیم: تا زمانی که libjpeg-turbo/libpng در سندباکس نیست، IO روی PPM P6 است
#         (کدک‌های واقعی در P0-05/P0-06 جایگزین می‌شوند).
# نکته: هیچ وابستگی خارجی در این مرحله نیست، پس روی هر پلتفرمی build می‌شود.
cmake_minimum_required(VERSION 3.20)
project(atlas_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ATLAS_BUILD_CLI "Build atlas-cli" ON)
option(ATLAS_BUILD_TESTS "Build unit tests" ON)

add_library(atlas-core STATIC
  src/util/log.cpp
  src/util/errors.cpp
  src/image/buffer.cpp
  src/image/tiling.cpp
  src/image/classical.cpp
  src/io/ppm.cpp
  src/api/context.cpp
)

target_include_directories(atlas-core PUBLIC include)
target_compile_options(atlas-core PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
  $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

if (ATLAS_BUILD_CLI)
  add_executable(atlas-cli ${CMAKE_CURRENT_SOURCE_DIR}/../cli/main.cpp)
  target_link_libraries(atlas-cli PRIVATE atlas-core)
endif()

if (ATLAS_BUILD_TESTS)
  enable_testing()
  add_executable(atlas-tests
    tests/test_tiling.cpp
    tests/test_classical.cpp
    tests/test_ppm.cpp
    tests/test_main.cpp
  )
  target_link_libraries(atlas-tests PRIVATE atlas-core)
  add_test(NAME atlas-unit COMMAND atlas-tests)
endif()

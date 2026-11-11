// SPDX-License-Identifier: Apache-2.0
// یادداشت: یک هارنس تست ۵۰ خطی به‌جای GoogleTest تا P0 صفر وابستگی بماند.
// در P6 اگر لازم شد به Catch2 مهاجرت می‌کنیم؛ قرارداد CHECK تغییر نمی‌کند.
#pragma once

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace atlas::test {

struct Case {
  const char* name;
  void (*fn)();
};

std::vector<Case>& registry();
int& failures();

struct Registrar {
  Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

}  // namespace atlas::test

#define ATLAS_TEST(name)                                        \
  static void name();                                           \
  static ::atlas::test::Registrar reg_##name(#name, name);      \
  static void name()

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::fprintf(stderr, "  CHECK failed: %s (%s:%d)\n", #cond, __FILE__,   \
                   __LINE__);                                                \
      ++::atlas::test::failures();                                           \
    }                                                                        \
  } while (0)

#define CHECK_NEAR(a, b, eps)                                                \
  do {                                                                       \
    const double va = (a), vb = (b);                                         \
    if (std::fabs(va - vb) > (eps)) {                                        \
      std::fprintf(stderr, "  CHECK_NEAR failed: %s=%.6f vs %s=%.6f (%s:%d)\n", \
                   #a, va, #b, vb, __FILE__, __LINE__);                      \
      ++::atlas::test::failures();                                           \
    }                                                                        \
  } while (0)

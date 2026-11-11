// SPDX-License-Identifier: Apache-2.0
// یادداشت: اجرای همهٔ تست‌ها و چاپ خلاصه؛ کد خروج ۱ در صورت شکست (برای CI).
#include "test.h"

namespace atlas::test {

std::vector<Case>& registry() {
  static std::vector<Case> cases;
  return cases;
}

int& failures() {
  static int count = 0;
  return count;
}

}  // namespace atlas::test

int main() {
  int executed = 0;
  for (const auto& c : atlas::test::registry()) {
    const int before = atlas::test::failures();
    c.fn();
    ++executed;
    std::printf("[%s] %s\n", atlas::test::failures() == before ? "ok" : "FAIL", c.name);
  }
  std::printf("%d test(s), %d failed check(s)\n", executed, atlas::test::failures());
  return atlas::test::failures() == 0 ? 0 : 1;
}

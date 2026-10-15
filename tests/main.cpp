// SPDX-License-Identifier: MIT
#include <cstring>

#include "test.hpp"

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0, failed = 0;
    for (const auto& c : cruth_test::registry()) {
        if (filter && !std::strstr(c.name, filter)) continue;
        int before = cruth_test::failures();
        c.fn();
        ++run;
        bool ok = cruth_test::failures() == before;
        if (!ok) ++failed;
        std::printf("[%s] %s\n", ok ? " ok " : "FAIL", c.name);
    }
    std::printf("\n%d tests, %d failed\n", run, failed);
    return failed == 0 && run > 0 ? 0 : 1;
}

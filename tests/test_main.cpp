#include "test_framework.hpp"

int main() {
    int failed_tests = 0;
    for (const auto &[name, fn]: test::registry()) {
        const int before = test::failures();
        fn();
        const bool ok = test::failures() == before;
        failed_tests += !ok;
        std::println("{} {}", ok ? "[ ok ]" : "[FAIL]", name);
    }
    std::println("\n{} tests, {} failed, {} failed checks", test::registry().size(), failed_tests, test::failures());
    return failed_tests == 0 ? 0 : 1;
}

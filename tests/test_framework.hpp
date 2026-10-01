#pragma once
// Minimal self-registering test framework (no external dependencies, in keeping with the rest of the project).
//
//   TEST(name) { CHECK(expr); CHECK_EQ(a, b); }
//
// Each TEST registers itself at static-init time; tests/test_main.cpp runs them all and exits non-zero
// if any check failed, so CTest picks up failures.
#include <cstdio>
#include <format>
#include <print>
#include <source_location>
#include <vector>

namespace test {
    struct Case {
        const char *name;
        void (*fn)();
    };
    inline std::vector<Case> &registry() {
        static std::vector<Case> cases;
        return cases;
    }
    inline int &failures() {
        static int count = 0;
        return count;
    }
    struct Registrar {
        Registrar(const char *name, void (*fn)()) { registry().push_back({name, fn}); }
    };

    inline void check(const bool ok, const char *expr, const std::source_location loc = std::source_location::current()) {
        if (!ok) {
            ++failures();
            std::println(stderr, "    FAIL {}:{}: {}", loc.file_name(), loc.line(), expr);
        }
    }

    template <class A, class B>
    void check_eq(const A &a, const B &b, const char *expr, const std::source_location loc = std::source_location::current()) {
        if (!(a == b)) {
            ++failures();
            if constexpr (std::formattable<A, char> && std::formattable<B, char>)
                std::println(stderr, "    FAIL {}:{}: {} ({} != {})", loc.file_name(), loc.line(), expr, a, b);
            else
                std::println(stderr, "    FAIL {}:{}: {}", loc.file_name(), loc.line(), expr);
        }
    }
} // namespace test

#define TEST(name)                                                                                                     \
    static void name();                                                                                                \
    static const test::Registrar name##_registrar{#name, name};                                                       \
    static void name()

#define CHECK(expr) test::check(static_cast<bool>(expr), #expr)
#define CHECK_EQ(a, b) test::check_eq((a), (b), #a " == " #b)

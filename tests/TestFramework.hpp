#pragma once

// Minimal header-only test harness — no third-party dependency, just a
// vector of TEST_CASE functions invoked in registration order. Fails a
// test via exception so one bad assertion doesn't take down the run.

#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace lob::test {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const std::string& name, std::function<void()> fn) {
        registry().push_back(TestCase{name, std::move(fn)});
    }
};

struct AssertionFailure {
    std::string message;
};

inline int run_all() {
    int passed = 0;
    int failed = 0;
    for (const auto& t : registry()) {
        try {
            t.fn();
            ++passed;
            std::cout << "[PASS] " << t.name << '\n';
        } catch (const AssertionFailure& f) {
            ++failed;
            std::cout << "[FAIL] " << t.name << ": " << f.message << '\n';
        } catch (const std::exception& e) {
            ++failed;
            std::cout << "[FAIL] " << t.name << ": unexpected exception: " << e.what() << '\n';
        }
    }
    std::cout << "\n" << passed << " passed, " << failed << " failed, "
               << registry().size() << " total\n";
    return failed == 0 ? 0 : 1;
}

} // namespace lob::test

#define LOB_CONCAT_INNER(a, b) a##b
#define LOB_CONCAT(a, b) LOB_CONCAT_INNER(a, b)

#define TEST_CASE(name)                                                          \
    static void LOB_CONCAT(test_fn_, __LINE__)();                                \
    static ::lob::test::Registrar LOB_CONCAT(test_registrar_, __LINE__)(         \
        name, LOB_CONCAT(test_fn_, __LINE__));                                   \
    static void LOB_CONCAT(test_fn_, __LINE__)()

#define EXPECT_TRUE(cond)                                                        \
    do {                                                                          \
        if (!(cond)) {                                                            \
            std::ostringstream oss;                                               \
            oss << "EXPECT_TRUE(" #cond ") failed at " << __FILE__ << ":" << __LINE__; \
            throw ::lob::test::AssertionFailure{oss.str()};                       \
        }                                                                          \
    } while (0)

#define EXPECT_EQ(a, b)                                                          \
    do {                                                                          \
        auto va = (a);                                                            \
        auto vb = (b);                                                            \
        if (!(va == vb)) {                                                        \
            std::ostringstream oss;                                               \
            oss << "EXPECT_EQ(" #a ", " #b ") failed at " << __FILE__ << ":" << __LINE__ \
                << " — got " << va << " vs " << vb;                               \
            throw ::lob::test::AssertionFailure{oss.str()};                       \
        }                                                                          \
    } while (0)

#define EXPECT_THROWS(expr)                                                      \
    do {                                                                          \
        bool threw = false;                                                       \
        try { (expr); } catch (...) { threw = true; }                             \
        if (!threw) {                                                             \
            std::ostringstream oss;                                               \
            oss << "EXPECT_THROWS(" #expr ") did not throw at " << __FILE__ << ":" << __LINE__; \
            throw ::lob::test::AssertionFailure{oss.str()};                       \
        }                                                                          \
    } while (0)

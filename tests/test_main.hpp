#ifndef WECHESS_TEST_HPP
#define WECHESS_TEST_HPP

// A deliberately tiny test harness: no external dependency, clear output, and a
// non-zero exit status on failure so CI/ctest can gate on it.
#include <cmath>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace wctest {

struct Case {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<Case>& cases() {
    static std::vector<Case> all;
    return all;
}

struct Failure {
    std::string message;
};

inline int& failures() {
    static int count = 0;
    return count;
}
inline int& checks() {
    static int count = 0;
    return count;
}
inline std::string& currentTest() {
    static std::string name;
    return name;
}

struct Registrar {
    Registrar(const std::string& name, std::function<void()> fn) { cases().push_back({name, std::move(fn)}); }
};

inline void fail(const std::string& file, int line, const std::string& expression) {
    ++failures();
    std::cout << "    FAIL " << file << ":" << line << "  " << expression << "\n";
}

inline int run() {
    int failedCases = 0;
    for (const auto& test : cases()) {
        currentTest() = test.name;
        const int before = failures();
        std::cout << "[ RUN  ] " << test.name << "\n";
        try {
            test.fn();
        } catch (const Failure& f) {
            ++failures();
            std::cout << "    FAIL unexpected assertion forwarding: " << f.message << "\n";
        } catch (const std::exception& ex) {
            ++failures();
            std::cout << "    FAIL exception: " << ex.what() << "\n";
        } catch (...) {
            ++failures();
            std::cout << "    FAIL unknown exception\n";
        }
        if (failures() == before) {
            std::cout << "[  OK  ] " << test.name << "\n";
        } else {
            std::cout << "[ FAIL ] " << test.name << "\n";
            ++failedCases;
        }
    }
    std::cout << "\n" << (cases().size() - static_cast<std::size_t>(failedCases)) << "/" << cases().size()
              << " test cases passed (" << checks() << " assertions, " << failures() << " failures)\n";
    return failedCases == 0 ? 0 : 1;
}

}  // namespace wctest

#define TEST(name)                                                    \
    static void name();                                               \
    static ::wctest::Registrar registrar_##name(#name, name);          \
    static void name()

#define CHECK(expr)                                                       \
    do {                                                                  \
        ++::wctest::checks();                                             \
        if (!(expr)) ::wctest::fail(__FILE__, __LINE__, #expr);           \
    } while (0)

#define CHECK_EQ(actual, expected)                                                              \
    do {                                                                                        \
        ++::wctest::checks();                                                                   \
        const auto& a_ = (actual);                                                              \
        const auto& e_ = (expected);                                                            \
        if (!(a_ == e_)) {                                                                      \
            std::cout << "    FAIL " << __FILE__ << ":" << __LINE__ << "  " << #actual          \
                      << " == " << #expected << "\n      actual:   " << a_ << "\n      expected: " \
                      << e_ << "\n";                                                            \
            ++::wctest::failures();                                                             \
        }                                                                                       \
    } while (0)

#define CHECK_NEAR(actual, expected, tolerance)                                                      \
    do {                                                                                             \
        ++::wctest::checks();                                                                        \
        const double a_ = static_cast<double>(actual);                                               \
        const double e_ = static_cast<double>(expected);                                             \
        const double t_ = static_cast<double>(tolerance);                                            \
        if (!(std::abs(a_ - e_) <= t_)) {                                                            \
            std::cout << "    FAIL " << __FILE__ << ":" << __LINE__ << "  " << #actual                \
                      << " ~= " << #expected << "\n      actual:   " << a_ << "\n      expected: "     \
                      << e_ << " (+/-" << t_ << ")\n";                                               \
            ++::wctest::failures();                                                                  \
        }                                                                                            \
    } while (0)

#endif  // WECHESS_TEST_HPP

#pragma once

#include <cmath>
#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace xtodo_test {

[[noreturn]] inline void Fail(const char* file, int line, const std::string& message) {
    std::ostringstream out;
    out << file << ':' << line << ": " << message;
    throw std::runtime_error(out.str());
}

template <class A, class B>
inline void ExpectEqual(const A& actual, const B& expected, const char* actualExpr,
                        const char* expectedExpr, const char* file, int line) {
    if (!(actual == expected)) {
        std::ostringstream out;
        out << "Expected equality: " << actualExpr << " == " << expectedExpr;
        Fail(file, line, out.str());
    }
}

inline void ExpectNear(double actual, double expected, double tolerance,
                       const char* actualExpr, const char* expectedExpr,
                       const char* file, int line) {
    if (std::fabs(actual - expected) > tolerance) {
        std::ostringstream out;
        out << "Expected near: " << actualExpr << " ~= " << expectedExpr
            << " (tolerance " << tolerance << ")";
        Fail(file, line, out.str());
    }
}

struct TestCase {
    const char* name;
    void (*fn)();
};

template <size_t N>
inline int RunTests(const char* suiteName, const TestCase (&tests)[N]) {
    int failed = 0;
    for (const TestCase& test : tests) {
        try {
            test.fn();
            std::cout << "[PASS] " << suiteName << "." << test.name << '\n';
        } catch (const std::exception& e) {
            ++failed;
            std::cerr << "[FAIL] " << suiteName << "." << test.name << ": " << e.what() << '\n';
        } catch (...) {
            ++failed;
            std::cerr << "[FAIL] " << suiteName << "." << test.name << ": unknown exception\n";
        }
    }
    if (failed) {
        std::cerr << failed << " test(s) failed in " << suiteName << '\n';
        return 1;
    }
    std::cout << N << " test(s) passed in " << suiteName << '\n';
    return 0;
}

} // namespace xtodo_test

#define EXPECT_TRUE(expr) \
    do { if (!(expr)) ::xtodo_test::Fail(__FILE__, __LINE__, std::string("Expected true: ") + #expr); } while (false)

#define EXPECT_FALSE(expr) \
    do { if ((expr)) ::xtodo_test::Fail(__FILE__, __LINE__, std::string("Expected false: ") + #expr); } while (false)

#define EXPECT_EQ(actual, expected) \
    do { ::xtodo_test::ExpectEqual((actual), (expected), #actual, #expected, __FILE__, __LINE__); } while (false)

#define EXPECT_NEAR(actual, expected, tolerance) \
    do { ::xtodo_test::ExpectNear((actual), (expected), (tolerance), #actual, #expected, __FILE__, __LINE__); } while (false)

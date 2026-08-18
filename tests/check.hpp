// Minimal assert based test runner. Deliberately tiny: the project has no
// third party dependencies, so the test framework is part of the repository.
#pragma once
#include <sstream>
#include <string>
#include <vector>

namespace check {

using TestFn = void (*)();

struct TestCase {
    std::string name;
    TestFn fn;
};

std::vector<TestCase>& registry();

struct Registrar {
    Registrar(const char* name, TestFn fn) { registry().push_back({name, fn}); }
};

// Set by the macros below; read by the runner to decide the exit code.
extern int failures;

void report(const char* file, int line, const std::string& message);

template <typename A, typename B>
std::string describe(const A& a, const B& b) {
    std::ostringstream out;
    out << "\n    left  = " << a << "\n    right = " << b;
    return out.str();
}

}  // namespace check

#define LEX_TEST(name)                                             \
    static void name##_body();                                     \
    static ::check::Registrar name##_registrar(#name, &name##_body); \
    static void name##_body()

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) ::check::report(__FILE__, __LINE__, "CHECK(" #cond ") failed"); \
    } while (0)

#define CHECK_EQ(a, b)                                                  \
    do {                                                                \
        const auto& lex_a = (a);                                        \
        const auto& lex_b = (b);                                        \
        if (!(lex_a == lex_b))                                          \
            ::check::report(__FILE__, __LINE__,                         \
                            "CHECK_EQ(" #a ", " #b ") failed" +         \
                                ::check::describe(lex_a, lex_b));       \
    } while (0)

#define CHECK_CONTAINS(haystack, needle)                                          \
    do {                                                                          \
        const std::string lex_h = (haystack);                                     \
        const std::string lex_n = (needle);                                       \
        if (lex_h.find(lex_n) == std::string::npos)                               \
            ::check::report(__FILE__, __LINE__,                                   \
                            "CHECK_CONTAINS failed" + ::check::describe(lex_h, lex_n)); \
    } while (0)

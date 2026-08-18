#include "check.hpp"

#include <cstring>
#include <iostream>

namespace check {

std::vector<TestCase>& registry() {
    static std::vector<TestCase> cases;
    return cases;
}

int failures = 0;

void report(const char* file, int line, const std::string& message) {
    ++failures;
    std::cout << "    FAIL " << file << ":" << line << ": " << message << "\n";
}

}  // namespace check

int main(int argc, char** argv) {
    const bool list_only = argc > 1 && std::strcmp(argv[1], "--list") == 0;
    std::vector<std::string> wanted(argv + 1, argv + argc);
    if (list_only) wanted.clear();

    int selected = 0;
    int failed_cases = 0;
    for (const auto& test : check::registry()) {
        if (list_only) {
            std::cout << test.name << "\n";
            ++selected;
            continue;
        }
        if (!wanted.empty()) {
            bool match = false;
            for (const auto& name : wanted) match = match || name == test.name;
            if (!match) continue;
        }
        ++selected;
        const int before = check::failures;
        std::cout << "[ RUN  ] " << test.name << "\n";
        test.fn();
        const bool ok = check::failures == before;
        failed_cases += ok ? 0 : 1;
        std::cout << (ok ? "[  OK  ] " : "[ FAIL ] ") << test.name << "\n";
    }

    if (selected == 0) {
        std::cout << "no test case matched the requested name\n";
        return 2;
    }
    if (!list_only) {
        std::cout << selected << " case(s) run, " << failed_cases << " failed, "
                  << check::failures << " assertion(s) failed\n";
    }
    return failed_cases == 0 ? 0 : 1;
}

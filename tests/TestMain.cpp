#include <exception>
#include <iostream>
#include <string_view>
#include <utility>
#include <vector>

void runScaleTests();
void runGeneratorTests();

int main() {
    const std::vector<std::pair<std::string_view, void (*)()>> suites{
        {"Scale", runScaleTests}, {"Generator", runGeneratorTests}};
    auto failures = 0;
    for (const auto& [name, suite] : suites) {
        try {
            suite();
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
        }
    }
    std::cout << suites.size() - static_cast<std::size_t>(failures) << "/" << suites.size()
              << " suites passed\n";
    return failures == 0 ? 0 : 1;
}


#pragma once

#include <cmath>
#include <stdexcept>
#include <string>

inline void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

inline void requireNear(double actual, double expected, double tolerance, const std::string& message) {
    if (std::abs(actual - expected) > tolerance)
        throw std::runtime_error(message + " (actual=" + std::to_string(actual) +
                                 ", expected=" + std::to_string(expected) + ")");
}


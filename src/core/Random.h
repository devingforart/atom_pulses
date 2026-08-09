#pragma once

#include <algorithm>
#include <cstdint>

namespace pulso {

class Random final {
public:
    explicit Random(std::uint64_t seed) noexcept : state(seed == 0 ? 0x9e3779b97f4a7c15ULL : seed) {}

    [[nodiscard]] std::uint64_t next() noexcept {
        auto x = state;
        x ^= x >> 12U;
        x ^= x << 25U;
        x ^= x >> 27U;
        state = x;
        return x * 0x2545F4914F6CDD1DULL;
    }

    [[nodiscard]] double unit() noexcept {
        return static_cast<double>(next() >> 11U) * (1.0 / 9007199254740992.0);
    }

    [[nodiscard]] bool chance(double probability) noexcept {
        return unit() < std::clamp(probability, 0.0, 1.0);
    }

    [[nodiscard]] int range(int minimum, int maximumInclusive) noexcept {
        const auto width = static_cast<std::uint64_t>(maximumInclusive - minimum + 1);
        return minimum + static_cast<int>(next() % width);
    }

private:
    std::uint64_t state;
};

} // namespace pulso


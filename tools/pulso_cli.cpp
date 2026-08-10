#include "core/Generator.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using namespace pulso;

namespace {
Role parseRole(std::string_view value) {
    if (value == "drums" || value == "percussion") return Role::Percussion;
    if (value == "counter" || value == "countermelody") return Role::Countermelody;
    return Role::Bass;
}

std::vector<std::vector<int>> minorProgression(int root, int bars) {
    const std::array<std::array<int, 3>, 4> degrees{{
        {0, 3, 7},   // i
        {8, 0, 3},   // VI
        {5, 8, 0},   // iv
        {7, 11, 2},  // V: sensible para crear tensión antes del retorno
    }};
    std::vector<std::vector<int>> harmony;
    harmony.reserve(static_cast<std::size_t>(bars));
    for (auto bar = 0; bar < bars; ++bar) {
        const auto& chord = degrees[static_cast<std::size_t>(bar % 4)];
        harmony.push_back({positiveModulo(root + chord[0], 12),
                           positiveModulo(root + chord[1], 12),
                           positiveModulo(root + chord[2], 12)});
    }
    return harmony;
}
} // namespace

int main(int argc, char** argv) {
    GenerationContext context;
    context.role = argc > 1 ? parseRole(argv[1]) : Role::Bass;
    context.seed = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 42;
    context.rootPitchClass = argc > 3 ? std::atoi(argv[3]) % 12 : 0;
    context.rootPitchClass = positiveModulo(context.rootPitchClass, 12);
    context.bars = std::clamp(argc > 4 ? std::atoi(argv[4]) : 4, 1, 16);
    context.evolutionStep = argc > 5 ? std::strtoull(argv[5], nullptr, 10) : 0;
    context.scale = ScaleKind::Minor;
    context.harmonyByBar = minorProgression(context.rootPitchClass, context.bars);
    context.chordPitchClasses = context.harmonyByBar.front();

    const auto pattern = Generator{}.generate(context);
    std::cout << "PULSO pattern | role=" << roleNames[static_cast<std::size_t>(context.role)]
              << " seed=" << context.seed << " root=" << context.rootPitchClass
              << " bars=" << context.bars << " evolution=" << context.evolutionStep << '\n';
    std::cout << "beat\tduration\tpitch\tvelocity\tchannel\n";
    for (const auto& note : pattern.notes)
        std::cout << std::fixed << std::setprecision(2) << note.startBeat << '\t'
                  << note.durationBeats << '\t' << note.pitch << '\t' << note.velocity << '\t'
                  << note.channel << '\n';
    return 0;
}

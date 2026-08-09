#include "core/Generator.h"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string_view>

using namespace pulso;

namespace {
Role parseRole(std::string_view value) {
    if (value == "drums" || value == "percussion") return Role::Percussion;
    if (value == "counter" || value == "countermelody") return Role::Countermelody;
    return Role::Bass;
}
} // namespace

int main(int argc, char** argv) {
    GenerationContext context;
    context.role = argc > 1 ? parseRole(argv[1]) : Role::Bass;
    context.seed = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 42;
    context.rootPitchClass = argc > 3 ? std::atoi(argv[3]) % 12 : 0;
    context.scale = ScaleKind::Minor;
    context.chordPitchClasses = {context.rootPitchClass,
                                 positiveModulo(context.rootPitchClass + 3, 12),
                                 positiveModulo(context.rootPitchClass + 7, 12)};

    const auto pattern = Generator{}.generate(context);
    std::cout << "PULSO pattern | role=" << roleNames[static_cast<std::size_t>(context.role)]
              << " seed=" << context.seed << " root=" << context.rootPitchClass << '\n';
    std::cout << "beat\tduration\tpitch\tvelocity\tchannel\n";
    for (const auto& note : pattern.notes)
        std::cout << std::fixed << std::setprecision(2) << note.startBeat << '\t'
                  << note.durationBeats << '\t' << note.pitch << '\t' << note.velocity << '\t'
                  << note.channel << '\n';
    return 0;
}


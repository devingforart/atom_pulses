#include "CompositionModel.h"

#include "SongComposer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>

namespace pulso {
namespace {

std::uint64_t mix(std::uint64_t value) noexcept {
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

int choosePhraseBars(const SongPlan& plan, const SongSection& section, int phraseIndex,
                     int remaining) noexcept {
    if (remaining <= 4) return remaining;
    const auto identity = mix(plan.seed ^ static_cast<std::uint64_t>(section.startBar + 1) *
        0x9e3779b97f4a7c15ULL ^ static_cast<std::uint64_t>(phraseIndex + 1) *
        0xd1b54a32d192ed03ULL);
    constexpr std::array choices{6, 8, 8, 10, 12};
    auto candidate = choices[static_cast<std::size_t>(identity % choices.size())];
    if (section.density < 0.34) candidate = std::min(12, candidate + 2);
    if (section.tension > 0.78) candidate = std::max(6, candidate - 2);
    if (remaining - candidate > 0 && remaining - candidate < 4) candidate = remaining;
    return std::clamp(candidate, 4, remaining);
}

bool contains(std::string text, const char* needle) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return text.find(needle) != std::string::npos;
}

MotifTransformation authoredTransformation(const SongSection& section) {
    const auto classify = [](const std::string& text) {
        if (contains(text, "invert")) return MotifTransformation::Invert;
        if (contains(text, "fragment")) return MotifTransformation::Fragment;
        if (contains(text, "augment") || contains(text, "sustain")) return MotifTransformation::Augment;
        if (contains(text, "sequence") || contains(text, "develop")) return MotifTransformation::Sequence;
        if (contains(text, "displace") || contains(text, "syncop")) return MotifTransformation::Displace;
        if (contains(text, "cadenc") || contains(text, "resolve") || contains(text, "coda"))
            return MotifTransformation::Cadence;
        return MotifTransformation::Original;
    };
    if (const auto explicitTreatment = classify(section.motifTreatment);
        explicitTreatment != MotifTransformation::Original)
        return explicitTreatment;
    if (const auto functionalTreatment = classify(section.function);
        functionalTreatment != MotifTransformation::Original)
        return functionalTreatment;
    return MotifTransformation::Original;
}

PhraseFunction functionFor(int phraseIndex, int bar, int length, bool finalPhrase,
                           double tension) noexcept {
    if (bar == 0) return phraseIndex == 0 ? PhraseFunction::Establish : PhraseFunction::Answer;
    if (bar + 1 == length)
        return finalPhrase ? PhraseFunction::Release :
               tension > 0.68 ? PhraseFunction::Suspend : PhraseFunction::Arrive;
    const auto position = static_cast<double>(bar) / std::max(1, length - 1);
    if (position < 0.34) return PhraseFunction::Question;
    if (position < 0.70) return PhraseFunction::Develop;
    return PhraseFunction::Answer;
}

MotifTransformation transformationFor(PhraseFunction function, int phraseIndex,
                                       std::uint64_t identity) noexcept {
    if (function == PhraseFunction::Establish) return MotifTransformation::Original;
    if (function == PhraseFunction::Arrive || function == PhraseFunction::Release)
        return MotifTransformation::Cadence;
    if (function == PhraseFunction::Suspend) return MotifTransformation::Augment;
    constexpr std::array palette{MotifTransformation::Fragment, MotifTransformation::Sequence,
        MotifTransformation::Invert, MotifTransformation::Displace, MotifTransformation::Augment};
    return palette[static_cast<std::size_t>((identity + static_cast<std::uint64_t>(phraseIndex)) %
                                            palette.size())];
}

} // namespace

std::vector<NarrativeBar> NarrativePlanner::create(const SongPlan& plan,
                                                    const SongSection& section) {
    std::vector<NarrativeBar> result;
    result.reserve(static_cast<std::size_t>(std::max(0, section.bars)));
    auto cursor = 0;
    auto phraseIndex = 0;
    while (cursor < section.bars) {
        const auto remaining = section.bars - cursor;
        const auto phraseBars = choosePhraseBars(plan, section, phraseIndex, remaining);
        const auto identity = mix(plan.seed ^ static_cast<std::uint64_t>(section.startBar + cursor + 1));
        const auto finalPhrase = cursor + phraseBars == section.bars;
        const auto authored = authoredTransformation(section);
        for (auto bar = 0; bar < phraseBars; ++bar) {
            NarrativeBar narrative;
            narrative.localBar = cursor + bar;
            narrative.phraseIndex = phraseIndex;
            narrative.barInPhrase = bar;
            narrative.phraseBars = phraseBars;
            narrative.phrasePosition = phraseBars <= 1 ? 1.0 :
                static_cast<double>(bar) / static_cast<double>(phraseBars - 1);
            narrative.function = functionFor(phraseIndex, bar, phraseBars, finalPhrase,
                                             section.tension);
            narrative.transformation = transformationFor(narrative.function, phraseIndex, identity);
            if (authored != MotifTransformation::Original &&
                narrative.function != PhraseFunction::Establish &&
                narrative.function != PhraseFunction::Release)
                narrative.transformation = authored;
            const auto arc = std::sin(narrative.phrasePosition * 3.14159265358979323846);
            narrative.intensity = std::clamp(section.energy * (0.78 + arc * 0.26) +
                                               section.tension * narrative.phrasePosition * 0.12,
                                               0.0, 1.0);
            narrative.arrival = narrative.function == PhraseFunction::Arrive ||
                                narrative.function == PhraseFunction::Release;
            narrative.breath = bar + 1 == phraseBars ||
                (phraseBars >= 8 && bar + 1 == phraseBars / 2 && section.density < 0.56);
            narrative.fullBreath = narrative.breath &&
                ((bar + 1 == phraseBars && (section.tension > 0.72 || section.energy < 0.42)) ||
                 (narrative.function == PhraseFunction::Release && section.density < 0.64));
            result.push_back(narrative);
        }
        cursor += phraseBars;
        ++phraseIndex;
    }
    return result;
}

} // namespace pulso

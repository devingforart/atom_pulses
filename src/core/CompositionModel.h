#pragma once

#include <cstdint>
#include <vector>

namespace pulso {

struct SongPlan;
struct SongSection;

enum class PhraseFunction : std::uint8_t {
    Establish = 0,
    Question,
    Answer,
    Develop,
    Suspend,
    Arrive,
    Release
};

enum class MotifTransformation : std::uint8_t {
    Original = 0,
    Fragment,
    Sequence,
    Invert,
    Augment,
    Displace,
    Cadence
};

struct NarrativeBar {
    int localBar{};
    int phraseIndex{};
    int barInPhrase{};
    int phraseBars{8};
    PhraseFunction function{PhraseFunction::Establish};
    MotifTransformation transformation{MotifTransformation::Original};
    double phrasePosition{};
    double intensity{0.5};
    bool breath{};
    bool fullBreath{};
    bool arrival{};
};

class NarrativePlanner final {
public:
    [[nodiscard]] static std::vector<NarrativeBar> create(const SongPlan&,
                                                           const SongSection&);
};

} // namespace pulso

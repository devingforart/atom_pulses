#pragma once

#include "Generator.h"
#include "RhythmPlan.h"

#include <functional>
#include <string>
#include <vector>

namespace pulso {

struct PlannedVoice {
    VoiceId id{VoiceId::CoreDrums};
    std::string function;
    std::string interaction;
    double activity{0.5};
    double syncopation{0.5};
    int minimumPitch{};
    int maximumPitch{127};
};

struct SongSection {
    std::string name;
    std::string function;
    std::string harmonicDirection;
    std::string motifTreatment;
    int startBar{};
    int bars{8};
    double energy{0.5};
    double tension{0.5};
    double density{0.5};
    int motifVariant{};
    std::vector<VoiceId> activeVoices;
    SectionRhythmPlan rhythm;
};

struct SongPlan {
    std::string title{"Untitled Song"};
    std::string key{"C minor"};
    std::string summary;
    int targetSeconds{210};
    int totalBars{64};
    double bpm{120.0};
    double beatsPerBar{4.0};
    int rootPitchClass{};
    ScaleKind scale{ScaleKind::Minor};
    GrooveFamily grooveFamily{GrooveFamily::DeepProgressiveHouse};
    std::uint64_t seed{1};
    std::vector<int> motifIntervals{0, 3, 5, 7, 3};
    std::vector<int> chordDegrees{0, 5, 3, 6};
    std::vector<RhythmMotif> rhythmMotifs;
    std::vector<PlannedVoice> voices;
    std::vector<SongSection> sections;
};

class SongComposer final {
public:
    using ProgressCallback = std::function<void(std::size_t, std::size_t, const SongSection&)>;

    [[nodiscard]] static SongPlan createLocalPlan(const std::string& direction,
                                                  int targetSeconds, double bpm,
                                                  double beatsPerBar, std::uint64_t seed,
                                                  int rootPitchClass, ScaleKind scale);
    static void normalizePlan(SongPlan&);
    [[nodiscard]] Pattern render(const SongPlan&, const GenerationContext& foundation,
                                 const ProgressCallback& = {}) const;
};

} // namespace pulso

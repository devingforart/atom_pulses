#include "PhraseDirector.h"

#include "SongComposer.h"

#include <algorithm>
#include <cmath>

namespace pulso {
namespace {

bool active(const SongSection& section, VoiceId voice) {
    return std::find(section.activeVoices.begin(), section.activeVoices.end(), voice) !=
           section.activeVoices.end();
}

void assign(BarDirection& bar, const SongSection& section, VoiceId voice,
            Participation participation, int maximumOnsets,
            double entryBeat, double exitBeat, double expression) {
    if (!active(section, voice)) return;
    auto& direction = bar.voices[static_cast<std::size_t>(voice)];
    direction.participation = participation;
    direction.maximumOnsets = std::max(0, maximumOnsets);
    direction.entryBeat = std::max(0.0, entryBeat);
    direction.exitBeat = std::max(direction.entryBeat, exitBeat);
    direction.expression = std::clamp(expression, 0.0, 1.0);
}

VoiceId voiceForLane(RhythmLane lane) {
    switch (lane) {
        case RhythmLane::Kick: return VoiceId::CoreDrums;
        case RhythmLane::SnareClap: return VoiceId::SnareClap;
        case RhythmLane::ClosedHats: return VoiceId::ClosedHats;
        case RhythmLane::OpenHatsShaker: return VoiceId::OpenHatsShaker;
        case RhythmLane::LowPercussion: return VoiceId::LowPercussion;
        case RhythmLane::HighPercussion: return VoiceId::HighPercussion;
    }
    return VoiceId::CoreDrums;
}

} // namespace

const VoiceDirection& BarDirection::forVoice(VoiceId voice) const noexcept {
    static constexpr VoiceDirection silent{};
    const auto index = static_cast<std::size_t>(voice);
    return index < voices.size() ? voices[index] : silent;
}

std::vector<BarDirection> PhraseDirector::create(const SongPlan& plan,
                                                  const SongSection& section) {
    std::vector<BarDirection> result(static_cast<std::size_t>(section.bars));
    const auto narrative = NarrativePlanner::create(plan, section);
    const auto holdBars = section.density < 0.38 ? 4 : section.tension < 0.72 ? 2 : 1;
    auto harmonicStep = 0;

    for (auto localBar = 0; localBar < section.bars; ++localBar) {
        auto& bar = result[static_cast<std::size_t>(localBar)];
        const auto& story = narrative[static_cast<std::size_t>(localBar)];
        const auto cell = positiveModulo(story.barInPhrase, 4);
        const auto lastBar = localBar + 1 == section.bars;
        const auto highEnergy = section.energy >= 0.76;
        const auto sparse = section.density < 0.42;
        bar.localBar = localBar;
        bar.phraseIndex = story.phraseIndex;
        bar.barInPhrase = story.barInPhrase;
        bar.phraseBars = story.phraseBars;
        bar.phrasePosition = story.phrasePosition;
        bar.intensity = story.intensity;
        bar.phraseFunction = story.function;
        bar.motifTransformation = story.transformation;
        bar.harmonicHoldBars = holdBars;
        bar.harmonicAttack = localBar % holdBars == 0 || lastBar;
        if (bar.harmonicAttack && localBar != 0) ++harmonicStep;
        bar.harmonicStep = harmonicStep;
        bar.breath = story.breath;
        bar.fullBreath = story.fullBreath;
        bar.arrival = story.arrival || lastBar;

        if (!bar.fullBreath) {
            const auto leadOwns = story.function == PhraseFunction::Establish ||
                story.function == PhraseFunction::Question ||
                (story.function == PhraseFunction::Develop && story.phraseIndex % 2 == 0);
            if (leadOwns) {
                bar.foreground = VoiceId::Lead;
            } else if (story.function == PhraseFunction::Answer ||
                       story.function == PhraseFunction::Develop ||
                       story.function == PhraseFunction::Arrive) {
                bar.foreground = VoiceId::Countermelody;
            }
        }
        bar.response = bar.foreground == VoiceId::Lead ? VoiceId::Countermelody :
                       bar.foreground == VoiceId::Countermelody ? VoiceId::Lead : VoiceId::Unspecified;

        const auto exit = bar.breath ? plan.beatsPerBar - (0.48 + section.tension * 0.42)
                                     : plan.beatsPerBar;
        const auto expression = std::clamp(0.30 + section.energy * 0.62, 0.0, 1.0);
        const auto kickGesture = std::any_of(section.rhythm.gestures.begin(),
            section.rhythm.gestures.end(), [localBar](const auto& gesture) {
                return gesture.barOffset == localBar &&
                       (gesture.kind == RhythmGestureKind::DoubleKick ||
                        gesture.kind == RhythmGestureKind::PickupFill);
            });
        const auto rhythmPhraseExit = cell == 3
            ? std::min(exit, plan.beatsPerBar - 0.50)
            : exit;
        const auto rhythmExit = (section.rhythm.kickState == KickState::FourOnFloor &&
                                 section.rhythm.continuity == KickContinuity::Required) || kickGesture
            ? plan.beatsPerBar : rhythmPhraseExit;
        if (section.rhythm.kickState != KickState::Muted || kickGesture)
            assign(bar, section, VoiceId::CoreDrums, Participation::Support,
                   section.rhythm.kickState == KickState::FourOnFloor ? 8 : 4,
                   0.0, rhythmExit, expression);
        assign(bar, section, VoiceId::SnareClap, Participation::Support,
               section.rhythm.percussionDensity > 0.25 ? 4 : 0, 0.0, rhythmPhraseExit, expression * 0.90);
        assign(bar, section, VoiceId::ClosedHats, Participation::Support,
               section.rhythm.percussionDensity > 0.30 ? 20 : 0, 0.0, rhythmPhraseExit, expression * 0.72);
        assign(bar, section, VoiceId::OpenHatsShaker, Participation::Support,
               section.rhythm.percussionDensity > 0.45 ? 16 : 0, 0.0, rhythmPhraseExit, expression * 0.66);
        assign(bar, section, VoiceId::LowPercussion, Participation::Accent,
               section.rhythm.percussionDensity > 0.38 ? 8 : 0, 0.0, rhythmPhraseExit, expression * 0.84);
        assign(bar, section, VoiceId::HighPercussion, Participation::Support,
               section.rhythm.percussionDensity > 0.32 ? 6 : 0, 0.0, rhythmPhraseExit, expression * 0.74);
        for (const auto& mutation : section.rhythm.mutations) {
            if (mutation.barOffset != localBar || mutation.kind == RhythmMutationKind::Remove) continue;
            const auto voice = voiceForLane(mutation.lane);
            if (!active(section, voice)) continue;
            auto& instruction = bar.voices[static_cast<std::size_t>(voice)];
            instruction.participation = Participation::Accent;
            instruction.maximumOnsets = std::max(instruction.maximumOnsets,
                mutation.kind == RhythmMutationKind::Ratchet ? mutation.amount + 2 : 4);
            instruction.entryBeat = 0.0;
            instruction.exitBeat = plan.beatsPerBar;
            instruction.expression = std::max(instruction.expression, expression * 0.82);
        }
        if (bar.fullBreath) {
            assign(bar, section, VoiceId::Atmosphere, Participation::Texture,
                   bar.harmonicAttack ? 1 : 0, 0.0, plan.beatsPerBar, expression * 0.72);
            assign(bar, section, VoiceId::Transitions, Participation::Accent,
                   lastBar ? 2 : 0, plan.beatsPerBar * 0.45, plan.beatsPerBar, expression);
            continue;
        }

        assign(bar, section, VoiceId::SubBass, Participation::Support,
               cell == 3 ? 0 : highEnergy ? 3 : 2, 0.0, exit, expression);
        assign(bar, section, VoiceId::MovementBass, Participation::Response,
               cell == 1 || (highEnergy && cell == 2) ? 2 : 0, 0.45, exit, expression * 0.84);
        assign(bar, section, VoiceId::HarmonicFoundation, Participation::Support,
               bar.harmonicAttack ? 4 : 0, 0.0, plan.beatsPerBar, expression * 0.80);
        assign(bar, section, VoiceId::HarmonicPulse, Participation::Support,
               cell == 1 || cell == 2 ? (highEnergy ? 4 : 2) : 0,
               0.25, exit, expression * 0.76);
        assign(bar, section, VoiceId::HarmonicUpper, Participation::Texture,
               (story.function == PhraseFunction::Develop || story.arrival) ? 1 : 0,
               0.4, plan.beatsPerBar, expression * 0.64);
        assign(bar, section, VoiceId::Atmosphere, Participation::Texture,
               bar.harmonicAttack && (story.phrasePosition < 0.42 ||
                                      story.function == PhraseFunction::Suspend || sparse) ? 1 : 0,
               0.0, plan.beatsPerBar, expression * 0.55);
        assign(bar, section, VoiceId::Transitions, Participation::Accent,
               lastBar ? 2 : 0, plan.beatsPerBar * 0.45, plan.beatsPerBar, expression);

        if (bar.foreground == VoiceId::Lead)
            assign(bar, section, VoiceId::Lead, Participation::Foreground,
                   story.function == PhraseFunction::Develop ? 4 : 3,
                   story.function == PhraseFunction::Answer ? 0.45 : 0.0, exit, expression);
        else if (bar.foreground == VoiceId::Countermelody)
            assign(bar, section, VoiceId::Countermelody, Participation::Foreground,
                   story.arrival ? 2 : 3, 0.35, exit, expression * 0.88);
    }
    return result;
}

} // namespace pulso

#pragma once

#include "core/Orchestration.h"

#include <juce_core/juce_core.h>

#include <cstdint>

namespace pulso::plugin {

enum class UiLanguage : std::uint8_t { English = 0, Spanish };

enum class TextId : std::uint8_t {
    Subtitle, PromptLabel, DurationLabel, PromptPlaceholder,
    GenerateIdea, ComposeSong, NextIdea, RegenerateUnlocked, Undo,
    PreviewAudio, HumanPerformance, MidiThru,
    LockHarmony, LockMelodic, LockBass, LockRhythm,
    LockHarmonyTip, LockMelodicTip, LockBassTip, LockRhythmTip,
    GenerateTip, NextTip, RegenerateTip, UndoTip, PreviewTip, PerformanceTip,
    SoundWorldTip, ThruTip, PromptTip, DurationTip, TitleTip, SubtitleTip,
    StatusTip, AiTip, IdeaTitleTip, IdeaDescriptionTip, PatternTip, LanguageTip,
    PreviewSound, Octave, Level, Audition, SoundTip, OctaveDownTip,
    OctaveOriginalTip, OctaveUpTip, LevelTip, AuditionTip,
    FullSong, Rhythm, Bass, Harmony, LeadsFx, Section,
    EmptyPattern, Play, Paused, Preview, Bar, Bars, Idea,
    ExportFailed, DropIntoAbleton, MidiReady, DragUnavailable,
    Solo, SoloOff, Muted, MuteOff,
    KickMuted, KickReduced, KickSparse, KickFourOnFloor,
    Cancel, Cancelling, ProgressTip, CancelTip, GptComposing,
    Composing, CurrentKeepsPlaying, Directing, Working,
    SoundStage, SoundStageTip, DeployLive, DeployLiveTip
};

[[nodiscard]] juce::String tr(UiLanguage, TextId);
[[nodiscard]] juce::String bullet();
[[nodiscard]] juce::String voiceDisplayName(UiLanguage, VoiceId);
[[nodiscard]] juce::StringArray localizedTimbreChoices(UiLanguage, VoiceId);
[[nodiscard]] juce::StringArray localizedSoundWorlds(UiLanguage,
                                                      const juce::String& automaticWorld);
[[nodiscard]] juce::String localizeStatus(UiLanguage, const juce::String&);

} // namespace pulso::plugin

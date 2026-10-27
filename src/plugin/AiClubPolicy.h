#pragma once

#include "core/SongComposer.h"
#include "core/TonalContract.h"
#include "AiClubNucleus.h"
#include <juce_core/juce_core.h>
#include <cmath>

namespace pulso::plugin::club {

inline juce::String grooveBrief() {
    return "SHARED GROOVE DESIGN v6: A stable floor is not permission for skeletal musical content. "
        "Give the piece an identifiable developed musical subject, a fluent recurring sequence "
        "and a consequential continuation/arrival within its requested scale. These are actual "
        "phrases, not a few signpost notes separated by whole passages of accompaniment. "
        "Keep the bass/chord reference stable while letting foreground pitch contour, register, "
        "articulation and continuation evolve. Let a recognisable sequence weave a substantial "
        "tonal fabric; do not turn an arpeggio into two disconnected chord markers per bar "
        "merely because the metric backbone must be simple. This is not a note-count quota: "
        "use complete idiomatic gestures and meaningful rests, not indiscriminate filling. "
        "Establish a persuasive stable core BEFORE layering support. "
        "The AI-chosen bass, chord pulse and sequence must make musical sense together alone. "
        "Keep their declared absolute attack phase across sections; novelty comes primarily "
        "from harmonic pressure, sound, articulation and selective layering, not shifting the floor. "
        "Leave perceptual space around the foreground; do not interpret this as a demand "
        "to delay all chord beds and textures by a sixteenth. Shared harmonic attacks are "
        "legitimate: separate layers through register, voicing and dynamics as well as rests. "
        "A soft envelope is not an excuse for a late MIDI attack. Give connected melodic "
        "phrases recognizable metric arrivals, not an uninterrupted evasive offbeat chain. "
        "Do not confuse interlocking rhythm with many unrelated syncopated solos. "
        "Compose a full electronic production fabric. "
        "Bass propulsion, recurring sequences, rhythmic chords, sustained chord bodies "
        "and pitched textures are complementary layers, not an orchestra of soloists. "
        "Several interlocking tonal rhythms and different cycle lengths may coexist "
        "around one perceptible metric reference; do not thin everything to one motor "
        "and decorative cues. Choose these positions yourself; no preset onset pattern "
        "is supplied. A SHARED cycle reference is not a demand for identical patterns. "
        "Keep a zero-based ABSOLUTE song phase through technical windows. Bass needs "
        "style-appropriate rhythmic propulsion, not only a root marker at each bar. "
        "Sustained pads and rhythmic stabs are different responsibilities. Full sections "
        "need real voiced harmonic bodies beneath independently useful tonal motion, "
        "not a sparse melody plus chord punctuation. A focus may be a sequence, bass, "
        "chord rhythm, texture or hook; it need not be a sung protagonist. A supporting "
        "texture must be a perceivable pitched environment, not four isolated FX pings "
        "counted as a developed instrument. Pads need living inner voice-leading, not "
        "only static block labels. Melodic "
        "phrases still need connected durations and arrivals where appropriate. "
        "Do not put every layer in the same register or let rapid shared-pitch attacks "
        "mask a sustained foreground. Judge interactions on a neutral piano AND on "
        "plain electronic envelopes, without relying on effects to invent missing music. ";
}

// Editorial instructions only. No note generation, repetition expansion,
// quantisation, automatic orchestration or duration manipulation lives here.
inline juce::String brief() {
    if (nucleus::enabled()) return nucleus::editorial();
    if (juce::SystemStats::getEnvironmentVariable("PULSO_AI_TONAL_CLUB_BRIEF_V1", {}) == "1")
        return "TONAL CLUB COMPOSITION: Write an electronic dance piece with NO drums or percussion. "
            "The MIDI itself must make the meter and motion audible on a neutral piano; do not rely on a kick, "
            "sound design or effects to supply the missing groove. First invent a short, memorable rhythmic "
            "relationship between bass and pitched chord attacks. Their accents, held notes and gaps must "
            "interlock around one clear pulse, rather than sound like two unrelated solos or a bass root on "
            "every bar. Add a recurring tonal sequence or arpeggio with its own contour, plus sustained "
            "polyphonic harmony that makes the tonal floor audible. Give the foreground a recognisable "
            "pitch-and-rhythm idea that can return; do not substitute scattered ornamental notes for a hook. "
            "Choose the actual pitches, attacks, durations, repetitions and rests yourself. No preset rhythmic "
            "cell, fixed number of notes, mandatory four-on-the-floor accent or automatic MIDI expansion is "
            "provided. Keep the core relationship recognizable long enough to create expectation. Develop "
            "it through purposeful variations in voicing, harmony, register, articulation and the entrance "
            "or withdrawal of complementary layers, not by changing every lane every few bars. A later "
            "return should be heard as the same musical identity under changed pressure. For a complete "
            "song, prepare a fuller statement, meaningful contrast and an earned ending; for a requested "
            "loop, preserve the cycle without forcing a finale. All parts must have distinct musical jobs "
            "and fit together as a production, not imitate chamber-music counterpoint. Before submitting "
            "the notes, imagine hearing only bass and chord pulse: if they do not establish a convincing "
            "tonal dance groove, rewrite that relationship. Then check that adding sequence, pads and "
            "foreground reveals a journey rather than just making the texture busier. ";
    return grooveBrief() + "CLUB COMPOSITION CONTRACT v3: Compose a continuous electronic club arrangement. "
        "The listener must retain a dependable tonal pulse, bass grammar and memorable hook. "
        "Think in approximately 64-bar macro developments, scaled to the available duration, "
        "not obligatory cuts, equal sections or cadences on bar 64. Short 1-8 bar bass, "
        "arpeggio and chord-stab cells may repeat literally for many cycles. Repetition is "
        "a deliberate musical function, not a lack of authorship. Do not force a new melodic "
        "sentence, counterargument, chord or solo at every phrase, section or writing boundary. "
        "Develop expectation through coordinated entries, withdrawals, harmonic pressure, "
        "voicing, register, articulation and an earned return. Keep a perceptible pulse during "
        "a plateau without changing its onset grammar just to demonstrate novelty. "
        "STAGED REVELATION: for a complete song, do not expose the finished production body at "
        "the opening. Preserve the same identity while revealing, withholding and recombining "
        "micro-elements patiently. Eight- or sixteen-bar changes may be useful producer-scale "
        "gestures, but they are never a compulsory clock: retain a state longer when hypnosis "
        "needs it, and change earlier when the musical consequence is earned. Separate stable "
        "groove from large-scale energy. A dependable pulse may continue while density, register, "
        "harmonic pressure and foreground ownership create a clearly audible journey. Treat "
        "opening, establishment, accumulation, arrival, withdrawal, return and release as possible "
        "dramatic functions, not a mandatory named template. Merge, omit or revisit them according "
        "to the requested duration and idea, but ensure a full song has preparation, consequential "
        "contrast and an ending rather than one nearly constant production state. Distinct "
        "roles need independent useful material, not the same melody split across tracks. "
        "A memorable developed foreground phrase, interlocking bass, repeating arpeggio, rhythmic polyphonic "
        "stabs, sustained chord bed, complementary upper pad and pitched atmosphere can coexist "
        "selectively. Choose appropriate electronic voices and stagger them; not everything "
        "plays continuously. Pads must contain actual voiced chords, not isolated marker notes. "
        "Respect all percussion exclusions: pitched rhythm never requires drums. "
        "Compose the sustained planes as one deliberate voicing against the ACTUAL sounding "
        "notes in the accepted ensemble. Sharing a scale or chord label is not sufficient: "
        "space extensions and leave a clear register corridor instead of accumulating "
        "unresolved adjacent held pitches across pads. Preserve purposeful colors with "
        "their voice-leading consequence; darkness is not an accidental sustained cluster. "
        "FULL-SONG TRAJECTORY: distinguish a convincing full production body from playing "
        "that body immediately and continuously. For an entire song, let the opening reveal "
        "the identity through coordinated entries; reserve something recognizable for its "
        "first full statement and its earned return. Give the foreground meaningful periods "
        "of absence while another layer retains its memory. Develop a few related ideas "
        "through the work rather than starting another miniature story in each window. "
        "Compose the ending as an audible consequence: finish the subject, settle harmonic "
        "pressure, let rhythmic layers complete their gestures, and decide which voice "
        "has the last word and which release follows it. A tonic note above an ongoing "
        "full groove until the file boundary is not by itself a prepared ending. Dynamics "
        "and track removal alone cannot substitute for a concluding musical sentence. "
        "Choose entry/withdrawal times musically, without mandatory counts or fixed bars. "
        "An explicitly requested repeatable section keeps its cyclic ending instead. "
        "In a full song an energetic peak must be prepared, a breakdown retains tonal memory "
        "and the return recalls the material. An explicitly requested loop or full production "
        "section instead sustains a rich, repeatable state: do not force a miniature "
        "intro/break/climax/outro or a final melodic cadence into it. All pitches, attacks, "
        "durations and literal repetitions are authored by you. "
        "Do not depend on the host adding notes, duplicating loops or repairing the music. ";
}

inline bool sustainedPlane(const InstrumentAssignment& part) {
    return part.sourceVoice == VoiceId::HarmonicFoundation ||
        part.sourceVoice == VoiceId::HarmonicUpper || part.sourceVoice == VoiceId::Atmosphere;
}

// Candidate evidence for an AI review, not a pitch correction or a ban on dissonance.
// Weight exposed semitones and held compound minor ninths by shared sounding time.
// Ordinary ninths, major sevenths and brief passing melodies are outside this scope.
inline TonalAuditReport adjacentPlanes(const SongPlan& plan, const TonalAuditReport& audit) {
    TonalAuditReport result;
    for (const auto& issue : audit.issues) {
        if (issue.kind == "retrigger_mask") {
            result.issues.push_back(issue); // Preselected from actual attack order below, not a dissonance claim.
            continue;
        }
        const auto interval = std::abs(issue.pitch - issue.otherPitch);
        if (issue.kind != "harsh_overlap" || interval % 12 != 1 ||
            (interval > 1 && issue.overlapBeats < 1.0) ||
            issue.partId == 0 || issue.otherPartId == 0 ||
            issue.partId > plan.instruments.size() || issue.otherPartId > plan.instruments.size()) continue;
        const auto leftPlane = sustainedPlane(plan.instruments[issue.partId - 1]);
        const auto rightPlane = sustainedPlane(plan.instruments[issue.otherPartId - 1]);
        if (issue.partId == issue.otherPartId && !(leftPlane && issue.overlapBeats >= 1.0)) continue;
        // Also expose a held melodic/transition collision against a pad. Short
        // passing ornaments remain outside this candidate list. This selects
        // review evidence only; the AI can retain a justified color unchanged.
        if ((leftPlane && rightPlane) || ((leftPlane || rightPlane) && issue.overlapBeats >= 1.0))
            result.issues.push_back(issue);
    }
    return result;
}

inline double adjacentWeight(const TonalAuditReport& audit) {
    auto weight = 0.0;
    for (const auto& issue : audit.issues) weight += issue.overlapBeats;
    return weight;
}

// Read the actual chronological rendering, including diatonic chord colors
// which the general tonal auditor may legitimately classify as intentional.
// Their musical function is then judged by the AI, not pre-decided by a label.
inline TonalAuditReport voicingEvidence(const SongPlan& plan, const Pattern& midi) {
    TonalAuditReport raw;
    for (std::size_t a = 0; a < midi.notes.size(); ++a) {
        const auto& left = midi.notes[a];
        for (auto b = a + 1; b < midi.notes.size(); ++b) {
            const auto& right = midi.notes[b];
            if (right.startBeat >= left.endBeat()) break;
            const auto gap = right.startBeat - left.startBeat;
            const auto retrigger = left.pitch == right.pitch && left.partId != right.partId &&
                left.partId > 0 && left.partId <= plan.instruments.size() &&
                right.partId > 0 && right.partId <= plan.instruments.size() &&
                gap > .001 && gap < .5 && left.durationBeats >= 2 &&
                sustainedPlane(plan.instruments[left.partId - 1]) &&
                !sustainedPlane(plan.instruments[right.partId - 1]);
            if (!retrigger && std::abs(left.pitch - right.pitch) % 12 != 1) continue;
            const auto overlap = std::min(left.endBeat(), right.endBeat()) -
                std::max(left.startBeat, right.startBeat);
            if (overlap <= 0) continue;
            raw.issues.push_back(TonalIssue{.beat = std::max(left.startBeat, right.startBeat),
                .voice = right.voice, .otherVoice = left.voice, .pitch = right.pitch,
                .otherPitch = left.pitch, .kind = retrigger ? "retrigger_mask" : "harsh_overlap", .partId = right.partId,
                .otherPartId = left.partId, .overlapBeats = overlap});
        }
    }
    return adjacentPlanes(plan, raw);
}

inline double internalPlaneWeight(const SongPlan& plan, const Pattern& pattern) {
    auto weight = 0.0;
    for (std::size_t a = 0; a < pattern.notes.size(); ++a) {
        const auto& left = pattern.notes[a];
        if (!left.partId || left.partId > plan.instruments.size() ||
            !sustainedPlane(plan.instruments[left.partId - 1])) continue;
        for (auto b = a + 1; b < pattern.notes.size(); ++b) {
            const auto& right = pattern.notes[b];
            if (right.startBeat >= left.endBeat()) break;
            if (left.partId == right.partId && std::abs(left.pitch - right.pitch) % 12 == 1)
                weight += std::max(0.0, std::min(left.endBeat(), right.endBeat()) -
                    std::max(left.startBeat, right.startBeat));
        }
    }
    return weight;
}

inline juce::String phraseBrief() {
    return brief() + "Plan one or two meaningful layer-state intentions per section using "
        "the existing phrase schema. A phrase may be a sustained hypnotic state spanning "
        "many repeated cycles, not a newly ornamented chamber-music sentence. motif_clue "
        "identifies the exact retained pitch/rhythm nucleus; dramatic_question describes "
        "the long tension, answer_principle its eventual release. speaker_id and response_id "
        "are exact cast IDs; use an empty response_id when no real answer is needed. "
        "gesture, rhythm_shape and relationship specify what stays unchanged and which "
        "single layer changes, without compulsory novelty. consequence states the audible "
        "arrangement/harmonic result. Use zero-based section/local bar positions and cover "
        "every section. Preserve active-section ownership and feasible pitch ranges. ";
}

} // namespace pulso::plugin::club

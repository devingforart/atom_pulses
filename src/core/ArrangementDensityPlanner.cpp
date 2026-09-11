#include "ArrangementDensityPlanner.h"

#include "OrchestrationScore.h"
#include "SongComposer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <map>
#include <numeric>
#include <set>
#include <string_view>
#include <tuple>

namespace pulso {
namespace {

struct CastSpec {
    std::string_view catalogId;
    VoiceId voice;
    std::string_view name;
    std::string_view role;
    std::string_view function;
    std::string_view articulation;
    std::string_view device;
    std::string_view preset;
    double activity;
    double prominence;
    TimbreSignature timbre;
};

const std::array electronicCast{
    CastSpec{"sub_synth", VoiceId::SubBass, "Sub Foundation", "Clean tonal floor with phrase-level rests", "foundation", "sustained", "Operator", "deep clean controlled sub", .66, .64, {"sine","sustained","dark","subtle","close","clean",.52}},
    CastSpec{"rolling_mid_bass", VoiceId::MovementBass, "Rolling Mid Bass", "Independent low-mid movement and answers", "counterpoint", "detached", "Wavetable", "rolling warm mono progressive mid bass", .58, .52, {"saw","short","warm","rhythmic","close","gritty",.66}},
    CastSpec{"reese_layer", VoiceId::MovementBass, "Reese Support", "Rare width and tension reinforcement", "color", "sustained", "Meld", "dark controlled reese bass support", .30, .34, {"saw","sustained","dark","evolving","wide","gritty",.72}},
    CastSpec{"analog_pad", VoiceId::HarmonicFoundation, "Analog Foundation Pad", "Slow chord memory with changing inversions", "foundation", "sustained", "Wavetable", "warm wide evolving analog foundation pad", .68, .58, {"saw","sustained","warm","evolving","wide","clean",.62}},
    CastSpec{"poly_synth", VoiceId::HarmonicFoundation, "Poly Chord Body", "Mid-register harmonic body with deliberate gaps", "body", "natural", "Meld", "warm restrained polyphonic chord body", .56, .48, {"hybrid","natural","warm","subtle","close","clean",.56}},
    CastSpec{"dub_chord", VoiceId::HarmonicFoundation, "Dub Chord Echo", "Syncopated harmonic identity and decay", "color", "staccato", "Drift", "warm delayed dub chord stab", .46, .42, {"saw","gated","warm","rhythmic","wet","organic",.70}},
    CastSpec{"filtered_stab", VoiceId::HarmonicPulse, "Filtered Stab", "Short punctuation that leaves spectral air", "counterpoint", "detached", "Meld", "short filtered analog chord stab", .48, .43, {"hybrid","short","dark","rhythmic","close","gritty",.72}},
    CastSpec{"hypnotic_arp", VoiceId::HarmonicPulse, "Hypnotic Pulse", "Motivic pulse with phrase-level omissions", "counterpoint", "ostinato", "Wavetable", "muted hypnotic arpeggiated synth pulse", .42, .38, {"square","pluck","neutral","rhythmic","close","clean",.58}},
    CastSpec{"shimmer_tail", VoiceId::HarmonicUpper, "Shimmer Extension", "High harmonic halo reserved for arrivals", "extension", "swelling", "Wavetable", "high restrained shimmer tail pad", .30, .29, {"noise","swelling","bright","evolving","wide","airy",.74}},
    CastSpec{"granular_pad", VoiceId::Atmosphere, "Granular Harmonic Bed", "Evolving breakdown continuity and depth", "foundation", "swelling", "Granulator III", "slow granular harmonic pad", .48, .38, {"sample","swelling","dark","evolving","deep","airy",.82}},
    CastSpec{"spectral_drone", VoiceId::Atmosphere, "Spectral Drone", "Tonal centre with long intentional absences", "transition", "sustained", "Wavetable", "low spectral drone bed", .34, .28, {"sine","sustained","dark","subtle","deep","clean",.64}},
    CastSpec{"ambient_texture", VoiceId::Atmosphere, "Air Texture", "Non-verbal air and distant harmonic motion", "color", "swelling", "Granulator III", "evolving airy atmospheric texture", .30, .25, {"noise","swelling","neutral","evolving","deep","airy",.76}},
    CastSpec{"noise_riser", VoiceId::Transitions, "Noise Transition", "Section-scale rises, withdrawals and breath", "transition", "swelling", "Wavetable", "filtered noise rise and reverse texture", .22, .20, {"noise","swelling","bright","evolving","wet","airy",.78}},
    CastSpec{"deep_pluck", VoiceId::Countermelody, "Deep Pluck Reply", "Sparse thematic answers in negative space", "counterpoint", "staccato", "Drift", "round deep pluck response", .34, .35, {"triangle","pluck","warm","subtle","close","organic",.66}},
    CastSpec{"fm_sequence", VoiceId::Countermelody, "FM Phrase", "Short transformed motif fragments", "color", "detached", "Operator", "soft expressive FM sequence counterline", .28, .30, {"fm","short","neutral","rhythmic","close","clean",.68}},
    CastSpec{"vocal_chop_texture", VoiceId::Countermelody, "Vocal Texture Reply", "Rare human-like call and response colour", "color", "detached", "Sampler", "short airy vocal chop texture", .20, .24, {"sample","short","bright","subtle","wet","vocal",.78}},
    CastSpec{"acid_line", VoiceId::Lead, "Resonant Phrase", "Occasional tension phrase, never constant foreground", "counterpoint", "detached", "Operator", "restrained resonant acid phrase", .24, .30, {"saw","short","bright","rhythmic","close","gritty",.72}},
    CastSpec{"lead_synth", VoiceId::Lead, "Primary Synth Speaker", "Economical foreground statement and thematic return", "counterpoint", "legato", "Wavetable", "warm expressive evolving mono lead", .36, .52, {"saw","natural","warm","subtle","wide","clean",.70}}
};

bool rhythmVoice(VoiceId voice) noexcept {
    return isVoiceInFamily(voice, VoiceFamily::Rhythm);
}

bool texturePart(const InstrumentPart& part) noexcept {
    return part.sourceVoice == VoiceId::Atmosphere || part.sourceVoice == VoiceId::Transitions ||
           part.catalogId == "granular_pad" || part.catalogId == "spectral_drone" ||
           part.catalogId == "ambient_texture" || part.catalogId == "noise_riser" ||
           part.catalogId == "shimmer_tail" || part.catalogId == "vocal_chop_texture";
}

bool containsInstrument(const SongPlan& plan, std::string_view catalogId) {
    return std::any_of(plan.instruments.begin(), plan.instruments.end(), [&](const auto& item) {
        return item.instrumentId == catalogId;
    });
}

void ensureVoice(SongPlan& plan, VoiceId voice) {
    if (std::any_of(plan.voices.begin(), plan.voices.end(),
        [&](const auto& item) { return item.id == voice; })) return;
    const auto& definition = voiceDefinition(voice);
    PlannedVoice addition;
    addition.id = voice;
    addition.function = rhythmVoice(voice) ? "rhythmic architecture" :
        voice == VoiceId::Atmosphere || voice == VoiceId::Transitions ? "structural texture" :
        voice == VoiceId::Lead || voice == VoiceId::Countermelody ? "thematic dialogue" :
        voice == VoiceId::SubBass || voice == VoiceId::MovementBass ? "independent low-end function" :
        "independent harmonic fabric";
    addition.interaction = "Respond to the shared motif and leave space for other departments";
    addition.activity = voice == VoiceId::Lead ? .34 : .52;
    addition.syncopation = voice == VoiceId::HarmonicPulse || voice == VoiceId::Countermelody ? .34 : .14;
    addition.minimumPitch = definition.minimumPitch;
    addition.maximumPitch = definition.maximumPitch;
    plan.voices.push_back(std::move(addition));
}

std::vector<std::string> rotatingSections(const SongPlan& plan, std::size_t ordinal,
                                          VoiceId voice) {
    std::vector<std::string> result;
    if (plan.sections.size() <= 2) return result;
    for (std::size_t index = 0; index < plan.sections.size(); ++index) {
        const auto& section = plan.sections[index];
        const auto transition = voice == VoiceId::Transitions;
        const auto foreground = voice == VoiceId::Lead || voice == VoiceId::Countermelody;
        const auto atmospheric = voice == VoiceId::Atmosphere || voice == VoiceId::HarmonicUpper;
        const auto selected = transition ? index > 0 && (index + ordinal) % 2 == 0 : foreground
            ? ((index + ordinal) % 2 == 0 && section.energy >= .34)
            : atmospheric ? (index + ordinal) % 2 == 0
            : (index + ordinal) % 3 != 0;
        if (selected) result.push_back(section.name);
    }
    if (result.empty()) result.push_back(plan.sections[ordinal % plan.sections.size()].name);
    return result;
}

std::uint64_t fingerprint(const std::vector<const NoteEvent*>& notes) {
    auto hash = std::uint64_t{1469598103934665603ULL};
    if (notes.empty()) return hash;
    const auto origin = notes.front()->startBeat;
    const auto pitchOrigin = notes.front()->pitch;
    for (const auto* note : notes) {
        const auto onset = static_cast<std::uint64_t>(std::llround((note->startBeat - origin) * 4.0));
        const auto duration = static_cast<std::uint64_t>(std::llround(note->durationBeats * 4.0));
        const auto contour = static_cast<std::uint64_t>(std::clamp(note->pitch - pitchOrigin, -36, 36) + 36);
        hash ^= onset + contour * 131 + duration * 17;
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::set<std::uint64_t> phraseFingerprints(const std::vector<const NoteEvent*>& notes,
                                           double beatsPerBar) {
    std::map<int, std::vector<const NoteEvent*>> windows;
    const auto width = std::max(1.0, beatsPerBar) * 4.0;
    for (const auto* note : notes)
        windows[static_cast<int>(std::floor(note->startBeat / width))].push_back(note);
    std::set<std::uint64_t> result;
    for (auto& [window, phrase] : windows) {
        (void) window;
        if (phrase.size() < 3) continue;
        std::sort(phrase.begin(), phrase.end(), [](const auto* left, const auto* right) {
            return std::tie(left->startBeat, left->pitch) < std::tie(right->startBeat, right->pitch);
        });
        result.insert(fingerprint(phrase));
    }
    return result;
}

} // namespace

ArrangementDensityTargets ArrangementDensityPlanner::targetsFor(const SongPlan& plan) {
    ArrangementDensityTargets result;
    result.electronic = plan.productionLanguage.electronicIntent >= .58 &&
        (plan.productionLanguage.domain == ProductionDomain::ClubElectronic ||
         plan.productionLanguage.domain == ProductionDomain::Hybrid);
    const auto rhythmParts = std::count_if(plan.instruments.begin(), plan.instruments.end(),
        [](const auto& item) { return rhythmVoice(item.sourceVoice); });
    result.percussionFree = !plan.instruments.empty() && rhythmParts == 0;
    if (!result.electronic) {
        result.proposedParts = std::clamp<std::size_t>(plan.instruments.size(), 8, 28);
        result.minimumPopulatedParts = std::max<std::size_t>(6, result.proposedParts * 3 / 4);
        result.minimumHarmonyParts = 4;
        result.minimumMelodyParts = 2;
        result.minimumTextureParts = 1;
        result.maximumSimultaneousParts = 10;
        return result;
    }
    const auto durationScale = std::clamp(static_cast<double>(plan.totalBars) / 96.0, .35, 1.5);
    const auto depth = std::max(plan.orchestrationLanguage.harmonicDepth,
                                plan.orchestrationLanguage.ensembleScale);
    const auto proposed = static_cast<int>(std::lround(10.0 + depth * 7.0 + durationScale * 2.0));
    result.proposedParts = static_cast<std::size_t>(std::clamp(proposed, result.percussionFree ? 16 : 14, 24));
    result.minimumPopulatedParts = result.proposedParts - (result.percussionFree ? 2 : 3);
    result.minimumHarmonyParts = result.percussionFree ? 9 : 7;
    result.minimumMelodyParts = result.percussionFree ? 3 : 2;
    result.minimumTextureParts = result.percussionFree ? 4 : 3;
    result.maximumSimultaneousParts = static_cast<std::size_t>(std::clamp(
        static_cast<int>(std::lround(4.0 + depth * 4.0)), 5, 8));
    // Relay, handoff and doubling assignments can deliberately share one content lane.
    // They are timbral destinations for one musical owner, not independent material;
    // demanding that every destination remain populated would reward the exact
    // leitmotif fragmentation that the ownership director removes.
    std::set<std::string> contentOwners;
    for (const auto& part : plan.instruments) {
        const auto owner = part.contentLaneId.empty() ? part.id : part.contentLaneId;
        if (!owner.empty()) contentOwners.insert(owner);
    }
    if (!contentOwners.empty())
        result.minimumPopulatedParts = std::min(result.minimumPopulatedParts,
                                                contentOwners.size());
    return result;
}

void ArrangementDensityPlanner::apply(SongPlan& plan) {
    const auto targets = targetsFor(plan);
    // A GPT-authored cast is compositional authority. Missing depth is returned to the
    // critic; silently adding generic parts produces impressive track counts but token MIDI.
    if (!targets.electronic || plan.instrumentCastAuthored ||
        plan.instruments.size() >= targets.proposedParts) return;
    auto ordinal = std::size_t{};
    for (const auto& spec : electronicCast) {
        if (plan.instruments.size() >= targets.proposedParts) break;
        // Low end is one coordinated system, not a density quota. Additional track
        // depth must come from harmony, texture and dialogue unless a bass lane is absent.
        if ((spec.voice == VoiceId::SubBass || spec.voice == VoiceId::MovementBass) &&
            std::any_of(plan.instruments.begin(), plan.instruments.end(), [&](const auto& item) {
                return item.sourceVoice == spec.voice;
            })) { ++ordinal; continue; }
        if (containsInstrument(plan, spec.catalogId)) { ++ordinal; continue; }
        const auto* definition = instrumentDefinition(spec.catalogId);
        if (definition == nullptr) { ++ordinal; continue; }
        ensureVoice(plan, spec.voice);
        InstrumentAssignment assignment;
        assignment.id = "density_" + std::string(spec.catalogId);
        assignment.instrumentId = std::string(spec.catalogId);
        assignment.name = std::string(spec.name);
        assignment.sourceVoice = spec.voice;
        assignment.role = std::string(spec.role);
        assignment.minimumPitch = definition->minimumPitch;
        assignment.maximumPitch = definition->maximumPitch;
        assignment.activity = spec.activity;
        assignment.prominence = spec.prominence;
        assignment.doubling = .04;
        assignment.activeSections = rotatingSections(plan, ordinal, spec.voice);
        assignment.orchestralFunction = std::string(spec.function);
        assignment.articulation = std::string(spec.articulation);
        assignment.divisiVoices = definition->polyphonic && spec.function == "foundation" ? 2 : 1;
        assignment.liveDevice = std::string(spec.device);
        assignment.livePresetIntent = std::string(spec.preset);
        assignment.timbre = spec.timbre;
        plan.instruments.push_back(std::move(assignment));
        for (auto& section : plan.sections) {
            const auto& activeSections = plan.instruments.back().activeSections;
            if (!activeSections.empty() && std::find(activeSections.begin(), activeSections.end(), section.name) == activeSections.end())
                continue;
            if (std::find(section.activeVoices.begin(), section.activeVoices.end(), spec.voice) == section.activeVoices.end())
                section.activeVoices.push_back(spec.voice);
        }
        ++ordinal;
    }
    // A deep arrangement may legitimately use two patches from the same synthesis
    // family. They remain separate instruments with distinct registers, sections and
    // sound-search intent; this is not MIDI duplication.
    auto variation = std::size_t{};
    for (const auto& spec : electronicCast) {
        if (plan.instruments.size() >= targets.proposedParts) break;
        if (spec.voice == VoiceId::SubBass || spec.voice == VoiceId::MovementBass ||
            spec.voice == VoiceId::Transitions)
            continue;
        const auto* definition = instrumentDefinition(spec.catalogId);
        if (definition == nullptr) continue;
        ensureVoice(plan, spec.voice);
        InstrumentAssignment assignment;
        assignment.id = "density_variant_" + std::to_string(++variation) + "_" +
                        std::string(spec.catalogId);
        assignment.instrumentId = std::string(spec.catalogId);
        assignment.name = std::string(spec.name) + " Variation " + std::to_string(variation + 1);
        assignment.sourceVoice = spec.voice;
        assignment.role = "Complementary register and sectional variation of " + std::string(spec.role);
        const auto span = definition->maximumPitch - definition->minimumPitch;
        const auto upperVariant = variation % 2 == 0;
        assignment.minimumPitch = definition->minimumPitch + (upperVariant ? span / 3 : 0);
        assignment.maximumPitch = definition->maximumPitch - (upperVariant ? 0 : span / 3);
        assignment.activity = std::max(.18, spec.activity - .12);
        assignment.prominence = std::max(.18, spec.prominence - .10);
        assignment.doubling = 0.0;
        assignment.activeSections = rotatingSections(plan, ordinal + variation * 3, spec.voice);
        assignment.orchestralFunction = std::string(spec.function);
        assignment.articulation = std::string(spec.articulation);
        assignment.divisiVoices = 1;
        assignment.liveDevice = std::string(spec.device);
        assignment.livePresetIntent = std::string(spec.preset) + " complementary variation";
        assignment.timbre = spec.timbre;
        assignment.timbre.uniqueness = std::min(1.0, assignment.timbre.uniqueness + .08);
        plan.instruments.push_back(std::move(assignment));
        for (auto& section : plan.sections) {
            const auto& activeSections = plan.instruments.back().activeSections;
            if (!activeSections.empty() && std::find(activeSections.begin(), activeSections.end(), section.name) == activeSections.end())
                continue;
            if (std::find(section.activeVoices.begin(), section.activeVoices.end(), spec.voice) == section.activeVoices.end())
                section.activeVoices.push_back(spec.voice);
        }
    }
}

ArrangementDensityReport ArrangementDensityPlanner::auditAndStamp(Pattern& pattern,
                                                                    const SongPlan& plan) {
    ArrangementDensityReport report;
    report.targets = targetsFor(plan);
    std::map<std::uint16_t, std::vector<const NoteEvent*>> notesByPart;
    for (const auto& note : pattern.notes)
        if (note.partId != 0) notesByPart[note.partId].push_back(&note);
    report.populatedParts = notesByPart.size();
    std::map<std::uint64_t, std::size_t> fingerprints;
    auto largestPart = std::size_t{};
    for (const auto& part : pattern.parts) {
        const auto found = notesByPart.find(part.id);
        if (found == notesByPart.end()) continue;
        largestPart = std::max(largestPart, found->second.size());
        ++fingerprints[fingerprint(found->second)];
        if (part.department == ScoreDepartment::Rhythm) ++report.rhythmParts;
        else if (part.department == ScoreDepartment::Melody) ++report.melodyParts;
        else ++report.harmonyParts;
        if (texturePart(part)) ++report.textureParts;
    }
    auto duplicateParts = std::accumulate(fingerprints.begin(), fingerprints.end(), std::size_t{},
        [](std::size_t total, const auto& item) { return total + (item.second > 1 ? item.second - 1 : 0); });
    // Detect a renamed/transposed leitmotif distributed over multiple independent
    // foreground tracks. Whole-track hashes miss that failure whenever the same cell
    // enters in different sections or registers.
    std::map<std::uint16_t, std::set<std::uint64_t>> phraseProfiles;
    for (const auto& part : pattern.parts) {
        const auto found = notesByPart.find(part.id);
        if (found != notesByPart.end() && part.department == ScoreDepartment::Melody)
            phraseProfiles[part.id] = phraseFingerprints(found->second, plan.beatsPerBar);
    }
    std::set<std::uint16_t> thematicClones;
    for (auto left = pattern.parts.begin(); left != pattern.parts.end(); ++left) {
        if (left->department != ScoreDepartment::Melody || left->lineRelationship != "independent") continue;
        for (auto right = std::next(left); right != pattern.parts.end(); ++right) {
            if (right->department != ScoreDepartment::Melody || right->lineRelationship != "independent") continue;
            const auto& a = phraseProfiles[left->id];
            const auto& b = phraseProfiles[right->id];
            if (a.empty() || b.empty()) continue;
            auto shared = std::size_t{};
            for (const auto value : a) if (b.contains(value)) ++shared;
            const auto denominator = std::min(a.size(), b.size());
            if (shared >= 1 && shared * 2 >= denominator) thematicClones.insert(right->id);
        }
    }
    duplicateParts += thematicClones.size();
    report.independenceScore = report.populatedParts == 0 ? 1.0 : std::clamp(
        1.0 - static_cast<double>(duplicateParts) / static_cast<double>(report.populatedParts),
        0.0, 1.0);
    report.maximumPartNoteShare = pattern.notes.empty() ? 0.0 :
        static_cast<double>(largestPart) / static_cast<double>(pattern.notes.size());

    const auto window = 1.0;
    for (auto beat = 0.0; beat < pattern.lengthBeats; beat += window) {
        std::set<std::uint16_t> active;
        for (const auto& note : pattern.notes)
            if (note.partId != 0 && note.startBeat < beat + window && note.endBeat() > beat)
                active.insert(note.partId);
        report.peakSimultaneousParts = std::max(report.peakSimultaneousParts, active.size());
    }
    report.ready = report.populatedParts >= report.targets.minimumPopulatedParts &&
        report.harmonyParts >= report.targets.minimumHarmonyParts &&
        report.melodyParts >= report.targets.minimumMelodyParts &&
        report.textureParts >= report.targets.minimumTextureParts &&
        report.independenceScore >= .80 &&
        // Permit a brief two-part arrival above the normal sectional budget, but reject
        // arrangements whose apparent depth is actually a persistent overcrowded tutti.
        report.peakSimultaneousParts <= report.targets.maximumSimultaneousParts + 2;

    pattern.arrangementTargetParts = report.targets.proposedParts;
    pattern.populatedInstrumentParts = report.populatedParts;
    pattern.populatedHarmonyParts = report.harmonyParts;
    pattern.populatedMelodyParts = report.melodyParts;
    pattern.populatedRhythmParts = report.rhythmParts;
    pattern.populatedTextureParts = report.textureParts;
    pattern.peakSimultaneousParts = report.peakSimultaneousParts;
    pattern.partIndependenceScore = report.independenceScore;
    pattern.maximumPartNoteShare = report.maximumPartNoteShare;
    return report;
}

} // namespace pulso

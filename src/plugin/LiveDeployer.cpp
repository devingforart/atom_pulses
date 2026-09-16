#include "LiveDeployer.h"
#include "AiModelConfig.h"

#include <algorithm>
#include <cmath>

namespace pulso::plugin {
namespace {

juce::File bridgeDirectory() {
    const auto overridePath = juce::SystemStats::getEnvironmentVariable("PULSO_LIVE_BRIDGE_DIR", {});
    if (overridePath.isNotEmpty()) return juce::File(overridePath);
   #if JUCE_WINDOWS
    const auto local = juce::SystemStats::getEnvironmentVariable("LOCALAPPDATA", {});
    if (local.isNotEmpty()) return juce::File(local).getChildFile("PULSO").getChildFile("LiveBridge");
   #endif
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("PULSO").getChildFile("LiveBridge");
}

juce::String departmentName(ScoreDepartment department) {
    if (department == ScoreDepartment::Rhythm) return "rhythm";
    if (department == ScoreDepartment::Melody) return "melody";
    return "harmony";
}

void addCandidate(juce::Array<juce::var>& candidates, const juce::String& value) {
    if (value.isNotEmpty() && !candidates.contains(value)) candidates.add(value);
}

bool isSilentContainer(const juce::String& device) {
    return device == "Drum Rack" || device == "Instrument Rack" || device == "Sampler" ||
           device == "Simpler" || device == "Drum Sampler" || device == "Impulse" ||
           device == "External Instrument";
}

juce::String neutralAuditionProfile(ScoreDepartment department, const juce::String& catalogId,
                                    const juce::String& intent) {
    const auto identity = catalogId.toLowerCase();
    const auto words = (identity.replaceCharacter('_', ' ') + " " + intent).toLowerCase();
    if (department == ScoreDepartment::Rhythm) return "drum";
    if (identity == "sub_synth") return "sub";
    if (identity == "ambient_texture" || identity == "granular_pad" ||
        identity == "spectral_drone" || identity == "noise_riser" ||
        identity == "shimmer_tail" || identity == "vocal_chop_texture") return "texture";
    if (identity == "analog_pad" || identity == "string_ensemble" ||
        identity == "chamber_strings" || identity == "choir") return "pad";
    if (words.contains("texture") || words.contains("noise") || words.contains("riser") ||
        words.contains("shimmer") || words.contains("spectral") || words.contains("transition") ||
        words.contains("atmos") || words.contains("ambient") || words.contains("upper air")) return "texture";
    if (words.contains("pad") || words.contains("drone") || words.contains("sustain") ||
        words.contains("foundation") || words.contains("pedal") || words.contains("floor") ||
        words.contains("continuum") || words.contains("harmonic field")) return "pad";
    if (identity == "electric_bass" || identity == "rolling_mid_bass" ||
        identity == "reese_layer" || identity == "contrabass" ||
        identity == "contrabassoon" || words.contains("bass line") ||
        words.contains("low end")) return "bass";
    if (words.contains("arpegg") || words.contains("ostinato") || words.contains("sequence") ||
        words.containsWholeWord("pulse")) return "arp";
    if (words.contains("pluck") || words.contains("staccato") || words.contains("stab") ||
        words.contains("detached") || identity == "piano" || identity == "harp" ||
        identity == "guitar" || identity == "mallets" || identity == "marimba" ||
        identity == "vibraphone") return "pluck";
    if (department == ScoreDepartment::Melody || words.contains("lead") ||
        words.contains("protagonist") || words.contains("foreground") ||
        words.contains("countermelody")) return "lead";
    return "chord";
}

juce::String neutralDeviceForProfile(const juce::String& profile) {
    if (profile == "drum") return "Drum Rack";
    if (profile == "pad" || profile == "lead" || profile == "texture") return "Wavetable";
    if (profile == "chord") return "Drift";
    return "Operator";
}

double neutralReleaseForProfile(const juce::String& profile) {
    if (profile == "sub") return 0.12;
    if (profile == "bass") return 0.18;
    if (profile == "pluck") return 0.12;
    if (profile == "arp") return 0.10;
    if (profile == "chord") return 0.28;
    if (profile == "lead") return 0.24;
    if (profile == "pad") return 0.58;
    if (profile == "texture") return 0.72;
    return 0.22;
}

juce::String expressionTypeName(ExpressionEventType type) {
    if (type == ExpressionEventType::ChannelPressure) return "channel_pressure";
    if (type == ExpressionEventType::PolyAftertouch) return "poly_aftertouch";
    return "pitch_bend";
}

template <typename Matcher>
void addPerformanceProperties(juce::DynamicObject& track, const Pattern& pattern, Matcher&& matches) {
    const auto audibleOnDestination = [&](VoiceId voice, double beat) {
        return std::any_of(pattern.notes.begin(), pattern.notes.end(), [&](const auto& note) {
            return note.voice == voice && matches(note.partId, note.voice) &&
                   beat >= note.startBeat - 1.0 && beat <= note.endBeat() + 0.50;
        });
    };
    juce::Array<juce::var> controls;
    std::vector<const ControlEvent*> selectedControls;
    for (const auto& control : pattern.controls) {
        if (!matches(control.partId, control.voice)) continue;
        const auto setup = (control.controller == 6 || control.controller == 38 ||
                            control.controller == 100 || control.controller == 101) &&
                           control.beat <= 0.001;
        if (!setup && !audibleOnDestination(control.voice, control.beat)) continue;
        const auto duplicate = std::find_if(selectedControls.begin(), selectedControls.end(), [&](const auto* item) {
            return std::abs(item->beat - control.beat) < 0.0001 && item->controller == control.controller &&
                   item->channel == control.channel;
        });
        if (duplicate != selectedControls.end()) {
            if ((*duplicate)->partId == 0 && control.partId != 0) *duplicate = &control;
            continue;
        }
        selectedControls.push_back(&control);
    }
    for (const auto* selected : selectedControls) {
        const auto& control = *selected;
        auto* item = new juce::DynamicObject();
        item->setProperty("beat", control.beat);
        item->setProperty("controller", control.controller);
        item->setProperty("value", control.value);
        item->setProperty("channel", control.channel);
        controls.add(juce::var(item));
    }
    juce::Array<juce::var> expressions;
    std::vector<const ExpressionEvent*> selectedExpressions;
    for (const auto& expression : pattern.expressions) {
        if (!matches(expression.partId, expression.voice)) continue;
        if (!audibleOnDestination(expression.voice, expression.beat)) continue;
        const auto duplicate = std::find_if(selectedExpressions.begin(), selectedExpressions.end(), [&](const auto* item) {
            return std::abs(item->beat - expression.beat) < 0.0001 && item->type == expression.type &&
                   item->note == expression.note && item->channel == expression.channel;
        });
        if (duplicate != selectedExpressions.end()) {
            if ((*duplicate)->partId == 0 && expression.partId != 0) *duplicate = &expression;
            continue;
        }
        selectedExpressions.push_back(&expression);
    }
    for (const auto* selected : selectedExpressions) {
        const auto& expression = *selected;
        auto* item = new juce::DynamicObject();
        item->setProperty("beat", expression.beat);
        item->setProperty("type", expressionTypeName(expression.type));
        item->setProperty("value", expression.value);
        item->setProperty("note", expression.note);
        item->setProperty("channel", expression.channel);
        expressions.add(juce::var(item));
    }
    track.setProperty("controls", controls);
    track.setProperty("expressions", expressions);
    track.setProperty("expression_projection_version", 1);
}

void addNativeSoundProperties(juce::DynamicObject& track, const juce::String& device,
                              const juce::String& intent, double prominence,
                              ScoreDepartment department, const juce::String& catalogId = {}) {
    track.setProperty("sound_source", "live_native_neutral_audition");
    track.setProperty("authored_native_device", device);
    track.setProperty("authored_preset_intent", intent);
    const auto semanticContext = intent + " " + track.getProperty("role").toString() + " " +
                                 track.getProperty("orchestral_function").toString();
    const auto profile = neutralAuditionProfile(department, catalogId, semanticContext);
    const auto requestedDevice = neutralDeviceForProfile(profile);
    track.setProperty("audition_policy", "neutral_role_v1");
    track.setProperty("audition_profile", profile);
    track.setProperty("native_device", requestedDevice);
    track.setProperty("preset_intent", "PULSO neutral " + profile + " audition");
    track.setProperty("playback_mode", department == ScoreDepartment::Rhythm
        ? "adaptive_percussion" : "chromatic_instrument");
    track.setProperty("same_pitch_overlap_policy", "trim_previous");
    track.setProperty("articulation_duration_policy", "instrument_bound");
    track.setProperty("timbre_priority", "audit");
    track.setProperty("minimum_intent_fidelity", 0.0);
    track.setProperty("strict_timbre_gate", false);
    track.setProperty("release_max_seconds", neutralReleaseForProfile(profile));
    juce::Array<juce::var> candidates;
    if (profile == "drum") {
        addCandidate(candidates, "909 Core Kit.adg");
        addCandidate(candidates, "808 Core Kit.adg");
    } else {
        addCandidate(candidates, requestedDevice);
        addCandidate(candidates, "Drift");
        addCandidate(candidates, "Operator");
        addCandidate(candidates, "Wavetable");
    }
    track.setProperty("device_candidates", candidates);
    track.setProperty("mixer_gain_db", juce::jlimit(-18.0, 0.0, -12.0 + prominence * 10.0));
}

void addSoundWorldProperties(juce::DynamicObject& track, const Pattern& pattern) {
    track.setProperty("sound_world", juce::String::fromUTF8(pattern.soundWorld.c_str()));
    track.setProperty("production_domain", juce::String::fromUTF8(pattern.productionDomain.c_str()));
    track.setProperty("creative_ready", pattern.creativeReady);
    track.setProperty("creative_score", pattern.creativeScore);
    track.setProperty("sound_warmth", pattern.soundWarmth);
    track.setProperty("sound_brightness", pattern.soundBrightness);
    track.setProperty("acoustic_electronic_balance", pattern.acousticElectronicBalance);
}

void addTimbreSignature(juce::DynamicObject& track, const TimbreSignature& timbre,
                        std::uint64_t seed, std::uint32_t variation, bool locked) {
    auto* signature = new juce::DynamicObject();
    signature->setProperty("source", juce::String::fromUTF8(timbre.source.c_str()));
    signature->setProperty("envelope", juce::String::fromUTF8(timbre.envelope.c_str()));
    signature->setProperty("spectrum", juce::String::fromUTF8(timbre.spectrum.c_str()));
    signature->setProperty("motion", juce::String::fromUTF8(timbre.motion.c_str()));
    signature->setProperty("space", juce::String::fromUTF8(timbre.space.c_str()));
    signature->setProperty("texture", juce::String::fromUTF8(timbre.texture.c_str()));
    signature->setProperty("uniqueness", juce::jlimit(0.0, 1.0, timbre.uniqueness));
    track.setProperty("timbre_signature", juce::var(signature));
    track.setProperty("sound_selection_seed", juce::String(static_cast<juce::int64>(seed)));
    track.setProperty("sound_variation", static_cast<int>(variation));
    track.setProperty("sound_locked", locked);
}

} // namespace

bool writeLiveDeploymentRequest(const Pattern& pattern, const LiveDeploymentOptions& options,
                                juce::String& statusMessage, const juce::File& directoryOverride) {
    if (pattern.notes.empty() || pattern.parts.empty()) {
        statusMessage = "COMPOSE A SONG BEFORE DEPLOYING";
        return false;
    }
    if (pattern.productionAuditPerformed && !pattern.productionReady) {
        statusMessage = "PRODUCTION GATE BLOCKED INVALID SCORE";
        return false;
    }
    auto root = new juce::DynamicObject();
    root->setProperty("schema_version", 11);
    root->setProperty("request_id", juce::Uuid().toString());
    root->setProperty("created_utc_ms", juce::Time::getCurrentTime().toMilliseconds());
    auto safeTitle = options.title.isNotEmpty() ? options.title : juce::String("PULSO Song");
    const auto middleDot = juce::String::charToString(static_cast<juce::juce_wchar>(0x00b7));
    const auto mojibakeDot = juce::String::charToString(static_cast<juce::juce_wchar>(0x00c2)) + middleDot;
    safeTitle = safeTitle.replace(mojibakeDot, " - ").replace(middleDot, " - ");
    root->setProperty("title", safeTitle);
    root->setProperty("bpm", options.bpm);
    root->setProperty("time_signature_numerator", options.numerator);
    root->setProperty("time_signature_denominator", options.denominator);
    root->setProperty("length_beats", pattern.lengthBeats);
    root->setProperty("deployment_mode",
        options.aggregateDepartmentStems ? "quick_3_stem" : "full_orchestration");

    const auto midiOnly = options.soundMode == LiveDeploymentOptions::SoundMode::MidiOnly;
    root->setProperty("sound_engine", midiOnly ? "midi_only" : "ableton_live_native");
    root->setProperty("audition_policy", midiOnly ? "midi_only" : "neutral_role_v1");
    root->setProperty("expression_delivery", "native_editable_with_lossless_midi_source");
    root->setProperty("production_score", pattern.productionScore);
    root->setProperty("production_domain", juce::String::fromUTF8(pattern.productionDomain.c_str()));
    root->setProperty("production_mode_source", juce::String::fromUTF8(pattern.productionModeSource.c_str()));
    if (pattern.productionModeSource == "gpt_plan") {
        root->setProperty("ai_model", ai_config::model);
        root->setProperty("ai_reasoning_effort", ai_config::reasoningEffort);
    }
    juce::Array<juce::var> productionIssues;
    for (const auto& issue : pattern.productionIssues)
        productionIssues.add(juce::String::fromUTF8(issue.c_str()));
    root->setProperty("production_issues", productionIssues);
    root->setProperty("electronic_production_audited", pattern.electronicProductionAudited);
    if (pattern.electronicProductionAudited)
        root->setProperty("electronic_production_score", pattern.electronicProductionScore);
    root->setProperty("sound_world", juce::String::fromUTF8(pattern.soundWorld.c_str()));
    root->setProperty("narrative_audited", pattern.narrativeAuditPerformed);
    root->setProperty("narrative_score", pattern.narrativeScore);
    root->setProperty("creative_ready", pattern.creativeReady);
    root->setProperty("causal_narrative_score", pattern.causalNarrativeScore);
    root->setProperty("narrative_resolution_score", pattern.narrativeResolutionScore);
    root->setProperty("narrative_spine_ready", pattern.narrativeSpineReady);
    root->setProperty("creative_score", pattern.creativeScore);
    root->setProperty("soundscape_audited", pattern.soundscapeAuditPerformed);
    root->setProperty("electronic_fabric_audited", pattern.soundscapeAuditPerformed);
    root->setProperty("percussion_free", pattern.percussionFreeArrangement);
    root->setProperty("soundscape_scene", juce::String::fromUTF8(pattern.soundscapeScene.c_str()));
    root->setProperty("soundscape_spatial_narrative",
                      juce::String::fromUTF8(pattern.soundscapeSpatialNarrative.c_str()));
    root->setProperty("soundscape_ready", pattern.soundscapeReady);
    root->setProperty("soundscape_score", pattern.soundscapeScore);
    root->setProperty("declared_soundscape_layers", static_cast<int>(pattern.declaredSoundscapeLayers));
    root->setProperty("meaningful_soundscape_layers", static_cast<int>(pattern.meaningfulSoundscapeLayers));
    root->setProperty("underdeveloped_soundscape_layers", static_cast<int>(pattern.underdevelopedSoundscapeLayers));
    root->setProperty("median_active_soundscape_layers", pattern.medianActiveSoundscapeLayers);
    root->setProperty("independent_musical_lines", static_cast<int>(pattern.independentMusicalLines));
    root->setProperty("meaningful_musical_lines", static_cast<int>(pattern.meaningfulMusicalLines));
    root->setProperty("protagonist_phrase_windows", static_cast<int>(pattern.protagonistPhraseWindows));
    root->setProperty("arpeggio_note_count", static_cast<int>(pattern.arpeggioNoteCount));
    root->setProperty("electronic_motion_required", pattern.electronicMotionRequired);
    root->setProperty("dialogue_musical_lines", static_cast<int>(pattern.dialogueMusicalLines));
    root->setProperty("harmonic_floor_coverage", pattern.harmonicFloorCoverage);
    root->setProperty("median_harmonic_floor_layers", pattern.medianHarmonicFloorLayers);
    root->setProperty("track_viability_audited", pattern.trackViabilityAudited);
    root->setProperty("track_viability_ready", pattern.trackViabilityReady);
    root->setProperty("track_viability_score", pattern.trackViabilityScore);
    root->setProperty("declared_viability_tracks", static_cast<int>(pattern.declaredViabilityTracks));
    root->setProperty("retained_viability_tracks", static_cast<int>(pattern.retainedViabilityTracks));
    root->setProperty("viable_instrument_tracks", static_cast<int>(pattern.viableInstrumentTracks));
    root->setProperty("token_instrument_tracks", static_cast<int>(pattern.tokenInstrumentTracks));
    root->setProperty("developed_instrument_tracks", static_cast<int>(pattern.developedInstrumentTracks));
    root->setProperty("merged_instrument_tracks", static_cast<int>(pattern.mergedInstrumentTracks));
    root->setProperty("pruned_instrument_tracks", static_cast<int>(pattern.prunedInstrumentTracks));
    root->setProperty("ai_authored_note_ratio", pattern.aiAuthoredNoteRatio);
    root->setProperty("primary_voice_authorship_coverage", pattern.primaryVoiceAuthorshipCoverage);
    root->setProperty("foreground_ai_authorship_ratio", pattern.foregroundAiAuthorshipRatio);
    root->setProperty("movement_bass_ai_authorship_ratio", pattern.movementBassAiAuthorshipRatio);
    const auto foregroundNotes = static_cast<int>(std::count_if(pattern.notes.begin(), pattern.notes.end(), [](const auto& note) {
        return note.voice == VoiceId::Lead || note.voice == VoiceId::Countermelody;
    }));
    const auto movementBassNotes = static_cast<int>(std::count_if(pattern.notes.begin(), pattern.notes.end(), [](const auto& note) {
        return note.voice == VoiceId::MovementBass;
    }));
    root->setProperty("foreground_note_count", foregroundNotes);
    root->setProperty("movement_bass_note_count", movementBassNotes);
    root->setProperty("groove_authorship_coverage", pattern.grooveAuthorshipCoverage);
    root->setProperty("thematic_recall_ratio", pattern.thematicRecallRatio);
    root->setProperty("audible_thematic_similarity", pattern.audibleThematicSimilarity);
    root->setProperty("literal_thematic_return_ratio", pattern.literalThematicReturnRatio);
    root->setProperty("thematic_development", pattern.thematicDevelopment);
    root->setProperty("bass_phrase_continuity", pattern.bassPhraseContinuity);
    root->setProperty("melodic_stepwise_ratio", pattern.melodicStepwiseRatio);
    root->setProperty("maximum_melodic_step_run", static_cast<int>(pattern.maximumMelodicStepRun));
    root->setProperty("maximum_club_drum_gap_bars", static_cast<int>(pattern.maximumClubDrumGapBars));
    root->setProperty("maximum_club_low_end_gap_bars", static_cast<int>(pattern.maximumClubLowEndGapBars));
    root->setProperty("density_control", pattern.densityControl);
    root->setProperty("peak_active_voices", static_cast<int>(pattern.peakActiveVoices));
    root->setProperty("attention_directed", pattern.attentionDirected);
    root->setProperty("structural_breath_bars", static_cast<int>(pattern.structuralBreathBars));
    root->setProperty("phrase_breaths_created", static_cast<int>(pattern.phraseBreathsCreated));
    root->setProperty("attention_notes_removed", static_cast<int>(pattern.attentionNotesRemoved));
    root->setProperty("harmonic_floor_notes_created",
                      static_cast<int>(pattern.harmonicFloorNotesCreated));
    root->setProperty("overcrowded_bars_before", static_cast<int>(pattern.overcrowdedBarsBefore));
    root->setProperty("overcrowded_bars_after", static_cast<int>(pattern.overcrowdedBarsAfter));
    root->setProperty("average_active_parts_before", pattern.averageActivePartsBefore);
    root->setProperty("average_active_parts_after", pattern.averageActivePartsAfter);
    root->setProperty("underfilled_bars_before", static_cast<int>(pattern.underfilledBarsBefore));
    root->setProperty("underfilled_bars_after", static_cast<int>(pattern.underfilledBarsAfter));
    root->setProperty("overloaded_bars_before", static_cast<int>(pattern.overloadedBarsBefore));
    root->setProperty("overloaded_bars_after", static_cast<int>(pattern.overloadedBarsAfter));
    root->setProperty("average_perceptual_load_before", pattern.averagePerceptualLoadBefore);
    root->setProperty("average_perceptual_load_after", pattern.averagePerceptualLoadAfter);
    root->setProperty("peak_perceptual_load_before", pattern.peakPerceptualLoadBefore);
    root->setProperty("peak_perceptual_load_after", pattern.peakPerceptualLoadAfter);
    root->setProperty("density_notes_removed", static_cast<int>(pattern.densityNotesRemoved));
    root->setProperty("semantic_notes_removed", static_cast<int>(pattern.semanticNotesRemoved));
    root->setProperty("authored_notes_preserved", static_cast<int>(pattern.authoredNotesPreserved));
    root->setProperty("thematic_ownership_directed", pattern.thematicOwnershipDirected);
    root->setProperty("foreground_tracks_before", static_cast<int>(pattern.foregroundTracksBefore));
    root->setProperty("foreground_tracks_after", static_cast<int>(pattern.foregroundTracksAfter));
    root->setProperty("thematic_tracks_consolidated",
                      static_cast<int>(pattern.thematicTracksConsolidated));
    root->setProperty("content_lane_count", static_cast<int>(pattern.contentLaneCount));
    root->setProperty("timbral_handoff_destinations",
                      static_cast<int>(pattern.timbralHandoffDestinations));
    root->setProperty("timbral_handoff_windows",
                      static_cast<int>(pattern.timbralHandoffWindows));
    root->setProperty("timbral_handoff_notes", static_cast<int>(pattern.timbralHandoffNotes));
    root->setProperty("exact_instrument_cast_published", pattern.exactInstrumentCastPublished);
    root->setProperty("thematic_notes_reassigned",
                      static_cast<int>(pattern.thematicNotesReassigned));
    juce::Array<juce::var> narrativeIssues;
    for (const auto& issue : pattern.narrativeIssues)
        narrativeIssues.add(juce::String::fromUTF8(issue.c_str()));
    root->setProperty("narrative_issues", narrativeIssues);

    juce::Array<juce::var> tracks;
    if (options.aggregateDepartmentStems) {
        for (auto departmentIndex = 0; departmentIndex < 3; ++departmentIndex) {
            const auto department = static_cast<ScoreDepartment>(departmentIndex);
            juce::Array<juce::var> notes;
            for (const auto& note : pattern.notes) {
                const auto part = std::find_if(pattern.parts.begin(), pattern.parts.end(), [&](const auto& item) {
                    return item.id == note.partId && item.department == department;
                });
                if (part == pattern.parts.end()) continue;
                auto item = new juce::DynamicObject();
                item->setProperty("pitch", note.pitch);
                item->setProperty("start", note.startBeat);
                item->setProperty("duration", note.durationBeats);
                item->setProperty("velocity", note.velocity);
                item->setProperty("channel", note.channel);
                item->setProperty("origin", juce::String(noteOriginKey(note.origin).data()));
                item->setProperty("narrative_id", static_cast<int>(note.narrativeId));
                notes.add(juce::var(item));
            }
            if (notes.isEmpty()) continue;
            auto track = new juce::DynamicObject();
            const auto label = department == ScoreDepartment::Rhythm ? "RHYTHM" :
                               department == ScoreDepartment::Harmony ? "HARMONY" : "MELODY";
            track->setProperty("name", "PULSO " + juce::String(label));
            track->setProperty("track_key", "department:" + departmentName(department));
            track->setProperty("department", departmentName(department));
            track->setProperty("role", "Native Live Sound Director stem");
            track->setProperty("catalog_id", department == ScoreDepartment::Rhythm ? "production_drums" :
                department == ScoreDepartment::Harmony ? "harmonic_ensemble" : "foreground_voice");
            addNativeSoundProperties(*track,
                department == ScoreDepartment::Rhythm ? "Drum Rack" :
                department == ScoreDepartment::Harmony ? "Instrument Rack" : "Wavetable",
                department == ScoreDepartment::Rhythm ? "cohesive production drum kit" :
                department == ScoreDepartment::Harmony ? "warm expressive harmonic ensemble" :
                                                         "expressive foreground voice", 0.72, department,
                department == ScoreDepartment::Rhythm ? "production_drums" :
                department == ScoreDepartment::Harmony ? "harmonic_ensemble" : "foreground_voice");
            addSoundWorldProperties(*track, pattern);
            addTimbreSignature(*track, TimbreSignature{}, pattern.seed + departmentIndex, 0, false);
            addPerformanceProperties(*track, pattern, [&](std::uint16_t partId, VoiceId voice) {
                if (partId != 0) {
                    const auto found = std::find_if(pattern.parts.begin(), pattern.parts.end(),
                        [&](const auto& part) { return part.id == partId; });
                    return found != pattern.parts.end() && found->department == department;
                }
                return std::any_of(pattern.parts.begin(), pattern.parts.end(), [&](const auto& part) {
                    return part.department == department && part.sourceVoice == voice;
                });
            });
            track->setProperty("notes", notes);
            tracks.add(juce::var(track));
        }
    } else {
    for (const auto& part : pattern.parts) {
        juce::Array<juce::var> notes;
        for (const auto& note : pattern.notes) {
            if (note.partId != part.id) continue;
            auto item = new juce::DynamicObject();
            item->setProperty("pitch", note.pitch);
            item->setProperty("start", note.startBeat);
            item->setProperty("duration", note.durationBeats);
            item->setProperty("velocity", note.velocity);
            item->setProperty("channel", note.channel);
            item->setProperty("origin", juce::String(noteOriginKey(note.origin).data()));
            item->setProperty("narrative_id", static_cast<int>(note.narrativeId));
            notes.add(juce::var(item));
        }
        if (notes.isEmpty()) continue;
        auto track = new juce::DynamicObject();
        track->setProperty("name", "PULSO " + juce::String::fromUTF8(part.name.c_str()));
        track->setProperty("track_key", "part:" + juce::String(static_cast<int>(part.id)));
        track->setProperty("part_id", static_cast<int>(part.id));
        track->setProperty("catalog_id", juce::String::fromUTF8(part.catalogId.c_str()));
        track->setProperty("department", departmentName(part.department));
        track->setProperty("role", juce::String::fromUTF8(part.role.c_str()));
        track->setProperty("content_lane_id", juce::String::fromUTF8(part.contentLaneId.c_str()));
        track->setProperty("line_relationship", juce::String::fromUTF8(part.lineRelationship.c_str()));
        track->setProperty("orchestral_function", juce::String::fromUTF8(part.orchestralFunction.c_str()));
        track->setProperty("articulation", juce::String::fromUTF8(part.articulation.c_str()));
        track->setProperty("divisi_voices", part.divisiVoices);
        addNativeSoundProperties(*track, juce::String::fromUTF8(part.liveDevice.c_str()),
            juce::String::fromUTF8(part.livePresetIntent.c_str()), part.prominence,
            part.department, juce::String::fromUTF8(part.catalogId.c_str()));
        addSoundWorldProperties(*track, pattern);
        addTimbreSignature(*track, part.timbre,
            pattern.seed ^ (static_cast<std::uint64_t>(part.id) * 0x9e3779b97f4a7c15ULL),
            part.liveSoundVariation, part.liveSoundLocked);
        addPerformanceProperties(*track, pattern, [&](std::uint16_t partId, VoiceId voice) {
            return partId == part.id || (partId == 0 && voice == part.sourceVoice);
        });
        track->setProperty("notes", notes);
        tracks.add(juce::var(track));
    }
    }
    root->setProperty("tracks", tracks);
    if (tracks.isEmpty()) {
        statusMessage = "NO ORCHESTRAL PARTS TO DEPLOY";
        return false;
    }

    const auto directory = directoryOverride.getFullPathName().isEmpty() ? bridgeDirectory()
                                                                         : directoryOverride;
    if (!directory.createDirectory()) {
        statusMessage = "CANNOT CREATE LIVE BRIDGE DIRECTORY";
        return false;
    }
    const auto pending = directory.getChildFile("request.pending.json");
    const auto request = directory.getChildFile("request.json");
    if (!pending.replaceWithText(juce::JSON::toString(juce::var(root), false), false, false, "\n")) {
        statusMessage = "CANNOT WRITE LIVE DEPLOYMENT REQUEST";
        return false;
    }
    const auto published = request.existsAsFile() ? pending.replaceFileIn(request)
                                                   : pending.moveFileTo(request);
    if (!published) {
        statusMessage = "CANNOT WRITE LIVE DEPLOYMENT REQUEST";
        return false;
    }
    statusMessage = "DEPLOY REQUEST SENT - " + juce::String(tracks.size()) + " TRACKS";
    return true;
}

juce::String readLiveDeploymentStatus() {
    if (!liveBridgeIsAvailable()) return "ENABLE PulsoDeployRemote IN LIVE SETTINGS";
    const auto file = bridgeDirectory().getChildFile("status.json");
    if (!file.existsAsFile()) return {};
    const auto parsed = juce::JSON::parse(file);
    if (auto* object = parsed.getDynamicObject())
        return object->getProperty("message").toString();
    return {};
}

bool liveBridgeIsAvailable() {
    const auto heartbeat = bridgeDirectory().getChildFile("heartbeat.json");
    return heartbeat.existsAsFile() &&
           juce::Time::getCurrentTime().toMilliseconds() - heartbeat.getLastModificationTime().toMilliseconds() < 4000;
}

juce::String readLiveNativeInventorySummary() {
    const auto file = bridgeDirectory().getChildFile("inventory.json");
    if (!file.existsAsFile()) return "NATIVE INVENTORY PENDING";
    const auto parsed = juce::JSON::parse(file);
    if (auto* object = parsed.getDynamicObject()) {
        const auto count = static_cast<int>(object->getProperty("loadable_count"));
        return juce::String(count) + " LIVE SOUNDS INDEXED";
    }
    return "NATIVE INVENTORY PENDING";
}

juce::String readLiveNativeCapabilitiesSummary() {
    const auto parsed = juce::JSON::parse(bridgeDirectory().getChildFile("inventory.json"));
    const auto* root = parsed.getDynamicObject();
    const auto* capabilities = root != nullptr
        ? root->getProperty("capabilities").getDynamicObject() : nullptr;
    if (capabilities == nullptr) return {};
    const auto join = [capabilities](const char* property) {
        juce::StringArray values;
        if (const auto* array = capabilities->getProperty(property).getArray())
            for (const auto& value : *array) values.add(value.toString());
        return values.joinIntoString(", ");
    };
    return "Ableton playback inventory. Prefer exact installed identities: " + join("exact") +
        ". Family-only substitutions (use intentionally and sparingly): " + join("family_fallback") +
        ". Unavailable identities (do not assign unless structurally essential): " + join("unavailable") + ".";
}

juce::String readLiveDeploymentReport() {
    const auto parsed = juce::JSON::parse(bridgeDirectory().getChildFile("status.json"));
    const auto* root = parsed.getDynamicObject();
    const auto* details = root != nullptr ? root->getProperty("details").getDynamicObject() : nullptr;
    if (details == nullptr) return {};
    juce::StringArray lines;
    if (const auto* sounds = details->getProperty("sounds").getArray()) {
        for (const auto& value : *sounds) {
            const auto* sound = value.getDynamicObject();
            if (sound == nullptr) continue;
            auto line = sound->getProperty("track").toString() + " -> " +
                        sound->getProperty("matched").toString() + " [" +
                        sound->getProperty("quality").toString() + "] " +
                        sound->getProperty("state").toString();
            if (static_cast<bool>(sound->getProperty("shared_sound"))) line += " (shared)";
            lines.add(std::move(line));
        }
    }
    return lines.joinIntoString("\n");
}

juce::String readLiveAudibleExecutionFeedback() {
    juce::StringArray issues;
    const auto status = juce::JSON::parse(bridgeDirectory().getChildFile("status.json"));
    const auto* root = status.getDynamicObject();
    const auto* details = root != nullptr ? root->getProperty("details").getDynamicObject() : nullptr;
    if (details != nullptr) {
        if (const auto* contracts = details->getProperty("timbre_contracts").getArray()) {
            for (const auto& value : *contracts) {
                const auto* contract = value.getDynamicObject();
                if (contract == nullptr || static_cast<bool>(contract->getProperty("passed"))) continue;
                issues.add(contract->getProperty("track").toString() + " matched " +
                    contract->getProperty("matched").toString() + " below fidelity floor " +
                    contract->getProperty("minimum_fidelity").toString());
                if (issues.size() >= 6) break;
            }
        }
    }
    const auto audible = juce::JSON::parse(bridgeDirectory().getChildFile("audible_audit.json"));
    if (const auto* object = audible.getDynamicObject()) {
        const auto observations = static_cast<int>(object->getProperty("expected_active_observations"));
        if (observations > 0) {
            const auto presence = static_cast<double>(object->getProperty("audible_presence_ratio"));
            const auto tails = static_cast<int>(object->getProperty("tail_violations"));
            if (presence < 0.92)
                issues.add("rendered meter presence ratio=" + juce::String(presence, 3));
            if (tails > 0)
                issues.add("rendered tail violations=" + juce::String(tails));
        }
    }
    if (issues.isEmpty()) return {};
    return "Previous Ableton audible execution defects: " + issues.joinIntoString("; ") +
        ". Choose installed identities and articulation/envelope intents that remove these causes.";
}

bool liveNativeInventoryIsReady() {
    if (!liveBridgeIsAvailable()) return false;
    const auto parsed = juce::JSON::parse(bridgeDirectory().getChildFile("inventory.json"));
    if (auto* object = parsed.getDynamicObject())
        return static_cast<bool>(object->getProperty("complete")) &&
               static_cast<int>(object->getProperty("loadable_count")) > 0;
    return false;
}

} // namespace pulso::plugin

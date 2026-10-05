#include "SongGenerationPipeline.h"

#include "core/ElectronicProductionDirector.h"

#include <algorithm>
#include <cmath>

namespace pulso::plugin {

juce::String SongGenerationPipeline::aiDirection(const SongGenerationRequest& request) {
    auto direction = request.direction;
    if (request.supportingContext.isNotEmpty())
        direction += "\n" + request.supportingContext;
    switch (request.orchestration) {
        case SongOrchestrationIntent::ClubElectronic:
            direction += "\nProduction mode: club electronic. Think as a producer and DJ: build kick-bass interlock, evolving groove DNA, one foreground hook, subtractive arrangement, automation, spectral restraint and useful mix-in/mix-out energy. Do not add orchestral instruments unless explicitly requested.";
            break;
        case SongOrchestrationIntent::DeepProduction:
            direction += "\nOrchestration mode: deep production. Build a detailed hybrid acoustic/electronic ensemble with independent harmonic families, controlled counterpoint, automation and production-ready negative space.";
            break;
        case SongOrchestrationIntent::Symphonic:
            direction += "\nOrchestration mode: symphonic. Treat the orchestra as multiple independent choirs with divisi strings, woodwind and brass dialogue, orchestral percussion, register-aware counterpoint, articulation contrast and a long-range chamber-to-tutti arc.";
            break;
        case SongOrchestrationIntent::Adaptive:
            break;
    }
    return direction;
}

int SongGenerationPipeline::targetBars(const SongGenerationRequest& request) {
    return SongComposer::phraseAlignedBars(static_cast<int>(std::lround(
        request.targetSeconds * request.bpm / 60.0 / request.beatsPerBar)));
}

SongPlan SongGenerationPipeline::plan(const SongGenerationRequest& request,
                                      std::stop_token token, juce::String& error,
                                      const AiSongProgress& progress,
                                      const AiSongCheckpoint& checkpoint) {
    return AiComposer::planSong(aiDirection(request), request.targetSeconds,
        targetBars(request), request.bpm, request.beatsPerBar, request.seed,
        request.behavior, token, error, progress, checkpoint, request.aiSovereign);
}

void SongGenerationPipeline::finalizePlan(SongPlan& plan,
                                           const SongGenerationRequest& request) {
    plan.compositionBehavior = request.behavior;
    plan.seed = request.seed;
    plan.targetSeconds = request.targetSeconds;
    plan.aiSovereign = request.aiSovereign;
    const auto inferredProduction = ElectronicProductionDirector::infer(
        request.direction.toStdString());
    switch (request.orchestration) {
        case SongOrchestrationIntent::ClubElectronic:
            plan.productionLanguage = inferredProduction;
            plan.productionLanguage.domain = ProductionDomain::ClubElectronic;
            plan.productionLanguage.electronicIntent = 1.0;
            plan.productionLanguage.clubFocus = 1.0;
            plan.productionLanguage.orchestralAllowance = 0.0;
            plan.productionModeSource = "user_club_electronic";
            break;
        case SongOrchestrationIntent::DeepProduction:
            plan.productionLanguage.domain = ProductionDomain::Hybrid;
            plan.productionLanguage.electronicIntent = std::max(plan.productionLanguage.electronicIntent, 0.70);
            plan.productionLanguage.orchestralAllowance = std::max(plan.productionLanguage.orchestralAllowance, 0.55);
            plan.orchestrationLanguage.ensembleScale = std::max(plan.orchestrationLanguage.ensembleScale, 0.78);
            plan.orchestrationLanguage.harmonicDepth = std::max(plan.orchestrationLanguage.harmonicDepth, 0.84);
            plan.orchestrationLanguage.counterpointActivity = std::max(plan.orchestrationLanguage.counterpointActivity, 0.66);
            plan.orchestrationLanguage.familyDialogue = std::max(plan.orchestrationLanguage.familyDialogue, 0.78);
            plan.orchestrationLanguage.hybridProduction = std::max(plan.orchestrationLanguage.hybridProduction, 0.72);
            plan.productionModeSource = "user_deep_hybrid";
            break;
        case SongOrchestrationIntent::Symphonic:
            plan.productionLanguage.domain = ProductionDomain::Orchestral;
            plan.productionLanguage.orchestralAllowance = 1.0;
            plan.orchestrationLanguage.ensembleScale = std::max(plan.orchestrationLanguage.ensembleScale, 0.92);
            plan.orchestrationLanguage.harmonicDepth = std::max(plan.orchestrationLanguage.harmonicDepth, 0.92);
            plan.orchestrationLanguage.counterpointActivity = std::max(plan.orchestrationLanguage.counterpointActivity, 0.80);
            plan.orchestrationLanguage.divisiDepth = std::max(plan.orchestrationLanguage.divisiDepth, 0.84);
            plan.orchestrationLanguage.articulationContrast = std::max(plan.orchestrationLanguage.articulationContrast, 0.82);
            plan.orchestrationLanguage.familyDialogue = std::max(plan.orchestrationLanguage.familyDialogue, 0.88);
            plan.orchestrationLanguage.hybridProduction = std::min(plan.orchestrationLanguage.hybridProduction, 0.28);
            plan.productionModeSource = "user_symphonic";
            break;
        case SongOrchestrationIntent::Adaptive:
            break;
    }
    SongComposer::normalizePlan(plan);
}

Pattern SongGenerationPipeline::render(const SongPlan& plan,
                                      const SongGenerationRequest& request,
                                      const std::function<void(std::size_t, std::size_t,
                                                               const SongSection&)>& progress,
                                      CompositionRenderReport* report) {
    GenerationContext context;
    context.role = Role::Ensemble;
    context.seed = request.seed;
    context.variationIndex = request.variationIndex;
    context.bars = plan.totalBars;
    context.beatsPerBar = plan.beatsPerBar;
    context.rootPitchClass = plan.rootPitchClass;
    context.scale = plan.scale;
    context.humanize = 0.0; // The MIDI score is grid-accurate on both hosts.
    const auto third = plan.scale == ScaleKind::Minor || plan.scale == ScaleKind::Dorian ? 3 : 4;
    context.chordPitchClasses = {plan.rootPitchClass,
        (plan.rootPitchClass + third) % 12, (plan.rootPitchClass + 7) % 12};
    return SongComposer{}.render(plan, context, progress, report);
}

} // namespace pulso::plugin

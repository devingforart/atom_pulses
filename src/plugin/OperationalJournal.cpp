#include "OperationalJournal.h"

#include <map>
#include <mutex>

namespace pulso::plugin {
namespace {

std::mutex journalMutex;

juce::File pulsoRoot() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("PULSO");
}

juce::Array<juce::var> stringArray(const std::vector<std::string>& values) {
    juce::Array<juce::var> result;
    for (const auto& value : values) result.add(juce::String::fromUTF8(value.c_str()));
    return result;
}

} // namespace

juce::File OperationalJournal::logFile() {
    return pulsoRoot().getChildFile("pulso-operational.log");
}

void OperationalJournal::write(const juce::String& level, const juce::String& stage,
                               const juce::String& message) {
    const std::scoped_lock lock(journalMutex);
    const auto file = logFile();
    file.getParentDirectory().createDirectory();
    juce::FileOutputStream stream(file);
    if (!stream.openedOk()) return;
    stream.setPosition(file.getSize());
    const auto line = juce::Time::getCurrentTime().formatted("%Y-%m-%d %H:%M:%S") +
        "  [" + level + "] [" + stage + "]  " + message + "\r\n";
    stream.writeText(line, false, false, "\r\n");
    stream.flush();
}

juce::File OperationalJournal::writeRejectedAudit(
    const SongPlan& plan, const CompositionRenderReport& report, const juce::String& reason,
    std::size_t repairPasses) {
    const std::scoped_lock lock(journalMutex);
    auto root = new juce::DynamicObject();
    root->setProperty("schema_version", 1);
    root->setProperty("created_at", juce::Time::getCurrentTime().toISO8601(true));
    root->setProperty("seed", juce::String(static_cast<juce::int64>(plan.seed)));
    root->setProperty("title", juce::String::fromUTF8(plan.title.c_str()));
    root->setProperty("key", juce::String::fromUTF8(plan.key.c_str()));
    root->setProperty("bars", plan.totalBars);
    root->setProperty("instruments", static_cast<int>(plan.instruments.size()));
    root->setProperty("performance_cells", static_cast<int>(plan.performanceScore.cells.size()));
    root->setProperty("performance_placements",
                      static_cast<int>(plan.performanceScore.placements.size()));
    root->setProperty("repair_passes", static_cast<int>(repairPasses));
    root->setProperty("terminal_reason", reason);
    root->setProperty("production_ready", report.production.ready);
    root->setProperty("production_score", report.production.score);
    root->setProperty("creative_ready", report.narrative.creativeReady);
    root->setProperty("creative_score", report.narrative.score);
    root->setProperty("narrative_resolution", report.narrative.resolutionScore);
    root->setProperty("density_control", report.narrative.densityControl);
    root->setProperty("soundscape_ready", report.soundscape.ready);
    root->setProperty("soundscape_score", report.soundscape.score);
    root->setProperty("soundscape_static_layers",
                      static_cast<int>(report.soundscape.staticLayerRuns));
    root->setProperty("soundscape_underdeveloped",
                      static_cast<int>(report.soundscape.underdevelopedVoices +
                                       report.soundscape.underdevelopedEnvironments));
    root->setProperty("track_viability_ready", report.trackViability.ready);
    root->setProperty("track_viability_score", report.trackViability.score);
    root->setProperty("token_tracks", static_cast<int>(report.trackViability.tokenTracks));
    root->setProperty("narrative_issues", stringArray(report.narrative.issues));
    root->setProperty("soundscape_issues", stringArray(report.soundscape.issues));
    root->setProperty("track_viability_issues", stringArray(report.trackViability.issues));

    std::map<std::string, std::size_t> notesByInstrument;
    for (const auto& cell : plan.performanceScore.cells)
        for (const auto& note : cell.notes)
            ++notesByInstrument[note.instrumentId];
    juce::Array<juce::var> instruments;
    for (const auto& instrument : plan.instruments) {
        auto item = new juce::DynamicObject();
        item->setProperty("id", juce::String::fromUTF8(instrument.id.c_str()));
        item->setProperty("catalog_id", juce::String::fromUTF8(instrument.instrumentId.c_str()));
        item->setProperty("name", juce::String::fromUTF8(instrument.name.c_str()));
        item->setProperty("role", juce::String::fromUTF8(instrument.role.c_str()));
        item->setProperty("authored_notes", static_cast<int>(notesByInstrument[instrument.id]));
        instruments.add(juce::var(item));
    }
    root->setProperty("instrument_audit", instruments);

    const auto auditDirectory = pulsoRoot().getChildFile("Audits");
    auditDirectory.createDirectory();
    const auto stamp = juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S");
    const auto file = auditDirectory.getChildFile(
        "rejected-" + stamp + "-seed-" + juce::String(static_cast<juce::int64>(plan.seed)) + ".json");
    file.replaceWithText(juce::JSON::toString(juce::var(root), false), false, false, "\n");
    return file;
}

} // namespace pulso::plugin

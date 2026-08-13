#include "Localization.h"

#include "PluginProcessor.h"

#include <array>

namespace pulso::plugin {
namespace {

juce::String utf8(const char8_t* value) {
    return juce::String::fromUTF8(reinterpret_cast<const char*>(value));
}

const char8_t* english(TextId id) noexcept {
    switch (id) {
        case TextId::Subtitle: return u8"AI COMPOSITION BROWSER";
        case TextId::PromptLabel: return u8"DESCRIBE THE IDEA (OPTIONAL)";
        case TextId::DurationLabel: return u8"SONG LENGTH";
        case TextId::PromptPlaceholder: return u8"e.g. intimate nocturnal soul, memorable hook, tension blooming before the climax…";
        case TextId::GenerateIdea: return u8"GENERATE IDEA";
        case TextId::ComposeSong: return u8"COMPOSE SONG";
        case TextId::NextIdea: return u8"NEXT IDEA";
        case TextId::RegenerateUnlocked: return u8"REGENERATE UNLOCKED";
        case TextId::Undo: return u8"UNDO";
        case TextId::PreviewAudio: return u8"PREVIEW AUDIO";
        case TextId::HumanPerformance: return u8"HUMAN PERFORMANCE";
        case TextId::MidiThru: return u8"MIDI THRU";
        case TextId::LockHarmony: return u8"LOCK HARMONY + FX";
        case TextId::LockMelodic: return u8"LOCK MELODIC";
        case TextId::LockBass: return u8"LOCK BASS";
        case TextId::LockRhythm: return u8"LOCK RHYTHM";
        case TextId::LockHarmonyTip: return u8"Preserve foundation, pulses, upper harmony, atmosphere and transitions while other families change.";
        case TextId::LockMelodicTip: return u8"Keep lead and countermelody exactly while regenerating other voice families.";
        case TextId::LockBassTip: return u8"Keep sub bass and movement bass exactly while regenerating unlocked voices.";
        case TextId::LockRhythmTip: return u8"Keep kick, snare, hats and both percussion layers exactly while other families change.";
        case TextId::GenerateTip: return u8"Ask GPT to compose a complete coherent idea. Without an API key, PULSO clearly uses its local engine.";
        case TextId::NextTip: return u8"Create the next idea. Locked layers remain note-for-note identical; unlocked layers are recomposed.";
        case TextId::RegenerateTip: return u8"Recompose only unlocked layers around everything you decided to keep.";
        case TextId::UndoTip: return u8"Restore the complete previous idea. Press again to toggle back.";
        case TextId::PreviewTip: return u8"Enable the multitimbral reference ensemble. MIDI export and MIDI output are unaffected.";
        case TextId::PerformanceTip: return u8"OFF keeps exact sixteenth-note onsets and expressive note lengths. ON adds one deterministic performance pass with voice-specific timing. Dragged MIDI keeps exact onsets; recording PULSO MIDI captures the performed timing.";
        case TextId::SoundWorldTip: return u8"Choose the preview world for all fifteen voices. AUTO reads the creative direction. This changes monitoring, not composition or exported MIDI.";
        case TextId::ThruTip: return u8"Pass incoming MIDI to the output alongside PULSO's composition.";
        case TextId::PromptTip: return u8"Describe mood, movement, instrumentation or narrative in natural language. Leave empty for an autonomous idea.";
        case TextId::DurationTip: return u8"Target duration. Use 9:00 or '9 min' for a full song; type IDEA for a short compositional sketch.";
        case TextId::TitleTip: return u8"PULSO turns compositional intent into editable multitrack MIDI.";
        case TextId::SubtitleTip: return u8"Installed version and current product mode.";
        case TextId::StatusTip: return u8"Host tempo, phrase length, idea lineage and transport state.";
        case TextId::AiTip: return u8"GPT status is explicit. PULSO never labels local fallback output as AI-generated.";
        case TextId::IdeaTitleTip: return u8"Title and tonal centre proposed for the current composition.";
        case TextId::IdeaDescriptionTip: return u8"Compositional intention behind the current idea.";
        case TextId::PatternTip: return u8"Each lane is an execution role; its label shows the orchestral parts sharing it. Click for preview sound, octave, level and audition. Full Song exports one MIDI track per populated instrument.";
        case TextId::LanguageTip: return u8"Change the complete PULSO interface and every tooltip between English and Spanish. The choice is saved with the project.";
        case TextId::PreviewSound: return u8"PREVIEW SOUND";
        case TextId::Octave: return u8"OCTAVE";
        case TextId::Level: return u8"LEVEL";
        case TextId::Audition: return u8"AUDITION";
        case TextId::SoundTip: return u8"Select a synthesis model for this voice. Follow Family uses the voice family's compatible default.";
        case TextId::OctaveDownTip: return u8"Preview this voice one octave lower.";
        case TextId::OctaveOriginalTip: return u8"Use the composition's original register.";
        case TextId::OctaveUpTip: return u8"Preview this voice one octave higher.";
        case TextId::LevelTip: return u8"Independent preview level from -36 dB to +6 dB. MIDI velocity and export remain unchanged.";
        case TextId::AuditionTip: return u8"Play this voice through its selected sound, octave and level without sending MIDI to Ableton.";
        case TextId::FullSong: return u8"FULL SONG";
        case TextId::Rhythm: return u8"RHYTHM";
        case TextId::Bass: return u8"BASS";
        case TextId::Harmony: return u8"HARMONY";
        case TextId::LeadsFx: return u8"LEADS + FX";
        case TextId::Section: return u8"SECTION";
        case TextId::EmptyPattern: return u8"Compose an idea to create editable MIDI";
        case TextId::Play: return u8"PLAY";
        case TextId::Paused: return u8"PAUSED";
        case TextId::Preview: return u8"PREVIEW";
        case TextId::Bar: return u8"BAR";
        case TextId::Bars: return u8"BARS";
        case TextId::Idea: return u8"IDEA";
        case TextId::ExportFailed: return u8"MIDI EXPORT FAILED";
        case TextId::DropIntoAbleton: return u8"DROP INTO ABLETON";
        case TextId::MidiReady: return u8"MIDI READY";
        case TextId::DragUnavailable: return u8"DRAG NOT AVAILABLE";
        case TextId::Solo: return u8"SOLO";
        case TextId::SoloOff: return u8"SOLO OFF";
        case TextId::Muted: return u8"MUTED";
        case TextId::MuteOff: return u8"MUTE OFF";
        case TextId::KickMuted: return u8"KICK MUTED";
        case TextId::KickReduced: return u8"REDUCED KICK";
        case TextId::KickSparse: return u8"SPARSE KICK";
        case TextId::KickFourOnFloor: return u8"FOUR ON THE FLOOR";
        case TextId::Cancel: return u8"CANCEL";
        case TextId::Cancelling: return u8"CANCELLING…";
        case TextId::ProgressTip: return u8"PULSO composes in the background. The current idea remains available until the new one is ready.";
        case TextId::CancelTip: return u8"Stop the request immediately and keep the composition already playing.";
        case TextId::GptComposing: return u8"GPT IS COMPOSING YOUR IDEA";
        case TextId::Composing: return u8"COMPOSING YOUR IDEA";
        case TextId::CurrentKeepsPlaying: return u8"The current composition keeps playing while the new one is prepared.";
        case TextId::Directing: return u8"DIRECTING HARMONY, MELODY, BASS AND RHYTHM";
        case TextId::Working: return u8"WORKING";
        case TextId::SoundStage: return u8"LIVE SOUND DIRECTOR";
        case TextId::SoundStageTip: return u8"Index installed native Live sounds, create editable Arrangement tracks and load the AI-selected device or a reported fallback.";
        case TextId::DeployLive: return u8"CREATE IN LIVE";
        case TextId::DeployLiveTip: return u8"Create one editable Arrangement track and MIDI clip per orchestral instrument, then load only native Live devices and Racks.";
    }
    return u8"";
}

const char8_t* spanish(TextId id) noexcept {
    switch (id) {
        case TextId::Subtitle: return u8"NAVEGADOR DE COMPOSICIÓN CON IA";
        case TextId::PromptLabel: return u8"DESCRIBE LA IDEA (OPCIONAL)";
        case TextId::DurationLabel: return u8"DURACIÓN";
        case TextId::PromptPlaceholder: return u8"p. ej. soul nocturno e íntimo, motivo memorable y tensión antes del clímax…";
        case TextId::GenerateIdea: return u8"GENERAR IDEA";
        case TextId::ComposeSong: return u8"COMPONER CANCIÓN";
        case TextId::NextIdea: return u8"SIGUIENTE IDEA";
        case TextId::RegenerateUnlocked: return u8"REGENERAR LIBRES";
        case TextId::Undo: return u8"DESHACER";
        case TextId::PreviewAudio: return u8"ESCUCHAR AUDIO";
        case TextId::HumanPerformance: return u8"INTERPRETACIÓN HUMANA";
        case TextId::MidiThru: return u8"PASO MIDI";
        case TextId::LockHarmony: return u8"FIJAR ARMONÍA + FX";
        case TextId::LockMelodic: return u8"FIJAR MELODÍAS";
        case TextId::LockBass: return u8"FIJAR BAJOS";
        case TextId::LockRhythm: return u8"FIJAR RITMO";
        case TextId::LockHarmonyTip: return u8"Conserva foundation, pulses, armonía superior, atmósferas y transiciones mientras cambian las demás familias.";
        case TextId::LockMelodicTip: return u8"Conserva exactamente lead y contramelodía mientras se regeneran las demás familias.";
        case TextId::LockBassTip: return u8"Conserva exactamente sub bass y movement bass mientras se regeneran las voces libres.";
        case TextId::LockRhythmTip: return u8"Conserva exactamente bombo, caja, hats y ambas percusiones mientras cambian las demás familias.";
        case TextId::GenerateTip: return u8"Pide a GPT una idea completa y coherente. Sin clave API, PULSO indica claramente que utiliza el motor local.";
        case TextId::NextTip: return u8"Crea la siguiente idea. Las capas fijadas permanecen idénticas; las libres se recomponen.";
        case TextId::RegenerateTip: return u8"Recompone solamente las capas libres alrededor de todo lo que decidiste conservar.";
        case TextId::UndoTip: return u8"Restaura la idea anterior completa. Vuelve a pulsar para alternar.";
        case TextId::PreviewTip: return u8"Activa el ensemble multitímbrico de referencia. La exportación y la salida MIDI no cambian.";
        case TextId::PerformanceTip: return u8"OFF conserva ataques exactos en semicorcheas y duraciones expresivas. ON agrega una interpretación determinista con timing propio por voz. El MIDI arrastrado conserva ataques exactos; grabar la salida captura la interpretación.";
        case TextId::SoundWorldTip: return u8"Elige el mundo sonoro de escucha para las quince voces. AUTO interpreta la dirección creativa. Sólo cambia la monitorización, no la composición ni el MIDI exportado.";
        case TextId::ThruTip: return u8"Envía el MIDI entrante a la salida junto con la composición de PULSO.";
        case TextId::PromptTip: return u8"Describe ambiente, movimiento, instrumentación o narrativa con lenguaje natural. Déjalo vacío para una idea autónoma.";
        case TextId::DurationTip: return u8"Duración objetivo. Usa 9:00 o '9 min' para una canción completa; escribe IDEA para un boceto corto.";
        case TextId::TitleTip: return u8"PULSO convierte intención compositiva en MIDI multipista editable.";
        case TextId::SubtitleTip: return u8"Versión instalada y modo actual del producto.";
        case TextId::StatusTip: return u8"Tempo del host, duración de frase, linaje de la idea y estado del transporte.";
        case TextId::AiTip: return u8"El estado de GPT es explícito. PULSO nunca presenta una salida local como generada por IA.";
        case TextId::IdeaTitleTip: return u8"Título y centro tonal propuestos para la composición actual.";
        case TextId::IdeaDescriptionTip: return u8"Intención compositiva de la idea actual.";
        case TextId::PatternTip: return u8"Cada fila es un rol de ejecución y su nombre muestra las partes orquestales que lo comparten. Pulsa para configurar la escucha. Full Song exporta una pista MIDI por instrumento con material.";
        case TextId::LanguageTip: return u8"Cambia toda la interfaz de PULSO y cada tooltip entre español e inglés. La elección se guarda con el proyecto.";
        case TextId::PreviewSound: return u8"SONIDO DE ESCUCHA";
        case TextId::Octave: return u8"OCTAVA";
        case TextId::Level: return u8"NIVEL";
        case TextId::Audition: return u8"ESCUCHAR";
        case TextId::SoundTip: return u8"Elige un modelo de síntesis para esta voz. Seguir familia usa el valor compatible predeterminado de la familia.";
        case TextId::OctaveDownTip: return u8"Escucha esta voz una octava más grave.";
        case TextId::OctaveOriginalTip: return u8"Usa el registro original de la composición.";
        case TextId::OctaveUpTip: return u8"Escucha esta voz una octava más aguda.";
        case TextId::LevelTip: return u8"Nivel de escucha independiente entre -36 dB y +6 dB. La velocidad MIDI y la exportación no cambian.";
        case TextId::AuditionTip: return u8"Reproduce esta voz con su sonido, octava y nivel sin enviar MIDI a Ableton.";
        case TextId::FullSong: return u8"CANCIÓN COMPLETA";
        case TextId::Rhythm: return u8"RITMO";
        case TextId::Bass: return u8"BAJOS";
        case TextId::Harmony: return u8"ARMONÍA";
        case TextId::LeadsFx: return u8"MELODÍAS + FX";
        case TextId::Section: return u8"SECCIÓN";
        case TextId::EmptyPattern: return u8"Compón una idea para crear MIDI editable";
        case TextId::Play: return u8"PLAY";
        case TextId::Paused: return u8"PAUSA";
        case TextId::Preview: return u8"ESCUCHA";
        case TextId::Bar: return u8"COMPÁS";
        case TextId::Bars: return u8"COMPASES";
        case TextId::Idea: return u8"IDEA";
        case TextId::ExportFailed: return u8"FALLÓ LA EXPORTACIÓN MIDI";
        case TextId::DropIntoAbleton: return u8"SUELTA EN ABLETON";
        case TextId::MidiReady: return u8"MIDI LISTO";
        case TextId::DragUnavailable: return u8"ARRASTRE NO DISPONIBLE";
        case TextId::Solo: return u8"SOLO";
        case TextId::SoloOff: return u8"SOLO DESACTIVADO";
        case TextId::Muted: return u8"SILENCIADA";
        case TextId::MuteOff: return u8"SILENCIO DESACTIVADO";
        case TextId::KickMuted: return u8"BOMBO SILENCIADO";
        case TextId::KickReduced: return u8"BOMBO REDUCIDO";
        case TextId::KickSparse: return u8"BOMBO ESPACIADO";
        case TextId::KickFourOnFloor: return u8"BOMBO EN NEGRAS";
        case TextId::Cancel: return u8"CANCELAR";
        case TextId::Cancelling: return u8"CANCELANDO…";
        case TextId::ProgressTip: return u8"PULSO compone en segundo plano. La idea actual permanece disponible hasta que la nueva esté lista.";
        case TextId::CancelTip: return u8"Detén inmediatamente la solicitud y conserva la composición que ya está sonando.";
        case TextId::GptComposing: return u8"GPT ESTÁ COMPONIENDO TU IDEA";
        case TextId::Composing: return u8"COMPONIENDO TU IDEA";
        case TextId::CurrentKeepsPlaying: return u8"La composición actual sigue sonando mientras se prepara la nueva.";
        case TextId::Directing: return u8"DIRIGIENDO ARMONÍA, MELODÍA, BAJO Y RITMO";
        case TextId::Working: return u8"TRABAJANDO";
        case TextId::SoundStage: return u8"DIRECTOR DE SONIDO LIVE";
        case TextId::SoundStageTip: return u8"Indexa sonidos nativos instalados, crea pistas editables y carga el dispositivo elegido por la IA o un fallback informado.";
        case TextId::DeployLive: return u8"CREAR EN LIVE";
        case TextId::DeployLiveTip: return u8"Crea una pista y un clip MIDI editables por instrumento y carga exclusivamente dispositivos y Racks nativos de Live.";
    }
    return u8"";
}

} // namespace

juce::String tr(UiLanguage language, TextId id) {
    return utf8(language == UiLanguage::Spanish ? spanish(id) : english(id));
}

juce::String bullet() { return juce::String::charToString(0x00b7); }

juce::String voiceDisplayName(UiLanguage language, VoiceId voice) {
    if (language == UiLanguage::English)
        return juce::String(voiceDefinition(voice).name.data());
    constexpr std::array<const char8_t*, static_cast<std::size_t>(VoiceId::Count)> names{
        u8"Bombo", u8"Percusión grave", u8"Percusión aguda", u8"Sub bajo",
        u8"Bajo móvil", u8"Base armónica", u8"Pulso armónico", u8"Armonía superior",
        u8"Melodía principal", u8"Contramelodía", u8"Atmósfera", u8"Transiciones / FX",
        u8"Caja / Clap", u8"Hi-hat cerrado", u8"Hi-hat abierto / Shaker"};
    return utf8(names[static_cast<std::size_t>(voice)]);
}

juce::StringArray localizedTimbreChoices(UiLanguage language, VoiceId voice) {
    auto choices = PulsoAudioProcessor::voicePreviewTimbreChoices(voice);
    if (language == UiLanguage::English) return choices;
    for (auto index = 0; index < choices.size(); ++index) {
        auto value = choices[index];
        value = value.replace("Follow Kit", utf8(u8"Seguir kit"))
                     .replace("Follow Bass", utf8(u8"Seguir bajo"))
                     .replace("Follow Harmony", utf8(u8"Seguir armonía"))
                     .replace("Follow Melody", utf8(u8"Seguir melodía"))
                     .replace("Follow World", utf8(u8"Seguir mundo"))
                     .replace("Deep", utf8(u8"Profundo"))
                     .replace("Warm", utf8(u8"Cálido"))
                     .replace("Soft", utf8(u8"Suave"))
                     .replace("Organic", utf8(u8"Orgánico"))
                     .replace("Closed", utf8(u8"Cerrado"))
                     .replace("Open", utf8(u8"Abierto"))
                     .replace("Modern", utf8(u8"Moderno"))
                     .replace("Body", utf8(u8"Cuerpo"))
                     .replace("Noise", utf8(u8"Ruido"))
                     .replace("Skin", utf8(u8"Piel"))
                     .replace("Metallic", utf8(u8"Metálico"))
                     .replace("Tight", utf8(u8"Seco"))
                     .replace("Kick", utf8(u8"Bombo"))
                     .replace("Snare", utf8(u8"Caja"))
                     .replace("Hat", utf8(u8"Hi-hat"))
                     .replace("Low Perc", utf8(u8"Percusión grave"))
                     .replace("Cowbell", utf8(u8"Cencerro"))
                     .replace("Rim", utf8(u8"Aro"))
                     .replace("Acid Pluck", utf8(u8"Pluck ácido"))
                     .replace("Pad", utf8(u8"Pad"))
                     .replace("Poly", utf8(u8"Polifónico"))
                     .replace("Organ", utf8(u8"Órgano"))
                     .replace("Glass", utf8(u8"Cristal"))
                     .replace("Mono", utf8(u8"Mono"))
                     .replace("Air", utf8(u8"Aire"))
                     .replace("Bell", utf8(u8"Campana"))
                     .replace("Sweep", utf8(u8"Barrido"))
                     .replace("Impact", utf8(u8"Impacto"))
                     .replace("Tonal Riser", utf8(u8"Subida tonal"))
                     .replace("Dub Hit", utf8(u8"Golpe dub"));
        choices.set(index, value);
    }
    return choices;
}

juce::StringArray localizedSoundWorlds(UiLanguage language, const juce::String& automaticWorld) {
    if (language == UiLanguage::English)
        return {"AUTO " + bullet() + " " + automaticWorld.toUpperCase(), "DEEP PROGRESSIVE",
                "ORGANIC MOTION", "ANALOG WARMTH", "DUB SPACE", "MINIMAL PULSE",
                "HYPNOTIC NIGHT", "CINEMATIC ARC", "DARK CLUB"};
    auto localizedAutomatic = automaticWorld.toUpperCase()
        .replace("DEEP PROGRESSIVE", utf8(u8"PROGRESIVO PROFUNDO"))
        .replace("ORGANIC MOTION", utf8(u8"MOVIMIENTO ORGÁNICO"))
        .replace("ANALOG WARMTH", utf8(u8"CALIDEZ ANALÓGICA"))
        .replace("DUB SPACE", utf8(u8"ESPACIO DUB"))
        .replace("MINIMAL PULSE", utf8(u8"PULSO MINIMAL"))
        .replace("HYPNOTIC NIGHT", utf8(u8"NOCHE HIPNÓTICA"))
        .replace("CINEMATIC ARC", utf8(u8"ARCO CINEMÁTICO"))
        .replace("DARK CLUB", utf8(u8"CLUB OSCURO"));
    return {utf8(u8"AUTO") + " " + bullet() + " " + localizedAutomatic,
            utf8(u8"PROGRESIVO PROFUNDO"), utf8(u8"MOVIMIENTO ORGÁNICO"),
            utf8(u8"CALIDEZ ANALÓGICA"), utf8(u8"ESPACIO DUB"),
            utf8(u8"PULSO MINIMAL"), utf8(u8"NOCHE HIPNÓTICA"),
            utf8(u8"ARCO CINEMÁTICO"), utf8(u8"CLUB OSCURO")};
}

juce::String localizeStatus(UiLanguage language, const juce::String& source) {
    if (language == UiLanguage::English) return source;
    auto result = source;
    return result.replace("CANCELLING", utf8(u8"CANCELANDO"))
                 .replace("CANCELLED", utf8(u8"CANCELADO"))
                 .replace("CURRENT IDEA KEPT", utf8(u8"IDEA ACTUAL CONSERVADA"))
                 .replace("GPT ARCHITECTING FULL SONG", utf8(u8"GPT DISEÑANDO LA CANCIÓN"))
                 .replace("GPT ARCHITECTURE - DRAFTING", utf8(u8"GPT CREANDO LA ARQUITECTURA"))
                 .replace("GPT CRITIC - OPTIONAL REVISION", utf8(u8"CRÍTICO GPT - REVISIÓN"))
                 .replace("RENDERING", utf8(u8"RENDERIZANDO"))
                 .replace("FULL SONG", utf8(u8"CANCIÓN COMPLETA"))
                 .replace("PROJECT IDEA RESTORED", utf8(u8"IDEA DEL PROYECTO RESTAURADA"))
                 .replace("UNDO RESTORED", utf8(u8"DESHACER RESTAURÓ LA IDEA"))
                 .replace("LOCAL ENGINE READY", utf8(u8"MOTOR LOCAL LISTO"))
                 .replace("LOCAL ENGINE", utf8(u8"MOTOR LOCAL"))
                 .replace("VALIDATED", utf8(u8"VALIDADO"));
}

} // namespace pulso::plugin

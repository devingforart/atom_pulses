cmake_minimum_required(VERSION 3.25)

project(Pulso VERSION 0.47.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PULSO_BUILD_PLUGIN "Build the JUCE plug-in" ON)
option(PULSO_BUILD_TESTS "Build unit tests" ON)
option(PULSO_BUILD_CLI "Build the pattern demo CLI" ON)

include(FetchContent)

add_library(pulso_core STATIC
    src/core/CompositionModel.cpp
    src/core/CompositionModel.h
    src/core/ElectronicProductionDirector.cpp
    src/core/ElectronicProductionDirector.h
    src/core/Generator.cpp
    src/core/Generator.h
    src/core/HarmonyEngine.cpp
    src/core/HarmonyEngine.h
    src/core/HarmonyPlan.cpp
    src/core/HarmonyPlan.h
    src/core/MusicTypes.h
    src/core/MusicalCritic.cpp
    src/core/MusicalCritic.h
    src/core/MusicalIdentityGate.cpp
    src/core/MusicalIdentityGate.h
    src/core/NarrativeScore.cpp
    src/core/NarrativeScore.h
    src/core/Orchestration.cpp
    src/core/Orchestration.h
    src/core/OrchestrationScore.cpp
    src/core/OrchestrationScore.h
    src/core/PerformanceTiming.cpp
    src/core/PerformanceTiming.h
    src/core/PerformanceExpression.cpp
    src/core/PerformanceExpression.h
    src/core/PerformanceScore.cpp
    src/core/PerformanceScore.h
    src/core/ProductionPolish.cpp
    src/core/ProductionPolish.h
    src/core/PhraseComposer.cpp
    src/core/PhraseComposer.h
    src/core/PhraseDirector.cpp
    src/core/PhraseDirector.h
    src/core/Random.h
    src/core/RhythmPlan.cpp
    src/core/RhythmPlan.h
    src/core/RhythmEngine.cpp
    src/core/RhythmEngine.h
    src/core/Scale.cpp
    src/core/Scale.h
    src/core/SongComposer.cpp
    src/core/SongComposer.h
    src/core/TonalContract.cpp
    src/core/TonalContract.h
    src/core/VerticalHarmonyGate.cpp
    src/core/VerticalHarmonyGate.h
)
target_include_directories(pulso_core PUBLIC src)
target_compile_features(pulso_core PUBLIC cxx_std_20)
set_target_properties(pulso_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(pulso_core PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(pulso_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(PULSO_BUILD_CLI)
    add_executable(pulso_cli tools/pulso_cli.cpp)
    target_link_libraries(pulso_cli PRIVATE pulso_core)
endif()

if(PULSO_BUILD_TESTS)
    enable_testing()
    add_executable(pulso_tests
        tests/TestMain.cpp
        tests/GeneratorTests.cpp
        tests/ScaleTests.cpp
    )
    target_link_libraries(pulso_tests PRIVATE pulso_core)
    add_test(NAME pulso_core_tests COMMAND pulso_tests)
endif()

if(PULSO_BUILD_PLUGIN)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(JUCE)

    juce_add_plugin(Pulso
        VERSION ${PROJECT_VERSION}
        COMPANY_NAME "Pulso Audio"
        BUNDLE_ID "audio.pulso.instrument"
        PLUGIN_MANUFACTURER_CODE Plso
        PLUGIN_CODE Puls
        PRODUCT_NAME "PULSO"
        DESCRIPTION "Context-aware generative MIDI instrument"
        FORMATS VST3 Standalone
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT TRUE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Instrument Generator
    )

    target_sources(Pulso PRIVATE
        src/plugin/AiComposer.cpp
        src/plugin/AiComposer.h
        src/plugin/CompositionProgress.cpp
        src/plugin/CompositionProgress.h
        src/plugin/LiveDeployer.cpp
        src/plugin/LiveDeployer.h
        src/plugin/LookAndFeel.cpp
        src/plugin/LookAndFeel.h
        src/plugin/Localization.cpp
        src/plugin/Localization.h
        src/plugin/MidiExporter.cpp
        src/plugin/MidiExporter.h
        src/plugin/PatternView.cpp
        src/plugin/PatternView.h
        src/plugin/PluginEditor.cpp
        src/plugin/PluginEditor.h
        src/plugin/PluginProcessor.cpp
        src/plugin/PluginProcessor.h
        src/plugin/PreviewSynth.cpp
        src/plugin/PreviewSynth.h
    )

    target_link_libraries(Pulso PRIVATE
        pulso_core
        juce::juce_audio_utils
        juce::juce_dsp
        PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
    )
    if(WIN32)
        target_link_libraries(Pulso PRIVATE winhttp)
    endif()

    target_compile_definitions(Pulso PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_PLUGINHOST_VST3=1
    )

    if(MSVC)
        target_compile_options(Pulso PRIVATE /utf-8)
    endif()

    if(PULSO_BUILD_TESTS)
        add_executable(pulso_processor_tests tests/ProcessorTests.cpp)
        target_include_directories(pulso_processor_tests PRIVATE src tests)
        target_link_libraries(pulso_processor_tests PRIVATE
            Pulso
            juce::juce_audio_utils
            juce::juce_dsp
        )
        if(MSVC)
            target_compile_options(pulso_processor_tests PRIVATE /utf-8)
        endif()
        add_test(NAME pulso_processor_tests COMMAND pulso_processor_tests)
    endif()
endif()

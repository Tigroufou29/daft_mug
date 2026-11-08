cmake_minimum_required(VERSION 3.22)

project(DaftMug VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Windows: link the C++ runtime statically so the installer works on a PC without the Visual C++ Redistributable.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(DAFTMUG_BUILD_PLUGIN "Build the VST3 + Standalone targets (needs JUCE)" ON)
option(DAFTMUG_BUILD_TESTS  "Build the engine unit tests (no JUCE needed)" ON)
if(WIN32)
    set(_dm_smoke_default OFF)   # the smoke test has only been run on Linux; opt in with -DDAFTMUG_BUILD_SMOKE_TEST=ON
else()
    set(_dm_smoke_default ON)
endif()
option(DAFTMUG_BUILD_SMOKE_TEST "Build the plug-in smoke test (processor + editor rendered to PNG)" ${_dm_smoke_default})
option(DAFTMUG_COPY_AFTER_BUILD "Copy the VST3 to the system plug-in folder after build (may need admin rights)" OFF)
set(DAFTMUG_JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (leave empty to download JUCE 8.0.16-security)")
set(DAFTMUG_ASIO_SDK_DIR "" CACHE PATH "Steinberg ASIO SDK folder (the one containing 'common/asio.h'). Enables ASIO in the Standalone app.")
set(DAFTMUG_AAX_SDK_DIR "" CACHE PATH "Avid AAX SDK folder. Enables the AAX format (Pro Tools). Requires the SDK from Avid.")

# ---------------------------------------------------------------------------------------------
# 1. Audio engine: plain C++, NO JUCE dependency. Shared by the VST3 and the standalone app.
# ---------------------------------------------------------------------------------------------
# MSVC must read our UTF-8 sources as UTF-8 (accents and symbols in the interface texts)
if(MSVC)
    add_compile_options(/utf-8)
endif()

configure_file(Source/Plugin/DaftMugPaths.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/DaftMugPaths.h" @ONLY)
add_library(daftmug_engine STATIC Source/Engine/SamplerEngine.cpp Source/Engine/Eq.cpp Source/Engine/Effects.cpp Source/Engine/Convolution.cpp Source/Engine/SliceDetector.cpp Source/Engine/Mutate.cpp Source/Engine/Tempo.cpp)
target_include_directories(daftmug_engine PUBLIC Source/Engine)
target_compile_features(daftmug_engine PUBLIC cxx_std_17)
if(MSVC)
    target_compile_options(daftmug_engine PRIVATE /W4 /permissive-)
else()
    target_compile_options(daftmug_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------------------------
# 2. Tests
# ---------------------------------------------------------------------------------------------
if(DAFTMUG_BUILD_TESTS)
    enable_testing()
    add_executable(daftmug_engine_tests Tests/EngineTests.cpp)
    target_link_libraries(daftmug_engine_tests PRIVATE daftmug_engine)
    add_test(NAME engine COMMAND daftmug_engine_tests)
endif()

# ---------------------------------------------------------------------------------------------
# 3. Plug-in / standalone (JUCE)
# ---------------------------------------------------------------------------------------------
if(DAFTMUG_BUILD_PLUGIN)
    if(DAFTMUG_JUCE_DIR)
        add_subdirectory("${DAFTMUG_JUCE_DIR}" JUCE EXCLUDE_FROM_ALL)
    else()
        include(FetchContent)
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG        8.0.16-security
            GIT_SHALLOW    TRUE)
        FetchContent_MakeAvailable(JUCE)
    endif()

    set(DAFTMUG_FORMATS VST3 Standalone)
    if(DAFTMUG_AAX_SDK_DIR)
        juce_set_aax_sdk_path("${DAFTMUG_AAX_SDK_DIR}")
        list(APPEND DAFTMUG_FORMATS AAX)
    endif()

    juce_add_plugin(DaftMug
        PRODUCT_NAME                "Daft Mug"
        COMPANY_NAME                "Daft Mug"
        BUNDLE_ID                   com.daftmug.sampler
        PLUGIN_MANUFACTURER_CODE    Dftm
        PLUGIN_CODE                 Dmg1
        FORMATS                     ${DAFTMUG_FORMATS}
        VST3_CATEGORIES             Instrument Sampler
        IS_SYNTH                    TRUE
        NEEDS_MIDI_INPUT            TRUE
        NEEDS_MIDI_OUTPUT           FALSE
        IS_MIDI_EFFECT              FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        ICON_BIG                    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Resources/icon.png"
        COPY_PLUGIN_AFTER_BUILD     ${DAFTMUG_COPY_AFTER_BUILD})

    target_sources(DaftMug PRIVATE
        Source/Plugin/PluginProcessor.cpp
        Source/Plugin/PadExporter.cpp
        Source/Plugin/PluginEditor.cpp
        Source/UI/LiquidGlassLookAndFeel.cpp
        Source/UI/PadComponent.cpp
        Source/UI/MasterMeter.cpp
        Source/UI/SystemInfo.cpp
        Source/UI/PadControls.cpp
        Source/UI/PadEditorPanel.cpp
        Source/UI/WaveformView.cpp
        Source/UI/EnvelopeSection.cpp
        Source/UI/EqSection.cpp
        Source/UI/FxSection.cpp
        Source/UI/ReverbSection.cpp
        Source/UI/SlicePanel.cpp
        Source/UI/MutatePanel.cpp
        Source/Plugin/Collector.cpp
        Source/Plugin/KitExporter.cpp
        Source/UI/PresetUi.cpp
        Source/Plugin/Presets.cpp
        Source/UI/OptionsPanel.cpp
        Source/AI/AiCatalog.cpp
        Source/AI/AiService.cpp
        Source/AI/SecretStore.cpp
        Source/UI/AiSettingsPanel.cpp
        Source/UI/AiAssistantPanel.cpp
        Source/UI/TermsPanel.cpp)

    target_include_directories(DaftMug PRIVATE Source Source/Plugin Source/AI "${CMAKE_CURRENT_BINARY_DIR}/generated")

    target_compile_definitions(DaftMug PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_USE_MP3AUDIOFORMAT=1     # MP3 import enabled
        JUCE_STRICT_REFCOUNTEDPOINTER=1)

    if(DAFTMUG_ASIO_SDK_DIR)
        target_compile_definitions(DaftMug PUBLIC JUCE_ASIO=1)
        target_include_directories(DaftMug PRIVATE "${DAFTMUG_ASIO_SDK_DIR}/common")
    endif()

    juce_add_binary_data(DaftMugAssets SOURCES Source/Resources/logo.png Source/Resources/background.jpg)
    set_target_properties(DaftMugAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

    if(WIN32)
        target_link_libraries(DaftMug PRIVATE advapi32 psapi)   # Credential Manager (API key storage)
    elseif(APPLE)
        target_link_libraries(DaftMug PRIVATE "-framework Security")   # Keychain (API key storage)
    endif()

    target_link_libraries(DaftMug
        PRIVATE
            DaftMugAssets
            daftmug_engine
            juce::juce_audio_utils
            juce::juce_audio_formats
            juce::juce_audio_processors
            juce::juce_gui_basics
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()

# ---------------------------------------------------------------------------------------------
# 4. Plug-in smoke test (console app that exercises the real processor and renders the UI to PNG)
# ---------------------------------------------------------------------------------------------
if(DAFTMUG_BUILD_PLUGIN AND DAFTMUG_BUILD_TESTS AND DAFTMUG_BUILD_SMOKE_TEST)
    juce_add_console_app(DaftMugSmokeTest PRODUCT_NAME "DaftMugSmokeTest")
    target_sources(DaftMugSmokeTest PRIVATE
        Tests/PluginSmokeTest.cpp
        Source/Plugin/PluginProcessor.cpp
        Source/Plugin/PadExporter.cpp
        Source/Plugin/PluginEditor.cpp
        Source/UI/LiquidGlassLookAndFeel.cpp
        Source/UI/PadComponent.cpp
        Source/UI/MasterMeter.cpp
        Source/UI/SystemInfo.cpp
        Source/UI/PadControls.cpp
        Source/UI/PadEditorPanel.cpp
        Source/UI/WaveformView.cpp
        Source/UI/EnvelopeSection.cpp
        Source/UI/EqSection.cpp
        Source/UI/FxSection.cpp
        Source/UI/ReverbSection.cpp
        Source/UI/SlicePanel.cpp
        Source/UI/MutatePanel.cpp
        Source/Plugin/Collector.cpp
        Source/Plugin/KitExporter.cpp
        Source/UI/PresetUi.cpp
        Source/Plugin/Presets.cpp
        Source/UI/OptionsPanel.cpp
        Source/AI/AiCatalog.cpp
        Source/AI/AiService.cpp
        Source/AI/SecretStore.cpp
        Source/UI/AiSettingsPanel.cpp
        Source/UI/AiAssistantPanel.cpp
        Source/UI/TermsPanel.cpp)
    target_include_directories(DaftMugSmokeTest PRIVATE Source Source/Plugin Source/AI "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_definitions(DaftMugSmokeTest PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1
        JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(DaftMugSmokeTest
        PRIVATE DaftMugAssets daftmug_engine juce::juce_audio_utils juce::juce_audio_formats juce::juce_audio_processors juce::juce_gui_basics
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()

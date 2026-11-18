cmake_minimum_required(VERSION 3.22)
project(Guitar2MIDI VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static MSVC runtime: the plugin works without installing VC++ redistributables.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.8
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

# --- Detection engine (no JUCE, unit-tested) --------------------------------
add_library(g2m_dsp STATIC
    src/dsp/GuitarToMidiEngine.cpp
    src/dsp/NoteTracker.cpp
    src/dsp/PitchAnalyzer.cpp)
target_include_directories(g2m_dsp PUBLIC src/dsp)
set_target_properties(g2m_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)

# --- Plugin -------------------------------------------------------------------
juce_add_plugin(Guitar2MIDI
    COMPANY_NAME "Guitar2MIDI"
    PRODUCT_NAME "Guitar2MIDI"
    BUNDLE_ID "com.guitar2midi.plugin"
    PLUGIN_MANUFACTURER_CODE G2mD
    PLUGIN_CODE G2m1
    FORMATS VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx Analyzer
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(Guitar2MIDI PRIVATE
    src/plugin/PluginProcessor.cpp
    src/plugin/PluginEditor.cpp
    src/plugin/MidiPortOutput.cpp)

target_compile_definitions(Guitar2MIDI PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Guitar2MIDI
    PRIVATE
        g2m_dsp
        juce::juce_audio_utils
        juce::juce_audio_devices
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# --- Tests --------------------------------------------------------------------
option(G2M_BUILD_TESTS "Build the engine tests" ON)
if(G2M_BUILD_TESTS)
    add_executable(engine_test tests/EngineTest.cpp)
    target_link_libraries(engine_test PRIVATE g2m_dsp)
    enable_testing()
    add_test(NAME engine_test COMMAND engine_test)
endif()

# Loads the built VST3 like a DAW and checks its MIDI output.
option(G2M_BUILD_HOST_TEST "Build the plugin hosting test" ON)
if(G2M_BUILD_HOST_TEST)
    juce_add_console_app(host_test PRODUCT_NAME "host_test")
    target_sources(host_test PRIVATE tests/HostTest.cpp)
    target_compile_definitions(host_test PRIVATE
        JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(host_test PRIVATE
        juce::juce_audio_processors juce::juce_gui_basics juce::juce_events
        juce::juce_recommended_config_flags)
    add_dependencies(host_test Guitar2MIDI_VST3)
endif()

# Developer helper that renders the UI to a PNG (not needed for normal builds).
option(G2M_BUILD_UI_SNAPSHOT "Build the UI snapshot helper" OFF)
if(G2M_BUILD_UI_SNAPSHOT)
    juce_add_console_app(ui_snapshot PRODUCT_NAME "ui_snapshot")
    target_sources(ui_snapshot PRIVATE tests/UiSnapshot.cpp
        src/plugin/PluginProcessor.cpp src/plugin/PluginEditor.cpp src/plugin/MidiPortOutput.cpp)
    target_compile_definitions(ui_snapshot PRIVATE
        JucePlugin_Name="Guitar2MIDI" JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(ui_snapshot PRIVATE g2m_dsp
        juce::juce_audio_processors juce::juce_audio_devices juce::juce_gui_basics
        juce::juce_recommended_config_flags)
endif()

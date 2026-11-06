cmake_minimum_required(VERSION 3.22)
project(ExpressiveMidiPlayer VERSION 1.0.0 LANGUAGES C CXX)

# Supply -DJUCE_ROOT=/path/to/JUCE to use an already-downloaded JUCE checkout.
# Otherwise the first configure needs Internet access.
if (DEFINED JUCE_ROOT AND EXISTS "${JUCE_ROOT}/CMakeLists.txt")
    add_subdirectory("${JUCE_ROOT}" "${CMAKE_BINARY_DIR}/juce")
else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
                         GIT_TAG 8.0.10 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(ExpressiveMidiPlayer
    COMPANY_NAME "ExampleOpenAudio"
    BUNDLE_ID "org.example.expressivemidiplayer"
    PLUGIN_MANUFACTURER_CODE ExMd
    PLUGIN_CODE EmPl
    FORMATS VST3
    PRODUCT_NAME "Expressive MIDI Player"
    IS_SYNTH TRUE
    IS_MIDI_EFFECT FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

juce_generate_juce_header(ExpressiveMidiPlayer)

target_sources(ExpressiveMidiPlayer PRIVATE
    Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_features(ExpressiveMidiPlayer PRIVATE cxx_std_17)
# The editor contains UTF-8 Japanese UI strings; MSVC otherwise decodes sources with the ANSI code page.
if (MSVC)
    target_compile_options(ExpressiveMidiPlayer PRIVATE /utf-8)
endif()
target_compile_definitions(ExpressiveMidiPlayer PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(ExpressiveMidiPlayer PRIVATE
    juce::juce_audio_utils juce::juce_audio_processors
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

cmake_minimum_required(VERSION 3.22)
project(ExpressiveMidiBridge VERSION 2.0.0 LANGUAGES C CXX)

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

# PLUGIN_CODE / MANUFACTURER_CODE are kept from earlier versions.
juce_add_plugin(ExpressiveMidiPlayer
    COMPANY_NAME "Expressive MIDI Bridge"
    BUNDLE_ID "io.github.ktakahiro1729.expressivemidibridge"
    PLUGIN_MANUFACTURER_CODE ExMd
    PLUGIN_CODE EmPl
    FORMATS VST3
    PRODUCT_NAME "Expressive MIDI Bridge"
    IS_SYNTH FALSE
    IS_MIDI_EFFECT FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx Tools
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

juce_generate_juce_header(ExpressiveMidiPlayer)

# The Python side of the AI engine (expressive_midi incl. the vendored Pianist Transformer
# inference code) is embedded as a zip and unpacked into the private AI folder at run time.
set(EMB_PYTHON_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
file(GLOB_RECURSE EMB_PYTHON_FILES CONFIGURE_DEPENDS
     "${EMB_PYTHON_ROOT}/expressive_midi/*.py"
     "${EMB_PYTHON_ROOT}/expressive_midi/_vendor/*/LICENSE"
     "${EMB_PYTHON_ROOT}/expressive_midi/_vendor/*/README.md")
set(EMB_STAGE "${CMAKE_BINARY_DIR}/python_app_stage")
set(EMB_ZIP "${CMAKE_BINARY_DIR}/python_app.zip")
set(EMB_COPY_COMMANDS)
foreach(f IN LISTS EMB_PYTHON_FILES)
    file(RELATIVE_PATH rel "${EMB_PYTHON_ROOT}" "${f}")
    list(APPEND EMB_COPY_COMMANDS COMMAND ${CMAKE_COMMAND} -E copy "${f}" "${EMB_STAGE}/${rel}")
endforeach()
add_custom_command(OUTPUT "${EMB_ZIP}"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${EMB_STAGE}"
    ${EMB_COPY_COMMANDS}
    COMMAND ${CMAKE_COMMAND} -E chdir "${EMB_STAGE}" ${CMAKE_COMMAND} -E tar cf "${EMB_ZIP}" --format=zip expressive_midi
    DEPENDS ${EMB_PYTHON_FILES}
    COMMENT "Packing the embedded Python AI code")
add_custom_target(EmbeddedPythonZip DEPENDS "${EMB_ZIP}")
juce_add_binary_data(EmbeddedPython HEADER_NAME EmbeddedPython.h NAMESPACE EmbeddedPython SOURCES "${EMB_ZIP}")
add_dependencies(EmbeddedPython EmbeddedPythonZip)

target_sources(ExpressiveMidiPlayer PRIVATE
    Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/AiEngine.cpp Source/Widgets.cpp)
target_compile_features(ExpressiveMidiPlayer PRIVATE cxx_std_17)
# The editor contains UTF-8 Japanese UI strings; MSVC otherwise decodes sources with the ANSI code page.
if (MSVC)
    target_compile_options(ExpressiveMidiPlayer PRIVATE /utf-8)
endif()
target_compile_definitions(ExpressiveMidiPlayer PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(ExpressiveMidiPlayer PRIVATE
    EmbeddedPython
    juce::juce_audio_utils juce::juce_audio_processors juce::juce_cryptography
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# ==============================================================================
#  Mutable — JUCE audio plugin
#
#  Phase 1 scaffold: an empty plugin that builds as VST3 on all platforms,
#  plus AU when building on macOS. No DSP yet.
#
#  Configure (Windows, from this folder):
#      cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#  Build:
#      cmake --build build --config Release --target Mutable_VST3
# ==============================================================================

cmake_minimum_required(VERSION 3.22)

project(Mutable
    VERSION 1.0.0
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ------------------------------------------------------------------------------
# JUCE 8, pulled in via FetchContent and pinned to a specific release tag.
# 8.0.17-security is the latest JUCE 8.x release tag on the JUCE repo.
# ------------------------------------------------------------------------------
include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.17-security)

FetchContent_MakeAvailable(JUCE)

# ------------------------------------------------------------------------------
# Plugin target
#
# VST3 is built everywhere; AU is only enabled on macOS.
# ------------------------------------------------------------------------------
set(MUTABLE_PLUGIN_FORMATS VST3)

if(APPLE)
    list(APPEND MUTABLE_PLUGIN_FORMATS AU)
endif()

# `juce_add_plugin` creates:
#   - a static library target named `Mutable` (the plugin's shared code), and
#   - one wrapper target per format in FORMATS (e.g. `Mutable_VST3`, `Mutable_AU`).
#
# Note: the AudioProcessor class name is independent of the target name — it is
# whatever your createPluginFilter() function (defined in Source/PluginProcessor.cpp)
# returns.
juce_add_plugin(Mutable
    COMPANY_NAME "liqwi"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    PLUGIN_MANUFACTURER_CODE Liqw
    PLUGIN_CODE Muta
    PRODUCT_NAME "Mutable"
    FORMATS ${MUTABLE_PLUGIN_FORMATS})

# Generate a JuceHeader.h for the target (the .cpp files include it).
juce_generate_juce_header(Mutable)

# The plugin's sources.
target_sources(Mutable
    PRIVATE
        Source/PluginProcessor.h
        Source/PluginProcessor.cpp
        Source/PluginEditor.h
        Source/PluginEditor.cpp)

# Not using web views or curl yet (matches the JUCE CMake example defaults).
target_compile_definitions(Mutable
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

# Link the modules we use, plus JUCE's recommended build flags.
target_link_libraries(Mutable
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
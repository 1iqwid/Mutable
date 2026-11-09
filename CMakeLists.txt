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
# third_party: Plaits DSP engine + stmlib, built as static libraries.
#
# Both projects are included as "plaits/..." and "stmlib/...", so the include
# root is third_party/ itself.
#
# The plaits_dsp file list comes from the Plaits project's own host build
# (third_party/plaits/test/makefile, CC_FILES), minus plaits_test.cc (the test
# harness) and the stmlib / stm_audio_bootloader files that live outside this
# tree. TEST (also from that makefile's g++ -DTEST) is set on the stmlib
# target and flows downstream to plaits_dsp and the plugin: it selects
# portable C code paths over ARM assembly and keeps plaits/user_data.h away
# from the STM32F37x headers - i.e. how Plaits builds for the desktop.
# ------------------------------------------------------------------------------

# stmlib: DSP math library (MIT). stmlib is mostly header-only; exactly these
# 3 .cc files are compiled, matching the Plaits project's own host build
# (third_party/plaits/test/makefile: PACKAGES = ... stmlib/utils ... stmlib/dsp ...).
# stmlib/system/ (bootloader_utils.cc, system_clock.cc) is STM32 firmware
# code (flash programming, system clock) and is NOT part of the host build.
add_library(stmlib STATIC
    third_party/stmlib/dsp/atan.cc
    third_party/stmlib/dsp/units.cc
    third_party/stmlib/utils/random.cc)
target_include_directories(stmlib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
# TEST: host-build mode, per third_party/plaits/test/makefile (g++ -c -DTEST).
# Selects the portable C paths in stmlib headers (e.g. Clip16/Sqrt in
# stmlib/dsp/dsp.h) instead of their ARM inline-assembly #else branches, and
# plaits/user_data.h's mock flash API instead of the STM32F37x headers.
# PUBLIC so it flows downstream to every target that links stmlib, i.e.
# plaits_dsp and the plugin.
target_compile_definitions(stmlib
    PUBLIC
        TEST)

# plaits_dsp: the Plaits voice + all engines (plaits/dsp/**) plus the
# generated resource tables (wavetables, FM patches, lookup tables).
add_library(plaits_dsp STATIC
    third_party/plaits/dsp/voice.cc
    third_party/plaits/dsp/chords/chord_bank.cc
    third_party/plaits/dsp/engine/additive_engine.cc
    third_party/plaits/dsp/engine/bass_drum_engine.cc
    third_party/plaits/dsp/engine/chord_engine.cc
    third_party/plaits/dsp/engine/fm_engine.cc
    third_party/plaits/dsp/engine/grain_engine.cc
    third_party/plaits/dsp/engine/hi_hat_engine.cc
    third_party/plaits/dsp/engine/modal_engine.cc
    third_party/plaits/dsp/engine/noise_engine.cc
    third_party/plaits/dsp/engine/particle_engine.cc
    third_party/plaits/dsp/engine/snare_drum_engine.cc
    third_party/plaits/dsp/engine/speech_engine.cc
    third_party/plaits/dsp/engine/string_engine.cc
    third_party/plaits/dsp/engine/swarm_engine.cc
    third_party/plaits/dsp/engine/virtual_analog_engine.cc
    third_party/plaits/dsp/engine/waveshaping_engine.cc
    third_party/plaits/dsp/engine/wavetable_engine.cc
    third_party/plaits/dsp/engine2/chiptune_engine.cc
    third_party/plaits/dsp/engine2/phase_distortion_engine.cc
    third_party/plaits/dsp/engine2/six_op_engine.cc
    third_party/plaits/dsp/engine2/string_machine_engine.cc
    third_party/plaits/dsp/engine2/virtual_analog_vcf_engine.cc
    third_party/plaits/dsp/engine2/wave_terrain_engine.cc
    third_party/plaits/dsp/fm/algorithms.cc
    third_party/plaits/dsp/fm/dx_units.cc
    third_party/plaits/dsp/physical_modelling/modal_voice.cc
    third_party/plaits/dsp/physical_modelling/resonator.cc
    third_party/plaits/dsp/physical_modelling/string.cc
    third_party/plaits/dsp/physical_modelling/string_voice.cc
    third_party/plaits/dsp/speech/lpc_speech_synth.cc
    third_party/plaits/dsp/speech/lpc_speech_synth_controller.cc
    third_party/plaits/dsp/speech/lpc_speech_synth_phonemes.cc
    third_party/plaits/dsp/speech/lpc_speech_synth_words.cc
    third_party/plaits/dsp/speech/naive_speech_synth.cc
    third_party/plaits/dsp/speech/sam_speech_synth.cc
    third_party/plaits/resources.cc)
target_include_directories(plaits_dsp
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
target_link_libraries(plaits_dsp
    PUBLIC
        stmlib)  # Also inherits TEST via stmlib's PUBLIC definition.

# MSVC's <cmath> only defines M_PI when _USE_MATH_DEFINES is set beforehand
# (GCC/Clang always define it, which is how Plaits' own desktop test build
# compiles). Plaits/stmlib use M_PI unguarded — e.g. stmlib/dsp/filter.h:54,
# stmlib/dsp/cosine_oscillator.h:54, plaits/dsp/oscillator/sine_oscillator.h:183.
# MSVC-only; has no effect on the macOS build.
if(MSVC)
    target_compile_definitions(stmlib
        PUBLIC
            _USE_MATH_DEFINES)
    target_compile_definitions(plaits_dsp
        PUBLIC
            _USE_MATH_DEFINES)
endif()

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
        Source/PluginEditor.cpp
        Source/PlaitsVoice.h
        Source/PlaitsVoice.cpp)

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
        plaits_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
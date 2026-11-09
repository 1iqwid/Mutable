# STATUS

## Phase 1: CMake + JUCE scaffold (COMPLETE, tagged v0.1)

Done:
- CMake project with JUCE 8 via FetchContent; VST3 target, AU only on macOS.
- Builds on Windows with Visual Studio 2026 (MSVC), Release.
- Output-only stereo bus (no input bus).
- processBlock clears the buffer and uses ScopedNoDenormals.
- Passes pluginval at strictness level 5 (SUCCESS).
- Fixed Ableton Live load failure ("plugin has instrument category, but no valid
  event input bus") by adding NEEDS_MIDI_INPUT TRUE to juce_add_plugin.
- Confirmed Mutable loads in Ableton Live and opens an empty plugin window.

Known issues:
- No sound yet (no DSP; that's Phase 2).
- Windows Smart App Control blocks unsigned plugins; it had to be turned off for
  local testing. Code signing is a Phase 7 item.

## Phase 2: Plaits integration (IN PROGRESS, branch phase-2-plaits-voice)

Task 1: CMake targets for Plaits + stmlib
- Added `stmlib` and `plaits_dsp` static library targets in the root
  CMakeLists.txt; `plaits_dsp` links `stmlib`, plugin links `plaits_dsp`.
  Include root is third_party/ (code includes "plaits/..." and "stmlib/...").
- `plaits_dsp` = 37 .cc files: everything in third_party/plaits/dsp/** plus
  resources.cc. Explicit list cross-checked against the Plaits project's own
  host build (third_party/plaits/test/makefile) and the on-disk file inventory.
  Compiled with -DTEST, same as that makefile, so plaits/user_data.h uses its
  mock flash API instead of STM32 headers. Firmware-only files (plaits.cc,
  settings.cc, ui.cc, user_data_receiver.cc, drivers/, bootloader/, test/)
  excluded.
- BLOCKED at CMake configure: third_party/stmlib/ is an EMPTY folder - no
  sources, nothing tracked in git, and no stmlib.h anywhere in the repo.
  Exact error: CMakeLists.txt:56 add_library: "No SOURCES given to target:
  stmlib". Every plaits DSP file includes stmlib headers (stmlib/stmlib.h,
  stmlib/dsp/filter.h, stmlib/utils/buffer_allocator.h, ...), so nothing
  compiles until the stmlib sources exist.
- stmlib sources are now in third_party/stmlib/ (936 files, MIT LICENSE present).
  stmlib is mostly header-only; the target compiles exactly 3 .cc files
  (dsp/atan.cc, dsp/units.cc, utils/random.cc), matching the Plaits host build
  packages (stmlib/utils, stmlib/dsp). stmlib/system/ is STM32 firmware
  (flash programming, system clock) and is excluded.
- First MSVC build failed in stmlib: stmlib/dsp/dsp.h:137/142/154 -
  C4235/C2059 on ARM inline asm (ssat/usat/vsqrt.f32). Root cause: those asm
  blocks sit in the #else branch of #ifdef TEST, and TEST was defined only on
  plaits_dsp, not on stmlib (CMake PUBLIC definitions flow downstream to
  consumers, NOT upstream to dependencies).
- RESOLVED: target_compile_definitions(stmlib PUBLIC TEST) applied (see
  CMakeLists.txt). stmlib now builds cleanly on MSVC (stmlib.lib).
- Build attempt 2: plaits_dsp - 35 of 37 .cc files compile. 2 files fail,
  both on MSVC-incompatible constructs in third_party (NO changes made yet):
  1. stmlib/utils/dsp.h:38-84 - `__attribute__((always_inline))` (GCC/Clang
     only; MSVC C3646/C2059 with cascading C2084/C2440). Only included by
     plaits/dsp/engine/waveshaping_engine.cc:34, which is required (Waveshaping
     engine is a Voice member in voice.h, registered in voice.cc).
  2. plaits/resources.cc:4032 - `const int32_t* const lookup_table_i32_table[]
     = {};` zero-length array (MSVC C2466). Nothing in the codebase indexes
     this table (only the extern in resources.h), but resources.cc itself is
     required (6 DSP headers include plaits/resources.h for wavetables/LUTs).

Known issues:
- plaits copy has no LICENSE/README file (per-file headers say MIT); need MIT
  license text + THIRD_PARTY_NOTICES entry (Phase 7 release item).
- No sound yet; no wrapper/DSP code written (Task 2+).

Next:
1. Decide how to handle the 2 MSVC-incompatible third_party constructs
   (see above) - minimal patches vs shadow copies.
2. Rebuild and get plaits_dsp + plugin linking on Windows.
3. Commit: "Phase 2: Plaits + stmlib as static libraries in CMake".
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
- stmlib target uses GLOB_RECURSE + CONFIGURE_DEPENDS, so once the sources
  are dropped in, the next configure picks them up automatically.
- Awaiting decision: where the stmlib sources come from.

Known issues:
- plaits copy has no LICENSE/README file (per-file headers say MIT); need MIT
  license text + THIRD_PARTY_NOTICES entry (Phase 7 release item).
- No sound yet; no wrapper/DSP code written (Task 2+).

Next:
1. Get stmlib sources into third_party/stmlib/ (decision pending).
2. Build; if ARM-firmware compile errors appear, show them for review before
   changing anything.
3. Commit: "Phase 2: Plaits + stmlib as static libraries in CMake".
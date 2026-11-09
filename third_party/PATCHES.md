# Patches to third-party code

This repo vendors two MIT-licensed projects: `stmlib/` and `plaits/`
(Plaits DSP by Mutable Instruments). To build them with MSVC on Windows,
two files were minimally patched. These are the only modifications to the
vendored sources; all license headers are left untouched.

## 1. `stmlib/utils/dsp.h`

**What changed.** The eleven GCC/Clang-only `__attribute__((always_inline));`
suffixes on the `Interpolate824/88/1022/115` and `Crossfade/1022/115`
function declarations (original lines 37-84) now use a new macro,
`STMLIB_ALWAYS_INLINE`, defined at the top of the declarations:

- non-MSVC: expands to `__attribute__((always_inline))` (unchanged behavior)
- MSVC: expands to nothing

**Why.** `__attribute__((...))` is not understood by MSVC (errors
C3646/C2059, with cascading C2084/C2440). The MSVC equivalent,
`__forceinline`, is a keyword that must precede the return type
(`__forceinline int16_t Interpolate824(...)`) and cannot follow the
declarator, so it cannot be used in this position. The declarations are
already `inline`, so on MSVC only the "always" inline *hint* is dropped;
there is no functional or ABI change.

## 2. `plaits/resources.cc`

**What changed.** `lookup_table_i32_table` (original line 4032) was a
zero-length array:

```c
const int32_t* const lookup_table_i32_table[] = {};
```

It is now a one-element array with a null placeholder:

```c
const int32_t* const lookup_table_i32_table[] = { nullptr };
```

**Why.** C++ has no zero-length arrays; MSVC rejects the original code with
C2466. Nothing in the codebase indexes `lookup_table_i32_table` (it is only
`extern`-declared in `plaits/resources.h`), so a single null element keeps
the symbol and its type intact and compiles on every compiler.

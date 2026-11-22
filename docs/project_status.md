# Project status

Updated: 2026-11-22

## Current state

- V0.4 builds the C API and decoder as one static library:
  `build/lzmacapi_x86.lib` and `build/lzmacapi_x86_debug.lib`.
- The C API signature is unchanged. C computes `dst_end`; an assembly preparation
  function receives `src`, `dst`, `workspace`, `dst_end`, `lc`, `lp`, and `pb`.
  Assembly reads the range initializer, derives the probability count and masks,
  and patches its own decoder immediates. The decoder then runs through a separate
  no-argument assembly entry.
- C has no patch offsets or patch table. It uses only the assembly section bounds
  to temporarily change page permissions and flush the instruction cache. The
  section is restored to its original protection before decoding.
- Since each call mutates shared static instructions, concurrent API calls are
  unsupported. This is an intentional prototype tradeoff.
- `SRC/lzma_static_x86.asm` now has detailed Chinese comments for the C/assembly
  interface, stack layout, range decoder, LZMA symbol paths, output copy, and exit.
- Parameter validation, output-boundary checks, workspace checks, and decoded
  length validation remain behind `LZMA_C_API_DEBUG`; the release build omits
  them. The decoder still does not validate compressed-input bounds.
- Project layout: `SRC/` sources, `script/` build scripts, `tools/` third-party
  tools, `work/` temporary files, and ignored `build/` generated artifacts.
- `V0` is the initial baseline. V0.1 removed the workspace-size helper and gated
  argument checks behind the debug macro. V0.2 made output-length validation
  debug-only. V0.3 removed C-side runtime patch metadata. V0.4 links the decoder
  as a separate NASM COFF section and moves parameter patching into assembly.
  The V0.4 source/build snapshot is under ignored `dist/V0.4/`.

## Verification

- Built with Clang 23.1.5 (`clang-cl`, i686 Windows target, `/O1`), NASM 3.01,
  LLD, and the installed Windows SDK/MSVC-compatible runtime.
- `script/build.ps1` passed three byte-for-byte API decode cases for `(lc, lp,
  pb)` values `(0,0,0)`, `(3,0,2)`, and `(2,2,4)`. Debug argument and workspace
  checks passed; the release smoke test decoded and compared 86,192 bytes.
- `dumpbin` measured C object `.text` sizes of 198 bytes release and 395 bytes
  debug. The linked assembly section is 641 bytes release and 654 bytes debug.
  These are object/section sizes, not final executable size deltas.
- The assembly section is linked with execute/read flags. The wrapper's
  `VirtualProtect` and `FlushInstructionCache` path was exercised by the tests.
- After adding assembly comments, NASM generated a 641-byte release `.lzma`
  section byte-for-byte identical to the prior build.
- Test builds still emit MSVC-compatible `fopen` deprecation warnings.

## Remaining limits

- x86 Windows only. Input must be trusted and complete; compressed source bounds
  are not checked by the decoder core.
- The prototype is not thread-safe because it patches a shared static code
  section for each call.
- `dist/V0.3/` remains the preserved V0.3 snapshot and was not modified.
- The V0.4 static-code patching approach is a prototype tradeoff; later revisions
  may simplify the C-to-assembly argument flow discussed during review.

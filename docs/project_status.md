# Project status

Updated: 2026-11-22

## Current state

- V0.5 builds the C API and decoder as one static library:
  `build/lzmacapi_x86.lib` and `build/lzmacapi_x86_debug.lib`.
- The public C API signature is unchanged. C calls the assembly decoder with
  `src+5`, `dst`, `workspace`, `dst_end`, `lc`, and `lp` as ordinary cdecl stack
  arguments. The decoder reads and updates the pointer argument slots directly.
- The assembly preparation entry now only patches the `pb`, `lp`, and `lc`
  immediates. The decoder reads the initial range `Code` from `range_src-4` and
  computes `2048 + (768 << (lc + lp))` at workspace initialization; neither value
  is routed through a patched immediate.
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
- V0.5 passes decoder arguments directly on the cdecl stack, initializes `Code`
  and the probability count in the decoder, and limits preparation to patching
  property immediates. Its snapshot is under ignored `dist/V0.5/`.

## Verification

- Built with Clang 23.1.5 (`clang-cl`, i686 Windows target, `/O1`), NASM 3.01,
  LLD, and the installed Windows SDK/MSVC-compatible runtime.
- `script/build.ps1` passed three byte-for-byte API decode cases for `(lc, lp,
  pb)` values `(0,0,0)`, `(3,0,2)`, and `(2,2,4)`. Debug argument and workspace
  checks passed; the release smoke test decoded and compared 86,192 bytes.
- `dumpbin` measured C object `.text` sizes of 211 bytes release and 422 bytes
  debug. The assembly `.lzma` section is 567 bytes release and 580 bytes debug;
  the static libraries are 4,300 and 4,556 bytes. These are object/section and
  archive sizes, not final executable size deltas.
- The assembly section is linked with execute/read flags. The wrapper's
  `VirtualProtect` and `FlushInstructionCache` path was exercised by the tests.
- Test builds still emit MSVC-compatible `fopen` deprecation warnings.

## Remaining limits

- x86 Windows only. Input must be trusted and complete; compressed source bounds
  are not checked by the decoder core.
- The prototype is not thread-safe because it patches a shared static code
  section for each call.
- `dist/V0.3/` remains the preserved V0.3 snapshot and was not modified.

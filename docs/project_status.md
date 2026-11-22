# Project status

Updated: 2026-11-21

## Current state

- The x86 Windows C wrapper and embedded static decoder build into the release
  library `build/lzmacapi_x86.lib`; the debug library is
  `build/lzmacapi_x86_debug.lib`.
- Parameter validation is enabled only when compiling with
  `LZMA_C_API_DEBUG`; the release library omits those checks. Callers calculate
  workspace size using the formula documented in `SRC/lzma_c_api.h`.
- Build scripts and fixture generation are under `script/`; source and API
  tests are under `SRC/`; generated files remain in ignored `build/`.
- `tools/` and `work/` are reserved for third-party tools and temporary work.
- Local tag `V0` marks the initial project baseline. `dist/V0/` is a local
  backup containing `SRC/`, `script/`, the built `build/` outputs, and the
  root README; it is intentionally excluded from Git to avoid duplicating
  generated binaries in the repository.
- Version `V0.1` removes the workspace-size helper and gates argument checks
  behind `LZMA_C_API_DEBUG`.
- The assembly template now contains the callable prologue and epilogue,
  including pointer setup, register preservation, and return cleanup. The C
  wrapper only copies and patches the template; saved registers are restored
  in reverse push order to preserve the caller ABI.
- Decoding stops when the output pointer reaches the caller-provided target end,
  so an end marker is not required. The release template has only the loop
  length check; the debug template separately checks whether a match would
  cross the target end. Output-length mismatch checking is also debug-only;
  release callers must compare `dst_size` with the expected size themselves.
- The build script consumes the 40-byte NASM metadata as compile-time macros;
  it is no longer embedded in either runtime code template. NASM intermediates
  are written under ignored `work/`.
- V0.3 records this metadata removal; the version snapshot is stored at
  `dist/V0.3/` and ignored by Git.
- A local `dist/V0.1/` snapshot contains the version sources, scripts,
  documentation, and current build outputs; the snapshot is ignored by Git.
- The V0.2 source and build snapshot is stored under `dist/V0.2/` and ignored
  by Git.

## Verification

- Built with Clang 23.1.5 (`clang-cl`, i686 Windows target), LLD, NASM 3.01,
  and the installed Windows SDK/MSVC-compatible runtime.
- `script/build.ps1` passed three public API decode tests. Each decoded output
  matched its expected bytes for `(lc, lp, pb)` values `(0,0,0)`, `(3,0,2)`,
  and `(2,2,4)`.
- Debug-build checks for invalid arguments, insufficient output capacity, and
  insufficient workspace passed. Workspace sizing is now calculated by callers.
- Current fixtures decoded successfully with both release and debug libraries;
  each fixture includes an end marker, so marker-free stream behavior has not
  yet been verified with a dedicated fixture.
- A targeted length-mismatch check confirmed the macro split: debug returned
  `LZMA_C_API_INVALID_STREAM` with `dst_size=0`; release returned `OK` with the
  actual 86,192-byte output length when asked for one byte more.
- With Clang 23.1.5 `/O1`, `lzma_c_api_decode` is 212 bytes without checks and
  408 bytes with `LZMA_C_API_DEBUG`. The release embedded template is 523 bytes;
  the debug template is 536 bytes. Neither contains metadata.
- The release library smoke test decoded and byte-compared the 86,192-byte
  default fixture successfully after the assembly wrapper change.
- Malformed and truncated compressed streams were not executed because the
  current decoder core does not enforce source bounds.

## Remaining limits

- x86 Windows only; valid LZMA stream with an end marker required.
- Do not decode untrusted or malformed input with the current core.

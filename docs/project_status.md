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

## Verification

- Built with Clang 23.1.5 (`clang-cl`, i686 Windows target), LLD, NASM 3.01,
  and the installed Windows SDK/MSVC-compatible runtime.
- `script/build.ps1` passed three public API decode tests. Each decoded output
  matched its expected bytes for `(lc, lp, pb)` values `(0,0,0)`, `(3,0,2)`,
  and `(2,2,4)`.
- Debug-build checks for invalid arguments, insufficient output capacity, and
  insufficient workspace passed. Workspace sizing is now calculated by callers.
- With Clang 23.1.5 `/O1`, `lzma_c_api_decode` is 274 bytes without checks and
  428 bytes with `LZMA_C_API_DEBUG`; its embedded template is 504 bytes.
- Malformed and truncated compressed streams were not executed because the
  current decoder core does not enforce source bounds.

## Remaining limits

- x86 Windows only; valid LZMA stream with an end marker required.
- Do not decode untrusted or malformed input with the current core.

# Project status

Updated: 2026-11-21

## Current state

- The x86 Windows C wrapper and embedded static decoder build into
  `build/lzmacapi_x86.lib`.
- Build scripts and fixture generation are under `script/`; source and API
  tests are under `SRC/`; generated files remain in ignored `build/`.
- `tools/` and `work/` are reserved for third-party tools and temporary work.
- Local tag `V0` marks the initial project baseline. `dist/V0/` is a local
  backup containing `SRC/`, `script/`, the built `build/` outputs, and the
  root README; it is intentionally excluded from Git to avoid duplicating
  generated binaries in the repository.

## Verification

- Built with Clang 23.1.5 (`clang-cl`, i686 Windows target), LLD, NASM 3.01,
  and the installed Windows SDK/MSVC-compatible runtime.
- `script/build.ps1` passed three public API decode tests. Each decoded output
  matched its expected bytes for `(lc, lp, pb)` values `(0,0,0)`, `(3,0,2)`,
  and `(2,2,4)`.
- Workspace sizing and API error returns for invalid arguments, insufficient
  output capacity, and insufficient workspace passed.
- Malformed and truncated compressed streams were not executed because the
  current decoder core does not enforce source bounds.

## Remaining limits

- x86 Windows only; valid LZMA stream with an end marker required.
- Do not decode untrusted or malformed input with the current core.

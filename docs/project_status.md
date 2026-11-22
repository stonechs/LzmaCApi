# Project status

Updated: 2026-11-22

## Current state

- V0.5.2 加入单文件 7z 容器适配：`SRC/sevenzip_single.c/.h` 解析头部并
  调用现有 LZMA1 API；支持压缩头部，校验签名头、下一头部、packed stream
  和可用的解压数据 CRC。限制为一个非空文件和一个 LZMA1 coder，不支持
  LZMA2、滤镜、加密、多文件或分卷；当前输出上限为 16 MiB。
- `script/test_sevenzip.ps1` 用 7-Zip 生成真实归档，验证 release/debug 解压、
  输出容量、签名和起始头 CRC。
- 2026-11-22：Clang 23.1.5 / i686、NASM 3.01 构建通过；原有 API 与 release
  smoke 测试通过。7-Zip 26.04 生成的默认和 `lc=2/lp=2/pb=4` 单文件 LZMA1
  归档均在 release/debug 逐字节匹配通过。
- 实现提交：`5c88d5fc3fd1ca1c9ac6988a45f574e52e678de2`，标签 `V0.5.2`。
- 后续：需要覆盖 LZMA2 时，须先为现有解码核心增加 LZMA2 chunk/state 支持；
  当前没有其他待做项。

- V0.5.2 builds the C API, decoder, and optional 7z container layer as one static library:
  `build/lzmacapi_x86.lib` and `build/lzmacapi_x86_debug.lib`.
- The public C API signature is unchanged. C passes `src`, `dst`, `workspace`,
  `dst_end`, `workspace_size`, and `lc/lp/pb` to the decoder as cdecl stack args.
  The decoder advances `src` by five bytes itself.
- The decoder computes the `pb` and `lp` masks once into stack locals, uses `lc`
  as a variable shift count, and uses `workspace_size/2` as the probability
  initialization count. It reads the initial range `Code` from `src[1..4]`.
- Release C code ignores `dst_size`; the assembly entry does not return an end
  pointer. Debug builds still set `dst_size` and validate the decoded length.
- The assembly code is read-only during decoding. C no longer changes page
  permissions or flushes the instruction cache; calls can run concurrently when
  their input, output, and workspace buffers do not overlap.
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
- V0.5 passes decoder arguments directly on the cdecl stack and initializes
  `Code` and the probability count in the decoder. V0.5.1 passes the caller's
  `workspace_size` directly to assembly, has assembly advance `src`, and removes
  release `dst_size` reporting. Its snapshot is under ignored
  `dist/V0.5.1_workspace-size-arg/`; the V0.5 snapshot remains under
  `dist/V0.5/`.

## Verification

- Built with Clang 23.1.5 (`clang-cl`, i686 Windows target, `/O1`), NASM 3.01,
  LLD, and the installed Windows SDK/MSVC-compatible runtime.
- `script/build.ps1` passed the base API tests and release smoke test. The current
  revision also passed all 235 byte-comparison stress cases in both release and
  debug builds: all API-accepted property tuples and ten long cases up to 1 MiB.
- `dumpbin` measured `lzma_c_api_decode` `.text` at 47 bytes release and 235 bytes
  debug. The assembly `.lzma` section is 534/550 bytes; static libraries are
  3,130/3,346 bytes. These are object/section and archive sizes, not final EXE
  size deltas.
- An earlier runtime-property build on an Intel N100 was compared with the
  7-Zip 26.04 x86 CLI using the same LZMA1 streams. It measured 903 vs 757 MiB/s
  on 128 MiB repeated data and 9.16 vs 12.78 MiB/s on 16 MiB random data. Those
  figures predate the current `workspace_size` and release `dst_size` changes;
  the CLI result includes process/archive overhead and writes to NUL.
- Test builds still emit MSVC-compatible `fopen` deprecation warnings.

## Remaining limits

- x86 Windows only. Input must be trusted and complete; compressed source bounds
  are not checked by the decoder core.
- The 7z layer accepts a single non-empty LZMA1 file (and a single LZMA1 coder for
  an encoded header), with a 16 MiB unpacked-size cap. It rejects LZMA2, encryption,
  filters, multiple files/streams, and split archives. Header/data CRCs are checked
  when present, but CRC checks do not make the unchecked decoder safe for hostile
  input.
- Concurrent calls are supported when each call uses non-overlapping input,
  output, and workspace buffers.
- `dist/V0.3/` remains the preserved V0.3 snapshot and was not modified.
- The preserved V0.5 baseline remains unchanged.

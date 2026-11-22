# Project status

Updated: 2026-11-22

## Current state

- V0.5.6 新增 `sevenzip_extract_single_lzma1_fast`：单个接口函数按固定布局
  读取 StartHeader、PackInfo 和 UnpackInfo 必需字段，跳过可选 Pack CRC 后直接
  调用 `decode_folder`，不调用 `parse_header` / `parse_streams`，也不解析
  SubStreamsInfo、FilesInfo 或验证 CRC。约束详见 `SRC/sevenzip_single.h`。
- 快速接口已随全套测试验证：`forTest/` 12/12、压力包 50/50 在 release/debug
  下均与 7-Zip 输出逐字节一致。结果位于
  `work/forTest_decode_20261122_123624_350/results.csv` 和
  `work/stress_sevenzip_50_20261122_123630_461/results.csv`。
- V0.5.6 实现提交：`46cd3f3125d71d83108c98b9c8800c36b825b1fc`，标签 `V0.5.6`；
  V0.5.5 基线为 `90438c6407203dd6bbcf657231abe3125c9819f0`。
- V0.5.5 显式隔离 Reader 失败分支：`reader_byte`、`reader_u32`、StreamsInfo
  和 Header 属性读取统一使用调试/发布两套宏。发布版不生成 `reader_take`
  越界失败分支，也不生成 `reader_size` 转换失败判断；格式标记、字段值和
  单文件/单流/LZMA1 支持范围检查仍保留。
- V0.5.4 收窄 7z 解析：只接受普通 Header、单非空文件、单 packed stream、
  单 LZMA1 coder。压缩 Header 分支已移除。Reader 读取越界、长度转换、归档
  偏移/长度、输出容量、签名和 CRC 检查均由 `LZMA_C_API_DEBUG` 隔离；发布版
  要求调用方传入结构和长度都有效的归档及足够大的输出缓冲区。
- `forTest/` 的 12 个现存归档在 release/debug 两种库下均解码成功，输出逐字节
  匹配 7-Zip 提取结果。`script/test_forTest_archives.ps1` 可复现该验证。
- 50 个 LZMA1 归档压力测试覆盖不同内容模式、文件长度（4 KiB 至 1 MiB）、
  `lc/lp/pb` 合法组合、字典大小和压缩等级；release/debug 共 100 次归档解码
  全部通过。生成器和结果位于 `script/stress_sevenzip_50.ps1` 与
  `work/stress_sevenzip_50_20261122_111055_800/results.csv`。
- 2026-11-22：Clang 23.1.5 / i686、NASM 3.01 构建通过；原有 API、release
  smoke、单文件 7z 正常与错误输入测试均通过。
- V0.5.5 回归：API 3 组与 release smoke、单文件 7z 正常/错误用例通过；
  `forTest/` 12/12、压力归档 50/50 均在 release/debug 与 7-Zip 逐字节匹配。
  新结果位于 `work/forTest_decode_20261122_114944_707/results.csv` 和
  `work/stress_sevenzip_50_20261122_114944_595/results.csv`。
- V0.5.5 实现提交：`90438c6407203dd6bbcf657231abe3125c9819f0`，标签 `V0.5.5`；
  V0.5.4 基线为 `f1591ae69b1a494bae9b0b93a6cfe8346c6e3a8b`。
- V0.5.4 提交：`f1591ae69b1a494bae9b0b93a6cfe8346c6e3a8b`，标签 `V0.5.4`；
  V0.5.3 基线为 `9608a829bb3e78abb93ceab50ed8593317009cba`。
- 后续：需要覆盖 LZMA2 时，须先为现有解码核心增加 LZMA2 chunk/state 支持；
  当前没有其他待做项。

- V0.5.6 builds the C API, decoder, and optional 7z container layer as one static library:
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
- All 12 archives supplied in `forTest/` and all 50 generated LZMA1 7z cases passed
  in release and debug builds, with output compared byte-for-byte against 7-Zip.
  Results: `work/forTest_decode_20261122_111055_827/results.csv` and
  `work/stress_sevenzip_50_20261122_111055_800/results.csv`.
- The release container object is compiled without signature/CRC checks; the test
  harness confirmed corrupted signature/CRC fields are ignored by release and
  rejected by debug.
- `dumpbin` measured `lzma_c_api_decode` `.text` at 47 bytes release and 235 bytes
  debug. The assembly `.lzma` section is 534/550 bytes; the current static
  libraries with the container layer are 7,332/9,564 bytes. These are object/section and archive sizes, not final EXE
  size deltas.
- Clang 23.1.5 `-Oz -ffunction-sections`, i686 MSVC COFF, function COMDAT raw sizes
  after the V0.5.6 fast entry:

  | Function | Release bytes | Debug bytes |
  | --- | ---: | ---: |
  | `sevenzip_extract_single_lzma1` (含已内联解析函数) | 1,202 | 1,783 |
  | `sevenzip_extract_single_lzma1_fast` | 195 | 301 |
  | `decode_folder` | 172 | 347 |
  | `reader_number` | 152 | 129 |
  | `read_digest_vector` | 92 | 99 |
  | `reader_byte` | 内联 | 28 |
  | `reader_size` | 内联 | 35 |
  | `reader_take` | 内联 | 68 |
  | `reader_u32` | 内联 | 39 |
  | `crc32_bytes` | — | 57 |
  | **7z container C total** | **1,813** | **2,886** |
  | `lzma_c_api_decode` wrapper | 47 | 216 |
  | NASM LZMA core (`.lzma`) | 534 | 550 |
  | **Combined code sections** | **2,394** | **3,652** |

  `parse_streams`, `parse_header`, `read_u32le`, `read_u64le`, and
  `decode_properties` are inlined into the normal entry under `-Oz`; the fast entry
  calls the separately measured `decode_folder` and `reader_number` sections.
  Assembly is not compiled by Clang, so its section is reported as-is.
  The totals exclude runtime-library functions, constants, alignment, and link-time
  dead-code removal; they are not final executable size deltas.
- An earlier runtime-property build on an Intel N100 was compared with the
  7-Zip 26.04 x86 CLI using the same LZMA1 streams. It measured 903 vs 757 MiB/s
  on 128 MiB repeated data and 9.16 vs 12.78 MiB/s on 16 MiB random data. Those
  figures predate the current `workspace_size` and release `dst_size` changes;
  the CLI result includes process/archive overhead and writes to NUL.
- Test builds still emit MSVC-compatible `fopen` deprecation warnings.

## Remaining limits

- x86 Windows only. Input must be trusted and complete; compressed source bounds
  are not checked by the decoder core.
- The 7z layer accepts only a plain Header with one non-empty LZMA1 file, one packed
  stream, and one coder. It rejects compressed headers, LZMA2, encryption, filters,
  multiple files/streams, and split archives. Release parsing has no structural
  bounds checks; debug parsing does. The decoder itself still does not validate
  compressed source bounds, so inputs must remain trusted and complete.
- Concurrent calls are supported when each call uses non-overlapping input,
  output, and workspace buffers.
- `dist/V0.3/` remains the preserved V0.3 snapshot and was not modified.
- The preserved V0.5 baseline remains unchanged.

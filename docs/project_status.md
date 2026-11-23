# Project status

Updated: 2026-11-22

## Current line

- `main` is the LZMA1-only line, based on V0.6.2 and tagged V1.0. The `lzma2` branch retains the separate V0.6.3+ development history.
- V1.0.1 changes: retain only the fast 7z path; remove the full parser and raw LZMA C API; use a 24-byte C-initialized context followed by the probability table; call the static decoder through `run(src, dst, context, output_size, lc, lp, pb)`.
- The C fast path owns allocation and initialization of the state/probability workspace. Assembly receives no workspace-size argument and does not initialize the probability table.

## Validation

- Clang-cl i686 Windows `/O1` and NASM `-O9`: fast static libraries built successfully (release 4,420 bytes; debug 4,644 bytes).
- LZMA1 smoke: release/debug passed.
- Current `forTest/`: 119 archives found; 116 LZMA1 archives passed release/debug and matched 7-Zip extraction byte-for-byte. Three LZMA2 archives were identified and skipped because this line is LZMA1-only: `金曲KTV95001.7z`, `三国志2-霸王的大陆.7z`, and `最终幻想7-FC南晶NJ063中文版.7z`.
- 50 generated LZMA1 7z stress archives passed in release/debug.
- `/O1` map sizes: release/debug `sevenzip_extract_single_lzma1_fast` 360/546 bytes; `read_number_fast` 79/79 bytes; assembly `.lzma` section 547/563 bytes. `decode_properties`, `initialize_probabilities`, and `decode_folder` are inlined into the fast entry. Maps and EXEs are colocated in `dist/builds/`.
- Archive reports: `work/forTest_decode_20261122_232140_750/results.csv` and `work/stress_sevenzip_50_20261122_232158_280/results.csv`.

## Artifact policy

- Static libraries are written under `build/`.
- Test run logs and generated inputs are written under `work/`.
- Each test executable is paired with its `.map` and `build_info.txt` under `dist/builds/<version>_<purpose>_<timestamp>/`.
- Version snapshots use `dist/<version>_<feature>/`.

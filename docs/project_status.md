# Project status

Updated: 2026-11-22

## Current line

- `main` is the LZMA1-only line, based on V0.6.2 and tagged V1.0. The `lzma2` branch retains the separate V0.6.3+ development history.
- V1.0.1 baseline: retain only the fast 7z path; remove the full parser and raw LZMA C API.
- V1.0.2: C allocates a fixed maximum probability-table workspace; assembly fills its 3,147,776 16-bit entries with 1024 using `rep stosw`, initializes LZMA state and repeat distances on its stack, and accepts `run(src, dst, workspace, output_size, lc, lp, pb)`.
- Assembly receives no workspace-size argument; workspace allocation and initialization do not calculate a per-stream probability count. The fixed workspace occupies 6,295,552 bytes (6 MiB).

## Validation

- Clang-cl i686 Windows `/O1` and NASM `-O9`: fast static libraries built successfully (release 3,866 bytes; debug 4,104 bytes).
- LZMA1 smoke: release/debug passed.
- `forTest/`: 116 LZMA1 archives passed in release/debug and matched 7-Zip extraction byte-for-byte. Three LZMA2 archives were identified and skipped because this line is LZMA1-only: `金曲KTV95001.7z`, `三国志2-霸王的大陆.7z`, and `最终幻想7-FC南晶NJ063中文版.7z`.
- 50 generated LZMA1 7z stress archives passed in release/debug.
- `/O1` map sizes: release/debug fast C entry approximately 292/491 bytes; `read_number_fast` 79/79 bytes; assembly `.lzma` section 547/563 bytes. C sizes are estimated from adjacent map symbols. Maps and EXEs are colocated in `dist/builds/`.
- Latest reports: `work/forTest_decode_20261122_233855_616/results.csv` and `work/stress_sevenzip_50_20261122_233911_896/results.csv`.
- The test executable still emits Clang's existing `fopen` deprecation warning; all builds and tests pass.

## Artifact policy

- Static libraries are written under `build/`.
- Test run logs and generated inputs are written under `work/`.
- Each test executable is paired with its `.map` and `build_info.txt` under `dist/builds/<version>_<purpose>_<timestamp>/`.
- Version snapshots use `dist/<version>_<feature>/`.

# Architecture decisions

## 2026-11-22: C initializes probabilities and owns LZMA2 block state

- C initializes the LZMA probability table before calling the shared x86 decoder entry. The raw C API, full LZMA1 container path, and fast LZMA1 path therefore use the same assembly entry and initialization behavior.
- The `LZMA_C_API_LZMA2` build option gates LZMA2-only parsing and the extra assembly state parameter. Default builds retain the compact LZMA1-only fast parser.
- In the macro-enabled fast build, the C-allocated workspace stores the probability table followed by a persistent state record. C parses chunk sizes and reset controls; the assembly decodes one compressed chunk and reads/writes the LZMA state, repeat distances, previous byte, and position bias.
- Range/code initialization remains per compressed chunk. Probability models and the LZMA state are preserved or reset according to each LZMA2 control byte.
- The ordinary 7z container parser remains LZMA1-only. LZMA2 support is opt-in through the fast entry.

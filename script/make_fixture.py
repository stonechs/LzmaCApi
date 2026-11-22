import lzma
import pathlib
import random
import sys


out = pathlib.Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
rng = random.Random(20261121)
cases = [
    ("minprops", 0, 0, 0,
     b"minimum properties\n" * 48 + bytes(range(64))),
    ("default", 3, 0, 2,
     (b"LzmaCApi wrapper test: repeated text and block transitions.\n" * 1300)
     + bytes(rng.randrange(256) for _ in range(4096))
     + bytes(range(256)) * 16),
    ("wideprops", 2, 2, 4,
     (b"lc=4 lp=1 pb=4 property coverage\n" * 200)
     + bytes((i * 37 + 11) & 0xff for i in range(8192))),
]

for name, lc, lp, pb, plain in cases:
    stream = lzma.compress(
        plain,
        format=lzma.FORMAT_ALONE,
        filters=[{"id": lzma.FILTER_LZMA1, "dict_size": 1 << 20,
                  "lc": lc, "lp": lp, "pb": pb}],
    )
    # This decoder requires a known uncompressed size and an end marker.
    stream = stream[:5] + len(plain).to_bytes(8, "little") + stream[13:]
    (out / f"{name}.raw").write_bytes(stream[13:])
    (out / f"{name}.expected").write_bytes(plain)
    print(f"{name}: input={len(stream) - 13} output={len(plain)} "
          f"lc={lc} lp={lp} pb={pb}")

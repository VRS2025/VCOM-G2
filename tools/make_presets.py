#!/usr/bin/env python3
# Конвертер пресетов: presets.csv -> presets.bin для заливки в QSPI (0x90100000) через DFU.
# Формат бинаря должен совпадать с FilePreset/PresetFileHeader в my_pedal.cpp.
#
# CSV: первая строка — имена колонок (любой порядок), дальше по одной строке на пресет.
# Колонки: band,song,model,ir,gain,mix,level,tone,revMix,delayMix,delayMs,delayFb,
#          tremDepth,tremRate,eqLow,eqMid,eqHigh,revFb,revDamp
# Обязательны: band,song,model,ir. Остальные можно опускать (берётся дефолт).
# Значения 0 для eqLow/eqMid/eqHigh/revFb/revDamp на устройстве означают «по умолчанию»
# (EQ плоский 0.5, revFb 0.84, revDamp 0.35).

import csv, struct, sys

MAGIC = b"VMG1"
VERSION = 1
MAX_BANK = 32

# имя колонки -> (тип, значение по умолчанию)
FIELDS = [
    ("band", str, ""), ("song", str, ""),
    ("model", int, 1), ("ir", int, 1),
    ("gain", float, 0.30), ("mix", float, 1.0), ("level", float, 0.55), ("tone", float, 0.50),
    ("revMix", float, 0.0), ("delayMix", float, 0.0), ("delayMs", float, 0.0), ("delayFb", float, 0.0),
    ("tremDepth", float, 0.0), ("tremRate", float, 0.0),
    ("eqLow", float, 0.0), ("eqMid", float, 0.0), ("eqHigh", float, 0.0),
    ("revFb", float, 0.0), ("revDamp", float, 0.0),
]
# FilePreset: <12s 12s 2i 15f  (92 байта)
FMT = "<12s12s2i15f"
assert struct.calcsize(FMT) == 92


def to12(s):
    b = s.encode("ascii", "replace")[:11]
    return b + b"\x00" * (12 - len(b))


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else "presets.csv"
    dst = sys.argv[2] if len(sys.argv) > 2 else "presets.bin"
    rows = []
    with open(src, newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            if not r.get("song", "").strip() and not r.get("band", "").strip():
                continue
            vals = {}
            for name, typ, dflt in FIELDS:
                raw = (r.get(name) or "").strip()
                if raw == "":
                    vals[name] = dflt
                else:
                    vals[name] = typ(raw) if typ is not str else raw
            rows.append(vals)

    if not rows:
        sys.exit("No presets in " + src)
    if len(rows) > MAX_BANK:
        sys.exit("Too many presets: %d (max %d)" % (len(rows), MAX_BANK))

    out = bytearray()
    out += struct.pack("<4sIII", MAGIC, VERSION, len(rows), 0)
    for v in rows:
        out += struct.pack(
            FMT, to12(v["band"]), to12(v["song"]),
            v["model"], v["ir"],
            v["gain"], v["mix"], v["level"], v["tone"],
            v["revMix"], v["delayMix"], v["delayMs"], v["delayFb"],
            v["tremDepth"], v["tremRate"],
            v["eqLow"], v["eqMid"], v["eqHigh"], v["revFb"], v["revDamp"],
        )
    with open(dst, "wb") as f:
        f.write(out)
    print("OK: %d presets -> %s (%d bytes)" % (len(rows), dst, len(out)))


if __name__ == "__main__":
    main()

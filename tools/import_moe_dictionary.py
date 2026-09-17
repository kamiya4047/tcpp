"""Generate the legacy TSV import from the untouched MOE workbook.

Run convert_legacy_dictionary.py and build_dictionary_index.py afterwards to
produce the canonical dictionary-v1 snapshot and runtime index.
"""

from pathlib import Path
import re
import sys

import openpyxl


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "data" / "sources" / "dict_concised_2014_20260626" / "dict_concised_2014_20260626.xlsx"
OUTPUT = ROOT / "data" / "lexicon" / "moe_concise.tsv"

ZHuyin_KEYS = dict(zip(
    "ㄅㄆㄇㄈㄉㄊㄋㄌㄍㄎㄏㄐㄑㄒㄓㄔㄕㄖㄗㄘㄙㄧ ㄨㄩㄚㄛㄜㄝㄞㄟㄠㄡㄢㄣㄤㄥㄦ",
    "1qaz2wsxedcrfv5tgbyhnu jm8ik,9ol.0p;/-",
))
TONES = {"ˊ": "6", "ˇ": "3", "ˋ": "4", "˙": "7"}


def cpp_string(value: str) -> str:
    value = value.replace("\\", "\\\\").replace('"', '\\"')
    value = value.replace("\r", " ").replace("\n", " ")
    value = re.sub(r"[\x00-\x08\x0b\x0c\x0e-\x1f]", " ", value)
    return value.strip()


def reading_keys(value: str) -> str:
    result = []
    for syllable in value.replace("\u3000", " ").split():
        tone = ""
        symbols = []
        for char in syllable:
            if char in TONES:
                tone = TONES[char]
            elif char in ZHuyin_KEYS:
                symbols.append(ZHuyin_KEYS[char])
            else:
                raise ValueError(f"unsupported Zhuyin character {char!r} in {value!r}")
        if not symbols:
            raise ValueError(f"missing Zhuyin symbols in {value!r}")
        # In the standard layout, ㄓ/ㄔ/ㄕ/ㄖ/ㄗ/ㄘ/ㄙ are complete "empty
        # final" syllables. A first-tone space is required to stop the next
        # syllable's medial from being joined to that initial.
        if len(symbols) == 1 and syllable[0] in "ㄓㄔㄕㄖㄗㄘㄙ" and not tone:
            tone = " "
        result.append("".join(symbols) + tone)
    return "".join(result)


def main() -> None:
    if not SOURCE.exists():
        raise SystemExit(f"MOE workbook not found: {SOURCE}")
    workbook = openpyxl.load_workbook(SOURCE, read_only=True, data_only=True)
    sheet = workbook.active
    rows = sheet.iter_rows(values_only=True)
    header = next(rows)
    columns = {name: index for index, name in enumerate(header)}
    required = ("字詞名", "注音一式", "釋義")
    missing = [name for name in required if name not in columns]
    if missing:
        raise SystemExit(f"MOE workbook is missing columns: {missing}")

    entries = []
    skipped = 0
    for row in rows:
        text = str(row[columns["字詞名"]] or "").strip()
        zhuyin = str(row[columns["注音一式"]] or "").strip()
        explanation = str(row[columns["釋義"]] or "").replace("_x000D_", " ").strip()
        if not text or not zhuyin or not all(ord(char) <= 0x10FFFF for char in text):
            skipped += 1
            continue
        try:
            keys = reading_keys(zhuyin)
        except ValueError:
            skipped += 1
            continue
        if len(text) != len(zhuyin.replace("\u3000", " ").split()):
            # The decoder models one Mandarin syllable per Unicode scalar.
            # Skip punctuation-bearing idioms and erhua spellings whose MOE
            # display reading intentionally omits a separate syllable.
            skipped += 1
            continue
        if len(text) > 32 or len(keys) > 128:
            skipped += 1
            continue
        entries.append((text, keys, explanation))

    character_frequency = {}
    for text, _, _ in entries:
        for char in text:
            character_frequency[char] = character_frequency.get(char, 0) + 1

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    with OUTPUT.open("w", encoding="utf-8", newline="\n") as stream:
        for text, keys, explanation in entries:
            frequency = 1000.0 + sum(character_frequency.get(char, 0) for char in text) * 10.0
            stream.write("\t".join((text.replace("\t", " "), keys, f"{frequency:.1f}",
                                    explanation.replace("\t", " ").replace("\n", " "))) + "\n")
    print(f"generated {OUTPUT} with {len(entries)} entries; skipped {skipped}")


if __name__ == "__main__":
    main()

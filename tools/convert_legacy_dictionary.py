"""Build the canonical dictionary-v1 export from its retained source inputs."""
from pathlib import Path
import argparse
import json
import re
import uuid


SOURCE_ID = "moe-concise-2014-20260626"
CURATED_SOURCE_ID = "taiwan-bopomofo-curated-2026"
NAMESPACE = uuid.uuid5(uuid.NAMESPACE_URL, "https://taiwan-bopomofo.local/dictionary/v1")


def stable_id(kind: str, text: str, keys: str) -> str:
    return str(uuid.uuid5(NAMESPACE, f"{kind}\0{text}\0{keys}"))


ZERO_INITIAL = {
    "yi": "u", "ya": "u8", "yo": "ui", "ye": "u,", "yao": "ul", "you": "u.",
    "yan": "u0", "yin": "up", "yang": "u;", "ying": "u/", "yong": "m/",
    "wu": "j", "wa": "j8", "wo": "ji", "wai": "j9", "wei": "jo", "wan": "j0",
    "wen": "jp", "wang": "j;", "weng": "j/", "yu": "m", "yue": "m,",
    "yuan": "m0", "yun": "mp",
}
INITIALS = (("zh", "5"), ("ch", "t"), ("sh", "g"), ("b", "1"), ("p", "q"),
            ("m", "a"), ("f", "z"), ("d", "2"), ("t", "w"), ("n", "s"),
            ("l", "x"), ("g", "e"), ("k", "d"), ("h", "c"), ("j", "r"),
            ("q", "f"), ("x", "v"), ("r", "b"), ("z", "y"), ("c", "h"), ("s", "n"))
FINALS = {
    "a": "8", "o": "i", "e": "k", "ai": "9", "ei": "o", "ao": "l", "ou": ".",
    "an": "0", "en": "p", "ang": ";", "eng": "/", "er": "-", "i": "u", "ia": "u8",
    "ie": "u,", "iao": "ul", "iu": "u.", "ian": "u0", "in": "up", "iang": "u;",
    "ing": "u/", "iong": "m/", "u": "j", "ua": "j8", "uo": "ji", "uai": "j9",
    "ui": "jo", "uan": "j0", "un": "jp", "uang": "j;", "ong": "j/", "v": "m",
    "ve": "m,", "van": "m0", "vn": "mp",
}
TONE_KEYS = {"1": " ", "2": "6", "3": "3", "4": "4", "5": "7"}


def reading_keys(pinyin: str) -> str:
    """Match the decoder's numbered-Hanyu-Pinyin conversion exactly."""
    result = []
    for syllable in pinyin.split():
        if len(syllable) < 2 or syllable[-1] not in TONE_KEYS:
            raise ValueError(f"reading must carry a tone: {syllable!r}")
        base, tone = syllable[:-1], TONE_KEYS[syllable[-1]]
        if base in ZERO_INITIAL:
            result.append(ZERO_INITIAL[base] + tone)
            continue
        initial = ""
        key = ""
        for name, candidate in INITIALS:
            if base.startswith(name):
                initial, key, base = name, candidate, base[len(name):]
                break
        if base == "i" and initial in {"zh", "ch", "sh", "r", "z", "c", "s"}:
            result.append(key + tone)
            continue
        if initial in {"j", "q", "x"} and base in {"u", "ue", "uan", "un"}:
            base = {"u": "v", "ue": "ve", "uan": "van", "un": "vn"}[base]
        if base not in FINALS:
            raise ValueError(f"unsupported pinyin final: {syllable!r}")
        result.append(key + FINALS[base] + tone)
    return "".join(result)


CURATED = re.compile(r'^L\("(?P<text>(?:[^"\\]|\\.)*)", "(?P<pinyin>[^"]+)", '
                     r'(?P<score>[0-9.]+), "(?P<definition>(?:[^"\\]|\\.)*)"\)$')


def decode_cpp_string(value: str) -> str:
    return value.replace(r'\"', '"').replace(r'\\', '\\')


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path, nargs="?", default=root / "data/lexicon/moe_concise.tsv")
    parser.add_argument("output", type=Path, nargs="?", default=root / "data/dictionary/entries.v1.jsonl")
    parser.add_argument("--curated", type=Path, default=root / "data/lexicon/curated.inc")
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    entries: dict[tuple[str, str], dict] = {}
    with args.source.open(encoding="utf-8", newline="") as source:
        for line_number, line in enumerate(source, 1):
            fields = line.rstrip("\r\n").split("\t", 3)
            if len(fields) != 4:
                raise SystemExit(f"{args.source}:{line_number}: expected four TSV fields")
            text, keys, score, definition = fields
            entry_id = stable_id("entry", text, keys)
            record = {
                "schema_version": 1,
                "entry_id": entry_id,
                "text": text,
                "locale": "zh-TW",
                "status": "imported",
                "pronunciations": [{"pronunciation_id": stable_id("pronunciation", text, keys),
                                      "system": "taiwan-bopomofo-keyboard", "input_keys": keys,
                                      "is_primary": True}],
                "senses": [{"sense_id": stable_id("sense", text, keys), "part_of_speech": "unknown",
                            "definition": definition, "usage_note": None, "source_id": SOURCE_ID}],
                "ranking": {"score": float(score), "rarity": "unclassified", "basis": "dictionary-entry-heuristic"},
                "variants": [],
                "confusion_group_ids": [],
                "source_ids": [SOURCE_ID],
            }
            entries[(text, keys)] = record
    curated_count = 0
    with args.curated.open(encoding="utf-8") as source:
        for line_number, line in enumerate(source, 1):
            if line.startswith("//") or not line.strip():
                continue
            match = CURATED.fullmatch(line.strip())
            if not match:
                raise SystemExit(f"{args.curated}:{line_number}: unsupported curated entry")
            text = decode_cpp_string(match["text"])
            keys = reading_keys(match["pinyin"])
            score = float(match["score"])
            definition = decode_cpp_string(match["definition"])
            record = entries.get((text, keys))
            if record is None:
                record = {
                    "schema_version": 1, "entry_id": stable_id("entry", text, keys), "text": text,
                    "locale": "zh-TW", "status": "published",
                    "pronunciations": [{"pronunciation_id": stable_id("pronunciation", text, keys),
                                         "system": "taiwan-bopomofo-keyboard", "input_keys": keys,
                                         "is_primary": True}],
                    "senses": [], "ranking": {"score": score, "rarity": "common", "basis": "editorial-prior"},
                    "variants": [], "confusion_group_ids": [], "source_ids": [CURATED_SOURCE_ID],
                }
                entries[(text, keys)] = record
            else:
                record["status"] = "published"
                record["ranking"]["score"] = max(float(record["ranking"]["score"]), score)
                record["ranking"]["rarity"] = "common"
                record["ranking"]["basis"] = "editorial-prior-plus-source-import"
                if CURATED_SOURCE_ID not in record["source_ids"]:
                    record["source_ids"].append(CURATED_SOURCE_ID)
            if definition and all(sense["definition"] != definition for sense in record["senses"]):
                record["senses"].append({"sense_id": stable_id("sense-curated", text, keys),
                                         "part_of_speech": "unknown", "definition": definition,
                                         "usage_note": None, "source_id": CURATED_SOURCE_ID})
            curated_count += 1
    with args.output.open("w", encoding="utf-8", newline="\n") as output:
        for record in entries.values():
            output.write(json.dumps(record, ensure_ascii=False, separators=(",", ":")) + "\n")
    print(f"generated {args.output} with {len(entries)} entries ({curated_count} merged curated entries)")


if __name__ == "__main__":
    main()

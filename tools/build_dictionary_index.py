"""Build the lazy runtime index from the canonical dictionary-v1 export."""
from pathlib import Path
import argparse
import json
import struct

MAGIC = b"TBIDX\0" + b"2\0"
HEADER = struct.Struct("<8sIIII")  # magic, schema, buckets, common records, all records
TOC = struct.Struct("<HQI")        # first-reading bytes, absolute offset, record count
RECORD = struct.Struct("<IIId")    # text, keys, definition byte lengths, ranking
TONE_KEYS = set(" 6347")
KEY_CATEGORY = {
    **{key: 1 for key in "1qaz2wsxedcrfv5tgbyhn"},
    **{key: 2 for key in "ujm"},
    **{key: 3 for key in "8ik,9ol.0p;/-"},
}


def first_reading(keys: str) -> str:
    """Return the first complete Taiwan-keyboard syllable."""
    last_category = 0
    for index, key in enumerate(keys):
        if key in TONE_KEYS:
            return keys[:index + 1]
        category = KEY_CATEGORY.get(key, 0)
        if category == 0:
            raise ValueError(f"unsupported input key {key!r}")
        if last_category and category <= last_category:
            return keys[:index]
        last_category = category
    return keys


def record_bytes(row: tuple[bytes, bytes, bytes, float]) -> bytes:
    keys, text, definition, ranking = row
    return RECORD.pack(len(text), len(keys), len(definition), ranking) + text + keys + definition


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path, nargs="?", default=root / "data/dictionary/entries.v1.jsonl")
    parser.add_argument("output", type=Path, nargs="?", default=root / "data/lexicon/dictionary.idx")
    args = parser.parse_args()

    rows = []
    common_rows = []
    with args.source.open(encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, 1):
            try:
                entry = json.loads(line)
                if entry["schema_version"] != 1 or entry["status"] not in ("imported", "published"):
                    continue
                text = entry["text"]
                ranking = entry["ranking"]
                value = float(ranking["score"])
                rarity = ranking["rarity"]
                if rarity not in {"common", "uncommon", "rare", "obsolete", "unclassified"}:
                    raise ValueError("invalid rarity")
                definitions = [sense["definition"] for sense in entry["senses"] if sense["definition"]]
                definition = "；".join(definitions)
                pronunciations = entry["pronunciations"]
            except (KeyError, TypeError, ValueError, json.JSONDecodeError) as error:
                raise SystemExit(f"{args.source}:{line_number}: invalid dictionary-v1 entry: {error}") from error
            for pronunciation in pronunciations:
                keys = pronunciation.get("input_keys", "")
                try:
                    bucket = first_reading(keys)
                except ValueError as error:
                    raise SystemExit(f"{args.source}:{line_number}: invalid input keys") from error
                if not text or not keys or value <= 0:
                    raise SystemExit(f"{args.source}:{line_number}: empty text/keys or non-positive ranking")
                row = (bucket.encode(), keys.encode(), text.encode(), definition.encode(), value)
                rows.append(row)
                # Editorial status, not an importer heuristic, decides what
                # remains resident. There is intentionally no count cap.
                if rarity == "common":
                    common_rows.append(row[1:])

    buckets: dict[bytes, list[tuple[bytes, bytes, bytes, float]]] = {}
    for bucket, keys, text, definition, ranking in rows:
        buckets.setdefault(bucket, []).append((keys, text, definition, ranking))
    for entries in buckets.values():
        entries.sort(key=lambda row: (-row[3], row[1], row[0]))
    common = sorted(common_rows, key=lambda row: (-row[3], row[1], row[0]))
    common_blob = b"".join(record_bytes(row) for row in common)
    bucket_blobs = [(key, b"".join(record_bytes(row) for row in entries)) for key, entries in sorted(buckets.items())]
    toc_size = sum(TOC.size + len(key) for key, _ in bucket_blobs)
    offset = HEADER.size + toc_size + len(common_blob)
    toc = []
    for key, blob in bucket_blobs:
        toc.append((key, offset, len(buckets[key])))
        offset += len(blob)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("wb") as stream:
        stream.write(HEADER.pack(MAGIC, 2, len(toc), len(common), len(rows)))
        for key, offset, count in toc:
            stream.write(TOC.pack(len(key), offset, count))
            stream.write(key)
        stream.write(common_blob)
        for _, blob in bucket_blobs:
            stream.write(blob)
    print(f"generated {args.output} with {len(rows)} entries, {len(toc)} phonetic buckets, and {len(common)} common entries")


if __name__ == "__main__":
    main()

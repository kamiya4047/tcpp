# Dictionary expansion guide for agents

This project has three dictionary forms:

- `data/sources/` contains original source files. Never edit these files.
- `data/dictionary/entries.v1.jsonl` is the canonical, versioned editor/export format.
- `data/schema/dictionary-v1.sql` is the matching PostgreSQL online-editor schema.
- `data/lexicon/moe_concise.tsv` is a retained MOE import artifact; do not expand it.
- `data/lexicon/dictionary.idx` is the generated runtime package. Never hand-edit it.

## Safe workflow

1. Check the source URL, exact version, attribution, and licence before importing data.
2. Preserve the original archive unchanged under `data/sources/`.
3. Convert it into dictionary-v1 JSONL. Each entry needs a stable `entry_id`, one
   or more pronunciations, senses with part of speech, ranking/rarity, variant
   links, explicit confusion-group links, and source IDs.

4. Validate every row: non-empty text, valid Taiwan Bopomofo keys, one syllable per
   Unicode character, positive finite ranking score, and no tabs or newlines inside
   fields.
5. Convert legacy imports, then build the binary index from the canonical JSONL:

   `python tools/convert_legacy_dictionary.py`
   `python tools/build_dictionary_index.py`

6. Run the portable and Windows tests before changing candidate ranking or UI logic.
7. Update `data/SOURCES.md` with the source version and redistribution conditions.

## Meaning of the fields

`input_keys` uses the project's standard Taiwan keyboard layout, including tone keys.
It is not Hanyu Pinyin. Use `bopomofo::display_reading()` or the existing MOE importer
to verify readings.

The current runtime model is a compiled projection of dictionary-v1: text, input keys,
ranking score, and definition. Do not put part-of-speech or confusion information into
the definition string; retain those fields in the canonical JSONL/database.

Definitions shown in the right-hand panel must be explicitly linked as a curated
confusion or misuse relationship. Do not show every homophone. A candidate should
produce the panel only when another candidate in the visible list belongs to the same
approved confusion group, such as `在/再`, `的/地/得`, or `權利/權力`.

## Ranking rules

Keep ranking deterministic. Prefer an observed corpus frequency when its licence
allows redistribution; otherwise label the value as a heuristic in `data/SOURCES.md`.
Do not claim that a dictionary-entry occurrence count is real-world usage frequency.
Keep rare, obsolete, regional, and variant forms available but rank them below common
forms unless the user explicitly typed an exact tone or selected them before.

`rarity: common` has runtime meaning: every published common entry is preloaded.
There is no numeric top-N cap. Mark entries `uncommon`, `rare`, or `obsolete` when
they should instead load from their phonetic bucket on demand.

## Licence and quality rules

Do not copy definitions from a website merely because they are visible online. Record
the source and licence, and confirm that transformed data may be redistributed. The
MOE Concise Dictionary currently retained in this workspace is CC BY-ND 3.0 Taiwan;
the maintainer has confirmed permission to distribute this project's derived release.
Preserve the required attribution and licence notice; do not silently mix it with
community-written definitions.

After import, report the input row count, accepted count, skipped count, output size,
source version, and test results. A generated index is disposable and reproducible;
the canonical JSONL and provenance record are the reviewable artifacts.

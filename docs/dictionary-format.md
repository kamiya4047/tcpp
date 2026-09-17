# Dictionary v1 and online editing

`data/dictionary/entries.v1.jsonl` is the canonical development snapshot. One
JSON object occupies each line. It matches the relational production schema in
`data/schema/dictionary-v1.sql` and can be imported into an online editor without
turning UI-only fields into text conventions.

An entry contains a stable `entry_id`, `text`, locale, status, pronunciations,
senses, ranking, variants, explicit confusion-group memberships, and source IDs.
The relevant fields are intentionally separate:

- A pronunciation owns Taiwan Bopomofo keyboard input keys. Entries can have more
  than one pronunciation.
- A sense owns a part of speech and definition. A word may have multiple senses.
- Ranking has a numeric `score` for decoder order and a human-readable `rarity`.
- Variants link two entry IDs and identify whether the relationship is an ordinary,
  regional, historical, or simplified variant.
- A confusion group is editorial metadata, not a phonetic lookup. The side panel is
  eligible only when two or more visible candidates share a published group.

## Collaboration lifecycle

The web service must store proposed patches in `edit_proposal`, based on a specific
entry revision. It must not modify `dictionary_entry` directly from a user form.
A reviewer accepts a proposal, records an immutable `entry_revision`, then publishes
a release. Releases export approved entries to dictionary-v1 JSONL and invoke:

```text
python tools/build_dictionary_index.py release-entries.v1.jsonl release.idx
```

The IME package uses only the generated release index. It never needs accounts,
network access, proposal data, or unreviewed definitions.

## Runtime loading

The index has a small table of first-reading buckets and an unlimited common set.
At startup the IME reads only those two pieces.
During decoding it reads and caches only the uncommon bucket(s) whose first Bopomofo
syllable exactly matches the current input or its valid chaining prefix. Every entry
editorially marked `common` is preloaded; there is no count cutoff. Rare fuzzy
families are not loaded during ordinary typing; the resident common set still supplies
typo-tolerant suggestions.
Definitions remain in those records but should only be displayed for published
confusion groups; they are not a trigger merely because two characters are homophones.

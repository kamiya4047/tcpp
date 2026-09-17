# Offline Bopomofo decoder

The portable implementation is `src/bopomofo/bopomofo.cpp`. `include/ime/bopomofo.h` is its caller-owned, thread-confined value interface. Immutable lexicon initialization is thread-safe. Decode never mutates the input. The first dictionary access can read an optional local index; subsequent decoding uses immutable memory. Memory allocation may throw; Windows adapters must catch exceptions at COM boundaries.

## Keyboard and parsing

The Taiwan standard keyboard maps physical lowercase ASCII keys to Bopomofo. `6`, `3`, `4`, and `7` terminate second, third, fourth, and neutral-tone syllables. Space terminates a first-tone syllable. Tone omission is accepted. An initial following an initial/medial/final starts another syllable; a repeated or backward phonetic category also starts another syllable. A partial final token is retained. Each token records its exact half-open source byte interval; tone keys belong to the preceding token. A neutral tone displays before its syllable.

Examples: `su3cl3` → ㄋㄧˇ ㄏㄠˇ → 你好; `sucl` also matches 你好; `sc` enables initial-only chaining. `5j/ jp6` → 中文. First-tone spaces are part of the reading, so raw input must not be trimmed.

Unknown bytes, misplaced/repeated tone markers, more than 128 raw bytes, or more than 32 syllables have an empty decoder interpretation. The orchestrator remains responsible for offering the literal raw candidate. Parsing is a bounded category parser; not all linguistically possible ambiguous syllable boundaries are represented. The phrase matcher constrains interpretations to dictionary readings.

## Phrase lattice and beam search

At every syllable position the decoder matches dictionary readings and creates phrase edges. The decoder accepts exact symbols and specified tones, omitted tones, and optional prefix/initial chaining. Optional fuzzy matching permits one phonetic confusion or adjacent physical-key substitution across the entire candidate. A supplied conflicting tone is never guessed away. Fuzzy prefix plus substitution, arbitrary edit distance, and deletion/insertion correction are intentionally absent to avoid uncontrolled false positives.

The decoder uses dynamic programming with a bounded beam (1–64 retained paths, default 32) and caps intermediate destinations at four times that size. It returns at most 50 candidates and defaults to 9. Each selected phrase edge becomes a segment with original raw keys, source offsets, full resolved reading, surface, and alternatives sharing that edge's boundaries. No keystrokes are discarded even when the displayed reading is completed or corrected.

The initial lexicon is compiled directly into the binary. Full phrase entries are supplemented with individual characters derived from those entries, allowing novel combinations of known characters. See `data/SOURCES.md` for provenance and limitations. New words can be added through the curated table; editorial entries carry numbered pinyin, a positive heuristic prior, and an optional explanation. Phrase text must have one Unicode scalar per syllable. Source spellings use `v` for ü in pinyin. All entries and notes are Traditional Chinese; common 臺/台 variants retain distinct candidates.

## Transformations and reverse lookup

Dictionary entries are grouped by their first syllable, so exact, prefix and fuzzy compatibility are checked once per distinct first reading before scanning its matching entries. Same-sound lookup reuses these parsed groups. Reverse lookup groups entries by their first text scalar.

The bundled canonical dictionary loads from `data/lexicon/dictionary.idx` relative to the containing Windows module (including the TSF DLL in a host process) or Linux executable. It never uses the checkout location or current directory. The release index keeps a small phonetic table plus every editorially `common` entry resident, then reads and caches only the Bopomofo buckets relevant to the keys being decoded. There is no TSV or compiled-curated runtime fallback. `decoder_cli --dictionary-status` reports the full diagnostic lexicon count, including derived character readings. MOE attribution and licence information are in `data/SOURCES.md`.

`display_reading` formats the parsed input; candidates carry resolved readings independently. `romanize` produces numbered Hanyu Pinyin, with `v` representing ü after n/l. Omitted tones remain omitted, and incomplete consonants remain incomplete. `reverse_reading` searches known phrase/character paths and chooses a frequency-weighted reading. It does not imply a unique reading for polyphonic text; callers must label committed-text readings as inferred. Unknown text returns no reading so reconversion can decline without modifying the host.

## Verification and limits

`tests/decoder_tests.cpp` checks keyboard mappings, all tone classes, malformed input, omissions, chaining, exact-before-fuzzy ordering, notes, Taiwanese variants, contextual particles, segment spans, reverse lookup, complete lexical validity, and deterministic randomized malformed streams. Corpus coverage is a starter baseline, not proof of production language accuracy. A large licensed language corpus, pronunciation auditing, broader ambiguity lattices, and measured host latency remain separate release work.

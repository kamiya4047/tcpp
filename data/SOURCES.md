# Data provenance and redistribution

The original curated starter entries were composed for this project. They have now been merged into the canonical dictionary export; they are not a separate runtime dictionary. Their short usage notes are original editorial examples. The individual words and standard pronunciation facts are not claims of ownership over the Chinese language.

| Asset | Source | License | Redistribution | Attribution | Model training |
| --- | --- | --- | --- | --- | --- |
| Curated phrases, readings, notes, and heuristic priors | Original project contribution, 2026 | MIT, same as project | Permitted | Preserve project MIT notice | Permitted |
| Derived single-character readings | Programmatically split from bundled original phrases | MIT | Permitted | Same notice | Permitted |
| Standard Taiwan keyboard mapping | Original implementation of the standard layout | MIT | Permitted | Same notice | Permitted |
| Decoder regression cases | Original test inputs and expected outputs | MIT | Permitted | Same notice | Permitted |

The workspace retains the Ministry of Education Concise Mandarin Dictionary source archive/workbook (`dict_concised_2014_20260626`) and a generated 45,029-row TSV. Its recorded source license is CC BY-ND 3.0 Taiwan; attribution is 中華民國教育部。《國語辭典簡編本》. The maintainer has confirmed distribution permission for this project's derived release; preserve the attribution and licence notice. See `sources/MOE_CONCISE_LICENSE.md` for the source URL and project record. This confirmation does not grant model-training permission.

`dictionary/entries.v1.jsonl` is the canonical merged dictionary for both the future collaborative editor and the shipped IME. It assigns deterministic IDs and records imported status, `unknown` part of speech, `unclassified` rarity, and empty variant/confusion links where the source does not provide editorial metadata. The original curated entries are merged in as published `common` entries, which have no count limit at runtime. The compiled `.idx` is generated only from this canonical v1 export.

No pretrained neural model is bundled. The frequency field is a heuristic ranking prior, **not an observed usage frequency statistic**: curated values are editorial, while imported values derive from character occurrence counts within dictionary entries. The curated-only build is a small offline starter dictionary; neither mode establishes production accuracy.

Any future data import must record its exact version, URL, license text, attribution, redistribution permission, and training permission before merging. Review both source data and derived artifacts. No runtime download is needed or performed by the decoder.

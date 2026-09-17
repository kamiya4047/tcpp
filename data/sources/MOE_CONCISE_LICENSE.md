# Ministry of Education dictionary source

The original source archive and workbook in this directory are the Taiwan Ministry of Education's Concise Mandarin Dictionary export:

- Version: `dict_concised_2014_20260626`
- Source: https://language.moe.gov.tw/001/Upload/Files/site_content/M0001/respub/dict_concised_download.html
- License: Creative Commons Attribution-NoDerivs 3.0 Taiwan (CC BY-ND 3.0 TW)
- Attribution: 中華民國教育部（Ministry of Education, R.O.C.）。《國語辭典簡編本》

The archive and workbook are retained unchanged. `data/lexicon/moe_concise.tsv` is a reproducible import artifact, while `data/dictionary/entries.v1.jsonl` is the canonical merged project export. The maintainer confirmed on 2026-09-14 that this project may distribute its derived dictionary release. Preserve the MOE attribution and CC BY-ND 3.0 TW notice with every release; this record does not grant permission for unrelated reuse or model training.

The IME ships `data/lexicon/dictionary.idx`, generated from the canonical export. It does not load a TSV, a source-tree fallback, or any data from the network.

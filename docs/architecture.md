# Architecture

The Windows TSF COM DLL translates input events and edit sessions to the same C++23 engine used by the native playground and deterministic decoder CLI. The engine has no Windows dependencies. UTF-8 is used for raw key storage and files; Unicode scalar strings are used for surfaces. Windows adapters convert UTF-16 only at the boundary.

The composition state retains the original keys separately from reading, candidates, and converted segments. Each segment has a half-open byte range into the original keys, its own reading and candidate selection. Escape first restores composition and then cancels; commit is the point where transient phonetic provenance is released. F6–F10 transform only the active segment. Left/Right move between segments, while Shift+Left/Right resize at phonetic token boundaries.

The active candidate list combines alternatives for every forward span, longest phrase first and then shorter phrases/individual characters. Selecting a shorter-span row shifts the removed reading into the following segment and decodes that new segment immediately (`[苦茶][葉]` -> `苦[茶葉]`). Moving back to the preceding segment exposes the longer-span row again, allowing the split to be reverted. Candidate UI starts with a compact nine-row page and can expand to the complete scrollable list (up to 50 rows). Choosing a numbered or clicked candidate advances to the next remaining segment. Once converted, Space cycles candidates for the active segment.

Candidate orchestration places exact phonetic interpretations ahead of fuzzy paths. Strong explicit utility triggers occupy a separate band; weak symbol associations remain below language candidates. A raw candidate occupies a stable final position even when candidate count is limited. Deduplication never removes that raw candidate. Deterministic provider values are not rewritten by language models.

The initial decoder uses bounded beam search over an immutable local lexicon. Worker/network/model facilities must publish a matching generation number before they can update a live composition; current local generation is synchronous and thread-confined. No clipboard text or full composition is logged. Settings are versioned UTF-8 and replaced atomically. Invalid settings return conservative defaults.

The native UI and TSF adapter own handles and COM references. An unsuccessful edit session must not consume a key. Registration and host tests are separate from portable tests: an executable loading the DLL proves binary integrity, not real host compatibility.

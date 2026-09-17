# PLAN.md — Windows Traditional Chinese Bopomofo IME

## Agent handoff rule

Before taking over work, every agent must read [`PROGRESS.md`](PROGRESS.md). Before ending a session, the agent must update that file with the current local timestamp, model name, session/thread ID, files or milestones completed, exact build and test evidence, unresolved failures, permission or environment blockers, and the next concrete commands. A progress report must never claim a quality gate passed without observed evidence. If a build or test is interrupted, record the interruption and leave the repository in a recoverable state so the next agent can continue.

## 0. Purpose

Build a production-quality Windows Traditional Chinese Bopomofo IME with Japanese-IME-style conversion ergonomics, strong Traditional Chinese/Taiwan language behavior, editable conversion segments, chaining/abbreviated Bopomofo input, typo tolerance, local neural ranking, English/raw fallback, and lower-priority utility conversion providers.

The IME must feel like a native Windows input method first. Extra conversion features are secondary and must never crowd out normal Chinese input unless the input strongly indicates the corresponding provider.

Primary product principle:

> Windows Japanese IME ergonomics + Taiwan Traditional Chinese intelligence + a unified, confidence-aware candidate system.

Secondary product principles:

- Typing must remain fast, predictable, and recoverable.
- Smart behavior may guess, but literal user input must never be lost.
- Normal Bopomofo conversion always outranks unrelated utility providers.
- Deterministic providers must remain deterministic; neural models must not reinterpret exact arithmetic, dates, Unicode code points, raw input, or clipboard text.
- Ordinary typing must work fully offline.
- Internet features are explicitly opt-in and strictly Taiwan-focused.
- Converted text should remain editable for as long as practical, using segment-based reconversion similar to the Windows Japanese IME.

---

# 1. Agent Operating Instructions

## 1.1 Startup strategy

Use subagents immediately to reduce startup time and parallelize independent investigation and implementation.

The lead agent owns architecture, integration, merge decisions, interface contracts, and milestone acceptance.

Recommended startup subagents:

1. **TSF/Windows Integration Agent**
   - Research and implement Text Services Framework integration.
   - Own COM registration, text service lifecycle, key event hooks, composition ranges, candidate positioning, reconversion integration, DPI behavior, accessibility, and packaging constraints.

2. **Bopomofo/Decoder Agent**
   - Own phonetic parsing, syllable representation, keyboard mapping, chaining, incomplete syllables, typo/fuzzy matching, phonetic lattice generation, segmentation, candidate generation, and beam search.

3. **Language/Data Agent**
   - Own Traditional Chinese/Taiwan lexicon ingestion, phrase dictionary, reading data, confusable-word metadata, symbol aliases, user dictionary schema, language-model corpus pipeline, and data licensing inventory.

4. **UI/UX Agent**
   - Reproduce Windows/Japanese-IME interaction patterns using native Windows primitives.
   - Own candidate window, segment highlighting, keyboard navigation, optional detail panel, settings app UX, theme behavior, dark/light/high-contrast support, and DPI/accessibility.

5. **Providers Agent**
   - Own calculator, date/time, ROC year conversion, Unicode/symbol/emoji, email-domain completion, typography conversion, and smart paste providers.

6. **ML/Ranking Agent**
   - Own baseline statistical ranking, local neural reranking, model interface, inference benchmarks, quantization, candidate scoring, personalization hooks, and fallback behavior.

7. **QA/Automation Agent**
   - Own test harnesses, golden tests, fuzz tests, regression corpus, host compatibility matrix, performance benchmarks, CI, installer smoke tests, and release checklist.

8. **Taiwan Internet Agent**
   - Only begins after local IME milestone is stable.
   - Own opt-in network provider, Taiwan location validation, weather/postal data adapters, caching, privacy boundaries, and failure behavior.

## 1.2 Rules for all subagents

- Do not independently redesign public interfaces after another agent begins depending on them. Propose interface changes to the lead agent first.
- Prefer small vertical slices over giant speculative implementations.
- Every new module must have tests before it is considered complete.
- Do not introduce a dependency unless it has a clear need, acceptable license, and measurable benefit.
- Never require internet access for normal Bopomofo input.
- Never discard raw keystrokes while a composition is active.
- Every heuristic must have a documented fallback path.
- Every user-visible smart feature must be disableable if it can interfere with ordinary typing.
- Extra providers must expose explicit trigger/confidence values so the central ranker can keep them below Chinese input by default.

---

# 2. Product Scope

## 2.1 Core features

### Windows IME foundation

- Windows 10/11 desktop support, with Windows 11 as primary visual target.
- C++23.
- TSF/COM text service.
- Native candidate window.
- Native settings application.
- Per-monitor DPI awareness.
- Dark/light/high-contrast support.
- Keyboard-first operation.
- No network dependency for ordinary typing.

### Bopomofo input

- Standard Taiwan Bopomofo keyboard layout first.
- Architecture allows future alternate layouts.
- Tone support.
- Tone omission.
- Incomplete syllables.
- Initial-only chaining.
- Common phonetic confusions.
- Keyboard-adjacent typo modeling.
- Phrase-level conversion.
- Context-aware candidate ranking.

### Japanese-IME-style editable conversion

- Composition -> converted segments -> final commit.
- Converted segments remain editable.
- Move active segment left/right.
- Space opens/cycles candidates for the active segment.
- Reopen candidates after conversion.
- Resize segment boundaries.
- Preserve the phonetic reading behind each converted segment.
- Support soft reconversion while phonetic provenance is retained.
- Add hard reconversion from selected committed text where host/TSF behavior permits.

### Mixed English/raw input

- Bopomofo mode remains active while Latin input may be entered.
- Preserve exact physical keystrokes.
- Detect repeatedly poor Bopomofo interpretations.
- Raise English interpretation only after sufficient confidence.
- Raw literal input is always available as a candidate.
- English spelling/casing suggestions must never remove raw input.
- Context-sensitive Chinese/English punctuation.

### Confusable Chinese dictionary

Examples:

- 的 / 得 / 地
- 什麼 / 甚麼
- Same-pronunciation distinctions.
- Variant forms.
- Commonly confused characters.
- Taiwan-preferred or conventional usage notes.
- Formal/colloquial notes where useful.
- Short examples.
- Optional character metadata such as reading, radical, strokes, and Unicode code point.

### Utility conversion providers

All utility providers are lower priority than ordinary Chinese input unless strongly triggered.

Providers:

- Calculator.
- Chinese number conversion.
- Financial Chinese numerals.
- Date/time conversion.
- Gregorian <-> ROC/Minguo year conversion.
- Natural relative date terms such as 今年 / 去年 / 明年.
- Unicode lookup.
- Symbol search.
- Emoji search.
- Semantic aliases such as 右 -> → ➡ ➔ ⇛ ☞ ▶.
- Email/domain completion after @.
- Typography conversion.
- Full-width/half-width conversion.
- Smart paste.

### Conversion hotkeys

Provide Japanese-IME-inspired F-key transformations. Defaults are provisional and must be remappable.

Suggested initial mapping:

- F6: normal Chinese conversion/current converted surface.
- F7: raw Bopomofo for the active segment.
- F8: romanized reading.
- F9: full-width Latin/raw representation.
- F10: half-width Latin/raw representation.

Behavior:

- Apply to the active segment when possible.
- Repeated press may cycle related forms.
- Allow disabling/remapping all hotkeys.

### Smart paste

Optional Ctrl+V interception.

Candidate actions:

- Paste original.
- Paste plain text/no style.
- Paste normalized text.
- Convert full-width/half-width.
- Convert punctuation/typography.
- More transformations.

Requirements:

- Original untouched clipboard content must always be available.
- Smart paste must be optional.
- Do not build clipboard history.
- Do not retain clipboard contents after the operation unless strictly required by OS APIs.

### Taiwan-only internet provider

Explicit master toggle.

When off:

- No network access from the internet provider.
- Core IME remains fully functional.

When on:

- Only respond to recognized Taiwan locations/entities.
- Potential results: Taiwan weather, postal codes, and future Taiwan public information.
- Validate location against local Taiwan place data before network access where practical.
- Handle unavailable services silently and never block typing.

---

# 3. Explicit Non-Goals for Initial Releases

- No cloud LLM required for ordinary input.
- No chatbot inside the IME.
- No general web search provider.
- No arbitrary global weather/location lookup outside Taiwan.
- No clipboard manager/history.
- No password manager behavior.
- No automatic rewriting of committed paragraphs without explicit user action.
- No Material-style candidate UI in v1; native Windows/Japanese-IME-like ergonomics come first.
- No massive feature expansion until normal Chinese conversion quality is strong.

---

# 4. Technical Architecture

## 4.1 High-level architecture

```text
Host application
    |
    v
Windows TSF adapter
    |
    +--------------------------+
    |                          |
    v                          v
Composition/Segment Manager   Candidate UI
    |
    v
Candidate Orchestrator
    |
    +-----------+-------------+--------------+----------------+
    |           |             |              |                |
    v           v             v              v                v
Bopomofo    English/Raw   Dictionary     Local Utility    Taiwan Internet
Provider    Provider      Provider       Providers        Provider
    |
    v
Phonetic Lattice
    |
    v
Phrase/Lexicon Search
    |
    v
Beam Decoder
    |
    +-------> Statistical/Neural Language Model
    |
    v
Segmented Chinese candidates
```

## 4.2 Core rule: preserve representations

For every active composition, retain as much of the following chain as practical:

```text
physical keystrokes
    -> raw text
    -> parsed Bopomofo tokens
    -> phonetic lattice
    -> candidate paths
    -> converted segments
    -> final committed text
```

Do not collapse these representations prematurely.

This enables:

- Raw fallback.
- English fallback.
- F7 Bopomofo output.
- Romanization output.
- Reconversion.
- Segment boundary editing.
- Error recovery.
- Learning correction.

---

# 5. Repository Layout

Use a monorepo.

```text
/
├─ PLAN.md
├─ README.md
├─ LICENSE
├─ CMakeLists.txt
├─ CMakePresets.json
├─ .clang-format
├─ .clang-tidy
├─ .editorconfig
├─ .gitignore
├─ cmake/
│  ├─ CompilerWarnings.cmake
│  ├─ Sanitizers.cmake
│  ├─ Dependencies.cmake
│  └─ Packaging.cmake
│
├─ src/
│  ├─ core/
│  │  ├─ include/ime/core/
│  │  │  ├─ input_context.h
│  │  │  ├─ candidate.h
│  │  │  ├─ candidate_kind.h
│  │  │  ├─ provider.h
│  │  │  ├─ score.h
│  │  │  └─ result.h
│  │  └─ src/
│  │
│  ├─ bopomofo/
│  │  ├─ include/ime/bopomofo/
│  │  │  ├─ keyboard_layout.h
│  │  │  ├─ phonetic_token.h
│  │  │  ├─ parser.h
│  │  │  ├─ lattice.h
│  │  │  ├─ typo_model.h
│  │  │  ├─ lexicon.h
│  │  │  ├─ decoder.h
│  │  │  └─ segmenter.h
│  │  └─ src/
│  │
│  ├─ language/
│  │  ├─ include/ime/language/
│  │  │  ├─ language_model.h
│  │  │  ├─ ngram_model.h
│  │  │  ├─ neural_model.h
│  │  │  ├─ user_model.h
│  │  │  └─ reranker.h
│  │  └─ src/
│  │
│  ├─ composition/
│  │  ├─ include/ime/composition/
│  │  │  ├─ composition_state.h
│  │  │  ├─ segment.h
│  │  │  ├─ segment_manager.h
│  │  │  ├─ reconversion.h
│  │  │  └─ hotkey_transform.h
│  │  └─ src/
│  │
│  ├─ providers/
│  │  ├─ calculator/
│  │  ├─ datetime/
│  │  ├─ roc_calendar/
│  │  ├─ numbers/
│  │  ├─ english/
│  │  ├─ raw_input/
│  │  ├─ dictionary/
│  │  ├─ unicode/
│  │  ├─ symbols/
│  │  ├─ emoji/
│  │  ├─ email/
│  │  ├─ typography/
│  │  ├─ smart_paste/
│  │  └─ taiwan_internet/
│  │
│  ├─ candidate/
│  │  ├─ include/ime/candidate/
│  │  │  ├─ orchestrator.h
│  │  │  ├─ trigger.h
│  │  │  ├─ confidence.h
│  │  │  ├─ merge_policy.h
│  │  │  └─ ranker.h
│  │  └─ src/
│  │
│  ├─ windows_tsf/
│  │  ├─ include/ime/windows/
│  │  │  ├─ text_service.h
│  │  │  ├─ key_event_sink.h
│  │  │  ├─ composition_adapter.h
│  │  │  ├─ reconversion_adapter.h
│  │  │  ├─ candidate_window_host.h
│  │  │  ├─ registration.h
│  │  │  └─ dpi.h
│  │  └─ src/
│  │
│  ├─ candidate_ui/
│  │  ├─ include/ime/ui/
│  │  │  ├─ candidate_window.h
│  │  │  ├─ candidate_view_model.h
│  │  │  ├─ theme.h
│  │  │  ├─ accessibility.h
│  │  │  └─ layout.h
│  │  └─ src/
│  │
│  ├─ settings/
│  │  ├─ app/
│  │  └─ settings_store/
│  │
│  └─ devtools/
│     ├─ playground/
│     ├─ decoder_cli/
│     ├─ lexicon_inspector/
│     └─ benchmark/
│
├─ data/
│  ├─ schemas/
│  ├─ keyboard/
│  ├─ lexicon/
│  ├─ phrases/
│  ├─ confusables/
│  ├─ symbols/
│  ├─ emoji/
│  ├─ taiwan_places/
│  └─ test_corpora/
│
├─ model/
│  ├─ training/
│  ├─ conversion/
│  ├─ evaluation/
│  └─ README.md
│
├─ tests/
│  ├─ unit/
│  ├─ integration/
│  ├─ golden/
│  ├─ fuzz/
│  ├─ performance/
│  ├─ compatibility/
│  └─ fixtures/
│
├─ tools/
│  ├─ build_lexicon/
│  ├─ build_symbols/
│  ├─ corpus_tools/
│  ├─ model_tools/
│  ├─ installer_tools/
│  └─ release/
│
├─ packaging/
│  ├─ installer/
│  ├─ signing/
│  └─ assets/
│
└─ docs/
   ├─ architecture.md
   ├─ tsf.md
   ├─ decoder.md
   ├─ ranking.md
   ├─ providers.md
   ├─ privacy.md
   ├─ testing.md
   └─ release.md
```

---

# 6. Coding Style

## 6.1 Language and standard

- C++23 where toolchain support is stable.
- Use the latest broadly available MSVC toolchain supported by the chosen Windows SDK.
- Avoid compiler-specific extensions unless required for Windows APIs.
- UTF-8 source files.

## 6.2 General style

- Prefer small, explicit interfaces.
- Prefer value types over shared mutable state.
- Prefer RAII for all OS handles and COM lifetimes.
- Prefer `std::unique_ptr` for ownership.
- Use `std::shared_ptr` only for genuinely shared lifetimes.
- Avoid global mutable state.
- Avoid singleton service locators.
- Use dependency injection through constructors/interfaces where practical.
- Keep Windows/COM types at adapter boundaries; core language modules should remain platform-agnostic.
- No exceptions across COM/ABI boundaries.
- Convert internal errors to HRESULT or typed result objects at Windows boundaries.
- Prefer `std::expected<T, E>` where toolchain support is adequate; otherwise use a small project `Result<T, E>` type.
- Use `std::span`, `std::string_view`, and views for non-owning data.
- Use immutable data for candidate results after publication.

## 6.3 Naming

- Namespaces: `ime::bopomofo`, `ime::candidate`, `ime::windows`, etc.
- Types: `PascalCase`.
- Functions/methods: `snake_case` or `PascalCase` only if required by COM/Windows interface conventions. Pick one project convention and enforce it; recommended: `snake_case` for project code.
- Variables: `snake_case`.
- Constants: `kPascalCase`.
- Private members: trailing underscore, e.g. `segments_`.
- COM overrides keep API-prescribed names.

## 6.4 Header policy

- One primary public type per header where practical.
- Public headers under `include/ime/...`.
- Private implementation headers stay beside source and are not exported.
- Minimize Windows headers in portable modules.
- Prefer forward declarations where safe.

## 6.5 Comments and documentation

Comments must explain why, not restate the code.

Required documentation for:

- Non-obvious ranking heuristics.
- Phonetic penalties.
- Provider trigger thresholds.
- TSF workarounds.
- Host compatibility workarounds.
- Model assumptions.
- Privacy-sensitive code.

Every public interface must have a short contract comment describing:

- Ownership.
- Threading.
- Error behavior.
- Latency expectations if relevant.

## 6.6 Logging

- Structured logging.
- Compile/runtime levels: error, warn, info, debug, trace.
- Never log clipboard contents by default.
- Never log full user compositions in release builds unless the user explicitly enables diagnostic logging.
- Redact or hash sensitive diagnostic fields where practical.
- Log provider timing and result counts for performance diagnostics without content.

## 6.7 Formatting/static analysis

- Enforce `.clang-format` in CI.
- Run `clang-tidy` on project code.
- Enable high warning levels in MSVC.
- Treat project warnings as errors in CI, excluding audited external/generated code.

---

# 7. Core Data Model

## 7.1 Candidate

Suggested shape:

```cpp
struct Candidate {
    CandidateId id;
    CandidateKind kind;
    std::u32string text;
    std::optional<std::u32string> reading;
    std::optional<CandidateExplanation> explanation;
    ProviderId provider;
    float provider_confidence;
    float trigger_confidence;
    float relevance_score;
    float language_score;
    float final_score;
    CandidateFlags flags;
};
```

Candidate kinds should include at least:

- Chinese
- English
- RawInput
- Correction
- Dictionary
- Symbol
- Emoji
- Calculation
- NumberConversion
- DateTime
- RocCalendar
- Unicode
- Email
- Typography
- Clipboard
- Internet

## 7.2 Phonetic token

```cpp
struct PhoneticToken {
    std::optional<Initial> initial;
    std::optional<Medial> medial;
    std::optional<Final> final;
    std::optional<Tone> tone;
    bool abbreviated;
    SourceKeyRange source_keys;
};
```

## 7.3 Segment

```cpp
struct Segment {
    TextRange surface_range;
    std::u32string surface;
    PhoneticSequence reading;
    std::vector<Candidate> candidates;
    std::size_t selected_candidate;
    SegmentState state;
};
```

## 7.4 Composition state

Track:

- Raw keystrokes.
- Parsed text.
- Phonetic tokens.
- Active candidate list.
- Segments.
- Active segment index.
- Cursor/caret.
- Conversion mode.
- English/raw confidence.
- Undo learning token.
- Host context needed for TSF commit/reconversion.

---

# 8. Candidate Ranking Policy

## 8.1 Provider priority principle

Normal Chinese input dominates unless another provider is strongly triggered.

Base priority order:

1. Bopomofo/Chinese exact and near-exact conversion.
2. English/raw fallback when input shape supports it.
3. Linguistic dictionary/correction helpers.
4. Utility providers with strong triggers.
5. Weakly related utility suggestions.
6. Internet suggestions.

## 8.2 Trust classes

Classify results into three trust levels.

### Deterministic

- Calculator.
- Gregorian/ROC calendar conversion.
- Unicode code-point lookup.
- Raw input.
- Clipboard original/plain text.
- Exact typography transformations.

### Dictionary/constrained

- Bopomofo lexicon lookup.
- English dictionary/casing.
- Confusable Chinese words.
- Symbol aliases.

### Predictive

- Neural ranking.
- Initial-only expansion.
- Typo recovery.
- Contextual phrase prediction.

Predictive candidates must never overwrite deterministic truth.

## 8.3 Trigger examples

- `1+1`: calculator trigger extremely high.
- `2026`: ROC date conversion medium/high.
- `民國115年`: Gregorian conversion extremely high.
- `@`: email provider extremely high.
- `U+2192`: Unicode provider extremely high.
- `右`: Chinese provider high, symbol provider medium.
- `右箭頭`: symbol provider high.
- Recognized Taiwan place + internet enabled: Taiwan internet provider medium/high.

## 8.4 Confidence behavior

- High confidence: candidate may become #1.
- Medium confidence: candidate may appear prominently.
- Low confidence: candidate appears below core language results.
- Very low confidence: candidate is omitted.

---

# 9. Bopomofo Decoder Design

## 9.1 Parsing stages

1. Capture physical key events.
2. Map through active Bopomofo keyboard layout.
3. Construct complete or partial phonetic tokens.
4. Generate typo/confusion alternatives with penalties.
5. Build phonetic lattice.
6. Search lexicon/phrase paths compatible with lattice.
7. Apply language model score.
8. Beam prune.
9. Segment best paths.
10. Publish top candidates and alternatives.

## 9.2 Error model

Keep error penalties explicit and data-driven.

Example relative ordering:

- Exact match: 0.
- Omitted tone: very small penalty.
- Tone confusion: small penalty.
- Initial-only abbreviation: small/medium penalty.
- Known phonetic confusion: medium penalty.
- Adjacent-key typo: medium penalty.
- Missing medial/final: medium/high penalty.
- Unrelated phoneme: very high penalty.

Do not hardcode final numbers until benchmark data exists.

## 9.3 Beam search

Beam search should combine:

```text
final_path_score =
    phonetic_match_score
  + phrase_frequency_score
  + language_model_score
  + user_model_score
  - segmentation_penalty
  - typo_penalty
  - abbreviation_penalty
```

Requirements:

- Deterministic given same model/data/settings.
- Beam width configurable for benchmarks.
- Safe hard latency budget.
- Fall back to dictionary-only ranking if neural inference fails or is too slow.

---

# 10. Language Model Plan

## 10.1 Phase 1

Start without neural dependence.

Implement:

- Phrase frequency.
- Unigram/bigram/trigram or equivalent statistical scoring.
- User selection weights.

This validates the decoder independently of ML.

## 10.2 Phase 2

Add a small local Transformer/neural reranker.

Purpose:

- Score/rerank linguistically plausible candidate paths.
- Improve long-context phrase selection.
- Improve chaining interpretation.

Do not use it for:

- Arithmetic.
- Exact date conversion.
- Unicode parsing.
- Raw clipboard handling.
- Exact user-input preservation.

## 10.3 Neural architecture constraints

- Local inference only for core typing.
- Quantizable.
- CPU-first baseline.
- Optional hardware acceleration later.
- Fast warm startup.
- Bounded memory.
- Hard timeout/fallback.
- No model call may block the UI indefinitely.

## 10.4 Model API

```cpp
class ILanguageModel {
public:
    virtual ~ILanguageModel() = default;
    virtual LanguageScores score(const LanguageRequest&) = 0;
};
```

Implementations:

- `NGramLanguageModel`
- `NeuralLanguageModel`
- `HybridLanguageModel`

---

# 11. Editable Conversion and Reconversion

## 11.1 States

```text
Raw/Typing
    -> Composing
    -> ConvertedSegments
    -> FinalCommit
```

ConvertedSegments must not be treated as irreversible text.

## 11.2 Keyboard behavior

Target behavior should feel familiar to Windows Japanese IME users.

- Space: convert/open candidates/cycle candidate for active segment.
- Enter: confirm current conversion/commit according to state.
- Esc: back out one level.
- Left/Right or configurable shortcuts: move segment/caret.
- Boundary-adjust keys: resize active conversion segment.
- Number keys: direct candidate selection when candidate window is open.
- F-key transforms: apply to active segment.

Exact keybindings must be validated against Windows behavior and conflicts before finalizing.

## 11.3 Soft reconversion

While provenance exists:

- Select active segment.
- Space reopens candidates based on stored reading.
- Candidate changes replace only that segment.

## 11.4 Hard reconversion

For already committed text:

- User selects text.
- Invoke reconversion command where host/TSF supports it.
- Reverse dictionary derives likely readings.
- Generate candidates with lower confidence.
- Never pretend inferred reading is exact.

---

# 12. English and Raw Fallback

Always retain literal physical input.

Track at least:

- Bopomofo confidence.
- English/Latin confidence.
- Consecutive poor-Bopomofo evidence.

Do not use a rigid “after N misses always switch” rule. Use N as one signal.

Expected behavior:

```text
f -> probably remain Bopomofo/raw ambiguous
fa -> still ambiguous
fac -> English likelihood rises
face -> English likely
facebook -> English dominant
```

Candidate ordering may become:

1. `Facebook`
2. `facebook`
3. Chinese interpretation if plausible
4. exact raw keystrokes

Raw keystrokes must never disappear.

---

# 13. Dictionary and Explanation System

Store explanations as curated local data, not generated free-form by the neural model.

Schema should support:

- Term.
- Reading.
- Short meaning.
- Usage type.
- Example.
- Confusable group.
- Variant relation.
- Taiwan convention note.
- Formality note.
- Character metadata.

The language model may rank or recommend among known entries, but explanations come from curated data.

UI rule:

- Normal candidate list stays compact.
- Show explanation only for known confusable groups, explicit info action, or focused candidate pause/selection.

---

# 14. Utility Providers

All providers implement a common contract.

```cpp
class IConversionProvider {
public:
    virtual ~IConversionProvider() = default;
    virtual ProviderMatch match(const InputContext&) const = 0;
    virtual ProviderResult query(const InputContext&) = 0;
};
```

Each provider returns:

- Trigger strength.
- Confidence.
- Candidates.
- Cost/latency estimate if useful.
- Whether it is deterministic.

## 14.1 Calculator

- Safe expression parser.
- No `eval`-style execution.
- Basic arithmetic first.
- Optional future scientific functions.
- Candidate variants may include Arabic and Chinese numerals.

## 14.2 Date/time

- Current date/time aliases such as 現在.
- Multiple configurable formats.
- ISO 8601.
- Locale-friendly Taiwan formats.
- ROC/Minguo conversion.
- Relative terms.

## 14.3 ROC/Minguo conversion

For years after 1911:

```text
ROC year = Gregorian year - 1911
Gregorian year = ROC year + 1911
```

Support full dates and year-only input.

## 14.4 Unicode/symbol/emoji

Support:

- Semantic Chinese aliases.
- Unicode character names where useful.
- `U+XXXX` lookup.
- Category browsing through candidate expansion.
- User-favorite ordering later.

Example:

```text
右 -> 右, 右邊, →, ➡, ➔, ⇛, ☞, ▶ ...
```

Chinese candidates remain higher unless trigger becomes more symbol-specific, such as `右箭頭`.

## 14.5 Email/domain completion

- `@` opens common domains.
- Partial domain completion.
- User-configurable custom domains.
- Never transmit typed addresses.

## 14.6 Typography

Support explicit transformations:

- Half-width/full-width.
- ASCII/Chinese punctuation where configured.
- Quotes -> 「」 / 『』.
- `...` -> `…`.
- dash variants.
- Unicode normalization.

Do not silently rewrite code-like input when English/code context is strong.

---

# 15. Smart Paste

Smart Paste is a provider over clipboard data, not a clipboard database.

Default disabled until stability is proven.

Potential behavior:

```text
Ctrl+V
  -> original paste
  -> plain text
  -> normalized text
  -> half-width
  -> full-width
  -> typography conversion
  -> more conversions
```

Requirements:

- Preserve original clipboard candidate at top or a stable known position.
- Never mutate clipboard contents merely to preview transformations.
- Do not store history.
- Avoid logging pasted content.
- Fast fallback to ordinary paste if anything fails.
- Allow Ctrl+Shift+V or configurable shortcut for direct plain-text paste.

---

# 16. Candidate UI

## 16.1 Visual goal

Primary target: native Windows/Japanese-IME-like compact candidate UI.

Do not prioritize Material Expressive for v1.

Candidate UI should:

- Be compact.
- Use Windows typography and scaling.
- Respect light/dark/high-contrast.
- Position correctly near caret.
- Avoid stealing focus.
- Support keyboard-only usage.
- Support mouse/touch selection where practical.
- Expose accessibility information.
- Avoid visual noise from low-priority providers.

## 16.2 Candidate rows

A row may contain:

- Selection key/index.
- Candidate text.
- Optional reading.
- Optional provider/type icon or subtle label.
- Optional recommendation marker.

Explanations should appear in a secondary area only when relevant.

## 16.3 Candidate grouping

Do not visibly group every provider by default.

The merged ranked list should feel like one IME.

Possible expanded view may show categories later.

---

# 17. Settings Application

Use native Windows application technology appropriate for modern Windows settings UI. Keep it separate from the TSF DLL.

Settings categories:

- General.
- Bopomofo keyboard layout.
- Chaining aggressiveness.
- Typo tolerance.
- Candidate count/orientation.
- Explanations.
- English fallback.
- Punctuation behavior.
- User dictionary.
- Hotkeys.
- Date/time formats.
- Email domains.
- Smart Paste.
- Internet Access.
- Privacy.
- Diagnostics.
- Model/performance options if needed.

Settings updates should propagate without requiring full IME reinstall.

---

# 18. Development and Debugging Workflow

## 18.1 Keep the TSF layer thin

The TSF DLL should adapt Windows events to the portable engine.

Most behavior must be testable without installing the IME.

## 18.2 Required dev tools

### Playground

Interactive desktop app showing:

- Raw keys.
- Parsed Bopomofo.
- Lattice.
- Beam hypotheses.
- Candidate scores.
- Segments.
- Provider matches.
- Neural/statistical scores.

### Decoder CLI

Feed scripted input and print deterministic candidate output.

Useful for golden tests and corpus evaluation.

### Lexicon inspector

Query:

- Readings.
- Phrase entries.
- Frequencies.
- Confusable relationships.
- Symbol aliases.

### Benchmark tool

Measure:

- Parse latency.
- Lexicon search latency.
- Beam decode latency.
- Model latency.
- End-to-end candidate latency.
- Memory use.

## 18.3 Actual TSF debugging

- Register debug IME build.
- Start with Notepad as reference host.
- Attach Visual Studio debugger to host process.
- Expand compatibility testing only after core flow is stable.

---

# 19. Performance Targets

Typing must feel immediate.

Initial targets; refine with real measurements:

- Key-event handling excluding model inference: target single-digit milliseconds.
- Candidate update common path: ideally < 20 ms.
- Neural reranking warm path: target low tens of milliseconds on typical supported hardware.
- Hard fallback threshold: never let a slow neural path stall text input.
- Candidate window render/update: no visible hitching.
- Settings or internet providers must never block TSF key processing.

Use async work only where safe; candidate state publication must be race-safe and versioned so stale results cannot overwrite newer input.

---

# 20. Threading Model

- TSF/UI-facing operations stay on the appropriate Windows thread/apartment.
- Heavy lexicon/model work may use worker threads.
- Every asynchronous request receives a composition generation/version ID.
- Results are discarded if the composition changed before completion.
- No blocking network calls in the TSF input path.
- No long model warmup during a keystroke; warm lazily ahead of need or use fallback.

---

# 21. Privacy and Security

## 21.1 Local by default

Keep local:

- Bopomofo input.
- Candidate ranking.
- User dictionary.
- Personal learning.
- Calculator.
- Dates.
- Symbols.
- Unicode.
- English fallback.
- Smart paste transformations.

## 21.2 Internet toggle

Internet access is explicit.

When disabled:

- Internet provider must not issue requests.
- No silent telemetry from the internet provider.

## 21.3 Sensitive inputs

Design an option/heuristic to disable learning and smart prediction in password/security-sensitive fields where TSF/application metadata permits.

Never log:

- Password text.
- Clipboard content.
- Full compositions in standard release logs.

## 21.4 Network behavior

- Taiwan-only provider scope.
- Minimal request data.
- Clear cache policy.
- No remote language-model dependency for core typing.

---

# 22. Data and Licensing

Before importing any dictionary/corpus/model data:

- Record source.
- Record license.
- Record redistribution permission.
- Record required attribution.
- Record whether derivative model training is allowed.

Create `data/SOURCES.md` before first release.

Do not assume online dictionaries can be bundled.

---

# 23. Testing Strategy

## 23.1 Unit tests

Cover:

- Keyboard mapping.
- Bopomofo parser.
- Tone parsing.
- Partial tokens.
- Typo alternatives.
- Lattice construction.
- Lexicon lookup.
- Beam search.
- Segment manager.
- Candidate merge/ranking.
- Every deterministic provider.
- ROC calendar boundaries.
- Unicode parsing.
- Smart paste transformations.

## 23.2 Golden tests

Maintain a corpus of input -> expected candidate order.

Categories:

- Common Taiwan phrases.
- Ambiguous homophones.
- Chaining.
- Missing tones.
- Common typos.
- Mixed Chinese/English.
- Confusable particles.
- Names/places.
- Utility triggers.

Golden tests should allow intentional ranking updates through reviewed snapshots.

## 23.3 Fuzz testing

Fuzz:

- Raw key streams.
- Malformed Unicode.
- Partial composition edits.
- Segment boundary operations.
- Provider parsers.
- Arithmetic parser.
- Date parser.

No input sequence should crash the IME host.

## 23.4 Host compatibility matrix

Minimum:

- Notepad.
- File Explorer text fields/search.
- Windows Search.
- Edge.
- Chrome.
- Firefox.
- Microsoft Word.
- Excel cells/formula bar where applicable.
- WPF app.
- WinUI app.
- Electron app.
- Legacy Win32 edit control.

Track:

- Composition rendering.
- Candidate positioning.
- Commit behavior.
- Reconversion.
- Ctrl+V/smart paste interaction.
- DPI.
- multi-monitor.

## 23.5 Performance tests

Regression gates for:

- Median and p95 key-to-candidate latency.
- Model warm/cold latency.
- Peak memory.
- Candidate UI frame/update latency.

---

# 24. CI/CD

CI stages:

1. Configure/build Debug and Release.
2. Format check.
3. Static analysis.
4. Unit tests.
5. Golden tests.
6. Fuzz smoke tests.
7. Performance smoke benchmark.
8. Package debug artifact.

Release pipeline additionally:

- Version stamping.
- Installer build.
- Signing.
- Clean VM install test.
- IME registration smoke test.
- Uninstall smoke test.
- Upgrade test from previous release.
- Generate release notes.

---

# 25. Milestones

## Milestone 0 — Repository/bootstrap

Deliverables:

- CMake project.
- Core libraries build.
- clang-format/clang-tidy.
- Unit test framework.
- Basic CI.
- `devtools/decoder_cli` skeleton.
- `devtools/playground` skeleton.

Exit criteria:

- Clean build on fresh developer machine/CI image.
- Tests run with one command.

## Milestone 1 — Minimal TSF IME

Deliverables:

- Registered TSF text service.
- Selectable input method.
- Key interception.
- Minimal composition text.
- Basic commit/cancel.
- Debug logging.

Exit criteria:

- Works reliably in Notepad.
- Can attach debugger to host.

## Milestone 2 — Basic Bopomofo

Deliverables:

- Taiwan standard layout.
- Syllable parser.
- Tone support.
- Small local lexicon.
- Basic Chinese candidates.
- Raw-input preservation.

Exit criteria:

- Common single-character and simple word input works.
- Golden tests established.

## Milestone 3 — Phrase conversion and segmentation

Deliverables:

- Phrase dictionary.
- Statistical language scoring.
- Beam decoder.
- Segment manager.
- Multi-syllable phrase conversion.

Exit criteria:

- Common phrases rank correctly at useful quality.
- Latency remains interactive.

## Milestone 4 — Japanese-style conversion UX

Deliverables:

- Converted segment state.
- Active segment navigation.
- Space candidate reopening.
- Segment boundary resizing.
- Soft reconversion.
- Candidate window with native behavior.

Exit criteria:

- Workflow feels coherent in Notepad and Word.
- No loss of phonetic provenance before final commit.

## Milestone 5 — Chaining and typo tolerance

Deliverables:

- Partial/initial-only tokens.
- Typo model.
- Fuzzy lattice paths.
- Confidence-aware ranking.

Exit criteria:

- Common abbreviated phrases work.
- Exact input still dominates.
- Fuzziness does not create excessive false positives.

## Milestone 6 — English/raw fallback

Deliverables:

- English confidence model.
- Consecutive mismatch evidence.
- English candidates.
- Raw fallback candidate.
- Context-sensitive punctuation.

Exit criteria:

- Common English terms can be typed without mode switching.
- Raw keystrokes always recoverable.

## Milestone 7 — Confusable dictionary

Deliverables:

- Curated confusable schema/data.
- Explanations.
- Candidate recommendation metadata.
- Optional detail panel.

Exit criteria:

- 的/得/地 and selected variant cases work well.
- Explanations are local and deterministic.

## Milestone 8 — Neural reranking

Deliverables:

- `ILanguageModel` abstraction finalized.
- Small local model.
- Quantized runtime.
- Reranking integration.
- Fallback to statistical model.

Exit criteria:

- Measurable candidate accuracy improvement on evaluation corpus.
- No unacceptable latency/memory regression.

## Milestone 9 — Conversion providers

Implement in this order:

1. Calculator.
2. ROC/Gregorian calendar.
3. Date/time formats.
4. Numbers/financial numerals.
5. Unicode/symbols/emoji.
6. Email domains.
7. Typography/full-width/half-width.

Exit criteria:

- Providers only rise when strongly triggered.
- Chinese input remains dominant for ordinary text.

## Milestone 10 — Hotkeys

Deliverables:

- F7 raw Bopomofo.
- Romanization conversion.
- Full-width/half-width Latin.
- Segment-scoped transformations.
- Hotkey settings/remapping.

Exit criteria:

- No major conflicts in compatibility matrix.

## Milestone 11 — Smart Paste

Deliverables:

- Optional Ctrl+V candidate menu.
- Original/plain/normalized/full-width/half-width options.
- Safe clipboard handling.

Exit criteria:

- Does not break ordinary paste when disabled.
- Graceful fallback across tested hosts.

## Milestone 12 — Settings and user dictionary

Deliverables:

- Settings app.
- Persistent settings schema/migration.
- User phrase registration.
- User ranking preferences.
- Undo learning.

Exit criteria:

- Settings update without reinstall.
- Corrupt settings recover safely.

## Milestone 13 — Taiwan internet provider

Deliverables:

- Master Internet Access toggle.
- Taiwan place validation.
- Postal lookup.
- Weather integration.
- Cache/failure policy.

Exit criteria:

- Zero requests when disabled.
- Internet failure never affects local typing.
- Non-Taiwan locations do not activate provider.

## Milestone 14 — Hard reconversion

Deliverables:

- Selected committed-text reconversion where supported.
- Reverse reading inference.
- Candidate confidence downgrade for inferred readings.

Exit criteria:

- Works in supported host matrix without corrupting surrounding text.

## Milestone 15 — Production hardening

Deliverables:

- Performance tuning.
- Accessibility audit.
- DPI/multi-monitor audit.
- Crash telemetry strategy if any, privacy reviewed.
- Installer/signing.
- Upgrade/uninstall reliability.
- Data/license audit.
- Documentation.

Exit criteria:

- Release candidate meets quality gates below.

---

# 26. Quality Gates

A release candidate is not ready unless:

- Normal Bopomofo input is more reliable than utility features are clever.
- Exact phonetic input strongly outranks fuzzy guesses.
- Raw keystrokes are recoverable during composition.
- Neural inference can be disabled/fail without breaking input.
- Candidate UI does not visibly lag during normal typing.
- Internet-off mode performs no internet-provider requests.
- Smart paste is optional and ordinary Ctrl+V remains available.
- No known host crashes in compatibility matrix.
- Installer and uninstall are repeatable.
- Required data licenses are documented.

---

# 27. Evaluation Metrics

Track over time:

## Language quality

- Top-1 accuracy.
- Top-3 accuracy.
- Mean reciprocal rank.
- Chaining accuracy.
- Typo-recovery precision/recall.
- False-positive fuzzy conversion rate.
- English fallback false-switch rate.

## UX

- Keystrokes to desired candidate.
- Candidate reopen success.
- Segment boundary correction success.
- Frequency of raw fallback usage.
- Utility provider accidental activation rate.

## Performance

- p50/p95/p99 candidate latency.
- model latency.
- memory footprint.
- startup/warmup cost.

---

# 28. First Implementation Order for Agents

The lead agent should start work in this exact order:

1. Bootstrap repo/build/tests.
2. Spawn TSF, Bopomofo, UI, QA subagents immediately.
3. Define shared `Candidate`, `InputContext`, `Provider`, and `CompositionState` interfaces before broad coding.
4. Build minimal TSF vertical slice in Notepad.
5. Build portable Bopomofo parser and decoder CLI in parallel.
6. Connect Bopomofo engine to TSF.
7. Add phrase conversion and segmentation.
8. Add Japanese-style reconversion behavior.
9. Add chaining/fuzzy input.
10. Add English/raw fallback.
11. Add confusable dictionary.
12. Add statistical baseline metrics.
13. Add neural reranker only after baseline metrics exist.
14. Add low-priority utility providers.
15. Add settings/hotkeys.
16. Add smart paste.
17. Add Taiwan internet provider last among major features.
18. Perform hardening, compatibility, installer/signing, and release work.

Do not begin with the neural model, pretty settings UI, or internet integrations. First prove that the TSF shell, Bopomofo engine, segmentation, and reconversion model are correct.

---

# 29. Decision Log Guidance

Maintain `docs/decisions/` with short ADR-style records for choices that are difficult to reverse, including:

- UI technology.
- TSF registration/installer strategy.
- Lexicon storage format.
- Model runtime.
- Settings persistence.
- User learning storage.
- Network/data providers.
- Candidate rank formula.

Each decision record should state:

- Context.
- Decision.
- Alternatives considered.
- Consequences.

---

# 30. Definition of Complete

The project is “complete” for the first stable release when a user can:

1. Install and select the IME on supported Windows systems.
2. Type standard Traditional Chinese Bopomofo reliably.
3. Use phrase conversion and context-aware candidate ranking.
4. Use abbreviated/chained Bopomofo for common phrases.
5. Recover from common Bopomofo mistakes.
6. Enter English without manually leaving Bopomofo mode.
7. Always recover raw keystrokes.
8. Edit converted text by segment in a Japanese-IME-like workflow.
9. Reopen candidates with Space and resize segment boundaries.
10. Use F7 or configured hotkey to output raw Bopomofo for an active segment.
11. Understand selected confusable Chinese words through concise local explanations.
12. Use calculator/date/ROC-year/Unicode/symbol/emoji/email/typography providers without them interfering with ordinary Chinese input.
13. Optionally use Smart Paste.
14. Optionally enable Taiwan-only internet results.
15. Use the IME across the supported host compatibility matrix with acceptable latency and no known data-loss issues.

The product should be judged primarily on the quality and responsiveness of Traditional Chinese input, not on the number of auxiliary providers implemented.

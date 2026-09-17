# Testing and acceptance

The automated suite exercises the portable engine independently of IME installation. A successful portable test run does **not** establish that Windows TSF works in a particular host, or that installer/signing/release gates have passed.

Configure and build the project using a C++23 toolchain, then run CTest from the build directory. Warnings-as-errors is enabled by default. On Windows, use the documented compiler preset or an explicit CMake generator appropriate to the installed toolchain.

```text
cmake -S . -B build -DIME_WARNINGS_AS_ERRORS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

`ime_tests` contains the following coverage:

- Decoder tests: standard keyboard layout, parsing, deterministic phrase lookup, and decoder-specific regressions maintained by the decoder module.
- Provider tests: safe arithmetic precedence and malformed expressions, 2000 seeded parser inputs, Gregorian leap rules, ROC boundaries and no year zero, injected relative dates, number grouping, Unicode scalar validation, domain validation, width/punctuation transforms, clipboard original preservation, stable result IDs, and sensitive-field suppression.
- Core tests: encoding round trips and malformed UTF-8, raw fallback, convert/cancel/commit, reconversion, segmentation, settings persistence and invalid values, and 300 seeded mixed composition/Unicode scenarios.
- QA regressions: selecting each displayed whole-composition candidate before conversion, preserving contiguous segment source spans across 200 seeded edits, raw recovery after 8 KiB input, UTF-8 backspace, Smart Paste refusal during active composition, clipboard original recovery after a hotkey, provider priority, candidate-count limits, and privacy changes removing stale utility/English candidates.

The arithmetic/parser fuzz cases are bounded deterministic smoke tests; they do not replace sanitizer builds or long-running coverage-guided fuzzing. A generated phrase lexicon or model requires separately measured quality, latency, and licensing acceptance. No external neural model accuracy improvement is claimed by these tests.

The Windows adapter suite also checks candidate popup sizing with synthetic candidates: compact single-character width, wider phrases, three nine-row columns without increasing height, second/third-column mouse selection, empty-cell rejection, stable width when selection moves, absence of footer space/hit targets, and screen-edge placement with 200 candidates. Number keys 1–9 apply to the selected column; all rows in that column show their numbers, while other columns leave the number gutter blank. PageUp/PageDown move between nine-candidate groups. Navigating beyond the first group expands the list; there is no bottom count, arrow, or expand control. Very large lists use screen-sized groups rather than drawing inaccessible columns off-screen. Manually check row alignment, definitions beside expanded columns, and mixed-DPI monitors in a real host; these automated checks are not a visual/accessibility acceptance pass.

Before a stable release, collect measured p50/p95/p99 latency and memory, run Debug/Release on supported toolchains, execute format/static analysis and sanitizer jobs, and complete the host matrix in [compatibility.md](compatibility.md). Retain the actual command output and toolchain version with the release evidence. Signing, clean VM installation, upgrade/uninstall, accessibility, dark/high-contrast, and multi-monitor checks require independent evidence.

Candidate definitions appear only in the separate same-sound side panel, not below the candidate list. The Windows layout regression checks that an entry's definition adds neither footer height nor a minimum menu width.

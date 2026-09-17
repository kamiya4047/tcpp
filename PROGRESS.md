# Handoff progress report

This file is the takeover point for the next agent. Read it before changing the repository, then update it at the end of every takeover session.

## Session metadata

- Recorded at: `2026-09-13T17:54:50.765319+08:00` (Asia/Taipei)
- Model: `GPT-5.6 Sol` (Zed coding agent)
- Session ID: unavailable in this client
- Workspace: `D:\GitHub\kamiya4047\tcpp`

## User objective

Implement `PLAN.md` with multiple subagents, compile with zero errors, verify usability, and report permission blockers.

## Work completed

- Fixed `ime_profile_probe` crashing with `0xC0000005` after printing successful registration: its COM smart pointer is now released before `CoUninitialize`. Added functional `--expect-absent` handling for unregistration verification. The currently registered Release profile now reports COM/profile/user enabled and exits 0 through both direct execution and `register_ime.ps1 -Action Status`.
- Added span-aware dynamic candidates and expandable candidate UI. The active list now combines whole-phrase, shorter-phrase, and individual-character alternatives (up to 50); the compact nine-row page expands to a scrollable full list. Selecting a shorter-span character shifts the removed reading into the following segment and re-decodes it (`[苦茶][葉]` -> `苦[茶葉]`), while moving back and selecting a longer-span phrase merges segments again. Added core boundary/merge and Windows UI expansion regressions.
- Added contextual Space handling for first tone: while composing, Space is appended as tone 1 only when the existing parser validates it as completing the current trailing syllable; otherwise Space opens conversion, and converted mode continues to cycle the active segment. Added decoder and Windows-handler regression coverage.
- Completed Japanese-IME-style candidate progression using the existing segment boundary editor: `Shift+Left/Right` breaks or expands segments at Bopomofo token boundaries down to individual characters, `Left/Right` moves the active segment, and explicit number/click selection confirms the candidate and advances to the next segment. Added core/Windows tests and native playground help text.
- Revalidated the previously unfinished integrations: user-phrase management is wired into settings and the TSF/playground reload paths; Smart Paste acquires clipboard text only after edit permission and sensitive input-scope validation; and the MOE dictionary resolves relative to the loaded DLL/executable rather than the host process.
- Generated the Release ZIP package successfully at `build/windows/packages/TaiwanBopomofo-0.1.0-win64.zip`.

- Imported the Taiwan Ministry of Education Concise Mandarin Dictionary workbook into an unchanged source archive plus a generated 45,029-entry lookup file. Added MOE pronunciation/definition loading, same-sound character lookup, and a right-side related-character definition popup for single-character candidates. Preserved the existing curated entries first and retained a correction candidate when the expanded dictionary fills the candidate limit.

- Bootstrapped CMake/C++23 project with portable `ime_core`, decoder CLI, tests, Windows presets, formatting/static-analysis configuration, CI workflow, MIT license, and build scripts.
- Added shared core contracts for candidates, settings, input context, segments, composition state, providers, Bopomofo, and user dictionary.
- Added UTF-8 scalar-safe conversion, full-width/half-width conversion, atomic versioned settings persistence, composition editing, conversion/cancel/commit, segment movement/resizing, reconversion, F6–F10 transforms, raw fallback, sensitive-input suppression, smart-paste handling, and local user phrase persistence.
- Added a curated local lexicon with Bopomofo parsing, tone/partial/chaining/fuzzy paths, bounded beam decoding, segment provenance, reverse readings, and decoder CLI/benchmark/lookup modes.
- Added deterministic local providers for arithmetic, ROC/Gregorian conversion, dates/times, numbers/financial numerals, Unicode, symbols/emoji, email domains, typography, and smart paste.
- Added native Windows TSF adapter sources, COM registration/exports, candidate UI, playground, settings app, display attributes, manifests, Windows smoke-test target, and Windows compatibility/privacy/architecture documentation.
- Added decoder/provider/core/QA test suites, randomized composition/Unicode smoke tests, data source inventory, ADRs, and honest compatibility/testing documentation.
- QA fixes were added for selecting candidates during composition, refusing smart paste over an active composition, sensitive-context refresh/suppression, cycle integer overflow, and invalid hotkey transforms.
- Fixed zero-error MSVC compilation: replaced deprecated `std::filesystem::u8path`, included Shell APIs, used the SDK's concrete PIN input scopes, corrected runtime GUID initialization, removed the nonexistent `msctf.lib` dependency, and provided the input-scope property GUID locally.
- Fixed the Windows decoder CLI to accept UTF-16 command-line arguments, so commands such as `decoder_cli --lookup 你好` work independently of the legacy Windows code page.
- Made `IME_BUILD_WINDOWS_TSF` explicit and disabled it in the `portable` preset, allowing the portable core/CLI/tests to build with MinGW GCC while preserving native TSF targets in the normal Windows preset.
- Reconciled the smart-paste core test with the intentional rule that paste cannot replace an active composition.
- Audited every `PLAN.md` section and milestone against source, tests, documentation, and recorded build evidence. The project is a validated development slice, but it does not satisfy the first stable release definition.
- Advanced the Windows-host installation slice: registration now uses the documented system `InstallLayoutOrTip` API to enable/disable the Traditional Chinese profile for the current user without making it default; added `ime_profile_probe`, administrator-gated `tools/register_ime.ps1`, host-test instructions, and ADR 0003.
- Resumed from this report and confirmed a fresh Windows CMake configure succeeds. Added an explicit optional MOE dictionary build flag, runtime dictionary copying/install rules, ZIP packaging metadata, and a normalized CMake launcher path for CTest/CPack.
- Added native playground reload hooks for settings/user phrases, candidate related-character positioning improvements, and page-aware number-key candidate selection. The playground was opened successfully and live input `su3cl3` produced the expected Bopomofo reading `ㄋㄧˇ ㄏㄠˇ` while showing raw keys and composition state.
- Added user-dictionary test target wiring and began integrating user-phrase persistence with the native settings UI. The user-phrase agent exhausted its model usage before final integration verification; inspect the current Engine API and rerun the full build.
- Assigned fresh follow-up work for TSF clipboard/input-scope hardening and dictionary relocation/performance. Those agents also exhausted model usage before reporting final verification.

## Verification status

- MSVC 19.41.34120, Windows SDK 10.0.26100.0, Visual Studio 2022 Build Tools, CMake 3.29.5, Ninja, and GCC 14.1 are installed.
- A normalized-environment launcher at `tools/run_cmake.py` resolves the inherited Windows `PATH`/`Path` duplicate issue that initially broke MSBuild process creation.
- Fresh Windows configure passed with `python tools/run_cmake.py --fresh --preset windows` using MSVC 19.41.34120 and Windows SDK 10.0.26100.0.
- Final Debug and Release builds passed with zero errors and warnings-as-errors enabled using `python tools/run_cmake.py --build --preset debug --parallel` and `--preset release --parallel`.
- The last completed baseline Windows Release build before the latest CMake/common/UI edits passed with 0 warnings and 0 errors; the latest incremental build output also reports 0 warnings and 0 errors for the generated targets, but the command returned a nonzero status from the wrapper and must be rerun cleanly before this session can claim a current full-build pass.
- After the host-registration changes, final Debug and Release builds again passed with zero errors. Debug CTest passed 2/2 in 0.56 seconds and Release CTest passed 2/2 in 0.18 seconds. `ctest.exe` had to be invoked by its Visual Studio absolute path because it is not on the inherited shell `PATH`.
- Portable MinGW GCC 14.1 configure/build passed with TSF disabled and explicit Ninja/compiler paths. Portable CTest passed 1/1 (`ime_tests`) in 0.24 seconds. The MinGW runtime directory had to be prepended to a process-local normalized `PATH` for linking and execution.
- Current MOE-enabled Release `decoder_cli --benchmark` passed the 20 ms p95 target with dynamic span candidates enabled: p50 0.2042 ms, p95 17.0834 ms, and p99 29.3623 ms.
- The non-registering Windows adapter CTest passed, and Release `ime_playground.exe --smoke` and `ime_settings.exe --smoke` both exited successfully.
- `ime_profile_probe` correctly reports the current clean state: COM server not registered, TSF profile not registered, current user not enabled (`0x80070490` for profile not found).
- A direct non-elevated `DllRegisterServer` call returned `0x80070005` (`E_ACCESSDENIED`). A follow-up profile probe confirmed no partial COM or TSF registration remained.
- Native playground smoke was observed through Windows UI automation: the window rendered the composition/candidate panels, accepted individual Bopomofo keys, and displayed `su3cl3` as `ㄋㄧˇ ㄏㄠˇ`; the TSF DLL was not registered, so this does not count as a host compatibility pass.
- Native host behavior in Notepad, Word, browsers, screen readers, DPI, installer registration, signing, clean-VM install/upgrade/uninstall, and the neural model are not yet verified. Do not claim those gates passed.
- Requirements audit result: Milestones 2 and 9 have their narrow development deliverables substantially implemented; Milestones 0–1, 3–7, 10–12, and 14 remain partial because acceptance evidence or planned behavior is missing; Milestones 8, 13, and 15 are missing in substance. No code changed during this audit, so the preceding build/test evidence remains current.

## Known blockers and risks

- Candidate-menu update (2026-09-13): width is measured from candidate text, selected/unselected rows reserve the same accent gutter, and expanded candidates use nine-row horizontal columns. Oversized lists page within the monitor work area, with space reserved for the definition popup. Added native layout/hit-test regression checks. Full Debug and Release builds succeeded. Debug CTest passed 2/2 (40.89 seconds), and final Release CTest passed 2/2 (2.60 seconds). The initial Release relink hit LNK1104 because the DLL was in use; after the user closed the app, rebuilding successfully replaced `taiwan_bopomofo.dll`. No registered-host or mixed-DPI visual acceptance is claimed by this change.

- The inherited shell still does not expose CMake, CTest, Ninja, GCC, or the MinGW runtime consistently. Use `tools/run_cmake.py` for MSVC; portable MinGW runs currently require explicit tool paths and a process-local `C:\msys64\ucrt64\bin` prefix.
- The workspace contains no `.git` directory, so `git status` and `git diff` fail with “not a git repository.” Current changes cannot be audited against a Git baseline until repository metadata is restored or the workspace is opened from the actual clone root.
- Zed project diagnostics are not configured with the generated MSVC/Windows SDK include environment and report many false errors despite successful warning-as-error compiler builds. Treat the recorded CMake builds as authoritative until `compile_commands.json` or equivalent editor configuration is supplied.
- `PLAN.md` describes a production-scale IME; the current repository is a usable development slice, not a completed stable release. The seed lexicon is intentionally small and license-documented.
- TSF COM registration is intentionally machine-wide under `HKEY_LOCAL_MACHINE`. This process is not elevated; the attempted non-elevated registration was denied with `0x80070005`. Interactive Notepad testing is blocked until the user runs the documented registration script from a 64-bit PowerShell opened as Administrator.
- Code signing requires a publisher certificate that is not present in the workspace.

- If a future build fails, inspect the first compiler error and update this report with the exact command, failure, and next action; do not hide failures behind a “complete” claim.
- The MOE source is CC BY-ND 3.0 Taiwan. The original archive/workbook is retained unchanged, but the generated lookup index is a transformed build artifact. Written permission or a compatible redistribution arrangement is required before shipping that transformed index as a public release.
- Current MSVC Debug and Release builds completed with warnings-as-errors and zero compiler errors after the probe fix. Release CTest passed 2/2 in 2.09 seconds; a standalone Debug rerun passed 2/2 in 30.40 seconds. (A deliberately parallel Release/Debug CTest invocation collided on shared temporary test filenames; do not run those presets concurrently.) Both UI smoke executables exited successfully. Earlier in this takeover, `decoder_cli --lookup 你好` returned the expected entry and CPack generated the Release ZIP.
- On this clean/unregistered machine, the synthetic adapter test cannot register a foreground `ITfKeystrokeMgr` sink (`E_INVALIDARG`). The test explicitly reports that host-dependent subtest as skipped while retaining parser/key-handler, DLL ABI, candidate UI, and COM interface checks. This is not a registered-host compatibility pass.
- Zed diagnostics still lack the MSVC/Windows SDK compile environment and report false include/type errors; warning-as-error MSVC builds are authoritative.

## Immediate takeover sequence

1. Open a 64-bit PowerShell as Administrator and run `.\tools\register_ime.ps1 -Action Register -Configuration Release`.
2. In a new non-elevated Notepad process, select **Taiwan Bopomofo** with Win+Space and execute the Notepad checklist in `docs/compatibility.md`. Include contextual first-tone Space and phrase-to-character segment resizing/selection in the observations; record Windows/Notepad versions and scaling.
3. Run `.\tools\register_ime.ps1 -Action Unregister -Configuration Release` from elevated PowerShell after testing, then verify `-Action Status` reports a clean state.
4. Restore/open the Git clone metadata so the working tree can be audited without risking existing work.
5. Continue the host matrix in browsers, Office, DPI/accessibility scenarios, and a clean VM. Do not mark these gates complete from the playground or synthetic adapter test.
6. Add installer/signing work only when a publisher certificate and deployment policy are available; the neural model and production-scale lexicon remain separate milestones.
7. Update this file at the end of the next takeover session with exact results and blockers.

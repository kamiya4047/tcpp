# Windows host compatibility matrix

Status below is deliberately **unverified** until an actual interactive host run is recorded. Portable tests, successful DLL compilation, or COM registration alone do not demonstrate host compatibility. The Windows integration owner may update this table only with observed evidence.

| Host | Composition and commit | Candidate placement | Segment editing/reconversion | Clipboard/paste | DPI/accessibility |
| --- | --- | --- | --- | --- | --- |
| Notepad | Unverified | Unverified | Unverified | Unverified | Unverified |
| File Explorer search/text fields | Unverified | Unverified | Unverified | Unverified | Unverified |
| Windows Search | Unverified | Unverified | Unverified | Unverified | Unverified |
| Microsoft Edge | Unverified | Unverified | Unverified | Unverified | Unverified |
| Google Chrome | Unverified | Unverified | Unverified | Unverified | Unverified |
| Mozilla Firefox | Unverified | Unverified | Unverified | Unverified | Unverified |
| Microsoft Word | Unverified | Unverified | Unverified | Unverified | Unverified |
| Excel cells/formula bar | Unverified | Unverified | Unverified | Unverified | Unverified |
| WPF application | Unverified | Unverified | Unverified | Unverified | Unverified |
| WinUI application | Unverified | Unverified | Unverified | Unverified | Unverified |
| Electron application | Unverified | Unverified | Unverified | Unverified | Unverified |
| Legacy Win32 edit control | Unverified | Unverified | Unverified | Unverified | Unverified |

For each run, record Windows build, application version, IME artifact version/hash, architecture, display scaling, keyboard layout, and observed outcomes. Start with Notepad; repeat each supported host at normal and elevated integrity where relevant.

1. Select the IME; enter `su3cl3`; convert, reopen/cycle candidates, and commit the intended Chinese text exactly once.
2. Enter a phrase and verify the candidate list shows both whole-phrase and shorter-span/individual-character rows. Select a shorter row and confirm the boundary shifts into the following segment (`[苦茶][葉]` -> `苦[茶葉]`), then move back and select the whole phrase to merge it again. Expand the compact nine-row list and select an item beyond row 9. Also apply F7–F10, Escape back to phonetic composition, and recover exact raw keys without changing surrounding document text.
3. Select a different candidate before conversion. Confirm the selected text matches the displayed whole-composition candidate, including raw fallback.
4. Test Enter, Escape, Backspace, navigation, selection keys, focus switches, host termination, and repeated activation/deactivation.
5. Open candidate UI near display edges and across monitors at 100%, 150%, and 200% scaling. Check light, dark, high contrast, keyboard-only use, and screen-reader exposure.
6. With Smart Paste off, verify Ctrl+V follows the host's normal behavior and formatting. With it on, verify original/rich formatting, plain text and explicit transformations; never lose an active composition or keep clipboard history. Exercise non-text clipboard data and locked clipboard failure.
7. In password/security-sensitive fields, verify the adapter declines prediction/learning and does not retain or log content. Confirm ordinary typing resumes in a normal field.
8. Test selected committed-text reconversion only where the host supports it; unsupported cases must leave surrounding text unchanged.
9. Verify Internet Access off produces no provider requests. Any future opt-in implementation requires Taiwan location validation and failure tests before its status can pass.

Pending release work includes clean VM install/uninstall/upgrade, signing, accessibility audit, host crash testing, and multi-monitor audit. A permission denial must be recorded separately from a code failure; do not mark an unperformed check as passed.

## Development registration

TSF COM registration is machine-wide and requires an elevated 64-bit PowerShell. The script does not self-elevate and never changes the user's default input method:

```powershell
# Build in an ordinary developer shell first.
python tools/run_cmake.py --build --preset release --parallel

# Run these in PowerShell as Administrator.
.\tools\register_ime.ps1 -Action Register -Configuration Release
.\tools\register_ime.ps1 -Action Status -Configuration Release
```

Registration also calls the documented `InstallLayoutOrTip` API so the `0x0404` Taiwan Bopomofo profile is enabled for the current user. Open a new non-elevated Notepad process, use Win+Space to select **Taiwan Bopomofo**, and execute the checklist above. Keep the DLL at its registered path while testing.

Always unregister after a development run from an elevated PowerShell:

```powershell
.\tools\register_ime.ps1 -Action Unregister -Configuration Release
```

A failed non-elevated `DllRegisterServer` call should return `0x80070005` (`E_ACCESSDENIED`) without leaving either the COM class or TSF profile registered.

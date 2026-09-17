# 0003 — Explicit machine-wide TSF registration

## Context

A TSF text service needs both standard in-process COM registration and TSF service, language-profile, and category registration. The COM class is machine-wide, while Windows separately tracks which registered profiles are enabled for each user. Development registration changes operating-system input settings and normally requires elevation.

## Decision

`DllRegisterServer` registers the 64-bit COM class under `HKEY_LOCAL_MACHINE`, registers the Traditional Chinese (`0x0404`) TSF profile and categories, then calls the documented `InstallLayoutOrTip` export from the system `input.dll` to enable that profile for the current user. `DllUnregisterServer` disables the user profile before removing TSF and COM registration.

Development uses `tools/register_ime.ps1`. The script requires an already elevated PowerShell, does not self-elevate, does not make the profile the default input method, verifies the resulting state with `ime_profile_probe`, and supports explicit status and unregister operations.

## Alternatives considered

- **Per-user COM registration:** rejected because it is not the supported installation model for a system-wide TSF input processor and does not replace TSF service/category registration.
- **Automatic UAC elevation:** rejected because build/test tooling should not unexpectedly display a secure-desktop prompt or hide which system changes are requested.
- **Register without `InstallLayoutOrTip`:** rejected because a registered language profile is not necessarily present in the current user's enabled input-method list.
- **Set the new profile as default:** rejected because development installation must not unexpectedly replace the user's preferred keyboard or IME.

## Consequences

Interactive host testing requires explicit administrator approval. The DLL must remain at its registered absolute path for the duration of the test. Tests must unregister after use, and clean-VM installer work must separately verify rollback, upgrade, architecture, and signing behavior. A non-elevated registration failure is an environment/permission blocker, not evidence of a source failure.

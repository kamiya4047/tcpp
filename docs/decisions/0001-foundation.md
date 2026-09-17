# 0001 — Portable engine with native Windows adapters

Context: the IME must be testable without registering a DLL in every development iteration.

Decision: use a C++23 static core, TSF COM DLL, Win32 candidate/settings/playground applications, CMake, and dependency-free assertion tests. Use UTF-8 line-based local settings and user dictionary files with an explicit version and atomic replacement.

Alternatives: a web UI adds runtime/memory cost inside arbitrary hosts; a Windows-only core prevents portable fuzzing; a database is unnecessary for the bounded initial user phrase store.

Consequences: the engine can be exercised by CLI and portable tests, while host behavior remains a separate acceptance gate. Candidate/UI accessibility and DPI require explicit native implementation and audits. The seed dictionary is a development dataset, not evidence of production language quality.

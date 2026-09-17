# 0002 — Release claims follow observed evidence

Context: PLAN.md spans bootstrap through signed production installation across a large Windows host matrix.

Decision: retain PLAN.md as the requested specification. Track implementation and acceptance gaps separately. Do not mark production, neural accuracy improvement, clean-VM upgrades, screen-reader compatibility, or Windows host behavior as passed without running the corresponding check.

Alternatives: declaring the project complete based on a successful compiler run would conflate binary correctness with input safety and language quality.

Consequences: the development package can be useful before it is a stable release. Signing requires a certificate owned by the publisher; real host/VM tests require the relevant installed applications and environments.

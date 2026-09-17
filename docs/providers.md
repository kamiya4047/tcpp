# Local conversion providers

Providers are pure value transformations, except relative dates which read the local calendar when the caller supplies no `InputContext.reference_datetime`. Tests inject an ISO date/time. Explicit conversions do not read time, files, clipboard, or network. Provider results bypass neural reinterpretation. Malformed input returns no utility results so the engine retains normal Chinese and literal fallback candidates.

| Input | Result | Notes |
| --- | --- | --- |
| `=1+2*3`, `1+2*3` | `7` | Parentheses, unary signs, decimal literals, + − * / %; no scripting |
| `roc:115`, `roc:before1` | `西元2026年`, `西元1911年` | ROC year zero is invalid; `roc:前1` also supported |
| `ad:1911` | `民國前1年` | Gregorian years 1–9999 |
| `date:2024-02-29` | Gregorian Chinese, ROC Chinese, slash date | Validates Gregorian leap rules |
| `time:13:05:09` | `下午1時5分9秒` | Strict 24-hour input; optional seconds |
| `今年`, `去年`, `明年` | Gregorian and ROC year | Lower priority than ordinary Chinese |
| `今天`, `昨天`, `明天`, `today`, `yesterday`, `tomorrow` | Local calendar date | Calendar day arithmetic across leap days/year boundaries |
| `num:10001`, `money:123` | `一萬零一`, `壹佰貳拾參` | Signed decimal numbers, up to 16 integer digits; financial digit form is not a currency formatter |
| `U+4E00`, `u+1F600` | `一`, `😀` | Rejects invalid scalars, surrogates, C0/C1 controls |
| `:右`, `:right`, `:smile:`, `:taiwan:` | Arrows, smile emoji, Taiwan flag | Flag remains one candidate containing both regional indicators |
| `右` | Arrow candidates | Weak semantic trigger; Chinese stays ahead |
| `@gm`, `alice@gm` | `@gmail.com`, `alice@gmail.com` | Configured ASCII domains; never sends email or queries DNS |
| `full:ABC 123`, `half:ＡＢＣ` | Full-/half-width ASCII forms | Other scripts preserved |
| `norm:text` | CRLF/CR → LF, nonbreaking spaces → spaces | Deliberately limited normalization, not Unicode NFC/NFKC |
| `type:"你好!"` | `「你好！」` | Explicit punctuation transform, including decimal periods |

Explicit prefixes and fully valid arithmetic get strong trigger confidence (1.0 or 0.95), while semantic Chinese aliases use 0.25 and relative dates use 0.7. The central ranker owns the final priority. All supplied candidates carry deterministic flags, stable FNV IDs, and the original source span. Queries in sensitive fields return no utilities. Alias data is authored in this repository and adds no external data license obligations.

Arithmetic is limited to 256 bytes, 128 factors, depth 32, finite intermediate magnitudes at most 10^18, and 16 significant output digits. It uses bounded long-double arithmetic, not arbitrary-precision financial arithmetic. Division/modulo by zero and malformed expressions return no result. Queries over 4096 bytes return no utility results. Email output is capped at 20 entries and validates domain labels.

`smart_paste(original)` returns six separate actions: original, plain text, normalized, full-width, half-width, and Chinese punctuation. The first two preserve every input code point. The caller must preserve native clipboard formats and execute ordinary paste for `clipboard-original`; this pure API only receives text and cannot restore rich clipboard data itself. It keeps no history, performs no clipboard access, and retains no values beyond caller-owned results. Extra transforms are skipped beyond one million code points. The application must gate interception on the Smart Paste setting and release its operation state afterward.

Taiwan weather/network providers are intentionally outside this local module and are not implemented here. Ordinary typing and every provider documented above work offline.

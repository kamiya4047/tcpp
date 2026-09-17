# Composition and candidate flow

`Mode::Composing` is typing/prediction; `Mode::Converted` is segmented composition.
These legacy enum names are internal. The candidate menu has a separate open/closed state.

## Keyboard

- Typing: predictions can complete a dictionary word, e.g. 礦 → 礦物 / 礦石 / 礦泉水.
- Space from typing supplies first tone when the last syllable has no tone yet.
  For example `g/` becomes `g/ `; the following Space segments the input.
  First tone remains omittable between syllables. Otherwise Space segments only
  the typed input and closes predictions.
- Left/Right in composition moves the active segment and closes its candidate menu.
- Space opens the active segment's candidate menu. Further Space presses cycle down.
- Up/Down preview candidate choices. Number keys select within the active column.
- Enter commits the whole composition, whether the menu is open or closed.
- Escape from an open menu restores the snapshot taken when it opened; Escape from
  composition returns to typing with the original input intact.
- Left/Right during typing expands predictions. Reaching candidate nine expands
  the candidate menu; vertical navigation does not collapse it.

## Candidate previews

The menu is built from direct dictionary entries beginning at the active input
offset, including shorter entries, single characters, and longer entries that cross
existing boundaries. It remains stable during previewing. No concatenated paths
are added as word alternatives. The selected original surface and raw input remain
recoverable fallbacks.

Each preview starts from the menu's saved segment snapshot. Segments before the
anchor remain unchanged. A shorter or longer choice consumes its explicit input
range and the remaining suffix is decoded again. Valid previous wording is retained
where possible. Thus 苦茶 / 葉 can become 苦 / 茶葉 and return to 苦茶 / 葉.
Reopening a menu performs the same forward dictionary search, allowing a shorter
segment to offer its longer word again.

The original input is never rewritten by previewing. Segments must cover it
exactly once, in order, with no gaps. Unmatched suffixes use a raw fallback.
Candidate previews do not learn. Successful commits record accepted segment choices
when the user's local-learning setting is enabled.

## Typo suggestions

The decoder has separate rules for commonly confused sounds (ㄣ/ㄥ, ㄓ/ㄗ,
ㄔ/ㄘ, ㄕ/ㄙ, ㄋ/ㄌ, ㄌ/ㄖ, ㄢ/ㄤ) and nearby keyboard keys. Keyboard
adjacency uses the standard Taiwan layout, including initial and medial keys.
An explicit tone must still match. Exact candidates remain ahead of corrections.
Corrections have a small right-aligned replacement-symbol label, independent of
definition visibility: typing `qu/3` can offer 品 labelled ㄣ.

Regression coverage: `tests/qa_tests.cpp` and the native keyboard tests in
`src/windows_tsf/windows_tests.cpp`.

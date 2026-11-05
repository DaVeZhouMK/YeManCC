# Existing native shortcut consumer — OneX selected group

Use the existing `tools/input_shortcut_toggle_selftest.mjs` producer. No new global ID, minimum set or automatic release gate is added. The default `all` mode includes the OneX group once and retains every historical assertion. The explicit `onex-consumption` mode runs only the OneX group before returning; it does not run the F7/default/other-family regression groups and cannot accept the complete suite.

```powershell
$env:YMCC_SHORTCUT_OUT = 'G:\YeManCC-Work\Testing\YMCC\Evidence\<fresh-directory>'
$env:YMCC_SHORTCUT_CASES = 'onex-consumption'
node tools/input_shortcut_toggle_selftest.mjs
```

Use a fresh output directory; preserve old raw results. `YMCC_SHORTCUT_SOURCE` optionally selects an explicit saved main.cpp; omission uses current mainline. Sources and local header closure are hashed before/after execution. Whole-main changes after a run invalidate a whole-current claim. Matching emitted source with only output-path normalization can reference an earlier run, but does not count as a new execution or complete current acceptance.

Coverage uses the existing total-library binding, chord profiles, real keyboard hook, legacy OneX HID parser, X2/OP3 ready/publication boundary, retained queue, rule parser/evaluator, dispatch and existing serial own-action callback. It covers registered action IDs in disabled/Steam Deck/Xbox output modes; neutral does not replay, an unbound key creates no default, and recording/exit/rule-epoch cancellation prevents queued actions. The representative action is `os.openTouchKeyboard`; keyboard-sequence injection remains a forbidden boundary in this producer. Physical keys, operation IDs and machine aliases are separate counts.

Device/OS/Host observations and final effects are memory-only. X2/OP3 raw transport and genuine OS effects are not executed. Recording lifecycle and model-audit sinks are outside this consumer test; entering the omitted recording-stop branch fails. Legion motion activation remains a separate test domain: its real session type is compiled with an I/O-forbidden memory port, and its activation/identity callbacks are external boundaries. Noncopyable fixture-owned Legion slots are reconstructed between cases; no product struct or parser is rewritten.

2026-11-05 evidence: `Testing/YMCC/Evidence/OneXField093203-20261105/shortcut-closure-repair/SELECTED-CONSUMER-AUDIT.json`. Selected current-source group: 3766 checks, 21 family/alias entries, 77 action IDs, 231 key/mode cases and 231 cases for each of recording/exit/rule-epoch cancellation. A compiled virtual-required fault fails the disabled-mode business assertion. The complete `all` run still fails `retired Start F7 adds no rule owner or other default override`; retain that result and do not label this module fully passed. Hardware and complete release acceptance remain open.

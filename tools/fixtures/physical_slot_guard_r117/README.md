# R117 physical slot guard fixture

Run `node tools/physical_slot_guard_r117_selftest.mjs` from the repository.
`--source MAIN_CPP` selects a frozen actual source; `--out DIR` selects evidence.

The complete outer `ymccOpenXInputSlotGuard`, physical identity type and virtual-ID
classifier are extracted verbatim. Proxy availability, virtual guard, GetUserIndex,
AssignSlot and logger leaves are inert. The AssignSlot seam honors the new optional
admission callback; the separate openxinput_assign_guard_r117 fixture covers the
entire actual wrapper and all writes/refresh/retry issue boundaries.

Release/Checked each exercise 53 scenarios / 56 assertions. On the repaired source
all pass; the same final fixture on frozen original source has 44 failed assertions.
Native/OFF and virtual-enabled healthy physical drift both remain one query + one
assignment. An already correct route stays read-only; the one-second cadence is
unchanged. Initial and query-return invalidation includes source/Host/power/topology,
intent writer/revision, exit/capture/priority/borrow/duty, Unknown or occupied targets.

The test changes only fixture globals and clocks, never a real device or process.
It proves these admission contracts, not actual OpenXInput driver remapping,
consumer input, field root cause or whole-machine CPU performance. It does not
prove atomicity between the final predicate and a Windows call; lifetime and actual
hardware results still require the established source/Host/consumer evidence.

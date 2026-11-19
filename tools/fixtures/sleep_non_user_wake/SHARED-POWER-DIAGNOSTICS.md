Shared sleep diagnostics supplement
==================================

This is a narrow supplement of the existing `sleep.non-user-wake-consumer`,
not a new canonical ID, module minimum or ordinary export/release prerequisite.
It executes the actual `sleep_power_dispatch.h` and `sleep_power_diagnostics.h`.
No real sleep, input device, provider, window, timer or subscriber is operated.

With the project's reviewed x64 MSVC/SDK environment, compile the source using
C++20, EHsc, MT, W3/WX and include roots `native` and `deps/json`. Keep object,
executable, source-dependency output and logs in a new caller-owned evidence
directory. Run the resulting executable with `all`, `timing` or `export`.
Unknown selections fail. `all` runs 11 diagnostic boundaries. The optional
`YMCC_ORIGINAL_DISPATCH_CASES` definition is only valid when the exact existing
`verifySharedPowerDispatch` body has been provided by its original fixture.
It preserves those five assertions without copying another permanent model.

Timing cases cover delayed older delivery, queue/execution separation, duplicate
completion, fixed eight-row retention, overflow without admission restriction,
latest replay and unknown timestamps. Export cases cover unknown native state,
renderer non-authority, bounded field filtering, invalid types and old renderers.

Required semantic controls restore the older-receipt tick loss and replace
unknown native authority with `known=true` in private source copies. Both must
compile and reach their expected behavioral assertions; compiler failure is not
a successful negative. The production dispatch decisions and actual cleanup
responsibilities remain authoritative.

An initial three-variant run took 8.114 seconds, plus outer Job cleanup. That is
a local measured cost, not a promised upper bound or whole-parent acceptance.
Current tool/source/dependency identity, exact selections and actual Job return
must still be recorded. Cached/reference, omitted tests and full/hardware results
remain distinct; no frozen budget is reopened by this supplement.

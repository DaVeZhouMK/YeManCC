# Original display feed consumer integration

The unchanged original monitor_display_feed_selftest.mjs executes the actual monitorDisplayFeed and monitorLayout exports with its original virtual monotonic clock and timer map. Its16 named checks cover no-subscriber caching, duplicate/burst suppression, early-timer rescheduling, 1Hz publication, health invalidation/recovery budget, late subscription, final unsubscribe, disposal, observer faults, pure resolution policy and display freshness keys. Loop iterations are not additional cases. Existing related factory coverage is not counted again.

The established tdp-runtime-display-consumer captured resolver/observer is reused unchanged solely as a tool boundary. Its one exact legacy Build output namespace redirects to the owned case. It does not invoke a TDP consumer or hardware. The compiler graph contains only the two actual TS modules; type-only imports do not execute their modules. Compiler metadata on/off must produce identical executed bytes, and the rebuild is not executed.

Two faults modify only private copies of monitorDisplayFeed: removing the normal publication budget and removing final-unsubscribe timer cancellation. The unchanged original assertions must fail at the exact original lines and values, naturally exit1, and produce no complete report. Compilation, timeout, wrong assertions, borrowed/truncated reports or a second JSON summary cannot substitute. The private faults are current counterexamples, not historical product incidents.

All original sources, module tools, Node/esbuild/PowerShell identities and actual outputs are hashed. Existing Windows owned Job contains fixture/compiler descendants. No native, Host, IPC, device, browser, installed configuration or real sleep is used. This virtual-clock suite is not a hardware/browser performance benchmark, full branch coverage or whole-product acceptance. Product source and original assertion changes are zero.

Coordinator import+spread, original public global execution, current registered audit and actual output-byte integrity controls remain necessary. The entire HC goal remains open.

# T1 explicit selections in the original Win5 continuity fixture

The original runner accepts these exclusive `--case` selections with a fresh
`--out` directory. The empty/default selection continues to use only the original
`fixture.cpp` group. No global ID or module minimum is added.

| Explicit selection | Exact selected cases | Template |
| --- | ---: | --- |
| `t1-queued-borrow` | 10 | `queued_borrow.cpp` |
| `t1-pending-borrow` | 7 | `pending_borrow.cpp` |
| `t1-pending-borrow-publication` | 2 | `pending_borrow_publication.cpp` |

`t1_borrow_consumer.mjs` shares the extraction/build recipe. It uses repository
relative helpers and templates, consumes the early queued-open struct once, and
extracts the actual context/retirement helpers. `renderT1Borrow` only prepares
source; `runT1Borrow` explicitly builds Release/Checked and runs the selected
template. Publication intentionally excludes unselected PowerEdge, preserving
`/W4 /WX` and the corrected private compilation scope. Unknown T1 names fail
before compilation. Runtime receipt names/order/count and each case result must
match the selected list.

Import evidence is under the workspace-relative directory
`Testing/YMCC/Evidence/VirtualControllerStability-20261108/t1-mainline-integration-r1/fixture-import/`.
The formal import was **NOT_RUN**: only JavaScript syntax, production extraction
and byte comparisons were performed. Pending7/publication2 generated source is
byte equivalent to private R4 Release/Checked compiled source. Queued10 keeps
its original template and selection, but the R4 helper/declaration interface
changes its generated source; its R4 compilation/runtime remain unverified.

Historical raw references remain in the original
`t1-queued-borrow-r1/remaining-boundaries/` evidence: `pump-r3-r1/RESULT.json`
retains the three R3 failures; `pump-r4-publication-r1/RESULT.json` retains the
first C4505 unused-PowerEdge compilation failure, with its correction in
`PUBLICATION-RECIPE-CORRECTION.json`. The import proof pins these originals and
the successful private compiled sources without rewriting or rerunning them.
Both exhausted T1 1800-second ledgers and all earlier costs remain intact.

The consumers use controlled memory ports. They do not execute full Main,
real OS/PnP or device IO, prove actual thread scheduling, global publication,
complete inflight IO ownership, or long-term resource stability.


P2 后继（2026-11-09）：stage0 待办取消日志补充了原deadline/intent/Host/power与原因；新的日志在原诊断Scope内走有限队列，拒绝/丢弃不代表持久化成功。此后继没有重跑旧用例，不能将前一epoch的CPP逐字等价结论转成新源PASS。三模板现包含真实input_host_diagnostic_dispatch.h，消费者声明、复制和末端校对该输入；不引入惰性Scope模型、第二日志owner或新global ID。新源正式执行仍NOT_RUN；原失败、耗尽预算和原件保留。


Close 发布边界（2026-11-09）：原入口新增互斥显式case `t1-close-publication`，三例为普通Close、idle发布点新OEM、刷新active保持pending。完整生产Close/Request/Toggle/Open函数体原文提取；唯一声明适配将stage存储委托给std::atomic包装器，在store0后通知一次真实Toggle。manifest明确该全局声明非生产原字节、受控调用顺序非真实线程调度。旧源Release compile0/runtime1，仅新OEM例FAIL并留下stage2/transition0；修正Release/Checked各3PASS。沿旧显式case体系，不扩默认/global/minimum。新fix为Close加原diagnostics Scope且在整个尾部最后发布idle，保留原I/O/责任/预算；原1500ms submit-stats RPC等仍会延长过渡合并窗口，不承诺完整Close硬时限。原失败/旧两1800账保留，本边界独立原900s计划见VirtualControllerStability-20261108/t1-close-publication-r1/PLAN.json。


Open diagnostic Scope（2026-11-09）：同一queued模板参数化 `diagnostic-scope`，原runner显式case为`t1-open-diagnostic-scope`；仅执行success/refused/expired/rollback四例，不执行原十例。真实Open函数体及真实Scope header，日志端口只记录Scope.active，返回后核Scope退出；不是磁盘阻塞端到端试验。原Scope缺口四marker失败，单行Scope修正Release/Checked各四例通过。同源logger/dispatcher原证据只作分层组合引用，不能把marker变成真实阻塞时延或物理效果。默认/global/minimum不扩，旧预算不重开。

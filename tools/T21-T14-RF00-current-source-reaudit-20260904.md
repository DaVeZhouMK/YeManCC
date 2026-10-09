# T21/T14 RF00 current-source re-audit (2026-09-04)

状态：`CURRENT-SOURCE-STATIC-REAUDIT-COMPLETE / DOCS-ONLY / RUNTIME-BLOCKED`

本工件绑定：

- manifestId: `T22-RF00-20260904-P08-C05-CURRENT`
- sourceDigest: `2F741159DCF9DFADE199660711505BE861D504A91EE2D12257A7A8FCAA384361`
- manifest: `T22-RF00-P08-C05-current-source-manifest-20260904.json`
- line-map: `T22-RF00-P08-C05-current-source-line-map-20260904.md`
- capture mode: static read/hash/documentation only; no test/build/runtime/device operation

## T21：9 条 current claim 对账

| REC | current-source evidence | current disposition |
|---|---|---|
| REC-01 | `native/main.cpp::inputCaptureStart/inputHostStart/inputHostSubmitPad`、`InputHost/Program.cs::RunFile`、`gyroConfigActivation.ts` | `CONTRACT-NOT-IMPLEMENTED / LEGACY-PARTIAL-CANDIDATE-PRESENT / RUNTIME-BLOCKED`；C02–C05 不等于 T10–T13 合同闭环 |
| REC-02 | `src/bridge/inputOwnerRuntime.ts`、`inputCoordinatorMock.ts`、`29-ATOMIC-OWNER-AND-GAME-CONSUMER-ISOLATION` | 仅保留 YMCC 内部 P-OWNER 语义范围；mock/renderer 计数不等于 Steam/游戏 consumer；外部消费者 `UNENCLOSED` |
| REC-03 | `native/main.cpp::gamepadReadState`、ROG HID bind/restore、历史包引用 | ROG 多槽/复合 HID 的 summon、消费者和恢复仍未闭；不得升级为游戏隔离 |
| REC-04 | `native/main.cpp:5404` pair gate、`5413-5427` GamepadMotion、`5515-5530` direct Host admission | C02/C03/C05 后，未配对/未锁定样本不再直通 GyroDps；provider identity、同代 pair、真实 first-frame 仍缺，保持 `safe-zero / RUNTIME-BLOCKED` |
| REC-05 | `native/main.cpp:5281-5310`、`InputHost/Program.cs:30-55,122-136`、settings/CAS bridge | C04 只修正 fallback；state-file 仍无 request/revision/hash/ACK/first-frame receipt，保持 `parameter-not-consumed / RUNTIME-BLOCKED` |
| REC-06 | `InputHost/Program.cs:122-136` 与 `PersonaDescriptorParity.v1.json` | API 字段位置不等于 HIDMaestro descriptor/report parity；保持 `descriptor-unverified / RUNTIME-BLOCKED` |
| REC-07 | `native/main.cpp:4968-5270` HidHide helper、`physicalVisibilityTransaction.ts` | P-HID 仍未准入；不得把 HidHide CLI/API 解释为 P-XINPUT/Steam/游戏隔离 |
| REC-08 | `tools/*selftest.ts`、T10–T13 JSON 工件和 provenance gate | located/mock/self-declared 状态不能升级；保持 `source-located / evidence-incomplete` |
| REC-09 | `HCParityDriftLedger.v2.json`、TASK/30 C1 说明 | 继续 `C1 option-2 resolved / no DGF-16 created`；有效编号仅 DGF-01…DGF-15 |

## T14：既有 DGF 映射重建

RF00 仅刷新 source/current/provenance 绑定，不新增 DGF：

- `DGF-01`/`DGF-02`：T22 P02 / pair、calibration、release 缺口；C03 收紧 direct state-file gate，但没有 runtime proof。
- `DGF-03`/`DGF-04`/`DGF-12`/`DGF-14`：T22 P04/P05；ROG/P-HID/P-XINPUT/P-OWNER 三平面仍分离，外部 consumer 未观察。
- `DGF-05`/`DGF-09`：T22 P03；DS4 persona/descriptor/IMU wire 未证。
- `DGF-06`/`DGF-10`/`DGF-11`/`DGF-15`：T22 P01/P06；C04 不等于参数事务、ACK 或 consumer closure。
- `DGF-07`：T22 P02；provider/time/pairing 未证。
- `DGF-08`/`DGF-13`：provenance/claim uplift gate；本工件本身也只产生 docs-only 结论。

任何不落入上述既有 DGF 的新行为差异都暂记 `candidate-drift / SAFE_STOP`，本轮不静默造号。

## 结论

`T22-RF00` 已恢复 current-source 静态身份，`T21/T14` 的 docs/source/provenance 结构对账可绑定到该 manifest；这不关闭 T10–T18、T15/T16 或 R1。下一授权点仍是实现合同符合的 Coordinator/Host ACK/provider pairing/descriptor/recovery，再分别授权构建和 runtime 实证。

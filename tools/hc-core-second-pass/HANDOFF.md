# 主代理交接：HC 核心本轮不提升

结论已经固定：`FINAL_NO_PROMOTION`。不要把本轮候选混入正在制作的普通包；`PROMOTION-LIST.json` 的 replace/remove/deploy 为空。本轮没有等待新的理想验证或开启下一轮探索。

最终 unsigned HC 为 **2,238,976 B**，SHA256 **4f9d2f534289c4e6cc7183deddd719cfd3dfa613fde1e99946ff5327697149ab**。原 HC 2,774,016 B/SHA08a386...，净减少 **535,040 B**。原样 86 文件 baseline 对照的整个候选目录为 **11,837,706 B**，父任务 JSON 精简独立，当前生产读取值与理论叠加值在 STAGE-REPORT.json 中记录。相对旧未提升的 devices-v2 HC，本轮最终额外节省 2,560 B。

候选保留 `HandheldCompanion, Version=0.32.4.0, Culture=neutral, PublicKeyToken=null`。2,239,488 B 的 signed 试验被拒绝，只在 scratch `rejected-signed/` 留存；不会对 HC 新增强名，也不触碰两 UI 库身份。

供父任务读取：

- `D:/Codex/2026-10-18/kai-s/work/hc-third-trim/core/STAGE-REPORT.json`
- `D:/Codex/2026-10-18/kai-s/work/hc-third-trim/core/PROMOTION-LIST.json`
- `D:/Codex/2026-10-18/kai-s/work/hc-third-trim/core/REFLECT-ROOT-SOURCES.json`
- `D:/Codex/2026-10-18/kai-s/work/hc-third-trim/core/DELETED-TYPES.json`
- `D:/Codex/2026-10-18/kai-s/work/hc-third-trim/core/VALIDATION-RECEIPTS.json`
- `D:/Codex/2026-10-18/kai-s/work/hc-third-trim/core/lifecycle-unsigned-final/verification.json`
- `D:/Codex/2026-10-18/kai-s/work/hc-third-trim/core/signature-baseline-candidate.json`
- `D:/Codex/2026-10-18/kai-s/work/hc-third-trim/core/candidate/HandheldCompanion.dll`（仅试验）

最终软件验证：HC 全 GetTypes 1768→1557，211 删除类型逐名列出；主 Devices 的77类型及全部91具体派生类型可解析。260个 Devices元数据类型/1826方法/1094字段不变；候选9732保留方法、80 native布局及额外327类型布局审计通过。13个Factory管理器类型/Status/事件/CloneProfile与App路径字段反射通过；Factory/App/管理器构造及隐式cctor闭包771方法全部保留且IL相等。22 ACPI软件callback通过。生产同形目录、精确8共享库、生产shared resolver下原7纯构造、14零句柄操作、4非零哨兵拒绝、被动计时均通过；无实际SDL加载。

旧固定token的零触摸fixture首次失败原样保留。最终仅在独立全部保留IL相等及原签名/token固定后重定位三个token常量；未放宽行为断言。baseline和候选同一完整方法签名walk分别26/23失败，候选新增0，包括baseline已有WindowsDisplayAPI缺失；没有加库。早期Helix字段walk是更广额外诊断，不能当候选独有GetTypes回归。

拒绝理由：保留HC内348个反射/序列化站点、消费者649词法站点虽完整记录，但尚未给所有动态名字和模型schema建立语义闭包。软件通过不替代这个证据，也不把未运行的真实Factory/设备生命周期写成已实测。

后续只建议三条具体切口：实际Host/Profile JSON及事件反射分类；按211删除名单核对UI闭包；证明11张OneX缩略图107169原始字节没有headless图像消费路径后再试资源删除。当前全部资源保留，没有应用叶库AssemblyRef消失，不批准额外叶DLL删除。SDK/WinRT/UI由原负责agent继续，本轮不写其实际文件。

所有仓库写入见 CHANGED-PATHS.txt，仅 tools/hc-core-second-pass；所有生成文件仅 scratch core。README.md包含方法、失败归因与可执行复核入口。此结果可以立即由主代理作为“已尝试、拒绝提升、有具体下一切口”的有界结论归档，不阻塞普通包。

# 高频监控快照响应直接构造优化

日期：2026-10-17。本轮已合入主线源码；没有链接、制包或部署，没有修改本机 YMCC/HWiNFO 设置，没有新增采集器、定时器、线程或助手 EXE。

## 修改与原因

`native/main.cpp::monitorSnapshotServeFn()` 的八字段响应封装原先使用 JSON 初始化列表，先构造临时键值数组，再复制进结果对象。现在直接创建对象并依次赋值。这个入口同时供 IPC 读取与原生 HUD 推送使用，减少每次消费者读取时的重复临时构造。

只替换此函数中响应对象的构造语句。读取锁、缓存副本、载荷字符串移动、16 KiB 单文档/24 KiB 合计上限、当前开关状态、代际/会话、生产时间及计数规则保持。`monitorHudPulseTick()`、RTSS 原有 100ms 生产通道与 HUD 200ms/1000ms 推送规则均未修改。

## 实际函数验证

新增 `tools/monitor_snapshot_envelope_selftest.mjs`，默认基线为 `tools/fixtures/monitor_snapshot_envelope_before_20261017.inc`。它编译完整生产 serve 函数与完整生产 pulse 函数；仅时钟、缓存、定时器和事件发送是隔离钩子。并非用手写替身比较构造式。

- 51,373 项断言通过。
- 4,497 组完整快照响应，JSON 值、数字类型与序列化字节一致。
- 2,432 组完整推送事件及定时器/租约效果一致。
- G3E、ROG、Win5 三份实际导出日志与本机已有统一缓存诊断，各选取 48 份快照，共 192 份真实快照回放。原始归档、成员读取字节哈希与抽样数见 `evidence/REAL-SNAPSHOT-MANIFEST.json`。
- 覆盖无效/有效缓存、空/超长载荷、单文档上限、合计上限、独立开关、会话/relay、消费者修改隔离、源缓存不变、过期租约静默停止、5999/6000ms 新鲜度边界、未来时间及 RTSS 标记缺失。
- 21 项原有实际发布/文件提交回归全部通过，保留原来会复现问题的历史基线。
- 13 项静态路由/上限检查通过。旧检查错误地匹配早期单一 24 KiB 导出文本；在本轮修改前的主线已复现失败。现已核对实际导出表达式中诊断 12 KiB、普通快照 24 KiB 与记录/队列/文件上限的有序常量绑定。生产上限未变。
- 整份候选 main.cpp 翻译单元编译成功；编译快照与合入后的 main.cpp 字节一致。编译不是整包链接或实机验证。

## 可量化收益

五组输入（无数据、小载荷、上限附近、合计超限、单文档超限）均分别测量完整 serve 与完整 pulse，每组每侧 2500 次；包含临时对象及返回对象销毁。完整 pulse 发送终点为无分配的隔离 sink，不包含真实 IPC 传输/序列化成本。

每 2500 次均减少 80,000 次分配、2,400,000 字节分配请求，即每次减少 **32 次分配、960 字节分配请求**。例如小载荷完整 serve 为 132,500 → 52,500 次分配；完整 pulse 为 162,500 → 82,500 次分配。

这是确实减少的构造/分配工作量，不是整进程 CPU 百分比、驻留内存减少量或帧时间收益。没有降低频率来换取节能。

## 合入身份与边界

- 合入前 main.cpp SHA-256：`e59aa4be75ef4fe27a3c2b07a2c8049b8e2842e7363b30d9bf157b01b62f6a6c`
- 合入后/已编译 main.cpp SHA-256：`675a676145b2eabacd786870ce97577acaf805a9cad6a36b86bbe2b9f1186a6a`
- 当前安装 EXE 仍为 `9b674c974510c1a1452b4a36177ec57fccf5c2e1c38992b1de37f944147e6220`，本轮未部署。
- `MERGE-IDENTITY.json` 记录精确函数前像与后像、保留所有其他实时源码字节的校验。
- 这不构成数据/CPU 优化长期目标完成：其余上下游路径仍需按证据继续优化。

## 重跑

在 `Mainline/YeManCC-source/YeManCC` 中设置独立输出目录再运行：

```powershell
$env:YMCC_SNAPSHOT_ENVELOPE_OUT='G:/YeManCC-Work/Build/Tasks/YMCC-Steam-HUD-Probe/your-new-run'
node tools/monitor_snapshot_envelope_selftest.mjs
```

可选 `YMCC_SNAPSHOT_ENVELOPE_REPLAY` 指向本轮 `evidence/REAL-SNAPSHOT-INPUT.json`，加入相同的 192 份真实快照回放。默认不依赖本轮目录或本机日志，使用已入源码的精确旧函数基线与合成边界用例。

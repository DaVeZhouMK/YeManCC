# 1% LOW 取数与闪烁修复（2026-10-13）

已小更新到 C:/SOFT/YeMan/YeManCC，并恢复正式页面、正常启动。实际 PowerControl 根保持原值。

## 已确认的原因

1. 旧模糊匹配把 0.1% low 当成 1% low，还允许 Displayed 分支覆盖 Presented。现有 YMCC 唯一采集器的 35 次有界取证证实：当 Presented (1% low) 为约 151.5 FPS 时，Displayed (0.1% low) 可以为 0 FPS，而旧 parser 发布的是后者。
2. 本机 HWiNFO 实际轮询周期为 3000ms；旧固定 3000ms 有效期与整秒 producer timestamp、原生 1Hz 读取错位，使正常统计在下一次更新前被清成 -1，消费者随后移除 DOM。保存的修复前短时序列 64 个快照中 20 个 1% LOW 无效。

## 最终行为

- 精确匹配同一个 Presented (avg) 传感器槽位的 Presented (1% low)，不以 (1%) 分位数、Displayed 或 0.1% low 替代。
- 为 1% LOW 发布独立来源、生产者时间、轮询周期、截止时间。3000ms 周期对应 5000ms 截止预算：包含生产者整秒戳和现有 1Hz 消费采集的传输边界；不改 HWiNFO 设置。
- 同一 poll 重复读到不续期；缺项当轮即无效；生产者停更到截止也无效；不冻结、不平滑、不计算伪造统计。
- 主 FPS 来源切成 RTSS 后，1% LOW 仍明确由 HWiNFO 的 Presented 统计提供。来源不同的统计没有强行改值到主 FPS 范围。
- 1% LOW 数字预留三位字符宽度，真实 0 正常显示，值变化不改变面板宽度。
- 缺失／null／字符串不转成假 0。
- 数据仍来自 YMCC 原来唯一数据层，未新增硬件采集线程／读者；未进行 CPU 基准测试。

## 验证与边界

- 原生、网页、Decky 构建，类型、传感器、数据链路、帧时间、渲染一致性、连续性回归均通过。
- 实际 parser/Output 回放保存的原生采集行；精确统计、真实 0、缺项、同 poll 不续期、独立过期均通过。
- 13 模板 × 2 缩放 × 10 数值 = 260 次实际 Vue/CSS 更新，面板与数字区域宽高稳定。
- 安装 120 文件哈希匹配；原生 HUD 可见，25 个只读鼠标命中点，0 拦截。
- 安装后的实测准备阶段 2077 已退出；无游戏 24 个快照没有 FPS 包，没有编造 1% LOW。**未完成安装后游戏前台的持续验证，不能声称已通过。**
- 临时 native 取证块已完全撤销；HTML 与正式构建一致；临时脚本因自动清理命令被拒暂留磁盘，正式页面不引用、不加载。

证据：INSTALLED-SMOKE.json、FPS1-NATIVE.json、FPS1-TRUTH.json、native-hw-before.jsonl、observed-before-initial.json、live-after.json。

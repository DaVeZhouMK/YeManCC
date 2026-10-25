# 内部连续控制的 FPS / LOW 证明（2026-10-17）

控制入口 statusFromSnapshot 保留原整包 5 秒门槛，同时校验主 FPS 和 LOW 各自的原始证明。复用 monitorFrameTime 的现有规则；LOW 使用 coherent collectedAt 上界，不能仅按较旧 RTSS 主包时间拒绝更新的真实 HW LOW。

硬件失联时不能继续用 HW 帧数据降功耗。有效 RTSS 主帧率可保留，但不证明 HW LOW 健康。LOW 失效返回现有 -1 缺失标记，真实零值不改写；主 FPS 失效沿用心跳 idle/null。显示保留时间、原生数据、HWiNFO 设置、优先级和控制算法不变。

原生 Output 生成包已复现：LOW 已过期但整包新鲜时，旧入口用旧 LOW 触发 30W→29W；新入口缺失标记使原算法保留 30W。仅隔离真实算法执行，无硬件写入。

24 组前后消费案例、6 次完整浏览器 tick、既有硬件/数值/准入回归与 TypeScript/前端构建通过。未部署，不代表本机 CPU 或 TDP 实测收益。

完整证据：G:/YeManCC-Work/Build/Tasks/YMCC-Steam-HUD-Probe/control-frame-proofs-20261017/REPORT.md。新工具：tools/monitor_control_frame_proofs_selftest.mjs、tools/monitor_control_frame_proofs_browser_selftest.mjs。

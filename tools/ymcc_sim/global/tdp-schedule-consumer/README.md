# Original CPU/TDP schedule consumer

本域直接执行原performance_schedule_tdp_selftest.ts的完整套件和原正式performanceSchedule/autofloat/cpuProfiles/settings/yeman消费者，不改原脚本断言或产品逻辑。getTdpScheduleConsumerAdapters()提供performance.tdp-schedule-consumer，父协调者串行登记到原global。

```powershell
node tools/ymcc_sim/global/tdp-schedule-consumer/run.mjs --evidence-dir <不存在的绝对目录>
node --test tools/ymcc_sim/global/tdp-schedule-consumer/runner.test.mjs
```

原脚本以void-main运行，清理原调度器和浮动状态后自己发出process.exit。VM只观察这个退出与原三个完成标记；不将仅编译、无退出或只有部分标记判通过。它包含原AC/DC连续bank、可选TDP失败不阻断CPU、G3E接受/拒绝、真实低层消费者的内存终端等断言；不把静态assert位置数当实际独立场景数。

最初安全原型在脚本切换base之前观察到默认C:/SOFT/YeMan/PowerControl/game-custom.json访问，被边界拒绝，原失败保留。正式fixture仅将这个明确的默认安装命名空间映射到同case的default-install目录；不触碰真实安装文件，也不改变原产品路径/消费者。其他case外文件仍拒绝，临时目录创建/递归清理须通过同一实际路径围栏，scratch根不可删除。

CJS编译读取固定原输入，metafile必须精确匹配已审查图。原test不改。故障副本仅把正式performanceSchedule中tdp-unsupported反馈分类改错；必须由原“Desktop-17h应有不支持提示”断言失败，不能用编译/环境错误算通过。它是当前源码故障注入，不是历史修前证据。

文件、进程和网络终端沿本组离线边界，原mock-shell不改；不启动TDP产品或操作系统注册表。原G3E、CPU、daemon回执都是原测试fake native响应，不证明OEM功耗实际读回、温控/设备接受、安装版本或总库统一绑定。原输入、真实生成CJS、metafile、输出与故障证据逐字节记录到原模拟器的sourceHashes。

# TS 性能模拟

性能调度的“模拟测试/重置”现为同 DOM 的 Vue TS 能力工作台。右侧复用当前 ContinuousPerformancePanel，通过 nativeLayout 和 sharedNavigation 保持主线尺寸、布局与正式手柄 engine。缩放只用主线 --ui-scale，没有 iframe 或按高度再次缩放右侧。

左侧瓦数轴独立于 TDP 设定，A/B 同瓦数对比，表格保留百分比与分/W。src/performance/ts 中只有已有曲线和有界估算，不是硬件实跑。

Z2E 按 3 Zen5 + 5 Zen5c 核心簇处理，不视作双 CCD 桌面机。关核保留原有 GPU 优化幅度；CPU 用冻结模型的延迟恢复且上限 10%，仍损失被关闭核心的多线程容量。258V/G3E 的关核额外正收益保留 20%。这些系数为模拟校准，非逐机型 TS 配对实测。

临时调节、恢复、重置都不写正式设置；只有确认保存通过原 updateContinuousPerformanceControls 的身份和 revision 检查应用。供电/游戏/设置变化提示恢复，不覆盖草稿。关闭释放计时器、监听、草稿和宽度租约。TDP 首焦点未进入编辑，B 先取消滑块/弹窗再关闭。

旧 public/performance-simulation 小游戏资产已删除，旧同步器和小游戏专属测试标记 RETIRED。TS 模型用例改为消费新模块；未把已删除场景计为 PASS。独立陀螺仪模拟保持原状。

证据：G:/YeManCC-Work/Testing/YMCC/Evidence/TsSimulationImport-20261104 。本次定向生产 engine、真实组件、720P/800P 与 DPR 1/1.25/1.5/2、12 组草稿保存正负例及 13 组模型检查通过。全应用导航首次尝试的专属菜单和触控板用例失败保留，不能宣称全应用 UI 放行；未验收实物手柄或硬件功耗。快速测试导出复用当前安装版相同 SHA256 的原生程序与原 PowerControl，不改安装目录或重置用户设置。

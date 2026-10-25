# 内部监控共享快照的响应式开销 · 2026-10-17

## 改动

`src/bridge/topmon.ts` 的 `topMonitorData` 改用 `shallowRef`。当前内部消费者接收的是整份替换的快照；保留根 ref 的更新通知，去掉逐字段读取经过深层响应式代理及登记字段依赖的工作。

setter 的 AC/DC 修正、已有的根对象复制、可见性需求登记、采集和控制节拍均保持原逻辑。没有新增数据入口、轮询器或产品 EXE。

## 数据所有权

内部消费者通过 `setTopMonitorData` 接收完整快照，并读取共享 ref。消费者应继续整份替换快照，不能用修改 `topMonitorData.value.someField` 的方式发送更新。当前生产引用已盘点，数据写入在统一 setter，其他使用方读取或订阅整份快照。源码引用记录保存在本轮证据目录的 `CONSUMER-REFERENCES.txt`。

这项改动保留的是当前使用方式的行为，不声称旧 deep ref 对任意外部就地修改的响应能力也保持不变。

## 验证

运行：

```powershell
node tools/monitor_top_shallow_ref_selftest.mjs
$env:YMCC_TOP_SHALLOW_REF_OUT = 'G:/YeManCC-Work/Build/Tasks/YMCC-Steam-HUD-Probe/internal-top-shallow-ref-20261017/production-vue'
node tools/monitor_top_shallow_ref_selftest.mjs --production
```

测试以当前生产模块和真实 Vue 比较旧 ref 与新 shallowRef，开发版和生产版各 16 项通过。保留了原 setter、实际快照投影，以及从当前 App.vue AST 提取的电池/视频电源判断函数和监控 watcher；下游视频实际执行由无副作用计数器代替。

覆盖：初始无数据、AC/DC 修正、输入根对象隔离、温度缺失/硬件中断/恢复、真实零温度、重复数值的新快照、电池缺失恢复、冻结数组共享、null 电源状态、八种投影场景、重复挂载和可见性需求。另比较实际 App 电池/电源消费者的九次状态变化，包含放电确认、20% 低电量静态、23% 滞回、未知电量保持、25% 恢复、两次充电确认和无快照保持。

已有目标回归：温度浏览器 21 例与顶部栏需求浏览器 2 例通过；Vue 全项目类型检查通过；前端生产构建通过。浏览器测试仍为隔离原生边界，不代表真实硬件、睡眠或视频解码生命周期已经验收。

开发版依赖诊断中的一组代表性消费，登记由 15 次（根 ref + 14 个字段）降为 1 次（根 ref）。生产版只做行为对照，不启用开发依赖诊断。该计数说明被移除的局部工作，不能换算成整个 YMCC 或 HWiNFO 的 CPU 占用降幅。

## 交付边界

本轮仅修改主线源码与生成隔离前端产物，没有替换本机安装文件、重启产品或 HWiNFO。上一轮 `YMCC-Data-Integration-20261017` 的冻结测试包不包含这次后续修改，不能用旧包验证本项。

证据：`G:/YeManCC-Work/Build/Tasks/YMCC-Steam-HUD-Probe/internal-top-shallow-ref-20261017/ROUND-RESULT.json`、同目录 `PRODUCTION.patch`、`RESULT.json`、`production-vue/RESULT.json`、`VUE-TSC.log`、`VITE-BUILD.log`。

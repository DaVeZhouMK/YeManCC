# 统一监控 top 发布序列化预算（2026-10-17）

nativeMonitorTopPayloadText 每线程复用 serializer/工作字符串，但始终返回独立 std::string。使用当前 nlohmann serializer，保留 replace 非法 UTF-8、字段和数值格式。localeconv 的千位/小数点字符变化时重建 serializer。

工作字符串长期容量最多 16KiB；超过时把完整缓冲移交返回值，异常时也释放超限容量。不是截断输出，原 top/FPS/合计快照预算不变；这项容量保证不包含原有 topJson 的存储。修改预算时同步审阅这里和自测中的预算断言。

上一轮标量核心数组在替换或擦除前先清空，避免 nlohmann 为普通数字/null 数组析构分配遍历栈。嵌套 legacy 输入保留原路径；原明确所有权移交约定不变。

正常 16 核移交完整发布的分配为 14→1，隔离构造周期中位数约少 4.87%；不能当作整进程 CPU 降幅。11,393 组输出、6000 次线程对照、异常/故障和原有所有权/快照回归通过，311 个冻结原生输入编译通过。未链接、打包或部署。

自测顺序：monitor_top_text_serializer_selftest.mjs、monitor_top_text_serializer_robustness_selftest.mjs、monitor_top_scalar_destruction_fault_selftest.mjs、monitor_top_text_serializer_timing_selftest.mjs。异常和计时工具先核对实际函数指纹，避免引用陈旧生成物；冻结旧函数位于 tools/fixtures/monitor_top_text_serializer_before_20261017.inc。

完整证据：G:/YeManCC-Work/Build/Tasks/YMCC-Steam-HUD-Probe/top-text-serializer-20261017/REPORT.md。源码依赖当前固定 nlohmann 内部 serializer 接口，更新 JSON 库时需重新编译和跑这些输出/区域/异常回归。

# 小型原生库无语义精简实验

有界范围：固定 SHA256 的 `GamepadMotion.dll`、`hidapi.dll`、`Xinput1_4.dll`。不写生产目录、profile、Host 或 native/main.cpp，不加载或执行被检查的 DLL。

`pe_small_trim.py` 逐件校验当前 runtime 清单和输入固定身份，把输入及候选写入独立证据目录。它实际检查 overlay、调试数据、全零节与节末尾填充；只回收不改变映射内容的对齐块，并保留所有节 RVA/VirtualSize、入口、完整导出表、导入/延迟导入、重定位和原代码/数据。调试清零只是内存影子测算，不进入候选。存在证书表即拒绝处理。COFF 符号、重定位、行号和 Debug 文件指针按新物理位置调整并核对引用内容；未知非零间隙及不支持的格式拒绝输出。

虚拟布局验证将映射后的完整代码/数据逐字节比较；允许改变的映射内字段只有经过单独验证的 Debug 文件指针。文件头只允许物理文件指针、raw size、物理字节汇总和 checksum 变动，禁止修改 `.text VirtualSize` 来消除文件空隙。该静态模型和 dumpbin 读取不能替代 Windows 加载器或实机验收。

在主仓库中复现：

```powershell
& 'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' tools/hc-native-small-trim/pe_small_trim.py --runtime PowerControl/handheldcompanion-runtime/YMCC-DEVICECORE-20261018-v1 --output D:/Codex/2026-10-18/kai-s/work/hc-followup-trim/native-small
& 'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' tools/hc-native-small-trim/static_selftest.py --evidence D:/Codex/2026-10-18/kai-s/work/hc-followup-trim/native-small
```

结果见证据目录 `HANDOFF.md`、`RESULT.json` 和 `STATIC-SELFTEST.json`。`static_selftest.py` 的两个 offset-fixture `.bin` 是指针搬移测试数据，禁止执行。

本轮结果：GamepadMotion/hidapi 无安全净收益；Xinput 仅产生 512 字节静态候选。生产文件未替换，正式包节省 0 字节。为这点空间不建议采用 Xinput 候选。GamepadMotion 的 YMCC 原生消费入口另外核对了 18 个名称，候选仍保留全部 32 个原导出；未用 HC/FanHost 根替代原生调用集合。

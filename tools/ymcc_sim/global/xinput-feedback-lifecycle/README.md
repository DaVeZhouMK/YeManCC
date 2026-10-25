# XInput 实体反馈生命周期接入

独占目录 xinput-feedback-lifecycle/。导出 `getXinputFeedbackLifecycleAdapters()`，id `controller.xinput-feedback-lifecycle`。

## 模式与执行边界

本包装必须显式 `--mode current --evidence-dir <new-owned-root>`；原driver固定收到 `--current <完整字节身份的私有main快照> --out <case>`。不转发任意参数，不允许默认原型模式、生成产品patch或写native主线。执行时冻结当次producer、C++夹具、native与policy字节；本批授权的总库接线及停止结果修复分别保留原前像和新源码，不把历史包装描述当作当前源码从未修改。

实际编译从主程序抽取的physicalFeedbackSlotCurrent/StopTarget/Finish、nativePhysicalFeedbackReady/Start、原调度函数、slot身份分类和断连片段。XInput/MSI写者、来源观察、时钟和调度唤醒由原夹具的内存终端承担。原模板中的g_feedbackSetState为fakeSetState，MSI写者仅更新夹具状态；所有编译、原exe和dumpbin位于现有自建Windows Job。另检查实际exe导入，不允许XInput/HID/USBIP等设备DLL或DeviceIoControl。

源码的实际调用者包含手动gamepad.vibrate/反馈pulse和触摸haptic调用者。本项不构成InputHost的OutputReceived/OutputDecoded订阅，不关闭游戏反馈到实体的回路。UI/消费合同中原有未闭合状态保持。

## 私有准备与正式验收分开

`--private-snapshot`仅验证有完整字节身份的私有源码。结果固定为 `PRIVATE_SOURCE_SNAPSHOT_VALIDATED_NOT_CURRENT_HEAD`、passed=false，并写private-result.json；即使完成时真实文件恰巧同sha，也不能当作正式current-head。公共reader始终拒绝该私有envelope作为正式PASS。

当前首次私有验证：原current函数两个diagnostic配置各37场景/168检查/0失败；原历史夹具两个配置各35/162，原断言各检出21失败。两个真实正式header故障（Unknown盲目zero、不同实体路径复用）先编译成功，再由原fixed-off业务断言失败；原脚本和断言不改，编译错误不算检出。

正式公开global必须等协调者给出稳定来源批次，再登记并执行一次/audit。此时去掉private-snapshot，执行结束还须验证全部当前来源与捕获字节相同。不能把旧私有镜像重签成当前源码。

## 历史基线来源

原driver必须预置baseline-fragments.json。这里逐字保留两份历史原件一致的sha256 `91a4cd8d7e3fd9f3cb8f564eeaa10fab94142beaf49fd06184dcd5e4501b89ab`，执行前后复核，不从当前源制造旧基线。

该基线CLASSIFICATION含R103 dirtyDiscovery/boundaryFullScan前提，而相邻main.cpp.before-adoption没有该前提；另9个单独片段逐字匹配。其历史声明的整文件source sha与相邻adoption前像并不相同。因此只把old profiles标为固定历史source-derived fixture，不宣称完整、独立验证的产品前像。两个current生产源码私有故障提供独立的当前断言检出证据。详见baseline-provenance.json与来源预审交付。

## 工具与原始证据

使用原安装MSVC/SDK。原driver将本机CP936编译器输出按UTF8保存，故包含诊断前缀会损坏；包装仅从双冒号结构提取实际绝对路径，再验证它们属于安装VC/SDK文件，并钉住真实观测191个include。与已审安装文件超集合并为333文件，加明确编译/运行工具共343。首次观测解析FAIL保留，原业务已通过；旧发现轮不补签。该身份范围不宣称完整操作系统或所有潜在SDK库覆盖。

完整source-ranges、原compile/execute命令、原JSON终点、EXE/OBJ、imports、原stdout/stderr与scope-drift均绑定。原终点JSON和自然退出必须同时存在；非法模式、缺终点和超时都拒绝。后补的精确stdout终点检查已在原私有执行输出上单独重验，没有追着正在变动的main重复业务。

私有产物6种字节变更由原公共reader的sourceMismatches实际检出，恢复后checksum匹配；私有passed=false仍一直拒绝，未伪造一个PASS容器来冒充当前验收。正式来源批次时再验证实际公开case的完整通过范围。

## 总库实体写者批次（2026-10-19）

现有producer和夹具已定向扩充，原业务断言及历史基线保留；当前夹具不再声称与扩充前逐字节一致。新增57场景/260检查（诊断开、关各一配置），7类当前源码故障必须经真实编译后触发指定业务断言。MSI已替换原写者成功桩：实际执行sourceSnapshot/Current、HID主游戏身份/Usage/长度准入、原帧构造、MsiFeedbackLeaseOps、FeedbackLease、原Write与停止函数，最终Win32/HID操作受控。覆盖6/32/64/96长度、大小马达不等、短写/错误/超时取消、未完成资源保留及后续回收，不操作设备。原source-derived旧35/162失败仍不是完整产品前像。

原StopTarget/Finish/调度/物理身份代码保持；总库令牌只标识已选写者，不能授权来源、硬件或游戏反馈。完整InputHost游戏回调和UI仍未闭环。私有fb0/fb2/fm1与独立head检查fb4分别保存，最终公共结果另有自己的实际来源截面，不能用旧sourceStillCurrent布尔值替当前源码续签。

后续停止失败真实反例发现MSI StopTarget忽略zero结果，已限MSI分支修复：同来源失败保持未决，复用原DeferredStop的有限重试；耗尽后无周期timer，由已有MSI来源发布/撤销边沿唤醒原worker。新helper实际执行；两处完整source发布/撤销函数只核验原调用hook，未在本夹具运行完整设备生命周期。新增失败zero、耗尽、晚到通知、排程不可用、替换来源和新pulse归属断言，当前64场景/299检查，9类语义故障在fixed-off实际编译后检出；正例fixed-off/on各自完整运行。早期57/260和原MSI失败反例分别保存，原历史91a4并非完整before-source的边界不变。游戏反馈、跨提供者联合归还和物理效果继续保留未验收。

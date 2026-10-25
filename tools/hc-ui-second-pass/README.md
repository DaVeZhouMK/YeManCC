# iNKORE UI 第二轮方法裁剪

只在冻结、无 HC 窗口/模板的 device-host profile 上使用。生产 profile/runtime、HC、SDK、Host、native 均不是本工具的写入目标。唯一提升对象是验证输出 `replacements` 中的两份 iNKORE DLL。

完整离线入口，所有生成物放入新空目录：

```powershell
& G:/YeManCC-Work/Mainline/YeManCC-source/YeManCC/tools/hc-ui-second-pass/build-and-verify.ps1 `
 -RuntimeRoot D:/Codex/2026-10-18/kai-s/work/hc-followup-trim/ui/input `
 -SigningKeyPath D:/Codex/2026-10-18/kai-s/work/hc-large-strip/binary-trim/keys/YMCC-DeviceCore-Development.private.pem `
 -OfflinePackageRoot D:/Codex/2026-10-18/kai-s/work/hc-large-strip/binary-trim/packages `
 -CallbackGoldenPath D:/Codex/2026-10-18/kai-s/work/hc-large-strip/binary-trim/callback-original-path-golden.json `
 -OutputDirectory D:/Codex/2026-10-18/kai-s/work/hc-third-trim/ui/new-reproduction
```

默认直接调用上一轮原有 `hc-ui-dependency-trim/build-and-verify.ps1`，重新获得原样 baseline 的完整回执和字节相同的 825344 B 输入。可传 `-BaselineRun <既有完整复现目录>` 复用已经通过的原样 baseline；这会核对 verification、源码 pin 与两库批准 SHA，所有 runtime 文件另外逐 manifest 哈希复核。没有复制或削弱上一轮七构造/22回调/缺库检查。

第二轮仅删除不可达方法，绝不生成空方法或改返回值。不再删类型、字段、属性、事件和资源。构造/静态构造、virtual/override/finalizer、所有接口实现（含继承）、属性事件访问器、WPF Get/Set/On 约定族、operator/conversion、方法属性、序列化/转换器/Attribute、反射动态调用所属族、全部外部调用、字符串和 type-token 反射根，以及它们的完整 IL/ldftn/ldvirtftn 闭包均保留。分析冻结 manifest 里全部 93 个托管文件，并追踪非 UI 所有者的 UI 基类，防止漏掉以 HC 派生类型为声明方的继承方法引用。

`method-closure.json` 保存每个根的原因、每条本地调用边、删除方法的原始 IL 大小和入边、接口/动态/serializer 调用盘点。`audit.json` 重新从 baseline 计算闭包，对全部保留方法比较指令、作用域、局部变量、InitLocals、MaxStack 和异常区域；对完整类型/属性/字段/事件/方法形状、全部托管消费者的 UI 引用、资源逐字节、DP 注册/委托目标和开发签名做独立比较。候选七构造和全 HC GetTypes、22 callbacks、glyph、74项表值及四个带默认上下文原版诱饵的缺库/deny 反例，直接复用上一轮原始 probe。错误 token 由真实严格 resolver 拒绝；另对两个 DLL 分别破坏签名字节，确认 RSA 数学验签拒绝。不能把 .NET 加载成功当成密码学强名称验签。

新增 `UiDpProbe` 只补充上一轮未覆盖的两个既有安全构造的属性路径：FontIcon/FontIconSource 的 Glyph、FontSize、FontStyle、FontWeight 共8项，原始输入/上一轮/第二轮真实 getter/setter 和 DP callback 结果相同；829项实际 interface mapping 在上一轮和第二轮完全相同。无需创建 Window/Application、模板、渲染/测量、真实设备、Host、Steam、SDL 或网络。

使用既有 development 密钥，token 538ea1f68f629a04。这不是 publisher 签名。Windows mscoree API 原始结果单列保存，不能用 RSA 数学通过覆盖 API 返回 false 的事实。其余动态 UI/资源/反射族保守保留；完整 HC 窗口和任意未知外部插件仍不属于此 profile 的支持范围。


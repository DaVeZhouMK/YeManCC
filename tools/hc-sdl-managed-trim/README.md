# SDL3-CS 托管层精简工具

本工具对固定 HC 设备核心的 SDL3-CS 叶子依赖做离线 Mono.Cecil 精简。它不运行 Host、设备构造、控制器服务、驱动、EC、原生 SDL 或安装产品。生产接入由主线所有者完成，只替换单个 SDL3-CS.dll 并更新所属清单。

当前接受的输入和输出哈希在 `pins.json`。默认输入为完整冻结副本 `D:/Codex/2026-10-18/kai-s/work/hc-followup-trim/sdl/input-snapshot`；主线正在去重或改 UI，所以不要用新主线目录冒充这个已固定的输入。原始输入 manifest SHA 为 `65a3ed817b38ba36ef974d9c005bd4ca7057b222efa920d25a4fa16e74b2eb18`。除 SDL3-CS 外，其余输入文件都保持原样。

```powershell
& 'G:/YeManCC-Work/Mainline/YeManCC-source/YeManCC/tools/hc-sdl-managed-trim/build-and-verify.ps1' `
  -OutputDirectory 'D:/Codex/2026-10-18/kai-s/work/hc-followup-trim/sdl/reproduce-01'
```

输出目录必须为 ScratchRoot 下的全新目录，脚本不会清理或覆盖旧证据。构建使用已安装 .NET 10 和固定哈希的本地 Mono.Cecil，以及一个空的本地 NuGet 源；不访问网络。输出包含 `candidate/SDL3-CS.dll`、`DELIVERY.json`、正反例证据、完整源码回执和软件探针 runtime。软件探针 runtime 与故意破坏的 negative fixture 都不能整体导入生产。

- `Program.cs` / `SdlManagedTrim.csproj`：扫描完整 runtime 中每个托管 DLL 的所有 SDL TypeRef/MemberRef，保留全部 HC 的引用而非仅当前 headless 路径；递归保留方法体引用、泛型和约束、属性、返回/参数类型、接口与虚方法、所有保留实例字段、枚举常量、委托运行时方法。保留被引用事件联合体的全部分支布局。
- `untouched` 模式完全保留每个被保留方法的 IL，包括 SDL 原静态初始化器，得到 792064 B 的保守候选。
- `delegate-leaf` 模式继续删除无消费方的私有静态委托缓存。只识别顺序执行且无局部变量、分支、EH 的初始化器中的精确 `ldnull / ldftn / newobj(Runtime delegate ctor) / stsfld(private static field)` 四指令组。其余指令全部保留，已保留字段的初始化组也全部保留。删除组中的 ldftn 目标没有被调用；不存在硬件 API 替身。原件字段是 private mutable cache，并非 readonly。
- `Audit.cs` / `Audit.csproj`：独立重读候选，验证完整源 runtime 所有直接 SDL 引用真正绑定到候选、候选内部引用闭合、保留方法语义及原生入口/返回/参数封送描述符等价。独立重建允许删除的初始化组，而不直接信任裁剪器的删除清单。
- `SurfaceProbe.cs` / `SurfaceProbe.csproj`：全新 OS 进程实际 Path 加载，拒绝所有非框架原生加载；比较保留类型/枚举/结构和实例字段的真实 CLR/Marshal 布局。HC GetTypes 必须为1768。
- `NegativeMutation.cs` / `NegativeMutation.csproj`：只生成明确标注 INTENTIONALLY-INVALID 的结构体偏移负例，验证审计和实际 Marshal 布局对照确实拒绝它；不修改任何真实硬件 API。
- `build-and-verify.ps1`：固定输入快照、离线构建、两种实际候选、独立审计、布局正反例、沿用未修改的原 SDL touchpad/timer 探针、22项真实 HC 软件 callback 对照、哈希/无 SDL 文件与模块检查。探针源来自 `tools/hc-devicecore-trim/probes`，只读使用，所有构建产物写 scratch。

最终候选 47616 B，SHA256 `12920248a03c280ec83f2bcef1b5adf758568ed64c1800e37db127ddfd8770b4`。原件1300480 B，减少1252864 B（96.3386%）。保留程序集身份 `SDL3-CS, Version=3.4.10.0, Culture=neutral, PublicKeyToken=null`，不改 HC。保留的 SDL 元数据有93个 TypeDef（含 `<Module>`，CLR反射92个）、218个方法和1191个字段；保留33个真实原生入口。1178个无消费方的私有委托初始化组被删除，33组仍保留。

这是固定现有 headless 设备核心的精简候选，不能据此启用 SDL 主动控制器服务。未知动态反射、真实控制器生命周期、完整 Dummy/IDevice 构造、EC、实际睡眠/唤醒、CPU/功耗不在已验证范围。主线若改变 HC 的 SDL 引用，应对新的精确消费者再次运行独立解析与接入测试。

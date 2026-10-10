# 导出器完整身份与发布修复（2026-10-10）

## 边界：严格的是导出器，不是程序

本批只改 tools 下的构建、导出和验证脚本。没有改 YMCC/Decky 的运行逻辑，没有新增程序启动/缓存/版本一致性拒绝门。export-identity.json 是随包审计元数据，程序运行不读取它来拦截用户。测试未部署产品、未清缓存、未重启 Steam。

## 原始问题与证据边界

本机 Y 图标正常不能替代最终 ZIP 验证。只读检查此前正式 ZIP 时，其 Decky 插件 SHA256 为 66d4ae73…，实际导出的插件 factory 也返回居中的 SVG Y。ROG 日志的 Native 身份却与当时正式 ZIP 不同；因此不能凭现有证据声称某个已提供 ZIP 一定用了旧 span，也未验证远端 Steam CSS。此次补齐的是导出全链的一致性与防混版保证。

## 更新内容

1. **构建绑定**：版本生成后、编译前捕获源码内容；编译结束再次对拍。保存 Native、Web 全文件以及六个 Decky 资产的 SHA256/字节数，产生唯一 buildId。构建过程中改源码不能取得 BUILD_OK。
2. **独立插件产物**：插件留在本次 Build/App/Decky/plugins/ymcc-sidebar，导出器从该目录复制，不能偷用另一目录的 dist 或只热修本机。
3. **暂存对拍**：暂存 Native/Web/Decky 与构建捕获逐文件一致；旧 Web chunk、缺文件、同长度改内容均拒绝。
4. **最终 ZIP 全量校验**：完整快照的所有文件对拍 SHA256 和字节数，拒绝缺失/多出/重复大小写或斜杠路径/危险路径；六个 Decky 资产还沿独立专项门对拍。包内 export-identity.json 列出该次 buildId 和完整内容清单；Release/version.json 也给出 Native 与插件 SHA256。
5. **延迟发布与回滚**：ZIP 验证通过前不轮转旧 Release。发布失败时恢复已备份项目；备份未移动的旧目录不再被错误删除。回滚路径限定在该工作区的 Release/Backup，错误不再静默吞掉。不同导出批的备份目录不重名。
6. **工作区互斥**：同一工作区不能由两个构建/导出器同时写入；只影响开发/导出工具。
7. **跨 PowerShell 稳定**：内容索引使用 Ordinal 排序，PowerShell 5.1/7、文件枚举顺序或词典顺序不影响指纹。没有用改文件时间冒充新构建。
8. **保持原发行规则**：唯一完整 YeManCC.zip、原驱动与 HC/FanHost 资产身份、优化继承、嵌套 CustomSteamLibrary、原全局/专属逻辑等没有放宽；所有原发行门仍运行。

## 验证

- 完整导出器隔离回归：37 项。
- Decky 导出回归：13 项；原四目录/六资产/六排除项分类回归通过。
- 原生产 TSX presentation：27 项。
- 从最终 ZIP 读取并在无 IO 的浏览器环境执行真实插件：5 项，确认导出的是原生居中 SVG Y。
- 真实完整构建和 package-release.ps1 全链通过，430 个最终 ZIP 文件全量对拍；原优化继承、发行 ZIP 身份与 FanHost 发行门均通过。
- 在真实构建目录故意破坏插件一个字节，再运行正式导出器，实测以 EXPORT_BUILD_CHANGED 拒绝；已有成功 Release 的目录与 ZIP 字节均未变。测试后构建产物恢复并复验。
- 真实成功发布保留旧三个 Release 目录的备份标记，内容均不丢失。
- 已安装 Native/插件与原正式 Release 全树只读复验未变。

## 如何导出

在主线源码目录使用既有入口 pnpm run release（build → package）；只打包时要求存在新版构建身份记录。升级导出器后不能把旧构建标成已更新，首次必须正常重建。错误包括缺构建记录、源码与构建不一致、产物被改、暂存/ZIP 混版等，均由导出器拒绝并指出失败阶段。程序端没有新增这些检查。

## 本批实际输出

- 完整包：G:\YeManCC-Work\Build\Tasks\YMCC-Decky-ExportIcon33\FullRelease-20261010-133801\Release\Packages\YeManCC.zip
- 版本：0.0.34
- buildId：2ce7faf8d7b984112f0879130ad9423735aca4050b4ded3d66af92f3bf092973
- ZIP SHA256：2c7a14f9140b87cbf6f096b14b488afb653f15bf1eda23d6b5fe0e7edbf27c3b
- Native SHA256：b60270b987b195b873c599e92d2b9856dffc82ad71c4f2c47e5307e7ff989f3c
- Decky 插件 SHA256：c68fbf8229a7df88ac5e45f2652e328775172f8017fbdfad95825488e03c3fc5
- 状态与证据：G:\YeManCC-Work\Build\Tasks\YMCC-Decky-ExportIcon33\validation\CURRENT-STATE33.json

本批包在隔离发行目录内，**没有覆盖旧正式发行目录，也没有安装到本机**。远端 Steam 的实际 CSS/截图以及新用户冷安装仍需单独验收，不凭静态/模拟检查宣称已通过。长期安装验收 goal 保持暂停。

# Steam 外观 v2：紧凑开关、自带主题与可恢复接管

日期：2026-10-10（Asia/Hong_Kong）。本次按用户反馈修改。

## 界面

- 删除“全部效果已开启/关闭”文字和开关外层按钮框。
- 裸开关移至右上角 7/7 左侧。
- 下方仅两个等宽并行按钮：编辑外观、重新应用。
- 全屏编辑页新增“恢复预设”，二次确认中提供“恢复 YMCC 预设”和“恢复接管前的外观”。B 取消确认不会误退出编辑页，方向/A/B/Y 使用原有手柄流程。
- 无滑块或参数编辑器；原有参数继续保留，恢复默认预设才会重置参数。

## 资源已经进入 YMCC 和源码

本机：C:\SOFT\YeMan\PowerControl\steam-appearance\themes
源码：G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\PowerControl\steam-appearance\themes

完整 7 个主题，61 个原始资源文件，另含 manifest.json 与 preset.ps1，共 63 个打包资源/模块文件。作者和原 theme.json 元信息保留。运行不再依赖 C:\Users\DaVe\homebrew 的开发机模板；按当前用户/实际 CSSLoader 主题路径套用自带资源。

已为 PowerControl/.gitignore 添加仅针对不可变主题定义/默认配置的例外，避免这些 JSON 被误当作用户运行状态忽略。实际发布过滤函数已验证包含这 63 个文件，并核对主题 JSON 对应清单摘要。资源不会写入用户的运行状态再拿去打包。

## 容错与优先级

- 首次套用：先在当前用户 LOCALAPPDATA\YMCC\SteamAppearance 下创建经过摘要验证的事务备份，再复制 YMCC 预设并暂停其它已启用主题。
- 同名主题位于其它目录：备份其 manifest/config，使用可逆的停用占位定义消除加载器同名歧义，不删除其目录或资源。
- 第三方坏配置：备份原始字节后写入停用配置；恢复可以还原坏配置的原字节，不把恢复误称为修复第三方主题。
- 已接管后的资源修复保留有效的现有开关和参数，不能把“修复”误判为首次套用并关闭其它分项。
- 资源校验失败、路径穿越、目录链接或写入异常：拒绝危险操作，写入中途失败回滚已经变更的文件。
- 每次操作重新读取状态，过期配置或中途出现新冲突不冒充应用成功。
- 恢复预设会重置 7 项开关和参数到随程序携带的默认值；恢复原外观还原首次接管前的备份。后续事务备份仍保留在备份目录。
- 不执行 Steam 重启。只有首次安装/覆盖、修复、重置或恢复等需要重新读入资源的情况，才重新加载已核实身份的 CSSLoader 后端，保留单实例互斥。
- 实测旧 CSSLoader 退出后会留下旧样式 UUID 与 test_css_loaded 标记；重载时仅在核实为 Steam 的本机 CEF 页面中清理 CSSLoader 自有样式/健康标记，让加载器重新注入。不会清理 Steam 自带 CSS 或 YMCC 的其它注入样式。

## 已完成验证

- 29 项其它用户/坏配置/重复主题/资源修复/重置/恢复/校验/路径/回滚检查。
- 28 项实际 Vue/typed service/手柄仿真及布局检查。
- 6 项真实 Steam 预设恢复检查：圆角 10px → 人为调整 30px → 恢复默认 10px；恢复原外观、再次套用均成功，Steam PID 不变。
- 原有 21 项脚本单实例检查、9 项真实 Steam 开关检查继续通过。
- 最终 Vite 生产构建通过。外观专项测试全部通过；最后一次全项目 vue-tsc 发现其它并行开发的 MonitorHudPreviewView.vue 第 20 行 TS2367 字面量类型比较错误，本任务没有修改该文件，不能把当前全项目类型检查报告为通过。
- 验收后 61 个主题资源与原参数 SHA-256 完全一致。

预览已更新在 G:\YeManCC-Work\Deliverables\SteamAppearance-20261010\preview，继续由原本机地址提供；其控制是内存适配，不会修改真实 Steam。验证证据在同目录 validation。

本机主题资源及控制脚本已同步；新 UI 源码和可交互预览已更新。没有替换正在运行的 YMCC EXE 或覆盖其它正在开发的功能。
## 启动 BAT 边界及最终核验

用户要求只检查启动 BAT，不直接修改。最终审计发现早期阶段改过 BAT 的进程检测分支，现已撤回这项改动：源码、本机和交付目录中的 YeManSteam2.bat 均与本任务开始前的原始备份逐字节一致，SHA-256 为 D788CE2906E67A4E4CCFE3538A1FF51A1BEDECF568C4D979F51CC2D2954E2479。撤回前的版本另行保存在 G:\YeManCC-Work\Backup\SteamAppearance-v2-20261010\bat-correction。

原 BAT 已能通过 YeManSteam TXT 联动启动 CSSLoader，适配现有外观控制。原有 tasklist 默认表格对长进程名截断的风险仅作为评估记录，未继续修改 BAT；因此历史 bat-result.json 不属于现版本 BAT 验收。外观页面自己的启动/重载仍使用独立 helper 单实例互斥控制，不需要重跑完整启动 BAT。

最终再执行的外观专项测试：29 项预设/其它用户容错、28 项浏览器/服务/控制器仿真、21 项脚本/单实例均通过。已完成的 9 项真实 Steam 开关和 6 项真实 Steam 重置/恢复报告保留，未重复操作用户 Steam。63 个自带资源在源码、本机、交付目录一致，实际发布过滤函数包含全部 63 项；61 个用户原主题资源与原参数摘要仍完全一致。当前 CSSLoader 就绪、7 项已开启，配置与后端一致，恢复可用，无待重载状态。

最终核验记录：G:\YeManCC-Work\Deliverables\SteamAppearance-20261010\validation\final-audit-v2.json
最终变更清单：G:\YeManCC-Work\Deliverables\SteamAppearance-20261010\change-manifest-v2.json


## 后续健壮性核验（2026-10-10）

请以 STEAM-APPEARANCE-ROBUSTNESS-20261010.md / Robustness-20261010.md 及 validation\robustness-audit.json 为准。加固版已进入源码和交付副本，安装目录同步命令被工具策略拒绝；本机仍保持先前脚本。后续全项目类型检查和最终构建已通过，本文上次类型错误记录仅为历史快照。

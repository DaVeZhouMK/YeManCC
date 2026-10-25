# 灯光普通安装入口边界复现

本工具只复现当前 `deploy-installed.ps1` 的灯光复制子段，以及既有 v4 连接器的旧原生程序拒绝行为。它不执行完整部署、不修改发布器、不启动产品、不做设备 IO，也不把子段复现当成全入口绕过证明。

复用同级 `lighting-component-continuity/v4/fixture.zip` 的原始固定证据。新目录实际校验 ZIP、索引和955成员，恢复独占 D 盘安装副本。PS7 与 Windows PowerShell 5.1 分别从当前部署脚本 AST 提取原赋值及 if 段，执行真实目录移动和复制；固定旧原生程序9e9与新助手b6组合，记录只有助手变更；另各持有一次源DLL的真实独占文件锁，复现旧目录已移入备份、复制失败却无子段自动归还。然后原生成器产生全新 v4 计划，替换独占副本中的 native 为原9e9，再实际执行原连接器，要求写入前拒绝、105文件保持原样。源码、fixture、原回执均不改写。

运行：`python tools/ymcc_sim/global/lighting-installed-preflight/run.py --out D:/Codex/OWNED_FRESH_PATH --ps7 ABS_PWSH_EXE --ps51 ABS_POWERSHELL_EXE`。使用本机现有 Python；仅启动四个复制子段进程、原计划生成器和原连接器。子进程使用进程级ExecutionPolicy参数，不修改系统或用户执行策略。全部子进程隐藏窗口，进程号、自然退出和原始stdout/stderr保留。结果 `PASS_BOUNDARY_REPRODUCTION` 代表复现与拒绝对照正确，**不代表产品缺口已修复或全产品验收通过**。

普通部署已有源码/构建身份、产物新鲜度和最后导出检查；本工具不移除、不绕过它们。真实发布器是否需增加候选/匹配元组的前置门及复制回退，由其现有owner根据完整链路评审后实施。当前不登记新public场景、不改既有六工具/v4叶，也不增加运行DLL。

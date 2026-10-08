# v10.1操作命令卡 · 2026-10-04

R6产品任务已由用户启动；这里不增加新的全局HOLD/审批。命令必须逐条按实际阶段/路径/动作授权使用，不整段自动运行。采集工具离线测试不会启动产品，真实动作需本批可信身份/场景。

## 1. 完整性与ROG离线复测（不涉及产品）

使用合法允许脚本的PS5或PS7；入口加载前拒绝保留控制台/exit，禁止Bypass、修改策略或动态加载脚本绕过。

```powershell
$kit = 'C:\CPU\ROG-CPU-COLLECTOR-20261004-v10.1' # 替换实际路径
powershell.exe -NoProfile -NonInteractive -File "$kit\Invoke-ROG-CPU.ps1" -Action validate
$LASTEXITCODE
# runner报告写到新目录；collector受写集门约束，只能写kit/validation的新collector-validation文件名，不覆盖随包报告：
$run = [guid]::NewGuid().ToString('N')
$out = Join-Path (Split-Path -Parent $kit) ('ROG-v10.1-offline-'+$run)
powershell.exe -NoProfile -NonInteractive -File "$kit\tests\Test-ROG-CPU-Runner.ps1" -ValidationDirectory $out
$LASTEXITCODE
# collector源码未改，不为本补正强制重跑ROGcollector；以下仅有独立需要时运行：
powershell.exe -NoProfile -NonInteractive -File "$kit\tests\Test-YMCC-CPU-Collector.ps1" -OutputPath (Join-Path "$kit\validation" ('collector-validation-ROG-ps5-'+$run+'.json'))
$LASTEXITCODE
```

有合法PS7时换pwsh.exe并用不同结果目录。每次立刻保存exit及打印的ROG_RESULT/VALIDATION_REPORT精确路径；真实计数以JSON为准，不要求固定140项。validate应valid/manifest.verified=true且rogRegressionPassed=false，不能据此签产品或CPU通过。

## 2. 本批需要时的只读身份发现

```powershell
$exe = 'C:\填写本批ROG产品目录\YeManCC\YeManCC.exe'
powershell.exe -NoProfile -NonInteractive -File "$kit\Invoke-ROG-CPU.ps1" -Action preflight -ExePath $exe
$LASTEXITCODE
# 仅确需高权限且preflight动作获授权时，改用一次有限请求：
powershell.exe -NoProfile -NonInteractive -File "$kit\Invoke-ROG-CPU.ps1" -Action preflight -AuthorizedAction preflight -AllowElevation -ExePath $exe
$LASTEXITCODE
```

status可带当前候选TargetPid而不带生辰做只读发现；不能沿用旧PID1632或估计创建时间。缺字段、类型错误、实际拒绝分别写。只从本次明确result/worker-result取读回事实，唯一匹配才绑定身份。默认不RunAs；AllowElevation和同名AuthorizedAction缺一不可，已经高token不再提权。记录请求/授权/尝试/实际权限、同用户/nonce/单次/worker退出；无UAC不是高token证明，“未尝试”不是“不可提权”。

## 3. 启停、功能和采样

执行方按脚本真实param实施：可信源/包hash、精确当前PID＋creationTime、对应动作授权仍必需；重启后重取身份。先正常退出，不按名kill、不force，不用计划任务/SYSTEM/服务/ACL换通道绕拒绝。未知EC/OEM/安装驱动/部署等副作用依既有用户授权与域内裁决，不借此卡自动执行。

observe是真CPU采样而非预检；capture另需真实生命周期与功能证据。其参数契约含ExePath、ExpectedSha256、TargetPid、TargetCreationTimeUtc；请求提权还要AllowElevation＋AuthorizedAction observe或capture。其余场景/证据参数按脚本实际定义，不手填active/握手JSON绕门。

Fan虚拟握手先核现有受限AI通道及Send-YMCC-AI-Fan-Mock.ps1契约，普通UI不可达不代表没有能力；精确session/owned Host、零物理写，mock不签RPM/OEM/HC物理交还。手柄/gyro按产品执行方交接客户端/能力；缺口反馈本机，不猜flag/token/端口。

LogFile仅显式最多32个普通日志元数据/open-close，不扫描/复制内容或从存在推断开启。ROG集中验证稳定A/B，不为每个本机补丁反复跑。

## 4. 回写

入口加载前拒绝只交实际控制台/exit，不造result.json。进入入口后每次唯一runId，保留result、同次worker-result和阶段/固定错误码；失败也交，发现身份不授信为产品授权。排除worker-request、秘密握手/token/lease/authorization/confirm、settings备份、profile和私密命令行，不压缩整个CPU根。

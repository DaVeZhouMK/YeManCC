# 完整 Host 自测接入原型

此目录为 D 区待审查原型，尚未写入 G 或注册统一入口。`fan.complete-host-selftest` 只覆盖实际候选的 `Program.Main → FanHostSelfTest.RunAsync → 最终 SelfTestResult`。本轮仅执行纯数据合同测试、已有日志解析及 PowerShell 语法检查，没有启动 Host、GUI、真实后端、硬件或安装事务。

适配器必须由现有 global powershell 路径运行，继续复用 `host-process.ps1` 的启动前事件门和 Windows kill-on-close Job。入口仅检查自己已处在 Job 中，不另建所有权体系。它在最初核对、紧接启动前分别执行 NoConsumers；未知或任一现有 YeManFanHost/YeManLightSetter 都阻断，不接管或停止它们。软件层的进程空闲检查仍有竞争窗口，Host 原有机器所有权门继续裁决；拒绝不能转成接管。

Host 的参数列表写死为 `['--self-test']`。无端口、session token、候选 HC 注入、real-backend 或硬件写开关。TEMP/TMP/LOCALAPPDATA/APPDATA/自测日志根全部落在当前 case，复用 Host 自己的 self-test 日志隔离。不会读用户 token/lease 文件。受控进程只可按本次实际 PID 回收，外层 Job 收尾其后代，无按名称杀进程。

调用前必须提供外部审阅合同：Host DLL、Backend DLL、apphost、payload manifest 四元组及完整平面 payload 成员的 SHA-256，还要固定 Node 与既有 Job helper 身份。描述器自动把合同、Node、全部 payload 成员放入 artifactInputs，不可通过漏登记第十份文件换来通过。脚本在启动前与结束后重新核对集合和字节，且再次核对 manifest 声明与外部合同。候选编译来源需要独立冻结归档；实时 Program.cs 只作合同参考，不冒充任意 DLL 的构建来源。

完成成功要求：实际 PID、固定参数、明确候选、两次 NoConsumers、既有受控 Job、stdout/stderr 原字节完整捕获、自然退出且 exit 0，以及唯一且位于结尾的真实 JSON 成功标记。结果必须有 `ok/checks/finalState/handshake/openState`，检查数仅由真实字符串检查列表长度得出，最终硬件标志和安全握手也须符合合同。异常、中止、超时、exit 0 但缺标记、只有 checksPassed/40 项只读门或 601/62 子套回执均不能代替完整自测。没有完整标记时检查数为 null、checkCountObserved=false，不伪填 400。

已有 149/73 候选的 FAN-934 异常与空 stdout 原字节保留。已有 98/d8 候选成功 stdout 解析出 428 项。两份旧运行均未重新执行，也没有把旧运行追认为本适配器的新受控执行：它们缺少本适配器规定的两次 NoConsumers 与 Job 记录。

## 接 G 和实际执行的任务书

1. 主任务固定本轮交付后，将 `src` 中 5 个实现文件和合同测试放到 `tools/ymcc_sim/global/complete-host-selftest/`；按统一路径运行已有 64 项测试。测试夹具已放入 fixtures，测试通过自身相对路径定位。D 原型运行时通过 YMCC_COMPLETE_HOST_TEST_REPO 指定只读的正式 runner 所在库；接 G 后自动按目录定位。YMCC_COMPLETE_HOST_TEST_ROOT 可指定测试临时目录。测试本身不需要 Host 窗口。
2. 用实际待测的新候选四元组和完整 payload 集合生成新的合同。示例 98 合同只适用于其 98/d8/675b/3afc 身份，149 合同只用于保留原失败；两者不得交叉使用。合同是外部输入并进入 artifactInputs，不能从当前目录自签后声称外部批准。
3. 在现有 extra adapter 注册器及 candidate 配置中仅加入此新 descriptor。用现有 `-PayloadRoot` 显式选择同一候选；descriptor 为 powershell/host-mock、full profile、requiresExclusiveHost/requiresPayloadRoot、blocked exit 10，超时 210 秒，内部 Host 上限 180 秒。保留所有现有测试、完整 full-nine 和已登记能力审计。
4. 由主任务与兼容线协调新的实际 Host 窗口；本阶段没有授权在本原型中自动运行。实际执行必须从现有 global guarded Job 开始，不能直接调用入口冒充受控运行。入口的全局环境标记和 IsProcessInJob 是必要检查，真正 Job 归属仍须由外层 processGuard/helper 原回执证明。
5. 归档 wrapper、Host 两层 PID、固定 argv、候选输入前后、两次 NoConsumers、Host 原 stdout/stderr、自然退出和唯一完整结果。新场景 PASS 只证明此候选 whole self-test；HC optional candidate groups 未通过参数启用，因此不在本次范围；不等于准入安装、full-nine 或硬件验收。

现有 `connect-installed-components.ps1` 的 checksPassed>=400 是其独立准入合同。本原型不会改它或替它伪造数字；未来接入使用真实 checks 数组长度，有无满足准入门由原门自行判定。

## 文件职责

- `adapter.mjs`：纯描述器及明确 payload 工件集合。
- `contract.mjs`：候选/执行/输出数据合同与纯解析，不启动进程。
- `Run-CompleteHost.ps1`：待集成实际入口，复用现有 Job 及 NoConsumers。
- `validate-contract.mjs`：启动前纯数据校验。
- `report.mjs`：产出统一可读取结果，绑定原始输出和自身实现文件。
- `contract.test.mjs`：64 项纯合同测试，其中包含实际原始日志与正式 structured reader 的拒改验证。

PowerShell 入口本轮只有语法与固定 argv 的静态检查；实际 Windows Job/NoConsumers/自然退出执行链尚未由本适配器跑过，必须保持待实测。
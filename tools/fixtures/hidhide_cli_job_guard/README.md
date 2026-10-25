# HidHide CLI 原子 job 启动叶（118，尚未接 main）

`native/hidhide_cli_job_guard.h` 提供 `ymcc::hidHideCli::JobGuard`，只准备 owned job 和 STARTUPINFOEX；不选择路径、不创建进程、不访问设备／Host、不做等待／重试，也不操作别人的 job。

```cpp
namespace cj = ymcc::hidHideCli;
cj::JobGuard job;
if (!job.prepare(ordinaryStartup, optionalFreshPrivateJobName)) {
    // job.error()；拒绝本次启动，不得退回普通无job创建。
    return false;
}
// 仍由原 processEfficiency::createProcess 主线程调用：
// flags = 原flags | CREATE_SUSPENDED | job.flags()
// startup = job.startup()
// 创建即归属，不使用 CreateProcess 后 AssignProcessToJobObject。
// Resume前仍按调用者原生命令/预算/所有权门校验。
```

`prepare(const STARTUPINFOW&, const std::wstring& jobName={}) noexcept`：默认匿名 job，可选调用方新建的私有名字；CreateJobObjectW返回ERROR_ALREADY_EXISTS立即拒绝，不设置或接管该旧job。调用方不可通过任意现有job名取得权力。ordinary.cb必须为sizeof STARTUPINFOW（不隐式合并另一份extended属性）；普通字段、标准句柄及STARTF标记原样复制，只有cb改成sizeof STARTUPINFOEXW。不改变调用方bInheritHandles或标准句柄继承策略。

job用nullptr security创建、验证HANDLE_FLAG_INHERIT未设置，不复制给child。唯一限制是JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE，没有ActiveProcessLimit、breakaway、全局权限或其他CLI限制。InitializeProcThreadAttributeList后以PROC_THREAD_ATTRIBUTE_JOB_LIST及稳定地址的job HANDLE在CreateProcess时原子归属；OS不支持或任意prepare步骤失败，释放本次资源、ready=false，不创建child或fallback。已ready对象再次prepare返回ERROR_BUSY，保留原拥有权，调用方必须看本次bool。

`ready()`、`error()`、`startup()->STARTUPINFOW*`、`flags()->DWORD`：准备后提供真实extended结构及EXTENDED_STARTUPINFO_PRESENT；失败时startup为空、flags=0。API不暴露job操作句柄。所有权在同一线程或调用方既有生命周期锁内序列化，无内部线程安全。

`requestStop(UINT exitCode=1) noexcept` 仅向当前ready的owned job调用TerminateJobObject，返回请求是否被接受；不等待、不清状态。返回true不是退出或idle证明。

`idle() const noexcept` 仅QueryInformationJobObject(JobObjectBasicAccountingInformation)并判定ActiveProcesses==0；没有ready或query失败一律false。rootchild Wait(0)不能代替子树观察。主线程拒新命令直到exact root退出和owned job idle分别成立；未知时将完整guard随exact pending句柄移动保留，不可遗失job。

`reset() noexcept` 先DeleteProcThreadAttributeList、释放其缓冲，再CloseHandle本对象创建的job，最后释放内部块；这可触发owned子树终止，不授予外部job权力。析构调用reset。Move ctor/assignment均noexcept且不分配，仅转移稳定heap块；JOB_LIST值及属性缓冲地址不随对象移动而变化。move assignment会先reset目的对象，调用方应确保其为空或已可释放，不能随意覆盖另一仍需保留的scope。

## 私有验证

```powershell
node tools/hidhide_cli_job_guard_selftest.mjs --out G:/<private>/fresh118 --toolchain G:/<existing>/production-link100
```

显式全新out、既有工具链，无安装／下载。实际编译此头文件，Release=/O2 /MT，Checked=/Od /RTC1 /MT，记录实际sourceDependencies、源码／工具／exe SHA-256、cl/link/dumpbin身份和imports。root与descendant都是本次专用夹具exe；无真实CLI/Host、设备、installed AppData或产品运行。写入和标准句柄只指向本轮私有目录。

真实Windows测试：CreateProcess使用EXTENDED_STARTUPINFO_PRESENT|CREATE_SUSPENDED，Resume前IsProcessInJob查询exact owned job；移动后实际创建保持原属性指针；父alive下child能写、继承stdout并自然退出。root另起descendant后自然退出，job accounting仍非idle，descendant结束或显式requestStop后再分别确认进程退出与idle。

父强退场景：observer另起私有native-parent；parent准备私有命名guard并创建child，发布PID/birth及Resume前membership回执，然后在事件触发后真实ExitProcess（绕过所有C++析构）。observer只OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION)观测exact child，没有job handle、没有DuplicateHandle，也不持另一个cleanup job；父退出后先独立确认child exit，再发迟到write请求，断言无执行。mutant下若child仍活，会明确观察迟到写，然后发正常finish事件使它自然退出，不能靠额外job杀进程伪造成功。测试bounded wait预算不进入产品。

故障仅在guard实际Kernel API叶/Heap分配包装上注入，覆盖prepare部分失败、inheritable-job拒绝、query/stop失败、现有名字碰撞、reset释放顺序；不替换guard算法。对所有成功测试不使用额外cleanup job。两个源码mutant实际编译：省略JOB_LIST update、去掉KILL_ON_JOB_CLOSE。原membership/父死后exit/迟到write断言必须捕获，编译失败或崩溃不算通过。

本交付只验证启动叶及owned job生命周期，不验证main实际CLI接线、命令超时策略、writer store、冷启动job reopening或跨native冷清理。父死亡至内核完成终止有调度窗口；证据要求先确证退出再验证禁止迟到写，不承诺ExitProcess同一CPU瞬间的零延迟物理终止。

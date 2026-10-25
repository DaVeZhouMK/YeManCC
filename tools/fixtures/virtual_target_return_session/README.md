# 历史归还记录存储会话（104，尚未接主线）

`native/virtual_target_return_session.h` 是 Windows 同步文件会话，复用实际101/103 record/store。它只持有同一路径的协作写者锁、读取历史责任、原子提交纯变换结果；未接 main、生产hook或任何runtime，不提供设备控制、冷启动清理或进程死亡判断。

## API

```cpp
namespace tr = ymcc::targetReturn;
auto session = tr::Session::open(explicitAbsolutePath, exactMachineBinding);
if (!session.ready()) {
    // session.record() == nullptr；保留 error()，不当作空历史。
    return;
}
const tr::Record* historical = session.record();
const auto revision = historical->revision;
if (!session.reserveTarget(revision, exactOrigin, {true, true})) {
    // error() 是变换或提交错误；旧内存和旧磁盘责任保持。
    return;
}
// 只有成功返回后，调用方才能按自身已有权限继续真实 dispatch。
```

- `Session::open(const std::wstring& absolutePath, const std::string& machineBinding)` 以值返回对象，要求C++17以上；对象不可复制、不可移动。析构释放锁。
- `ready()` 表示会话拥有writer句柄且有已加载/初始化的记录；不表示目标接纳、可输出、旧owner已死亡或允许cleanup。
- `record()` 返回 `const Record*`，打开失败为nullptr。该指针用于即时读取；成功变换会替换内容，调用方不得跨变换持有内部字段引用。`openedFrom()` 返回初次load的Missing/Valid/Invalid/RetryableIo状态；锁/路径阶段失败先看error。
- `error()` 返回 `const SessionError&`：`code`为None/Invalid/RetryableIo/TransformRejected，另有`win32Error`和`reason`。打开失败的错误不会被后续操作掩盖；操作失败不撤销已就绪会话，下一次明确成功操作清除操作错误。
- `reservePath(expectedRevision, exactOrigin, instanceId, ReservationProof)`、`reserveTarget(expectedRevision, exactOrigin, TargetCreationProof)`、`attachAcceptedPsPin(expectedRevision, exactOrigin, pin)`、`consumePath(expectedRevision, exactOrigin, instanceId)`、`consumeOrigin(expectedRevision, exactOrigin)` 均返回bool。先调用原纯transform，变化候选只有store committed后才发布；失败保持旧内存/旧文件。纯变换revision未变化时不调用store。
- 不生成或缓存Origin，完整Origin由主线程提供；不改变transactionId分组、exactOrigin及revision校验。`reserveTarget`调用原`reserveTargetCreation`，保留无物理源/无ACK/无pin责任；物理路径仍需原proof和非空物理根。
- consume由调用方先完成外部归还/目标不存在证明，再明确调用。此会话不收集这些证明，也不因读取历史、旧PID、最后路径移除或关闭会话而自动清旧origin。

## 锁与打开语义

明确传入的record路径先经store现有`fileDetail::normalize/parents`检查；同目录owner路径固定为`recordPath + L".owner"`并单独做normalize。父目录必须已存在。锁文件以CreateFileW、GENERIC_READ|GENERIC_WRITE、share=0、不可继承句柄持有至Session析构。先CREATE_NEW；已存在则OPEN_EXISTING，绝不截断/覆盖。share冲突立即返回RetryableIo，不等待/不重试。

**owner的完整规范就是零字节普通单链接文件，无magic或内容初始化。** owner通过store的`fileDetail::info`验证磁盘文件、非目录/非reparse、单硬链接，大小必须精确为零；再核对同一独占句柄上的文件身份/元数据稳定。新建owner的CREATE_NEW一旦成功，磁盘状态已满足同一个零字节规范，无后续WriteFile、ReadFile或FlushFileBuffers。已有零字节owner同样有效；非零内容（包括旧草稿V1 magic）、目录、reparse、多链接和未知file type拒绝，不删除或修复。当前互斥来自share=0的活句柄；空owner文件从不授予target身份、已接纳或cleanup权限。

持锁后只load一次：Missing初始化有精确machineBinding/revision=1的新内存Record，不立即写磁盘；Valid保留原历史；Invalid/RetryableIo无record、保留错误并释放锁。owner sentinel持久保留，正常析构只关闭句柄；进程即使在CREATE_NEW成功后、Session尚未收到返回句柄时异常终止，OS也释放锁，残留零字节owner可由下一进程重新持锁，旧record或Missing保持不变。record本身的schema/来源及store原子提交规则继续独立校验。

这是使用同一显式record/owner路径的协作写者协议。所有生产写者必须经过Session。`.owner`只保护跨Session／跨进程的协作写者；同一个Session实例的所有方法调用（包括ready/error/record读取）仍须由现有唯一Host生命周期锁序列化，内部字段读取也应在该锁保护期间完成。Session不是内部线程安全API。绕过Session直接调用store的写者不会被此协议约束。调用方仍须提供一致的路径名称、可信稳定目录；不宣称防御恶意同用户替换父目录、短名称别名绕过或伪造磁盘JSON。单次调用有限同步、无轮询重试/定时器/后台线程；底层文件系统API并无任意驱动下的硬实时截止保证。

## 验证

```powershell
node tools/virtual_target_return_session_selftest.mjs --out <全新绝对证据目录> --toolchain <现有捕获工具链目录>
```

runner只编译新session与实际record/store和本夹具；显式工具链目录，无安装、SDK或产品启动。Release=/O2 /MT；Checked=/Od /RTC1 /MT（不冒称算术溢出检测）；每个变体记录源码快照、实际编译输入闭包和SHA-256、编译器/linker/dumpbin身份、exe哈希和静态导入。

真实同文件测试使用本轮私有目录和新建夹具子进程：在活跃writer下竞争失败，正常析构后可重新打开，强制终止本夹具自建锁持有子进程、绕过C++析构后，另一个独立进程成功持锁并读回原target-only责任；只终止本次专用夹具进程。还覆盖实际hardlink和目录拒绝、未知owner标记、record/owner共享、record读写故障及owner元数据不确定性、失败store留旧内存和原文件、幂等零写盘、无ACK目标和混合债务、101/103真实交付JSON兼容及无自动清理。

reparse/未知file-type/读取中元数据变化拒绝通过文件API叶上的明确回执注入验证；不冒称测试恶意OS目录交换。无设备、SDK、普通Host、产品、Steam、睡眠、部署或导出动作。

四个源码语义负例必须实际编译并被业务断言捕获：owner放宽共享、失败store提前发布内存、幂等重复写盘、放过非零owner内容。编译失败或崩溃不算负例通过。所有失败与旧run保留，不覆盖证据。

## owner创建崩溃返工与证据

原marker草稿虽有39场景/251断言绿例，但遗漏CREATE_NEW与marker初始化之间的崩溃窗口，该交付已撤回。其完整源码、原run01及原RESULT/READY等保存在104证据目录的`history/marker-owner-delivery-run01/`，不覆盖。原owner payload写/刷盘/读/短读四个场景在零字节规范下改为“不得进行payload IO”的对照；其他责任/锁/故障测试继续保留，新增预存零字节owner及两个精确创建边界场景。

修复前红例模式：

`node tools/virtual_target_return_session_selftest.mjs --out <全新绝对证据目录> --toolchain <现有捕获工具链目录> --baseline-session <归档的原session头文件绝对路径>`

该模式仅编译Release/Checked归档模块，使用同一真实创建边界夹具运行两种前置状态：已有历史record、record为Missing。专用子进程在原生CreateFileW(CREATE_NEW)成功后，尚未返回Session、故尚未owner初始化或record访问时，通过仅用于夹具的事件回执停住；父夹具验证其他写者仍被拒，然后强制终止自己启动的子进程。下一独立进程必须ready并保留旧record或Missing。红例明确要求这两项业务断言失败，不能将编译失败或崩溃误作复现。

本次`pre-fix-red01/`中，原模块Release/Checked各2场景/28断言、恰好2项预期失败：下个进程因空owner返回Invalid；原record/Missing没有被写动。`run02/`使用修复模块执行完整42场景/283断言，并运行上述同一创建崩溃边界；每个正式变体有13个专用子进程、3次仅针对自建夹具进程的异常终止。两种第二进程均ready，旧记录字节或Missing保持原样。

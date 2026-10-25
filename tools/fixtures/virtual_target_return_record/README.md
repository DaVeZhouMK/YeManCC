# 精确虚拟目标归还记录（任务101）

仅为后续唯一生命周期提供数据与文件存储。未接 main.cpp、启动/退出/恢复 runtime、设备API、能力库、global wrappers。它不会认领设备或做 hide/unhide；磁盘内容通过结构校验也不证明来源可信、旧进程死亡、端点Absent或可解除隐藏。

## 数据契约

`ymcc::targetReturn` 位于 `native/virtual_target_return_record.h`。`Record` 有 `schema=1`（线缆键为schemaVersion）、精确 `machineBinding`、非零 `revision` 和 `obligations`。空obligations是有明确身份和修订号的tombstone，Missing/Invalid不等价于它。

每个Origin保留transactionId、parent/host的PID及creationTime、hostInstanceId/runId/epoch/targetId/persona/physicalRoot。支持的persona恰为steamdeck、dualsense-edge、elite。机器串不查询能力库、不设机型白名单。受支持的变换禁止同transactionId改写原tuple/owner；不同origin抢同一路径（ASCII大小写等价）拒绝，原路径拼写保留。

Paths只标记pre-dispatch-reservation或may-have-issued。前者已经是可能留下效果的责任，不得在崩溃后当作“没有下发”。只能在到达单个路径的实际dispatch边界时reserve，不预先认领批次尾部。`ReservationProof`三个值分别是dispatchBoundaryReached、exactPhysicalAncestry、absentFromPriorHiddenSet；这些只是调用方已核验事实的输入，模块不独立验证硬件，也不产生权限。原先已被他人隐藏且没有原责任的路径，不可凭当前列表新增。

新增 `Obligation::targetMayExist=false` 表示调用方已到达 PREPARE 下发边界且核验精确 Host 元组后留下的“目标可能存在”历史责任，包括没有物理源、未收到 ACK、还没有 acceptedPsPin 的情形。`TargetCreationProof{dispatchBoundaryReached, authenticatedHostTuple}` 两项必须同时为真；默认值均为 false。该位不表示当前目标接纳、可输出或已经创建成功。编码总是写出组级布尔 `targetMayExist`；旧101记录缺省为 false；非布尔、重复键（含转义别名）及未知字段仍拒绝。

physicalRoot 仅在组有 targetMayExist 或 acceptedPsPin 且没有物理路径责任时可为空；parent/host 的 PID+birth、transactionId 和完整 tuple/persona 仍全部必填且不可改写。同 transactionId 不能后补不同 physicalRoot；路径责任仍须由 `reservePath` 提供真实非空物理根、单路径下发边界、精确祖先及此前未隐藏证明。空根 witness/pin 不能代替这些证明，直接构造空根+路径也不能编码。

可选acceptedPsPin只接受此前真实State消费者已接纳的PS信息：sdkRootInstanceId、hidInstanceIds、bindingGeneration。没有complete/valid字段，拒绝用磁盘布尔值冒充当前PS接纳。可保留只有pin没有路径的责任；consumePath最后一条物理路径不会顺带删除pin或 targetMayExist。`mergeForCarry` 对 targetMayExist 取 OR，不能降级；只有调用方另行证明归还/目标不存在后，以当前 revision 和完整 exact Origin 调用 `consumeOrigin` 才生成清除该组的候选，模块不提供该外部证明。缺pin保持unknown，不能用设备名、VID/PID或当前run补造旧身份。

明确上限：文件/编码64KiB；16个origin；每origin32条路径、总128条；每pin最多128条HID；PnP实例512B；transaction及tuple文本128B；raw机器身份4096B；PID为非零uint32，creationTime为1—20位无前导零的非零uint64十进制字符串，epoch/revision/generation为非零uint64。PnP实例是有界ASCII三段实例ID，非接口符号链接、shell命令、通配符或路径遍历。实例语法检查不认定其设备角色或物理所有权。

## 纯接口

| API | 作用及失败行为 |
|---|---|
| validRecord(record, error?) | 仅结构校验，不读文件或设备 |
| encode(record, outBytes, error?) | 严格编码；失败保留输出 |
| decode(rawBytes, outRecord, error?) | 从原始UTF-8解码，拒绝重复/未知字段、坏编码、过深/超限/截断；失败保留输出 |
| reserveTargetCreation(current, expectedRevision, exactOrigin, TargetCreationProof, candidate, error?) | 仅保留可能创建目标的历史责任；允许无物理源/无ACK/无pin；重复幂等，错误证明、旧revision或原tuple改写拒绝 |
| reservePath(current, expectedRevision, origin, path, proof, candidate, error?) | 单路径预留；同origin重复幂等；不覆盖别的origin |
| markMayHaveIssued(current, expectedRevision, origin, path, candidate, error?) | 仅升级可能下发阶段；不标记成功 |
| attachAcceptedPsPin(current, expectedRevision, origin, pin, candidate, error?) | 仅保留调用方已接纳的数据；冲突pin不可覆盖 |
| mergeForCarry(current, expectedRevision, inherited, candidate, error?) | 保留所有原origin/路径/pin，并 OR 合并 targetMayExist；冲突拒绝；不得借此导入来源不明文件 |
| consumePath / consumeOrigin(current, expectedRevision, exactOrigin, [path,] candidate, error?) | 调用方已证明归还后构造candidate，拒绝旧revision或错误origin；不做归还 |

所有变换先生成完整candidate，不修改current；有变化才revision+1，幂等保持原修订；溢出拒绝。允许current/candidate同对象，但调用方集成应保持两个对象，直到磁盘成功才发布。普通数据结构可直接构造，不能绕过supported transforms手改origin/revision再把encode当作授权校验。

## Windows显式文件IO

`native/virtual_target_return_store.h`：`load(absolutePath, expectedRawMachineBinding)`返回LoadResult。只有`LoadState::Valid`才有optional<Record>：

- Missing：父目录有效、确切文件不存在，无record；不代表旧虚拟端点不存在。
- Valid：结构有效且raw机器身份一致；仍仅历史来源。
- RetryableIo：共享冲突、读取不全、元数据变化、父目录不可读等不确定性，无record。
- Invalid：坏记录、foreign机器、大小/类型/重解析点/硬链接/不安全路径，无record。

路径只接受调用方明确传入的本地盘符绝对路径（可归一化正斜杠），不自动选择appdata，不接受UNC、设备路径、ADS、遍历、DOS保留名、末尾点/空格。父目录必须已存在且稳定；读前检查父目录、叶类型，用OPEN_REPARSE_POINT打开并核对磁盘文件/非目录/非reparse/单链接；SHARE_READ不允许同时写入或替换，在读前后核对大小、文件ID及元数据。

`store(absolutePath, record)`返回StoreResult：Committed / RetryableIo / Invalid，`committed()`仅文件发布成功。创建同目录唯一CREATE_NEW临时文件，完整WriteFile后FlushFileBuffers，关闭，再MoveFileExW(REPLACE_EXISTING|WRITE_THROUGH)。失败不修改caller，不删除责任文件，只清理本次确切拥有的临时文件。没有重试、线程、timer、设备/日志扫描或进程存活查询。同步文件API不承诺任意文件系统驱动的硬实时截止。

**原子替换不是跨进程CAS。** expectedRevision只保护内存candidate变换，不比较磁盘上的并发写者。父任务必须用原单实例/序列化生命周期、PID+creation存活核验约束实际写者，并使用可信且稳定的目录。父目录检查与rename不是原子身份锁；不宣称能防恶意同用户交换目录/reparse、伪造JSON或另一个软件抢同一全局隐藏路径。不能将codec/store的成功当作这些权限或归还证明。

## 主任务集成顺序（示例，不执行控制）

```cpp
namespace tr = ymcc::targetReturn;
// current来自调用方真实初始化/Valid历史数据和既有所有权判定。
tr::Record candidate;
if (!tr::reservePath(current, current.revision, exactOrigin, exactInstance,
                     {true, true, true}, candidate)) {
    // 保留未知责任，不下发新hide。
    return;
}
const auto saved = tr::store(explicitRecordPath, candidate);
if (!saved.committed()) return; // current及旧责任保持，禁止下发。
current = std::move(candidate);
// 此后才由原runtime做这一个路径的真实hide；本模块没有该动作。
// hide回执失败/未知也不能提前consume责任。
```

冷启动load失败不能赋一个empty Record去覆盖旧文件。旧owner是否死亡、PS目标是否Absent、路径是否仍应还原、是否存在其他控制者，均由主任务另外判定。consume也只生成candidate，持久化成功后才发布。不要恢复旧全局journal、cloak或应用注册表。

## 测试

```powershell
node tools/virtual_target_return_record_selftest.mjs --out <全新绝对证据目录> --toolchain <已有捕获工具链目录>
```

工具链路径由CLI显式传入，不硬编码本轮私有证据目录。保留实际源副本、编译参数、395个实际编译输入的哈希绑定、cl/link/dumpbin身份和exe哈希，结果/生成物不得复用覆写。Release为/O2 /MT；Checked为/Od /RTC1 /MT，未冒称C++算术溢出检查或动态CRT跟踪。

夹具编译实际codec/store；文件IO使用本轮独占目录。写入失败/短写/刷盘/rename、读失败/短读/元数据改变注入仅在这些文件API叶上；硬链接及共享冲突使用真实文件；reparse拒绝覆盖使用明确的属性回执注入，未测试恶意OS目录交换。每场景检查store句柄释放，失败临时文件清理。独立新子进程从同一真实文件回读全部字段，属于持久性证据，不是OS重启或产品冷OFF恢复验收。

三类源码语义破坏（放过stale revision、丢重复键检查、跳过刷盘）必须编译并被原业务断言捕获；编译失败或崩溃不算负例通过。静态导入仅KERNEL32，但不冒称实际CRT动态加载或全产品设备调用跟踪。未加载SDK、运行产品/普通Host、接线runtime、操作手柄/服务/驱动/Steam/睡眠、导出或部署。
103增补夹具完整保留101的84个原场景、每场景断言数与重名场景的出现次数；另测 source-free target witness、缺ACK且无pin往返、两项proof拒绝、精确元组不可改写、最后路径保留target duty、carry OR双向、不正确/过期consume、101真实旧文件缺省false、坏布尔/重复/未知键及空根路径拒绝。独立子进程同时回读原101样本与新增无物理源/无pin目标记录。继续使用原 portable runner 和显式现有工具链，Release/Checked及原3个语义mutant。

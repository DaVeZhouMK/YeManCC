# OEM 输入统一身份绑定

独占产品头文件为 `capabilities/HcOemInputCapabilityBinding.h`。本目录只编译并验证该绑定与从当前主程序逐字提取的原 profile selector、key catalog selector、完整表以及 `oemChordEvalRules`。主程序调用点尚未切换，不能把本地通过签成产品接线完成。

## 行为边界

绑定以实际 family 和 deviceClass 为键。总库已登记别名归一为 consumerClass 后交原选择器；未知/扩展类保留原 family 和原字符串，继续走既有明确 fallback。family、class 或显式 reset 变化均重新绑定。

缓存只保存不可变的登记条目、原 profile id 和原静态目录。hook、接口准入、设备所有权、输入新鲜度、epoch、键态、keyup 和释放状态均不存入缓存。缓存须由既有串行配置/身份 owner 独占，或由主线 owner 在经审查的调用点使用 thread_local 实例；不新增无锁的全局可变缓存。

CPU/GPU 不属于该支线；人类已明确它们本批仅作资料录入，现有 TDP 消费独立。

## 已执行范围

最新 r2：3,366 个 family×alias 选择、13,688 项断言通过。所有主程序静态 profile/key 表以及选择器、实际 evaluator 原文编译；原 key-up 清空和 GPD Win5 实时接口拒绝仍被实际执行。稳定身份重复 10,000 次只各调用一次原 selector；这是选择次数证据，不代表整进程 CPU 下降。

三种头文件真实源码变异均编译成功，且被原消费断言检出：

- 停止 consumerClass 归一：actual original catalog selection。
- 丢失 family 变化检查：family change invalidates same-class cache。
- 丢失 class 变化检查：class change invalidates original profile consumer。

首轮 r1 的 15,724 项记录保留。r2 将每个相同 profile 下重复的全零 key-up 输入收敛为每次绑定一次，增加独立的 GPD Win4→Win5 消费选择检查，并让归一变异保留登记指针，确保失败来自实际消费选择而不是元数据比较。数量变化不是放宽覆盖。

编译与执行复用现有安装的 MSVC/SDK 和 global/host-process.ps1 的自建 Job。没有设备枚举、输入注入、Host、YMCC 进程或硬件调用。

## 后续串行接入

主程序/生成器/现有 remaining-model fixture 属总库 owner，不由本目录覆盖。待 owner 评审实际调用点：

- oemBackMappingRefreshGate 选择 profileId 后的原 exchange/epoch/chordCapable 和 hook/归还逻辑保持。
- oem.keys.get 的原 key catalog。
- gamepadOemPressedKeys 的原 key catalog 热路径。

若采用共享 helper，原 OEM query、presentation 和 remaining-model 提取夹具须包含新增 helper/header 的实际字节，不以假函数替代。随后补运行实际主线调用片段与正式登记审计，才能将产品接入状态改为完成。

显式本地运行：

```powershell
node tools/ymcc_sim/global/oem-capability-binding/run.mjs --evidence-dir <新的绝对自有工作目录>
```

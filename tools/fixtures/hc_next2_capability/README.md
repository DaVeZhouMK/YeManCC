# NEXT2 实际生产函数离线测试

从仓库目录运行：

```powershell
node tools/hc_next2_capability_selftest.mjs --mutation-checks
```

默认输出位于工作区的 `Build/Validation/HC-ModelCapabilities-20261017/NEXT2/selftest`，可用 `--out` 指定仓库外的其他目录。脚本需要已冻结的 `NEXT2/main.before.cpp`；可用 `--before`、`--source` 指定明确的前像和待测快照，`--vcvars` 指定 MSVC 环境脚本。`--generate-only` 只生成材料，报告不会标为验证通过。

脚本按原文提取当前 `main.cpp` 的身份解析、目录、profile 选择、VK 编码、chord evaluator、canonical 名称解析、IMU recipe 解析和应用函数，以及 rearMap 解析、键盘状态重算、生产禁用分支的发布清理片段。生成的独立 C++ 程序以 Release、Checked 两种配置编译并运行，断言不受 `NDEBUG` 影响。源码绑定文件记录完整源码、每个定义/片段、前像、固定 HC 输入、JSON 头、测试源及二进制 SHA-256。

九键期望来自固定 HC `a250b19a4b7e29dac3b383f2d1ac517d1af43e4e` 的 `AYANEONEXT2.cs`，再套用本适配单独声明的稳定 canonical ID/bit 合同（该映射是实现选择，不是 HC 原始编号或用户指定编号）；IMU 期望从该版本 `AYANEONEXT2.json` 的 Axis 与 AxisSwap 独立计算。HC 文件同时受固定 SHA-256 和研究目录 manifest 校验。旧 profile 编号、规则、目录和 evaluator 行为与主线程冻结前像对拍。NEXT2 期待不从被测表反推。

覆盖 15 身份别名、76 个厂商/近似型号负例、九项单键按下/释放、Win+D 两种顺序与部分释放、512 全组合、262144 有序转换、canonical 位互斥、显式 rearMap、旧 NEXT/Lite、ROG、MSI、GPD 回归及 gyro/accel 基向量和数值回放。可选 `--mutation-checks` 仅修改输出目录中的源码副本，验证 F19 编码、LC1 位碰撞、IMU 符号、清理后残留输入四种错误确实触发语义断言；编译/提取失败不能冒充错误被检出。

状态证据边界：执行的是生产禁用分支中“撤销 LL/mouse 所有者、清零私有状态、重算发布”的原文片段，及真实 `oemBackHookRecompute()`。完整 reconciliation 函数只绑定哈希；调度顺序、profile 换代与类似睡眠的禁用/恢复由测试内存状态显式驱动。测试同时验证该片段保留独立 Raw Input 所有者，因此不能将其说成所有输入源都已释放，更不能声称通过 Windows 实机睡眠、hook 安装/卸载、通知投递、回放账本或设备重连验收。

`wholeFileChanged` 单独报告整文件在运行期间是否变化，当前提取相关定义和完整禁用父函数必须在运行结束仍逐项匹配，前像必须保持不变；相关定义变化将失败。未启动 Host、HID、Steam，未调用设备/注入/固件接口，未做 NEXT2 实机输入验证。灯光仅验证生产能力门保持 false；风扇与灯光硬件路由未测试，也不宣称支持。


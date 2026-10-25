#pragma once
#include "virtual_start_protection.h"

// [AI:ENTRY:VIRTUAL-CONTROLLER] 虚拟启用 + YMCC输出屏蔽：同一个输出生命周期。
// 本文件集中说明两者的接线；注释不执行，不新增状态机、线程或能力库。
// 实际请求时序的唯一权威仍是 VirtualStartProtection (VC-LIFECYCLE-20261020-R3)。
//
// 一条链，两个状态维度：
//   玩家请求决定“当前虚拟目标是否存在”；YMCC输入归属决定“该目标提交真实帧还是中性帧”。
//   两者共享当前 Host / bound persona / epoch / 物理源，不建立第二个目标或独立释放周期。
//
// T0 请求准入：inputHostTargetAdmission -> 同一把玩家意图锁，绝对预算30秒。
//    开/关/换persona均串行进入现有生命周期owner，未完成归还不能先创建新目标。
// T1 执行：Stop旧目标并证明归还 -> 能力总库/现有身份准入 -> Start新目标。
//    HELLO/PREPARE/中性预备成功不代表已有可用物理输入，必须继续核验当前样本与提交回执。
//    MSI延迟源停在同epoch的HELLO阶段：原票重发可执行；当前主源与隐藏证明通过才PREPARE。
//    等源时只让出Host锁供BUS，保留串行owner并逐次复核原意图；路线耗尽先退虚拟再归还实体。
// T2 可用输出：inputCaptureOnGamepad -> focusIsolationPlanTick -> *UiOwnsInput。
//    YMCC/已有自有子窗口/轮盘接管 => 同一目标的最终完整中性帧；物理采集和YMCC导航保留。
//    失焦/收起 => 用最新有效样本恢复真实输出；不补播历史，不调用Stop/Start，不重置请求锁。
//    最终装配、typed报告和回执消费均属这条输出链，不能只屏蔽按钮而漏掉轴/IMU/背键。
// T3 执行检查：首次有效输入/完整关闭/实际失败回执开启10秒主动检查，目标可用但按钮仍锁。
//    ROG已有XInput归还义务时，Stop/cold成功仅是本地清理回执；当前实体读数满足原窗口后
//    才是完整关闭Tc。等待属于原执行，仍受原30秒硬期限；不新建重试/计时器，也不补播旧意图。
//    正常聚焦中性、录制/电源/外接优先等已有暂停不是故障，不触发重建，也不延长原截止。
// T4 保底/出口：异常有明确证据时才用现有有限预算，先关闭和归还，再开启同一意图。
//    一个请求只有一个最终结果；成功候选不等于按钮放行；30秒上限不被重试/回调续期。
//
// 能力：virtual_controller_capability_runtime.h只消费现有权威；capabilities/总库不在此修改。
// 执行：virtual_controller_start_runtime.h + virtual_controller_stop_runtime.h。
// 冷归还：virtual_target_return_runtime.h先持盘实际来源；cold_execution.h为唯一归还算法，
//   cold_runtime.h只接当前OS/driver和原ticket；旧native/Host/CLI均证明结束、全目标族
//   完整不存在后，只归还原exact path。无权限清陌生path、全局cloak或应用名单。
//   ON在MSI准备/源准入/新Host之前检查；OFF启动由既有worker排队，同R3 continuation
//   最多三次有界观察，不增加Start/Stop、不消耗保底配额、不续30秒；已知UI借用暂停。
// 输出：virtual_controller_ui_output_runtime.h（计划/中性帧/当前Host身份/ACK一起维护）。
// 检查/保底：virtual_switch_verification_runtime.h、virtual_switch_recovery_runtime.h；
//           正常归还/唤醒仍使用 virtual_switch_execution_return_runtime.h、virtual_wake_execution_runtime.h。
//
// 修改任一端前同时核对：实际bound身份、源新鲜度、执行锁、预期UI中性和唯一出口。
// 新日志确认的故障补入现有实际消费者夹具；不得复制算法做第二套“测试生命周期”。
// 四个定义模块由同一个main.cpp编译，不分别交付启用版/屏蔽版二进制。
// 按职责定位和验证命令见 virtual_controller_runtime.md。
namespace ymcc::inputHost::runtimeContract {
using Lifecycle = ymcc::VirtualStartProtection;
inline constexpr auto lifecycleVersion = Lifecycle::lifecycleVersion;
// 2026-10-21 G3E: approved SDK sweep took 10.631s after a 92ms teardown.
// This is a ceiling within the SAME player deadline, never a new 20s period.
inline constexpr std::uint64_t releaseReceiptCeilingMs = 20000;
// A rejected PREPARE includes its serialized cleanup before replying; the G3E
// failure receipt took 19.17s. It obeys the same original 30s player cap.
inline constexpr std::uint64_t prepareReceiptCeilingMs = 20000;
static_assert(Lifecycle::maxMs == 30000 && Lifecycle::executionInspectionMs == 10000 &&
              Lifecycle::fallbackMaxAttempts == 1,
              "Update the shared virtual-controller timeline and actual-consumer fixtures together.");
}

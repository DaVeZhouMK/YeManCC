#pragma once
#include "virtual_controller_runtime_contract.h"

// VC-LIFECYCLE-20261020-R3 — production definition module.
// YMCC foreground virtual-output ownership. Read the actual bound Host, retain source sampling and UI routing, and neutralize only the final virtual report. Expected UI neutral is never a lifecycle failure.
// Shared enable/UI-output timeline: virtual_controller_runtime_contract.h.
// Navigation and capability boundaries: virtual_controller_runtime.md.
// This is compiled production code; the reference comments do not execute.
// Maintenance boundary: lifecycle, physical sampling and UI admission stay independent.
// Bound persona + Host epoch are captured together. Source, power and writer gates
// remain live checks; capabilities are never a cached substitute for these gates.
// Historical isolation bookkeeping below retains its original dormant switches.
// Active UI shielding uses FocusPlan/*UiOwnsInput and final semantic-neutral frames;
// moving the compatibility code here does not enable the old prototype.

// ══════════════════════════════════════════════════════════════════════════
//  B1：展开/最大化 YMCC 时的聚焦隔离（独立原型，**默认关闭**）
// ══════════════════════════════════════════════════════════════════════════
// 目标：展开且前台时，普通手柄输入只用于 YMCC 导航；**供外部消费的输出**改为语义中性；
//       收起/失焦/断源/Host 重建后恢复最新有效输入，不补播历史。
//
// B0 → B1（《W6-B0裁决与最终输出修复单-20260921》§1–§3 四组修复）：
//   P0-1 默认关闭零回归
//        · focusIsolationPublishOwnership 未启用即返回：不读窗口、不推进 epoch、不写日志；
//        · gamepadSerialSubmit 按**动作语义**分派：只有「外部输入注入」受归属约束，
//          YMCC 自身动作（TDP/亮度/触摸键盘/鼠标模式/关闭脚本）不受影响；
//        · inputHostSubmitPad 的 rawPad 实参还原为原始 pad（B0 误用 canonical 顶替，
//          会改变轻推摇杆/原始轴语义）。
//   P0-2 中性化在**所有合成之后**
//        · 体感→摇杆合成抽成纯函数 focusIsolationBlendStick（生产与自测共用同一算术）；
//        · 最终帧由 focusIsolationAssembleFrame 统一装配；语义中性时**全部 12 个 wire
//          字段**（buttons/lx/ly/rx/ry/lt/rt/ltRaw/rtRaw/raw/imu/backButtons）归零。
//          不依赖调用点清零，也不靠 imuAdmitted=false 间接实现 ⇒ B0 的「体感转摇杆」
//          旁路由此封闭。
//        · 接管有效且目标可写但**源缺失** ⇒ 明确发中性清理帧（保证释放），
//          不冒充既有「未接管断源跳过」；未接管的断源策略保持原样。
//   P0-3 同步/恢复/回执
//        · g_fullHeight 已原子化（UI 唯一写者）；Host 代次经 g_inputHostMx（原写者边界）
//          读取；锁顺序：任何路径都不同时持有 g_focusIsolationMx 与 g_inputHostMx
//          （一律「先拷贝后释放」）。
//        · 退出释放屏障：退出接管时仍按下的按钮位逐位跟踪，未松开前不进外部输出，
//          其它键立即恢复最新活值（不无限吞输入）。
//        · applied 只在**同代次/同目标**且拿到明确成功回执（frame-accepted）时置位；
//          跳过/拒绝/失败分别计数，不再用 hostSubmission != "timeout" 判成功。
//        · 每拍 focus-isolation-frame 常开日志改为转场/结果类别变化事件 + 有界计数。
//
// 未做（如实登记）：不改驱动/SDK/HidHide/物理模式；不新增线程/定时器/硬件轮询；
//   触摸平面不在本帧（见 DSU/触摸登记）；物理直通（M-P，虚拟关闭）仍无输出面可用。
//
// B2（《W6-B1裁决与试用包门禁-20260921》§3 四项）：
//   §3.1 适用条件贯穿归属门：publishOwnership 接收 applicable（虚拟目标已启用），
//        requested 恒含 applicable ⇒ 不适用时不设 neutralNow、不设 injectionBlocked、
//        不建释放屏障；适用性切换推进 epoch（旧外部任务/旧回执一并失效）。
//   §3.2 stale-ack：发送**开始前**捕获 {epoch, hostGen, 目标会话 runId}，
//        回执回来后在同一状态锁内复核；不一致 ⇒ 记 staleAck、不置 applied。
//   §3.3 断源保持退出屏障：屏障推进只在**有有效样本**时进行；退出瞬间无样本时
//        屏障置为「全部位 + unresolved」，等重新取得有效输入再按实际位收敛。
//   §3.4 背键纳入屏障：位图 0..15 = wButtons、16..17 = backButtons（M1/M2）。
//        背键是**外部输出**（Host/Program.cs:2039-2058 解析、:2394-2399 映射为
//        HMButton.LeftPaddle/RightPaddle = L5/R5）⇒ 不是 YMCC 专用例外，必须同屏障。
//
// B3（《W3-W6-联合裁决与主线接回执行单》§4.1/§4.2）：
//   §4.1 交付补丁重做：改为「父源字节保真物化 + 合法 unified diff」，并由独立工具从
//        **落盘补丁**应用到父源副本，要求逐字节 + SHA256 与候选一致（不再只验内存列表）。
//   §4.2 stale-ack 绑定**权威 Host 身份**：捕获与回执复核都改读 g_inputHostMx 保护的
//        g_inputHostEpoch/g_inputHostRunId，覆盖「Host 已换代、BUS 缓存尚未刷新」窗口；
//        不再只比较 g_focusIsolation 的缓存副本。锁序：先读权威 Host（不持 focus 锁），
//        再进 focus 锁比较，避免双锁倒序。

// DSU extern（真实定义在 dsu_server.cpp；此处前置声明供本模块自测驱动实际编码）。
extern "C" {
void dsu_publish(const ymcc::dsu::Frame& frame);
void dsu_feed(bool hasMotion, float gx, float gy, float gz, float ax, float ay, float az,
              uint16_t buttons, float lxRaw, float lyRaw, float rxRaw, float ryRaw,
              uint8_t l2, uint8_t r2);
size_t dsu_encode_pad_data_for_test(uint8_t* out, size_t outLen, int slot, uint64_t timestampMs);
}

struct FocusIsolationState {
    bool enabled = true;           // 2026-09-23 B 版：默认启用（腿由 neutralOnly 决定）
    // B 版（operator 裁决 2026-09-23）：只做"前台期间向消费者发中性帧"这一支；
    // 不拦外部注入、不建退出释放屏障（那两支保留给 --focus-isolation 完整模式与真机验证）。
    bool neutralOnly = true;
    // B2（裁决 §3.1）：适用条件 = 虚拟目标已启用。不适用 ⇒ 不接管、不拦注入、不建屏障、不推进 epoch。
    bool applicable = false;
    bool requested = false;        // 本拍判定：applicable ∧ 实际展开 ∧ 前台 ∧ 可见 ∧ 非最小化
    bool applied = false;          // 同代次/同目标已有明确成功回执（≠ 消费者已验证）
    unsigned long long epoch = 0;  // 归属代次：requested 翻转或 Host/电源换代 +1
    unsigned long long appliedEpoch = 0;    // applied 对应的归属代次
    unsigned long long appliedHostGen = 0;  // applied 对应的 Host 代次
    std::string targetRunId;       // 本代次的 Host 目标会话身份（Host runId）
    std::string appliedRunId;      // applied 对应的目标会话身份
    unsigned long long staleAcks = 0;       // B2：回执期间换代导致被丢弃的成功回执数
    unsigned long long powerGen = 0;
    unsigned long long hostGen = 0;
    bool expanded = false;
    bool foreground = false;
    ULONGLONG sinceTick = 0;
    unsigned long long transitions = 0;
    // 退出释放屏障：退出接管时仍按下的按钮位，逐位等到松开才放行（不吞其它键）。
    // B2：位图 0..15 = XInput wButtons，16..17 = backButtons(M1/M2)（裁决 §3.4）。
    unsigned long long releaseBarrierMask = 0;
    bool releaseBarrierActive = false;
    // B2（裁决 §3.3）：退出瞬间没有有效样本 ⇒ 不知道哪些位按住 ⇒ 全部位先保持，等有效样本再收敛。
    bool releaseBarrierUnresolved = false;
    // 提交结果类别（只在该类别**变化**时落一条日志，避免 125Hz 常开日志）。
    int loggedOutcome = -1;
};
// 屏障位图分区（B2）：低 16 位 = wButtons，16..17 = backButtons。
static constexpr unsigned long long kFocusBarrierButtonBits = 0xFFFFull;
static constexpr unsigned long long kFocusBarrierBackShift = 16;
static constexpr unsigned long long kFocusBarrierBackBits = 0x3ull;
static constexpr unsigned long long kFocusBarrierAllBits =
    kFocusBarrierButtonBits | (kFocusBarrierBackBits << kFocusBarrierBackShift);
static std::mutex g_focusIsolationMx;
static FocusIsolationState g_focusIsolation;
// 2026-09-23 B 版（operator 裁决）：默认启用"前台中性"腿。原子镜像是所有热路径的第一道门，
// 必须与结构体默认一致，否则热路径会提前返回、B 腿永不生效。
static std::atomic<bool> g_focusIsolationEnabled{false};      // 2026-09-23 执行单 §3：聚焦隔离策略已舍弃
                                                              // （ARCHIVED_REFERENCE）⇒ 默认关闭，且无运行时开启口。
static std::atomic<bool> g_focusIsolationApplicable{false};   // B2：适用条件镜像（虚拟目标已启用）
static std::atomic<unsigned long long> g_focusIsolationEpoch{0};
static std::atomic<bool> g_focusIsolationNeutralNow{false};
static std::atomic<bool> g_focusIsolationInjectionBlocked{false};
static std::atomic<unsigned long long> g_focusIsolationNeutralFrames{0};
static std::atomic<unsigned long long> g_focusIsolationSubmitFailures{0};
static std::atomic<unsigned long long> g_focusIsolationSubmitSkipped{0};
static std::atomic<unsigned long long> g_focusIsolationSubmitRejected{0};
static std::atomic<unsigned long long> g_focusIsolationBarrierHolds{0};
static std::atomic<unsigned long long> g_focusIsolationInjectionDropped{0};
static std::atomic<unsigned long long> g_focusIsolationCleanupFrames{0};
static std::atomic<unsigned long long> g_focusIsolationStaleAcks{0};

// ── 纯函数层（无设备、无全局；--focus-isolation-selftest 直接驱动）──

enum class FocusDecision { PassThrough, Neutral, SkipHostUnavailable };

static const char* focusDecisionName(FocusDecision d) {
    switch (d) {
        case FocusDecision::Neutral:             return "neutral";
        case FocusDecision::SkipHostUnavailable: return "skip-host-unavailable";
        default:                                 return "pass-through";
    }
}

// 优先级即安全优先级：未启用 → 直通（零副作用）；未接管 → 直通（断源仍走既有跳过）；
// Host 不可写 → 跳过（绝不把活输入送出去）；否则 → 中性（含源缺失时的中性清理）。
static FocusDecision focusIsolationDecide(bool enabled, bool requested, bool hostUsable) {
    if (!enabled) return FocusDecision::PassThrough;
    if (!requested) return FocusDecision::PassThrough;
    if (!hostUsable) return FocusDecision::SkipHostUnavailable;
    return FocusDecision::Neutral;
}

// 队列任务归属判定（纯函数）。未启用 ⇒ 一律放行（默认关闭零回归）；
// 非外部注入 ⇒ 不受归属约束（YMCC 自身动作）；外部注入 ⇒ 接管中或归属代次已变一律丢弃。
static bool focusIsolationTaskDecision(bool enabled, bool externalInjection, bool neutralNow,
                                       unsigned long long capturedEpoch,
                                       unsigned long long currentEpoch) {
    if (!enabled) return true;
    if (!externalInjection) return true;
    if (neutralNow) return false;
    return capturedEpoch == currentEpoch;
}

// ── B2（裁决 §3.1）：适用条件贯穿归属门 ──
// 实验只覆盖「虚拟目标已启用」。不适用（物理直通）⇒ 不接管、不拦外部注入、不建释放屏障。
static bool focusIsolationOwnershipRequested(bool applicable, bool expanded, bool foreground,
                                             bool visible, bool iconic) {
    return applicable && expanded && foreground && visible && !iconic;
}
static bool focusIsolationInjectionBlockedNow(bool applicable, bool requested, bool barrierActive) {
    return applicable && (requested || barrierActive);
}
// 退出接管时是否要建立释放屏障：适用 ∧ 非接管。**无论有无有效样本都要建**
// （无样本时由 focusIsolationNoteExitHoldLocked 置 unresolved，不把缺样本当释放）。
// 生产路径（focusIsolationPublishOwnership）与自测调用的是同一函数。
static bool focusIsolationBarrierShouldStart(bool applicable, bool requested) {
    return applicable && !requested;
}

// 屏障位运算（纯函数；生产与自测共用）。位图分区见 kFocusBarrier*。
static unsigned long long focusIsolationBarrierPack(bool live,
                                                    unsigned long long liveButtons,
                                                    unsigned long long liveBackButtons) {
    if (!live) return 0;
    return (liveButtons & kFocusBarrierButtonBits) |
           ((liveBackButtons & kFocusBarrierBackBits) << kFocusBarrierBackShift);
}
// 退出接管瞬间建立屏障：有有效样本 ⇒ 只记此刻按下的位；无有效样本 ⇒ 全部位保持（不当作已释放）。
static void focusIsolationNoteExitHoldLocked(FocusIsolationState& st, bool live,
                                             unsigned long long liveButtons,
                                             unsigned long long liveBackButtons) {
    if (live) {
        st.releaseBarrierMask = focusIsolationBarrierPack(true, liveButtons, liveBackButtons);
        st.releaseBarrierUnresolved = false;
    } else {
        st.releaseBarrierMask = kFocusBarrierAllBits;
        st.releaseBarrierUnresolved = true;
    }
    st.releaseBarrierActive = (st.releaseBarrierMask != 0);
}
// 屏障逐位推进（生产路径唯一实现；publishOwnership 在状态锁内调用，自测直接调用同一函数）。
// 关键（裁决 §3.3）：**无有效样本一律保持屏障**，不把缺样本当作释放。
static void focusIsolationAdvanceBarrierLocked(FocusIsolationState& st, bool live,
                                               unsigned long long liveButtons,
                                               unsigned long long liveBackButtons) {
    if (!st.releaseBarrierActive) return;
    if (!live) return;
    const unsigned long long bits = focusIsolationBarrierPack(true, liveButtons, liveBackButtons);
    if (st.releaseBarrierUnresolved) {
        st.releaseBarrierMask = bits;
        st.releaseBarrierUnresolved = false;
    } else {
        st.releaseBarrierMask &= bits;
    }
    if (st.releaseBarrierMask == 0) st.releaseBarrierActive = false;
}
// 屏障期间的实际输出按钮：屏蔽屏障位，其它键用最新活值（不无限吞输入）。
static unsigned long long focusIsolationBarrierApply(unsigned long long barrierMask,
                                                     unsigned long long liveButtons) {
    return liveButtons & ~(barrierMask & kFocusBarrierButtonBits);
}
static unsigned long long focusIsolationBarrierApplyBack(unsigned long long barrierMask,
                                                         unsigned long long liveBackButtons) {
    const unsigned long long backMask = (barrierMask >> kFocusBarrierBackShift) & kFocusBarrierBackBits;
    return liveBackButtons & ~backMask;
}

// 提交回执分类：只有 frame-accepted 是明确成功；跳过/拒绝/失败/缺字段分开记录。
enum class FocusSubmitOutcome { Accepted, Skipped, Rejected, Failed, Missing };
static FocusSubmitOutcome focusIsolationClassifySubmission(const std::string& hostSubmission) {
    if (hostSubmission == "frame-accepted") return FocusSubmitOutcome::Accepted;
    if (hostSubmission == "publication-suppressed" ||
        hostSubmission == "publication-skipped-source-absent") return FocusSubmitOutcome::Skipped;
    if (hostSubmission == "not-admitted-or-failed") return FocusSubmitOutcome::Rejected;
    if (hostSubmission.empty()) return FocusSubmitOutcome::Missing;
    return FocusSubmitOutcome::Failed;   // not-running / timeout / 未知
}
static const char* focusSubmitOutcomeName(FocusSubmitOutcome o) {
    switch (o) {
        case FocusSubmitOutcome::Accepted: return "accepted";
        case FocusSubmitOutcome::Skipped:  return "skipped";
        case FocusSubmitOutcome::Rejected: return "rejected";
        case FocusSubmitOutcome::Missing:  return "missing";
        default:                           return "failed";
    }
}

// 2026-09-23 W3 批 96：提交**结果**事实（常开区；只在第一次"非中性帧被接受"时写一行）。
// 背景：批 92 的 input-host-submit-flow 记的是提交门**入参**（准入位/中性位），逐帧回执不镜像到日志，
// 所以裁决第 4 点"非中性 SUBMIT_FRAME 成功提交"一直缺**结果**证据。本行在调用点读 hostFrame.hostSubmission
// （生产焦点隔离为 PassThrough，该分类原本不执行），只在第一次 Accepted 落盘；命中后每帧仅一次 relaxed 原子读。
static std::atomic<bool> g_submitFlowLoggedAccepted{false};
static void inputHostSubmitOutcomeNote(bool neutral, const json& hostFrame) {
    if (neutral) return;                                                       // 中性清理帧不算成功提交
    if (g_submitFlowLoggedAccepted.load(std::memory_order_relaxed)) return;    // 已记录：零成本早退
    const std::string hostSubmission = hostFrame.value("hostSubmission", std::string());
    if (focusIsolationClassifySubmission(hostSubmission) != FocusSubmitOutcome::Accepted) return;
    bool expected = false;
    if (g_submitFlowLoggedAccepted.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        appendNativeLifecycleLog("input-host-submit-flow", {
            {"reason", "first-non-neutral-accepted"},
            {"hostSubmission", hostSubmission},
            {"neutral", false},
            {"virtualTargetEnabled", g_realStickTest.virtualTargetEnabled},
            {"hostPrepared", g_inputHostPrepared},
            {"ymccForeground", focusMainWindowIsForeground()}
        });
    }
}

// DSU 中性载荷：只填**实际被回包编码**的字段（buttons/l2/r2/motion）。
// 摇杆域按调用方契约（0..65535，center 32768）给出中心值（该域已随 dsu_feed 值域修正对齐）。
struct FocusNeutralDsu {
    bool hasMotion = false;
    float gx = 0, gy = 0, gz = 0, ax = 0, ay = 0, az = 0;
    uint16_t buttons = 0;
    float lx = 32768.0f, ly = 32768.0f, rx = 32768.0f, ry = 32768.0f;
    uint8_t lt = 0, rt = 0;
};
static FocusNeutralDsu focusIsolationNeutralDsu() { return FocusNeutralDsu{}; }

// ── 体感→摇杆合成（抽成纯函数；生产与自测共用同一算术）──
// 对应 HC LayoutManager：ApplyAxisModifiers(贡献) → weight = gyroWeight - stickNorm →
// base(物理原始轴) + 贡献 * weight。B0 的缺陷正出在这一步：调用点清零 pad 之后，
// 这里的贡献仍会加回摇杆。B1 让最终帧在中性时整体归零，本函数只负责产出真实合成值。
struct FocusStickBlend {
    bool useMotion = false;
    bool targetLeft = true;
    double contributionX = 0.0, contributionY = 0.0;
    double baseX = 0.0, baseY = 0.0;
    double stickNorm = 0.0;
    double weightFactor = 0.0;
    double outX = 0.0, outY = 0.0;
    short lxShort = 0, lyShort = 0, rxShort = 0, ryShort = 0;
};
static FocusStickBlend focusIsolationBlendStick(const XINPUT_GAMEPAD& outputPad,
                                               bool analogAdmitted,
                                               const MotionContribution& motionContribution,
                                               const std::string& targetStick,
                                               double gyroWeight) {
    FocusStickBlend b;
    b.useMotion = analogAdmitted && motionContribution.mapped;
    b.contributionX = b.useMotion ? motionContribution.shortX : 0.0;
    b.contributionY = b.useMotion ? motionContribution.shortY : 0.0;
    if (b.useMotion)
        inputHostApplyAxisModifiers(b.contributionX, b.contributionY, b.contributionX, b.contributionY);
    b.targetLeft = (targetStick == "left");
    b.baseX = b.targetLeft ? outputPad.sThumbLX : outputPad.sThumbRX;
    b.baseY = b.targetLeft ? outputPad.sThumbLY : outputPad.sThumbRY;
    b.stickNorm = (std::min)(1.0, std::sqrt(b.baseX * b.baseX + b.baseY * b.baseY) / 32767.0);
    b.weightFactor = gyroWeight - b.stickNorm;
    b.outX = b.contributionX * b.weightFactor;
    b.outY = b.contributionY * b.weightFactor;
    MergedVirtualPadFrame merged;
    merged.leftX = outputPad.sThumbLX;
    merged.leftY = outputPad.sThumbLY;
    merged.rightX = outputPad.sThumbRX;
    merged.rightY = outputPad.sThumbRY;
    if (b.targetLeft) {
        merged.leftX = inputHostClampShort(b.baseX + b.outX);
        merged.leftY = inputHostClampShort(b.baseY + b.outY);
    } else {
        merged.rightX = inputHostClampShort(b.baseX + b.outX);
        merged.rightY = inputHostClampShort(b.baseY + b.outY);
    }
    const short targetX = b.targetLeft ? merged.leftX : merged.rightX;
    const short targetY = b.targetLeft ? merged.leftY : merged.rightY;
    b.lxShort = b.targetLeft ? targetX : outputPad.sThumbLX;
    b.lyShort = b.targetLeft ? targetY : outputPad.sThumbLY;
    b.rxShort = b.targetLeft ? outputPad.sThumbRX : targetX;
    b.ryShort = b.targetLeft ? outputPad.sThumbRY : targetY;
    return b;
}

// ── 最终帧内容与装配（生产路径唯一入口）──
// 12 个 wire 字段一个不少：buttons / lx / ly / rx / ry / lt / rt / ltRaw / rtRaw / raw / imu / backButtons。
struct FocusFrameContent {
    int buttons = 0;
    short lxShort = 0, lyShort = 0, rxShort = 0, ryShort = 0;
    float lt = 0.0f, rt = 0.0f;
    float ltRaw = 0.0f, rtRaw = 0.0f;
    // 与基线逐字节同型：raw 触发子在原字面量里是 BYTE（unsigned char），不是 int。
    unsigned char rawLtByte = 0, rawRtByte = 0;
    bool imuAdmitted = false;
    double gyroX = 0, gyroY = 0, gyroZ = 0;
    double accelX = 0, accelY = 0, accelZ = 0;
    double qw = 1.0, qx = 0, qy = 0, qz = 0;
    int backButtons = 0;
    ymcc::screenpads::Snapshot touchpads{};
    unsigned screenButtons=0;
};
// 语义中性：全部 12 个 wire 字段归零（quaternion 用单位四元数，其余默认即中性）。
static void inputHostNoteIdleSubmit(const FocusFrameContent& content,bool accepted) {
    if(!g_inputIdleActivity.enabled())return;
    if(msiControllerRepairScope())g_msiRearAudit.submit(static_cast<unsigned>(content.backButtons),accepted);
    const ymcc::idle::Pad observed{static_cast<uint32_t>(content.buttons),static_cast<uint32_t>(content.backButtons),
        content.screenButtons,{content.lxShort,content.lyShort,content.rxShort,content.ryShort},{content.rawLtByte,content.rawRtByte}};
    g_inputIdleActivity.submit(observed,accepted,content.imuAdmitted,
        {content.gyroX,content.gyroY,content.gyroZ,content.accelX,content.accelY,content.accelZ},GetTickCount64());
}

static FocusFrameContent focusIsolationNeutralContent() {
    FocusFrameContent c;
    c.imuAdmitted = false;
    c.qw = 1.0;
    return c;
}
static json focusIsolationAssembleFrame(const FocusFrameContent& c) {
    // Build each owned object once; initializer-list temporaries otherwise
    // repeatedly allocate/copy nested JSON values before the final frame exists.
    json frame = json::object();
    frame["backButtons"] = c.backButtons;
    frame["buttons"] = c.buttons;
    json imu = json::object();
    imu["admitted"] = c.imuAdmitted;
    json gyro = json::object();
    gyro["x"] = c.imuAdmitted ? c.gyroX : 0.0;
    gyro["y"] = c.imuAdmitted ? c.gyroY : 0.0;
    gyro["z"] = c.imuAdmitted ? c.gyroZ : 0.0;
    imu["gyroDps"] = std::move(gyro);
    json accel = json::object();
    accel["x"] = c.imuAdmitted ? c.accelX : 0.0;
    accel["y"] = c.imuAdmitted ? c.accelY : 0.0;
    accel["z"] = c.imuAdmitted ? c.accelZ : 0.0;
    imu["accelG"] = std::move(accel);
    json quaternion = json::object();
    quaternion["w"] = c.imuAdmitted ? c.qw : 1.0;
    quaternion["x"] = c.imuAdmitted ? c.qx : 0.0;
    quaternion["y"] = c.imuAdmitted ? c.qy : 0.0;
    quaternion["z"] = c.imuAdmitted ? c.qz : 0.0;
    imu["quaternion"] = std::move(quaternion);
    frame["imu"] = std::move(imu);
    frame["lt"] = c.lt;
    frame["ltRaw"] = c.ltRaw;
    frame["lx"] = inputHostNormalizeAxis(c.lxShort);
    frame["ly"] = inputHostNormalizeAxis(c.lyShort);
    json raw = json::object();
    raw["bLeftTrigger"] = c.rawLtByte;
    raw["bRightTrigger"] = c.rawRtByte;
    raw["buttons"] = c.buttons;
    raw["sThumbLX"] = c.lxShort;
    raw["sThumbLY"] = c.lyShort;
    raw["sThumbRX"] = c.rxShort;
    raw["sThumbRY"] = c.ryShort;
    frame["raw"] = std::move(raw);
    frame["rt"] = c.rt;
    frame["rtRaw"] = c.rtRaw;
    frame["rx"] = inputHostNormalizeAxis(c.rxShort);
    frame["ry"] = inputHostNormalizeAxis(c.ryShort);
    if (c.touchpads.present) frame["touchpads"] = ymcc::screenpads::snapshotJson(c.touchpads);
    if(c.screenButtons) frame["screenButtons"]=c.screenButtons;
    return frame;
}

// Serialize the authoritative typed snapshot without a per-frame JSON tree.
// Scalar formatting and all identity getters reuse production helpers.
// FocusFrameContent is the already-admitted, already-neutralized value snapshot.
static std::string focusIsolationBuildTypedSubmitText(const FocusFrameContent& c,
    const std::string& requestId, unsigned long long sequence,
    unsigned long long sampleSequence, unsigned long long monotonicTick,
    const std::string& normalizedInputHash, const char* motionPairProofId) {
    std::string out;
    out.reserve(2048);
    const auto number = [&](double v) { out += c6aNumberTextCache().text(v); };
    out += "{\"backButtons\":"; c6aAppendInt(out, c.backButtons);
    out += ",\"buttons\":"; c6aAppendInt(out, c.buttons);
    out += ",\"command\":\"SUBMIT_FRAME\",\"configHash\":"; c6aAppendEscaped(out, kInputHostProtocolConfigHash);
    out += ",\"configRevision\":"; c6aAppendUInt(out, g_inputHostConfigRevision);
    out += ",\"epoch\":"; c6aAppendUInt(out, g_inputHostEpoch);
    out += ",\"imu\":{\"accelG\":{\"x\":"; number(c.imuAdmitted ? c.accelX : 0.0);
    out += ",\"y\":"; number(c.imuAdmitted ? c.accelY : 0.0);
    out += ",\"z\":"; number(c.imuAdmitted ? c.accelZ : 0.0);
    out += "},\"admitted\":"; out += c.imuAdmitted ? "true" : "false";
    out += ",\"gyroDps\":{\"x\":"; number(c.imuAdmitted ? c.gyroX : 0.0);
    out += ",\"y\":"; number(c.imuAdmitted ? c.gyroY : 0.0);
    out += ",\"z\":"; number(c.imuAdmitted ? c.gyroZ : 0.0);
    out += "},\"quaternion\":{\"w\":"; number(c.imuAdmitted ? c.qw : 1.0);
    out += ",\"x\":"; number(c.imuAdmitted ? c.qx : 0.0);
    out += ",\"y\":"; number(c.imuAdmitted ? c.qy : 0.0);
    out += ",\"z\":"; number(c.imuAdmitted ? c.qz : 0.0);
    out += "}},\"lt\":"; number(c.lt);
    out += ",\"ltRaw\":"; number(c.ltRaw);
    out += ",\"lx\":"; number(inputHostNormalizeAxis(c.lxShort));
    out += ",\"ly\":"; number(inputHostNormalizeAxis(c.lyShort));
    out += ",\"monotonicTick\":"; c6aAppendUInt(out, monotonicTick);
    out += ",\"motionPairProofId\":"; c6aAppendEscaped(out, motionPairProofId);
    out += ",\"normalizedInputHash\":"; c6aAppendEscaped(out, normalizedInputHash);
    out += ",\"ownerIdentity\":"; c6aAppendEscaped(out, kInputHostOwnerIdentity);
    out += ",\"persona\":"; c6aAppendEscaped(out, inputHostBoundOrCurrentPersona());
    out += ",\"powerGeneration\":"; c6aAppendUInt(out, inputHostProtocolPowerGenerationLocked());
    out += ",\"profileIdentity\":"; c6aAppendEscaped(out, inputHostBoundOrCurrentProfileIdentity());
    out += ",\"protocolVersion\":1,\"raw\":{\"bLeftTrigger\":"; c6aAppendUInt(out, c.rawLtByte);
    out += ",\"bRightTrigger\":"; c6aAppendUInt(out, c.rawRtByte);
    out += ",\"buttons\":"; c6aAppendInt(out, c.buttons);
    out += ",\"sThumbLX\":"; c6aAppendInt(out, c.lxShort);
    out += ",\"sThumbLY\":"; c6aAppendInt(out, c.lyShort);
    out += ",\"sThumbRX\":"; c6aAppendInt(out, c.rxShort);
    out += ",\"sThumbRY\":"; c6aAppendInt(out, c.ryShort);
    out += "},\"requestId\":"; c6aAppendEscaped(out, requestId);
    out += ",\"rt\":"; number(c.rt);
    out += ",\"rtRaw\":"; number(c.rtRaw);
    out += ",\"runId\":"; c6aAppendEscaped(out, g_inputHostRunId);
    out += ",\"rx\":"; number(inputHostNormalizeAxis(c.rxShort));
    out += ",\"ry\":"; number(inputHostNormalizeAxis(c.ryShort));
    out += ",\"sampleSequence\":"; c6aAppendUInt(out, sampleSequence);
    if(c.screenButtons) {out += ",\"screenButtons\":"; c6aAppendUInt(out, c.screenButtons);}
    out += ",\"sequence\":"; c6aAppendUInt(out, sequence);
    out += ",\"startupNonce\":"; c6aAppendEscaped(out, g_inputHostStartupNonce);
    out += ",\"targetId\":"; c6aAppendEscaped(out, kInputHostTargetId);
    if(c.touchpads.present) {
        out += ",\"touchpads\":";
        c6aAppendJsonValue(out, ymcc::screenpads::snapshotJson(c.touchpads));
    }
    out.push_back('}');
    return out;
}

// ── 归属快照与发布 ──

static unsigned long long focusIsolationEpochSnapshot() {
    return g_focusIsolationEpoch.load(std::memory_order_acquire);
}
static bool focusIsolationTaskAllowed(bool externalInjection, unsigned long long capturedEpoch) {
    return focusIsolationTaskDecision(
        g_focusIsolationEnabled.load(std::memory_order_acquire), externalInjection,
        g_focusIsolationNeutralNow.load(std::memory_order_acquire),
        capturedEpoch, g_focusIsolationEpoch.load(std::memory_order_acquire));
}
// 外部注入的**发送边界**复检：不只靠入队时的一次 epoch 检查。
// nativeKeyboardShortcutSend requests down/up in one SendInput batch. A return
// count below the request does not prove the pair was fully inserted; R2 logs
// the actual count. Dropping an unexecuted task creates no input. This gate only
// blocks new injection during the ownership/release barrier.
static bool focusIsolationExternalInjectionBlocked() {
    return g_focusIsolationInjectionBlocked.load(std::memory_order_acquire);
}
// 屏障期间要屏蔽的位图（0..15 = wButtons，16..17 = backButtons）。不适用 ⇒ 恒 0。
static unsigned long long focusIsolationReleaseBarrierMask() {
    std::lock_guard<std::mutex> lk(g_focusIsolationMx);
    if (!g_focusIsolation.applicable || !g_focusIsolation.releaseBarrierActive) return 0ull;
    return g_focusIsolation.releaseBarrierMask;
}

// 归属判定（窗口侧语义；BUS tick 每拍调用一次，不新增线程/定时器）。
// applicable 由调用方给出（虚拟目标已启用）；不适用时 requested 恒 false。
static bool focusIsolationRequestedNow(bool applicable, bool* expandedOut, bool* foregroundOut) {
    const bool windowValid = g_hwnd && IsWindow(g_hwnd);
    const bool visible = windowValid && IsWindowVisible(g_hwnd);
    const bool iconic = windowValid && IsIconic(g_hwnd);
    // 实际展开：全高贴边布局（应用自定义铺满）或系统最大化，两者都算。
    const bool expanded = windowValid &&
        (g_fullHeight.load(std::memory_order_acquire) || IsZoomed(g_hwnd));
    const bool foreground = windowValid && focusMainWindowIsForeground();
    if (expandedOut) *expandedOut = expanded;
    if (foregroundOut) *foregroundOut = foreground;
    return focusIsolationOwnershipRequested(applicable, expanded, foreground, visible, iconic);
}

// 每拍发布归属快照。**未启用时零副作用**：不读窗口、不推进 epoch、不写日志、不抢锁。
// B2（裁决 §3.1）：applicable=false（虚拟关闭/物理直通）时 requested 恒 false ⇒ 不设 neutralNow、
// 不设 injectionBlocked、不建屏障；且适用性切换会推进 epoch 使旧外部任务/旧回执失效。
static void focusIsolationPublishOwnership(const char* why, bool applicable, bool live,
                                          unsigned long long liveButtons,
                                          unsigned long long liveBackButtons) {
    if (!g_focusIsolationEnabled.load(std::memory_order_acquire)) return;

    bool expanded = false, foreground = false;
    const bool requested = focusIsolationRequestedNow(applicable, &expanded, &foreground);

    const unsigned long long powerGen = currentPowerGeneration();
    unsigned long long hostGen = 0;
    std::string runId;
    {
        // Host 代次与目标会话身份的唯一写者边界（裁决 §3）：与原写者用同一把锁。
        std::lock_guard<InputHostMutex> lk(g_inputHostMx);
        hostGen = g_inputHostEpoch;
        runId = g_inputHostRunId;
    }

    bool reqChanged = false;
    bool applicableChanged = false;
    bool invalidated = false;
    bool barrierActive = false;
    unsigned long long epoch = 0;
    unsigned long long barrierMask = 0;
    {
        std::lock_guard<std::mutex> lk(g_focusIsolationMx);
        reqChanged = (g_focusIsolation.requested != requested);
        applicableChanged = (g_focusIsolation.applicable != applicable);
        // epoch 必须对影响所有权的 Host/电源/目标换代生效，不能只记字段不失效任务。
        invalidated = reqChanged || applicableChanged ||
            (hostGen != g_focusIsolation.hostGen) || (powerGen != g_focusIsolation.powerGen);
        g_focusIsolation.applicable = applicable;
        g_focusIsolation.expanded = expanded;
        g_focusIsolation.foreground = foreground;
        g_focusIsolation.powerGen = powerGen;
        g_focusIsolation.hostGen = hostGen;
        g_focusIsolation.targetRunId = runId;
        if (applicableChanged) {
            g_focusIsolation.transitions++;
            g_focusIsolation.sinceTick = GetTickCount64();
            if (!applicable) {
                // 切到不适用：清实验态（接管/屏障/已生效），旧外部任务与旧回执随 epoch 失效。
                g_focusIsolation.requested = false;
                g_focusIsolation.releaseBarrierMask = 0;
                g_focusIsolation.releaseBarrierActive = false;
                g_focusIsolation.releaseBarrierUnresolved = false;
            }
        }
        if (reqChanged) {
            g_focusIsolation.requested = requested;
            g_focusIsolation.transitions++;
            g_focusIsolation.sinceTick = GetTickCount64();
            if (requested) {
                // 进入接管：屏障无效。
                g_focusIsolation.releaseBarrierMask = 0;
                g_focusIsolation.releaseBarrierActive = false;
                g_focusIsolation.releaseBarrierUnresolved = false;
            } else if (!g_focusIsolation.neutralOnly && focusIsolationBarrierShouldStart(applicable, requested)) {
                // 退出接管 ⇒ 进入释放屏障：把此刻仍按下的位记下，逐位等松开。
                // 不这样做的后果：仍按住的退出键（含背键）会在下一拍直接进外部输出。
                focusIsolationNoteExitHoldLocked(g_focusIsolation, live, liveButtons, liveBackButtons);
            }
        }
        if (invalidated) {
            // 归属/适用性/Host/电源换代 ⇒ 撤销「已生效」声明（applied 必须同代次/同目标）。
            ++g_focusIsolation.epoch;
            g_focusIsolation.applied = false;
        }
        if (applicable && !requested) {
            focusIsolationAdvanceBarrierLocked(g_focusIsolation, live, liveButtons, liveBackButtons);
        }
        epoch = g_focusIsolation.epoch;
        barrierActive = g_focusIsolation.releaseBarrierActive;
        barrierMask = g_focusIsolation.releaseBarrierMask;
    }
    g_focusIsolationApplicable.store(applicable, std::memory_order_release);
    g_focusIsolationEpoch.store(epoch, std::memory_order_release);
    g_focusIsolationNeutralNow.store(requested, std::memory_order_release);
    // 外部注入：接管中或释放屏障期间都不新增注入；**不适用时一律不拦**（裁决 §3.1）。
    // 2026-09-23 B 版：neutralOnly 时不启用注入拦截（完整模式 --focus-isolation 保持原语义）。
    g_focusIsolationInjectionBlocked.store(
        !g_focusIsolation.neutralOnly &&
            focusIsolationInjectionBlockedNow(applicable, requested, barrierActive), std::memory_order_release);
    if (barrierActive) g_focusIsolationBarrierHolds.fetch_add(1, std::memory_order_relaxed);
    if (reqChanged || applicableChanged) {
        appendNativeLifecycleLog("focus-isolation-transition", {
            {"requested", requested}, {"why", why ? why : ""}, {"epoch", epoch},
            {"applicable", applicable},
            {"expanded", expanded}, {"foreground", foreground},
            {"powerGen", powerGen}, {"hostGen", hostGen},
            {"barrierMask", barrierMask},
            {"barrierUnresolved", g_focusIsolation.releaseBarrierUnresolved},
            {"neutralFrames", g_focusIsolationNeutralFrames.load(std::memory_order_relaxed)},
            {"cleanupFrames", g_focusIsolationCleanupFrames.load(std::memory_order_relaxed)},
            {"submitFailures", g_focusIsolationSubmitFailures.load(std::memory_order_relaxed)},
            {"submitSkipped", g_focusIsolationSubmitSkipped.load(std::memory_order_relaxed)},
            {"submitRejected", g_focusIsolationSubmitRejected.load(std::memory_order_relaxed)},
            {"staleAcks", g_focusIsolationStaleAcks.load(std::memory_order_relaxed)},
            {"injectionDropped", g_focusIsolationInjectionDropped.load(std::memory_order_relaxed)},
            {"enabled", g_focusIsolation.enabled}
        });
    }
}

// ── 驻留/重确认/hide-only 恢复 离线自测（2026-09-23 执行单 §6「离线生产路径」）──
// 纯逻辑 + 纯函数驱动：不含设备、不睡眠、不启停服务、不写固件；exit 0 = PASS，3 = FAIL。
// 覆盖：保留集策略不变量（隔离已失效）/ 入队判据（同元组去重、换代可再排队、目标关、journal 关、
// runId 空）/ 过期上限（20 s 边界）/ 完整中性清理帧（12 wire 字段 + 单位四元数）/ 保留的旧 ACK 判定。
static bool inputHostResidencySelfTest() {
    std::string report = "residency-recovery-selftest (offline, fake state; execution sheet section 6):\n";
    bool pass = true;
    auto check = [&](const char* name, bool ok) {
        report += std::string("  ") + (ok ? "PASS " : "FAIL ") + name + "\n";
        if (!ok) pass = false;
    };

    // G1 保留集策略不变量：聚焦隔离策略已舍弃 ⇒ 生产函数在默认状态下必须是惰性的。
    check("isolation-inert-barrier-apply-no-mask",
          focusIsolationBarrierApply(0ull, 0xFFFFu) == 0xFFFFu &&
          focusIsolationBarrierApply(0ull, 0u) == 0u);
    check("isolation-inert-release-barrier-mask-zero",
          focusIsolationReleaseBarrierMask() == 0ull);
    check("isolation-inert-external-injection-not-blocked",
          !focusIsolationExternalInjectionBlocked());
    // applicable 的唯一写者是已删除的 PublishOwnership 调用 ⇒ 该谓词只会收到 applicable=false。
    check("isolation-inert-ownership-not-requested",
          !focusIsolationOwnershipRequested(false, true, true, true, false) &&
          focusIsolationOwnershipRequested(true, true, true, true, false));   // 谓词本体未被误改

    // G2 入队判据（生产与自测共用 inputHostLateHideSkipGate）。末三参 = GP-MSIG3E-4B 的
    // MSI 旁路上下文；本组为**非 MSI / 未知来源**基线，故来源恒 None（原语义不变）。
    const std::string kA = "1|10|100|run-A";
    const std::string kB = "1|11|100|run-A";
    const std::string kC = "2|10|100|run-A";
    const std::string kD = "1|10|101|run-A";
    const InputHostLateHideBypassSource kNoSource = InputHostLateHideBypassSource::None;
    check("gate-queue-when-fresh",
          inputHostLateHideSkipGate(true, true, false, "run-A", kA, std::string(),
                                    kNoSource, false, false) == nullptr);
    check("gate-dedupe-same-tuple",
          std::string(inputHostLateHideSkipGate(true, true, false, "run-A", kA, kA,
                                                kNoSource, false, false)) ==
              "already-queued-this-tuple");
    check("gate-host-gen-change-requeues",
          inputHostLateHideSkipGate(true, true, false, "run-A", kB, kA,
                                    kNoSource, false, false) == nullptr);
    check("gate-power-gen-change-requeues",
          inputHostLateHideSkipGate(true, true, false, "run-A", kC, kA,
                                    kNoSource, false, false) == nullptr);
    check("gate-source-gen-change-requeues",
          inputHostLateHideSkipGate(true, true, false, "run-A", kD, kA,
                                    kNoSource, false, false) == nullptr);
    check("gate-virtual-target-off",
          std::string(inputHostLateHideSkipGate(false, true, false, "run-A", kA, std::string(),
                                                kNoSource, false, false)) ==
              "virtual-target-not-enabled");
    check("gate-no-journal-no-hold",
          std::string(inputHostLateHideSkipGate(true, false, false, "run-A", kA, std::string(),
                                                kNoSource, false, false)) ==
              "no-hide-journal-and-no-wake-hold");
    check("gate-journal-inactive-but-wake-hold-queues",
          inputHostLateHideSkipGate(true, false, true, "run-A", kA, std::string(),
                                    kNoSource, false, false) == nullptr);
    check("gate-run-id-empty",
          std::string(inputHostLateHideSkipGate(true, true, false, "", kA, std::string(),
                                                kNoSource, false, false)) ==
              "run-id-empty");

    // G2b 过期上限：20 s 边界（19999 ms 未过期 / 20001 ms 过期 / 未排队不算过期）。
    check("expiry-0-tick-not-expired", !inputHostLateHideExpired(0, 999999999ull));
    check("expiry-within-window", !inputHostLateHideExpired(1000ull, 1000ull + 19999ull));
    check("expiry-past-window", inputHostLateHideExpired(1000ull, 1000ull + 20001ull));

    // G3 保留的"完整中性清理帧"：12 个 wire 字段 + 单位四元数 + backButtons 全中性。
    {
        const json frame = focusIsolationAssembleFrame(focusIsolationNeutralContent());
        const json imu = frame.value("imu", json::object());
        const json quat = imu.value("quaternion", json::object());
        const bool neutral =
            frame.value("buttons", 1) == 0 && frame.value("backButtons", 1) == 0 &&
            frame.value("lx", 1.0) == 0.0 && frame.value("ly", 1.0) == 0.0 &&
            frame.value("rx", 1.0) == 0.0 && frame.value("ry", 1.0) == 0.0 &&
            frame.value("lt", 1.0) == 0.0 && frame.value("rt", 1.0) == 0.0 &&
            imu.value("admitted", true) == false &&
            quat.value("w", 0.0) == 1.0 && quat.value("x", 1.0) == 0.0;
        check("neutral-cleanup-frame-complete", neutral);
        const FocusNeutralDsu nd = focusIsolationNeutralDsu();
        // DSU 值域 0..65535，中性 = 摇杆居中 32768（不是 0）；按钮/扳机/运动部位全零。
        check("neutral-dsu-wire-centered",
              !nd.hasMotion && nd.buttons == 0 &&
              nd.lx == 32768.0f && nd.ly == 32768.0f && nd.rx == 32768.0f && nd.ry == 32768.0f &&
              nd.lt == 0 && nd.rt == 0);
    }

    // G4 保留的旧 ACK 判定（Host 换代后旧 ACK 不得重新宣布已生效）。
    check("stale-ack-classification-kept",
          focusIsolationClassifySubmission("frame-accepted") == FocusSubmitOutcome::Accepted &&
          focusIsolationClassifySubmission("not-running") == FocusSubmitOutcome::Failed &&
          focusIsolationClassifySubmission("") == FocusSubmitOutcome::Missing);

    report += std::string("RESULT=") + (pass ? "PASS" : "FAIL") + "\n";
    try {
        std::ofstream f(yemancc_data_dir() + L"\\residency-recovery-selftest.txt",
                        std::ios::binary | std::ios::trunc);
        f << report;
    } catch (...) { }
    return pass;
}

// 帧提交处的计划：读快照 + Host 状态，落决策。
// sourceAdmitted 走**产品路径真实值**（B0 硬编码 true，断源场景没走产品路径）：
// 接管中且目标可写时，源缺失也要发中性清理帧，因此这里只把它记录成 neutralCleanup。
// MSI's managed physical source must remain available to the existing UI.
// Extend only its final virtual-output isolation to the other bound personas;
// requested personas, unknown MSI boards and every other family are excluded.
struct FocusPlan {
    FocusDecision decision = FocusDecision::PassThrough;
    bool neutralCleanup = false;   // 接管中但源缺失 ⇒ 用中性帧清理/释放
    bool steamDeckTarget = false;  // Bound target, captured under the existing Host lock.
    bool ps5Target = false;        // Current UI PS5 option: dualsense-edge only.
    bool msiTarget = false;        // Authorized MSI + actual bound supported target only.
    const char* virtualUiPersona = "";
    unsigned long long virtualUiHostEpoch = 0;
};
static FocusPlan focusIsolationPlanTick(bool sourceAdmitted) {
    // 2026-09-23 执行单 §3/§4：本函数只剩**保留集**语义（已无隔离策略、无接管、无窗口读取、
    // 无归属代次推进、无隔离日志）：
    //   Host 暂不可用            ⇒ SkipHostUnavailable（抑制发布，绝不放活输入出去）
    //   Host 可写但物理源暂缺    ⇒ Neutral + neutralCleanup（发**一次完整中性清理帧**，避免残留按键）
    //   其余                     ⇒ PassThrough（原样直通）
    FocusPlan plan;
    bool hostUsable = false;
    {
        std::lock_guard<InputHostMutex> lk(g_inputHostMx);
        hostUsable = g_inputHostProcess != nullptr && g_inputHostPrepared && g_inputHostNeutralized &&
                     !g_inputHostTransportFault;
        plan.steamDeckTarget = g_inputHostBoundPersona == "steamdeck";
        plan.ps5Target = g_inputHostBoundPersona == "dualsense-edge";
        plan.msiTarget = msiUiBoundTargetAdmitted(g_inputHostBoundPersona);
        plan.virtualUiPersona = virtualUiBoundPersona(g_inputHostBoundPersona);
        plan.virtualUiHostEpoch = g_inputHostEpoch;
    }
    hostUsable = hostUsable && !inputHostLifecycleBusy() &&
                 !g_inputHostFaultHoldsStart.load(std::memory_order_acquire) &&
                 g_inputReady.load(std::memory_order_acquire) && sgVirtualWriterAllowedNow() &&
                 !g_inputHostPowerAdmissionRevoked.load(std::memory_order_acquire);
    if (!hostUsable) {
        plan.decision = FocusDecision::SkipHostUnavailable;
        g_focusIsolationSubmitSkipped.fetch_add(1, std::memory_order_relaxed);
        return plan;
    }
    if (!sourceAdmitted) {
        plan.decision = FocusDecision::Neutral;
        plan.neutralCleanup = true;
        g_focusIsolationNeutralFrames.fetch_add(1, std::memory_order_relaxed);
    }
    return plan;
}

// Reuse the authoritative physical-UI admission boundary for virtual output.
// It validates both YMCC foreground and its parent-owned CustomSteamLibrary
// child, including the existing launch/return gates. Do not add a child poller
// or a second ownership state machine here.
static bool gamepadUiInputEligible();

static std::atomic<bool> g_radialUiOwnsInput{false};
static std::atomic<bool> g_radialReturning{false};
static void radialProcessUiInput(const XINPUT_GAMEPAD&, ULONGLONG);
static void radialSummonRequest(const char*,int=0,int=0);
static bool radialHasInputOwner(){return g_radialUiOwnsInput.load(std::memory_order_acquire)||g_radialReturning.load(std::memory_order_acquire);}
static bool virtualUiOwnsInput(const FocusPlan& plan) {
    if (!plan.virtualUiPersona[0] || !g_realStickTest.enabled ||
        !g_realStickTest.virtualTargetEnabled) return false;
    return gamepadUiInputEligible();
}

static void dsuUiOutputNote(bool uiOwnsInput, bool submitNeutral) {
    static int previousState = -1;
    const int state = (uiOwnsInput ? 1 : 0) | (submitNeutral ? 2 : 0);
    if (state == previousState) return;
    previousState = state;
    appendNativeLifecycleLog("dsu-ui-output", {
        {"phase", submitNeutral ? "neutral" : "passthrough"},
        {"uiOwnsInput", uiOwnsInput}, {"submitNeutral", submitNeutral},
        {"verificationScope", "dsu-feed-only"}
    });
}

static void virtualUiOutputNote(const FocusPlan& plan, bool neutral, bool submitNeutral,
    const json& hostFrame, bool sourceLive, const std::string& source) {
    static const char* previousPersona = "";
    static unsigned long long previousHostEpoch = 0;
    static bool previousNeutral = false;
    static bool awaitingNeutralReceipt = false;
    const bool changed = neutral != previousNeutral ||
        plan.virtualUiHostEpoch != previousHostEpoch ||
        std::strcmp(plan.virtualUiPersona, previousPersona) != 0;
    if (!changed && !awaitingNeutralReceipt) return;
    const bool accepted = hostFrame.value("hostSubmission", std::string()) == "frame-accepted";
    const bool neutralAccepted = accepted && submitNeutral &&
        hostFrame.value("focusIsolationSemanticNeutral", false);
    if (!changed && !neutralAccepted) return;
    previousPersona = plan.virtualUiPersona;
    previousHostEpoch = plan.virtualUiHostEpoch;
    previousNeutral = neutral;
    awaitingNeutralReceipt = neutral && !neutralAccepted;
    appendNativeLifecycleLog("virtual-ui-output", {
        {"persona", plan.virtualUiPersona},
        {"hostEpoch", plan.virtualUiHostEpoch},
        {"phase", changed ? (neutral ? "neutral-requested" : "passthrough-requested") : "neutral-accepted"},
        {"uiOwnsInput", neutral}, {"submitNeutral", submitNeutral},
        {"frameAccepted", accepted}, {"neutralAccepted", neutralAccepted},
        {"hostSubmission", hostFrame.value("hostSubmission", std::string("no-frame"))},
        {"sourceLive", sourceLive}, {"inputSource", source},
        {"verificationScope", "final-virtual-output-only"}
    });
}

// GP-UIINPUT-3: quiet only the resident Deck output while the existing native
// UI route consumes the unchanged physical sample. Keep the target alive and
// check the bound persona, not a requested persona that may still be switching.
static bool steamDeckUiOwnsInput(const FocusPlan& plan) {
    if (!plan.steamDeckTarget || !g_realStickTest.enabled ||
        !g_realStickTest.virtualTargetEnabled) return false;
    return gamepadUiInputEligible();
}

// Transition-only receipt. These flags observe output and never gate input.
static void steamDeckUiOutputNote(bool neutral, bool submitNeutral, const json& hostFrame) {
    static bool previousNeutral = false;
    static bool awaitingNeutralReceipt = false;
    const bool changed = neutral != previousNeutral;
    if (!changed && !awaitingNeutralReceipt) return;
    const bool accepted = hostFrame.value("hostSubmission", std::string()) == "frame-accepted";
    const bool neutralAccepted = accepted && submitNeutral &&
        hostFrame.value("focusIsolationSemanticNeutral", false);
    if (changed) {
        previousNeutral = neutral;
        awaitingNeutralReceipt = neutral && !neutralAccepted;
        appendNativeLifecycleLog("steamdeck-ui-output", {
            {"phase", neutral ? "neutral-requested" : "passthrough-requested"},
            {"uiOwnsInput", neutral}, {"submitNeutral", submitNeutral},
            {"frameAccepted", accepted}, {"neutralAccepted", neutralAccepted},
            {"hostSubmission", hostFrame.value("hostSubmission", std::string("no-frame"))}
        });
    } else if (awaitingNeutralReceipt && neutralAccepted) {
        awaitingNeutralReceipt = false;
        appendNativeLifecycleLog("steamdeck-ui-output", {
            {"phase", "neutral-accepted"}, {"uiOwnsInput", true},
            {"submitNeutral", true}, {"frameAccepted", true}, {"neutralAccepted", true}
        });
    }
}

// GP-UIINPUT-4: PS5 shares the same native physical-UI admission boundary.
// Only final virtual output is quiet; source sampling and lifecycle are unchanged.
static bool ps5UiOwnsInput(const FocusPlan& plan) {
    if (!plan.ps5Target || !g_realStickTest.enabled ||
        !g_realStickTest.virtualTargetEnabled) return false;
    return gamepadUiInputEligible();
}

// MSI-only addition: share the exact foreground/parent-owned-child UI boundary.
// It never takes over the source reader, rewrites the pad, or lifts Host gates.
static bool msiUiOwnsInput(const FocusPlan& plan) {
    if (!plan.msiTarget || !g_realStickTest.enabled ||
        !g_realStickTest.virtualTargetEnabled) return false;
    return gamepadUiInputEligible();
}

// Transition-only MSI receipt distinguishes input-source health from output
// neutralization. It never claims that HidHide suppressed an external reader.
static void msiUiOutputNote(bool neutral, bool submitNeutral, const json& hostFrame,
    bool sourceLive, const std::string& source) {
    if (!msiClawKnownBoardAuthority()) return;
    msiForegroundIsolationSetUiOwned(neutral);
    static bool previousNeutral = false;
    static bool awaitingNeutralReceipt = false;
    const bool changed = neutral != previousNeutral;
    if (!changed && !awaitingNeutralReceipt) return;
    const bool accepted = hostFrame.value("hostSubmission", std::string()) == "frame-accepted";
    const bool neutralAccepted = accepted && submitNeutral &&
        hostFrame.value("focusIsolationSemanticNeutral", false);
    if (!changed && !neutralAccepted) return;
    previousNeutral = neutral;
    awaitingNeutralReceipt = neutral && !neutralAccepted;
    appendNativeLifecycleLog("msi-ui-output", {
        {"phase", changed ? (neutral ? "neutral-requested" : "passthrough-requested") : "neutral-accepted"},
        {"uiOwnsInput", neutral}, {"submitNeutral", submitNeutral},
        {"frameAccepted", accepted}, {"neutralAccepted", neutralAccepted},
        {"sourceLive", sourceLive}, {"inputSource", source},
        {"hostSubmission", hostFrame.value("hostSubmission", std::string("no-frame"))},
        {"verificationScope", "final-virtual-output-only"},
        {"physicalReaderChanged", false}, {"externalPhysicalSuppression", "unverified"}
    });
}

// Observe the existing source snapshot and the final-output receipt at transitions.
// No source probing, ownership changes or per-action tracing in this path.
static void ps5UiOutputNote(bool neutral, bool submitNeutral, const json& hostFrame,
    bool sourceLive, const std::string& source, DWORD slot, DWORD admittedMask, WORD buttons) {
    static bool previousNeutral = false;
    static bool awaitingNeutralReceipt = false;
    const bool changed = neutral != previousNeutral;
    if (!changed && !awaitingNeutralReceipt) return;
    const bool accepted = hostFrame.value("hostSubmission", std::string()) == "frame-accepted";
    const bool neutralAccepted = accepted && submitNeutral &&
        hostFrame.value("focusIsolationSemanticNeutral", false);
    if (changed || (awaitingNeutralReceipt && neutralAccepted)) {
        previousNeutral = neutral;
        awaitingNeutralReceipt = neutral && !neutralAccepted;
        appendNativeLifecycleLog("ps5-ui-output", {
            {"phase", changed ? (neutral ? "neutral-requested" : "passthrough-requested") : "neutral-accepted"},
            {"uiOwnsInput", neutral}, {"submitNeutral", submitNeutral},
            {"frameAccepted", accepted}, {"neutralAccepted", neutralAccepted},
            {"hostSubmission", hostFrame.value("hostSubmission", std::string("no-frame"))},
            {"sourceLive", sourceLive}, {"inputSource", source},
            {"sourceSlot", slot}, {"sourceAdmittedMask", admittedMask}, {"sourceButtons", buttons}
        });
    }
}

// ── B2/B3（裁决 §3.2 + §4.2）：提交身份捕获 + 回执时**权威身份**复核（stale-ack）──
// 发送**开始**时捕获 {归属代次, Host 代次, 目标会话身份}；回执回来后重新核对。
// B3（§4.2）：Host 代次与目标会话身份以 **g_inputHostMx 保护的权威值**为准，不再只比
// g_focusIsolation 的 BUS 缓存 —— 否则「Host 已换代、下一拍缓存尚未刷新」的窗口里，
// 旧 ACK 会被误判为当前；归属代次仍取 focus 模块自己的权威 epoch。
struct FocusSubmitIdentity {
    unsigned long long epoch = 0;
    unsigned long long hostGen = 0;
    std::string runId;
};
// 读权威 Host 身份（与 Host 写者同一把锁；**不持有** g_focusIsolationMx，避免双锁倒序）。
static void focusIsolationReadAuthoritativeHost(unsigned long long* hostGen, std::string* runId) {
    std::lock_guard<InputHostMutex> lk(g_inputHostMx);
    if (hostGen) *hostGen = g_inputHostEpoch;
    if (runId) *runId = g_inputHostRunId;
}
// B5（裁决 §3）：生命周期线程在 Host 换代/换目标后调用。**只取 focus 锁**（Host 锁已释放）。
// 语义：发布新序号 ⇒ 在途回执按新序号判 stale；撤销 applied ⇒ 已写入的旧回执不再代表当前状态。
// D18：与其它入口同一约定（42816「关闭时零副作用」、focusIsolationPublishOwnership/focusIsolationPlanTick
// 的同名守卫）——**关闭时必须是 no-op**：不推进代次、不刷新缓存、不撤销 applied、不落日志。
// 关闭时它语义上必然无意义：回执写入路径只在 decision != PassThrough（需 enabled）时可达。
static void focusIsolationNoteHostGenerationChanged(unsigned long long hostGen, const std::string& runId) {
    if (!g_focusIsolationEnabled.load(std::memory_order_acquire)) return;
    unsigned long long newEpoch = 0;
    {
        std::lock_guard<std::mutex> lk(g_focusIsolationMx);
        if (g_focusIsolation.hostGen == hostGen && g_focusIsolation.targetRunId == runId) return;
        g_focusIsolation.hostGen = hostGen;
        g_focusIsolation.targetRunId = runId;
        // 换代 ⇒ 旧外部任务与旧回执一并失效（与 BUS 侧 invalidated 分支同语义）。
        ++g_focusIsolation.epoch;
        g_focusIsolation.applied = false;
        g_focusIsolation.appliedEpoch = 0;
        g_focusIsolation.appliedHostGen = 0;
        g_focusIsolation.appliedRunId.clear();
        newEpoch = g_focusIsolation.epoch;
    }
    g_focusIsolationEpoch.store(newEpoch, std::memory_order_release);
    appendNativeLifecycleLog("focus-isolation-host-generation", {
        {"hostGen", hostGen}, {"runId", runId}, {"epoch", newEpoch},
        {"why", "lifecycle-owner-published"}
    });
}
static FocusSubmitIdentity focusIsolationCaptureIdentity() {
    FocusSubmitIdentity id;
    {
        std::lock_guard<std::mutex> lk(g_focusIsolationMx);
        id.epoch = g_focusIsolation.epoch;
    }
    focusIsolationReadAuthoritativeHost(&id.hostGen, &id.runId);
    return id;
}

// 提交结果：只有**同代次/同目标**的明确成功回执才置 applied；跳过/拒绝/失败分别记录。
// 日志只在结果类别**变化**时落一条（替换 B0 的每拍 focus-isolation-frame）。
static void focusIsolationNoteSubmitResult(FocusSubmitOutcome outcome, const FocusSubmitIdentity& identity,
                                           bool neutralCleanup) {
    if (neutralCleanup) g_focusIsolationCleanupFrames.fetch_add(1, std::memory_order_relaxed);
    bool stale = false;
    if (outcome == FocusSubmitOutcome::Accepted) {
        {
            // 先在 Host 写者边界读**权威身份**（不持 focus 锁），再进 focus 锁比较。
            unsigned long long authHostGen = 0;
            std::string authRunId;
            focusIsolationReadAuthoritativeHost(&authHostGen, &authRunId);
            std::lock_guard<std::mutex> lk(g_focusIsolationMx);
            // 回执期间换代/换目标（含「Host 已换代、BUS 缓存尚未刷新」窗口）⇒ 旧回执不得置 applied。
            // B5（裁决 §3）：Host 换代由**生命周期线程**执行，与 BUS tick 不同线程；其发布函数在
            // **本锁内**推进 epoch 并撤销 applied ⇒ 覆盖「读完权威身份 → 释放 Host 锁 → 换代 →
            // 写 applied」交错：发布先拿到锁则此处 epoch 不等（stale）；本处先写入则发布随后撤销。
            stale = (g_focusIsolation.epoch != identity.epoch) ||
                    (authHostGen != identity.hostGen) ||
                    (authRunId != identity.runId);
            if (!stale) {
                g_focusIsolation.applied = true;
                g_focusIsolation.appliedEpoch = identity.epoch;
                g_focusIsolation.appliedHostGen = identity.hostGen;
                g_focusIsolation.appliedRunId = identity.runId;
            } else {
                g_focusIsolation.applied = false;
                g_focusIsolation.staleAcks++;
            }
        }
        if (stale) {
            g_focusIsolationStaleAcks.fetch_add(1, std::memory_order_relaxed);
            // 有界记录：只在类别变化时落一条（这里用 loggedOutcome 之外的独立标志）。
            bool logNow = false;
            {
                std::lock_guard<std::mutex> lk(g_focusIsolationMx);
                if (g_focusIsolation.loggedOutcome != 100) { g_focusIsolation.loggedOutcome = 100; logNow = true; }
            }
            if (logNow) {
                appendNativeLifecycleLog("focus-isolation-stale-ack", {
                    {"capturedEpoch", identity.epoch}, {"currentEpoch", g_focusIsolationEpoch.load(std::memory_order_acquire)},
                    {"capturedHostGen", identity.hostGen}, {"capturedRunId", identity.runId},
                    {"staleAcks", g_focusIsolationStaleAcks.load(std::memory_order_relaxed)}
                });
            }
        }
        return;
    }
    if (outcome == FocusSubmitOutcome::Skipped)
        g_focusIsolationSubmitSkipped.fetch_add(1, std::memory_order_relaxed);
    else if (outcome == FocusSubmitOutcome::Rejected)
        g_focusIsolationSubmitRejected.fetch_add(1, std::memory_order_relaxed);
    else
        g_focusIsolationSubmitFailures.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lk(g_focusIsolationMx);
        g_focusIsolation.applied = false;
    }
    const int code = static_cast<int>(outcome);
    bool logNow = false;
    {
        std::lock_guard<std::mutex> lk(g_focusIsolationMx);
        if (g_focusIsolation.loggedOutcome != code) {
            g_focusIsolation.loggedOutcome = code;
            logNow = true;
        }
    }
    if (logNow) {
        appendNativeLifecycleLog("focus-isolation-submit-failed", {
            {"outcome", focusSubmitOutcomeName(outcome)},
            {"epoch", identity.epoch}, {"hostGen", identity.hostGen},
            {"neutralCleanup", neutralCleanup},
            {"neutralFrames", g_focusIsolationNeutralFrames.load(std::memory_order_relaxed)},
            {"cleanupFrames", g_focusIsolationCleanupFrames.load(std::memory_order_relaxed)},
            {"failures", g_focusIsolationSubmitFailures.load(std::memory_order_relaxed)},
            {"skipped", g_focusIsolationSubmitSkipped.load(std::memory_order_relaxed)},
            {"rejected", g_focusIsolationSubmitRejected.load(std::memory_order_relaxed)}
        });
    }
}

// 2026-09-23 执行单 §3：聚焦隔离自测夹具（含 TestSet*/Snapshot/SelfTest 与 fake 时钟驱动）
// 已舍弃（ARCHIVED_REFERENCE）——隔离验收不在本阶段候选内，相关证据保留在历史目录。



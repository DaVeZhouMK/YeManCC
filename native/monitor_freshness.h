// 197/198 C 组：HWiNFO 生产者新鲜度与读取一致性判定（产品与夹具共用；无 Windows 依赖）。
//
// 已证格式（198 §3C；第三方固定提交 Hwinfo.SharedMemory.Net + 本地实测，二者分开标注）：
//   基本头 44 字节，Pack=1：+0 signature / +4 version / +8 revision / **+12 pollTime（64 位，
//   epoch 秒）** / +20 sensorOffset / +24 sensorSize / +28 sensorCount / +32 readingOffset /
//   +36 readingSize / +40 readingCount —— 以上为第三方固定提交口径（非官方 SDK）。
//   本地实测（Patches\Tests\hwinfo_header_probe.cpp，2026-09-20，只读）：本机 version=2、
//   revision=1、sensorOffset=48、sensorSize=392、sensorCount=13、+44=轮询周期 3000ms、
//   readingOffset=5144、readingSize=460、readingCount=462（198c：462 是元素数，不是行尺寸）。
//   ⇒ +44 只在 **version==2 且 sensorOffset>=48**（存在扩展字段）时按上述实测口径读取；
//   未证格式不盲读周期，按"周期未知"（0 ⇒ 默认阈值）处理。
// 口径（197 §3C）：映射仍可读 != 数据在更新；判停更结合实际发布周期；整秒重复或单次值不变
// 不算停更；停更时不得继续刷新数据/健康有效期；pollTime==0 不是健康。
#pragma once
#include <cstdint>

struct MonitorFreshnessState {
    unsigned long long lastPollTime = 0;   // 上次观察到的生产者时间
    unsigned long long lastChangeTick = 0; // 上次"变化"的单调钟(ms)
    unsigned long long staleSinceTick = 0; // 判定停更的单调钟(ms)（诊断用）
    bool stale = false;                    // 198：显式停更标志（不再用 tick==0 当哨兵）
};

// 停更阈值 = max(5000, 周期+3000)，周期钳制 [500, 60000]（实测 3000 ⇒ 6000ms，
// 与既有 6s 健康门限同阶：冻结后不再刷新健康，健康门自然过期）；周期未知（0）⇒ 5000ms。
inline unsigned long long monitorFreshnessThresholdMs(unsigned long long periodMs) {
    unsigned long long p = periodMs;
    if (p < 500ULL) p = 500ULL;
    if (p > 60000ULL) p = 60000ULL;
    unsigned long long t = p + 3000ULL;
    if (t < 5000ULL) t = 5000ULL;
    return t;
}

// 每次读取调用一次：返回"是否停更"。pollTime==0（不可用/不可读）保持既有判定，不因不可读而
// 新判停更（读取失败由 sharedOk 路径处理）——调用方另按 pollTime!=0 决定"是否健康"。
// 198 C：producerAgeMs = 现在 - 生产者时间（调用方注入；未知传 0）。**首次观察**若生产者时间
// 已经超过阈值（例如读到上一实例遗留的映射、或生产者早已停更），直接判陈旧并要求生产者再次
// 更新才恢复；不再无条件把"首次读到的时间"当成新的更新事件。
// 199：**每一次观察**（不只首次）都先过 age 门——"生产者时间发生了变化"不等于"数据新鲜"：
// 换到另一个很旧的时间（旧映射/回退）必须仍判陈旧。时钟回退由调用方把 age 钳到 0 处理，
// 不会因此永久失效；正常 3s 发布 × 1s/5s 读取仍在阈值内保持可用。
inline bool monitorFreshnessTick(MonitorFreshnessState& s, unsigned long long pollTime,
                                 unsigned long long periodMs, unsigned long long nowTick,
                                 unsigned long long producerAgeMs = 0) {
    if (pollTime == 0) return s.stale;
    const unsigned long long threshold = monitorFreshnessThresholdMs(periodMs);
    if (producerAgeMs >= threshold) {
        s.lastPollTime = pollTime;
        s.staleSinceTick = nowTick;
        s.stale = true;
        return true;
    }
    if (s.lastPollTime == 0 || pollTime != s.lastPollTime) {
        // 首次观察 / 生产者更新 / 生产者重启（时间回退也算更新，但须通过上面的 age 门）
        s.lastPollTime = pollTime;
        s.lastChangeTick = nowTick;
        s.staleSinceTick = 0;
        s.stale = false;
        return false;
    }
    if (s.stale) return true; // 已判停更：直到再次更新才解除
    if (nowTick - s.lastChangeTick >= threshold) {
        s.staleSinceTick = nowTick;
        s.stale = true;
        return true;
    }
    return false;
}

// 198 C：健康判定的纯粹部分（产品调用点与夹具共用**同一实现**）：
// 可读 + 布局可读（sharedOk）+ 生产者时间存在（pollTime==0 不是健康）+ 未停更。
inline bool monitorHwHealthyVerdict(bool readable, bool sharedOk, unsigned long long pollTime,
                                    bool fresh) {
    return readable && sharedOk && pollTime != 0 && fresh;
}

// 199：展示时间（top 载荷 ts）的**唯一入口**——输入必须是"完整有效性"的四个原始条件
//（读成功 + 布局有效 + 生产者时间存在 + 数据够新），而不是分项 hwFresh：
//   有效   ⇒ 用本次采集时间（now）
//   否则   ⇒ 沿用最近一次确实有效的采集时间（lastFreshMs；0 = 从未有效过）
// 这样"读取失败/pollTime=0/数据陈旧"都不会给旧或空的数值盖上当前时间。
// 注意：198 的 monitorTopTimestampMs(bool dataValid,...) 已删除——只吃布尔"是否新鲜"的旧入口
// 正是本项缺陷的来源，故不保留该形态，强制调用方走完整判定。
inline long long monitorDisplayTimestampMs(bool readable, bool sharedOk, unsigned long long pollTime,
                                          bool fresh, long long nowMs, long long lastFreshMs) {
    return monitorHwHealthyVerdict(readable, sharedOk, pollTime, fresh) ? nowMs : lastFreshMs;
}

// 199：FPS 有效载荷是否允许产出（供控制的上游）的**唯一入口**：与展示/健康同一份完整有效性，
// 再加传感器存在与数值有效。不健康 ⇒ 不产出，控制端不会拿到不可靠的帧率。
inline bool monitorFpsPayloadAllowed(bool readable, bool sharedOk, unsigned long long pollTime,
                                     bool fresh, bool fpsSensor, double fps) {
    return monitorHwHealthyVerdict(readable, sharedOk, pollTime, fresh) && fpsSensor && fps > 0.0;
}

// header 前后一致性印记（无锁读取的局限如实标注：仅证明"未观察到变化"，非绝对原子）。
// 198 C：印记覆盖**全部决定布局的字段**（口径对齐第三方固定提交的 Describes()），避免只比
// 签名/时间/计数而漏掉传感器区/读数区重排；前后不等 ⇒ 本次读取不稳定，交由有界重试。
struct MonitorHeaderStamp {
    unsigned long long signature = 0;
    unsigned long long version = 0;
    unsigned long long revision = 0;
    unsigned long long pollTime = 0;
    unsigned long long sensorOffset = 0;
    unsigned long long sensorSize = 0;
    unsigned long long sensorCount = 0;
    unsigned long long readingOffset = 0;
    unsigned long long readingSize = 0;
    unsigned long long readingCount = 0;
};

inline bool monitorHeaderStable(const MonitorHeaderStamp& a, const MonitorHeaderStamp& b) {
    return a.signature == b.signature && a.version == b.version && a.revision == b.revision &&
           a.pollTime == b.pollTime && a.sensorOffset == b.sensorOffset &&
           a.sensorSize == b.sensorSize && a.sensorCount == b.sensorCount &&
           a.readingOffset == b.readingOffset && a.readingSize == b.readingSize &&
           a.readingCount == b.readingCount;
}
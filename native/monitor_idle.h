#pragma once
// A5（191 §2 / 193 §2 P2）：监控空闲转换与有界删除重试的**共享决策实现** ——
// 产品 native/main.cpp 与隔离 harness 引用同一份，避免"复刻一套算法自证"。
//
// 边界（如实标注）：条件变量的等待/通知本身仍在 main.cpp（不重做架构无法抽出），
// 本头只固化**可判定**的决策：是否清理、是否继续重试（有界）、等待谓词、是否退出。
// 产品调用点：nativeMonitorLoop 的闲置分支 / 循环顶退出判断。

static constexpr int kMonitorDeleteMaxAttempts = 3;
static constexpr int kMonitorDeleteRetryBackoffMs = 20;

// 同一段闲置只清理一次：活跃期由调用方复位标记，使下一次闲置转换再清一次。
static inline bool monitorIdleShouldCleanup(bool idleCleanupDone) { return !idleCleanupDone; }

// 有界重试决策：已执行 attemptsDone 次；仍有残留且未达上限才继续（无界重试被排除）。
static inline bool monitorIdleShouldRetryDelete(int attemptsDone, bool stillExists) {
    return stillExists && attemptsDone < kMonitorDeleteMaxAttempts;
}

// 闲置等待谓词（唤醒条件）：stop / top / fps 任一为真即返回（off→on 恢复在此唤醒）。
static inline bool monitorIdleWaitPredicate(bool stop, bool top, bool fps) {
    return stop || top || fps;
}

// 等待返回后是否退出循环。
static inline bool monitorIdleShouldExit(bool stop) { return stop; }

// 执行"进入闲置清理一次 + 有界重试"：注入删除动作、残留检查与退避，便于隔离验证
// （产品注入真实文件操作；harness 注入可失败的假操作）。返回实际执行轮数（≤ 上限）。
template <class DeleteOnce, class ResidualCheck, class SleepMs>
static inline int monitorIdleCleanupBounded(DeleteOnce deleteOnce, ResidualCheck residual, SleepMs sleepMs) {
    int attempts = 0;
    for (;;) {
        deleteOnce();
        ++attempts;
        if (!monitorIdleShouldRetryDelete(attempts, residual())) break;
        sleepMs(kMonitorDeleteRetryBackoffMs);
    }
    return attempts;
}
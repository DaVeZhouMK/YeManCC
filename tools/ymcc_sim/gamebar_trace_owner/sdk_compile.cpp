#define NOMINMAX
#include <atomic>
#include <initializer_list>
#include <string>
#include <utility>
struct GamebarSdkLogAtom {
    GamebarSdkLogAtom(const char*){} GamebarSdkLogAtom(bool){}
    GamebarSdkLogAtom(int){} GamebarSdkLogAtom(unsigned long long){}
};
static std::atomic<bool> g_exitRequested{false};
static void appendNativeLifecycleLog(const char*,std::initializer_list<std::pair<const char*,GamebarSdkLogAtom>>) {}
#include "gamebar_trace_runtime.h"
// Compile only: do not link/run this real-SDK binding probe.
bool gamebarTraceSdkConsumerCompileProbe() {
    gamebarTraceEnsureInit(); gamebarTraceEnsureInitIfDue(); gamebarTraceStartInitThread();
    (void)gamebarTraceRefresh("compile-only"); gamebarTraceRequestStop();
    return gamebarTraceShutdown(0)&&gamebarTraceStopped();
}

#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>

namespace ymcc::rtss {
inline constexpr wchar_t kWindowClass[] = L"YMCC.RTSS.OverlayBridge.v1";
inline constexpr ULONG_PTR kCopyDataTag = 0x594d5254;
inline constexpr std::uint32_t kVersion = 1;
enum class Command : std::uint32_t { Ping = 1, Load = 2, Status = 3 };
enum class Reply : std::uint32_t {
    Accepted = 1, Complete = 2, Invalid = 10, Busy = 11,
    EditorUnavailable = 12, TemplateMissing = 13, PersistFailed = 14,
    QueueFailed = 15, SdkException = 16, UnknownRequest = 17, ProfileUnavailable = 18
};
struct Packet {
    std::uint32_t version = kVersion;
    Command command = Command::Ping;
    std::uint32_t requestId = 0;
    char layout[64]{};
};
static_assert(sizeof(Packet) == 76); // identical x86 RTSS / x64 YMCC wire ABI
inline bool allowedLayout(const char* name) {
    if (!name || !std::memchr(name, 0, 64)) return false;
    return !std::strcmp(name, "YeManOBS-W-1.ovl") ||
           !std::strcmp(name, "YeManOBS-L-1.ovl") ||
           !std::strcmp(name, "YeManOBS-JJ-1.ovl") ||
           !std::strcmp(name, "Empty.ovl");
}
}

#pragma once
#include <string_view>

namespace ymcc::gameoverlay {
// Explorer can retain the system-wide FSE flag while its desktop or a folder is
// foreground. Only classify known shell windows, not other Explorer-hosted UI.
inline bool desktopShellTarget(std::wstring_view path, std::wstring_view windowClass) noexcept {
    const auto slash = path.find_last_of(L"\\/");
    if (slash != std::wstring_view::npos) path.remove_prefix(slash + 1);
    constexpr std::wstring_view explorer = L"explorer.exe";
    if (path.size() != explorer.size()) return false;
    for (size_t i = 0; i < path.size(); ++i) {
        const auto ch = path[i];
        if ((ch >= L'A' && ch <= L'Z' ? ch + (L'a' - L'A') : ch) != explorer[i]) return false;
    }
    return windowClass == L"CabinetWClass" || windowClass == L"ExploreWClass" ||
        windowClass == L"Progman" || windowClass == L"WorkerW" || windowClass == L"Shell_TrayWnd";
}

// An OS D3D-fullscreen observation is independent of the desktop window's
// current frame bounds. Both still require the exact captured foreground.
// This is a route candidate, never proof of actual exclusive pixel coverage.
struct RouteFacts {
    bool enabled=false,foregroundBound=false,fseActive=false;
    bool capturedFullscreen=false,notificationKnown=false,notificationD3dFullscreen=false;
    bool desktopShell=false;
};
inline bool overlayRoute(const RouteFacts& facts) noexcept {
    return facts.enabled&&facts.foregroundBound&&!facts.desktopShell&&(facts.fseActive||facts.capturedFullscreen||
        (facts.notificationKnown&&facts.notificationD3dFullscreen));
}
}

#pragma once
#include <string_view>

namespace ymcc::idle {
inline constexpr unsigned WindowScopeSchema = 1;
inline constexpr char ProcessCostScopeSchemaField[] = "processCostScopeSchema";
inline constexpr unsigned ProcessCostScopeSchema = 1;
inline constexpr char SteamDeckyCpuScope[] = "Whole identified OS process; Steam owner/UI candidates and other plugins may share work. Not exclusive wallpaper or Decky JS CPU. GPU engine utilization and video-decode attribution unavailable; candidate process type not guaranteed.";
inline constexpr char SteamDeckyPlaybackScope[] = "Authenticated context request and accepted queued frame; not video decode/presented-frame acknowledgement.";
struct DeckyRoleAttribution { const char* role; const char* text; };
inline constexpr DeckyRoleAttribution DeckySharedJsContext{
    "wallpaper-shared-js-context", "Binding-verified Steam context owner process; does not prove the JS/render execution process. Shared Steam work included; not exclusive CPU."};
inline constexpr DeckyRoleAttribution DeckySteamUiCandidate{
    "steam-ui-window-candidate", "Existing Steam UI WinEvent candidate; not proof of Big Picture or exclusive wallpaper painting."};
inline constexpr DeckyRoleAttribution DeckyLoaderRoot{
    "decky-loader-root", "Exact Runtime-owned Loader PID; the official PyInstaller parent can be idle while its child does work."};
inline constexpr DeckyRoleAttribution DeckyLoaderJobChild{
    "decky-loader-job-child", "Existing owned Loader Job member; exact image/session verified. No process-name discovery."};
inline constexpr DeckyRoleAttribution DeckyRoleAttributions[]{DeckySharedJsContext, DeckySteamUiCandidate, DeckyLoaderRoot, DeckyLoaderJobChild};
inline constexpr char DeckyRoleAttributionSchemaField[] = "roleAttributionSchema";
inline constexpr unsigned DeckyRoleAttributionSchema = 1;
inline bool matchingDeckyRoleAttribution(std::string_view role, std::string_view text) noexcept {
    for (const auto& entry : DeckyRoleAttributions)
        if (role == entry.role) return text == entry.text;
    return false;
}
inline constexpr char EnvironmentScope[] = "GetLastInputInfo is current-session evidence, not every HID/Steam activity; timeout comparison is not the OS display-idle timer or causal proof; powercfg requests supplies request owners";
inline constexpr char ReceiptScope[] = "YMCC producer and Host ACK, not encoded HID bytes or external Steam suppression";
inline constexpr char ObserverRole[] = "YMCC physical reader, exemption must be checked from HidHide receipts";
inline constexpr char ExternalPhysicalVisibility[] = "unknown; use independent collector plus Steam evidence";
}

#pragma once
#include <windows.h>
#include <d3dkmthk.h>
#include <array>
#include "json.hpp"

namespace ymcc::gpuschedule {
// One read on demand when no explicit HwSchMode preference exists. No writes,
// worker, polling, cache or new scheduling owner is created by this reader.
inline nlohmann::json readWindowsState() {
    struct Library { HMODULE handle = LoadLibraryExW(L"gdi32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        ~Library() { if (handle) FreeLibrary(handle); } } library;
    if (!library.handle) return {{"ok", false}, {"reason", "graphics-api-unavailable"}};
    const auto enumerate = reinterpret_cast<PFND3DKMT_ENUMADAPTERS2>(GetProcAddress(library.handle, "D3DKMTEnumAdapters2"));
    const auto query = reinterpret_cast<PFND3DKMT_QUERYADAPTERINFO>(GetProcAddress(library.handle, "D3DKMTQueryAdapterInfo"));
    const auto close = reinterpret_cast<PFND3DKMT_CLOSEADAPTER>(GetProcAddress(library.handle, "D3DKMTCloseAdapter"));
    if (!enumerate || !query || !close) return {{"ok", false}, {"reason", "graphics-api-unavailable"}};
    struct Adapters {
        std::array<D3DKMT_ADAPTERINFO, 64> values{};
        PFND3DKMT_CLOSEADAPTER close;
        ~Adapters() { for (const auto& value : values) if (value.hAdapter) {
            D3DKMT_CLOSEADAPTER args{}; args.hAdapter = value.hAdapter; close(&args);
        } }
    } adapters{{}, close};
    D3DKMT_ENUMADAPTERS2 args{};
    args.NumAdapters = static_cast<ULONG>(adapters.values.size()); args.pAdapters = adapters.values.data();
    const auto status = enumerate(&args);
    if (status < 0 || !args.NumAdapters || args.NumAdapters > adapters.values.size())
        return {{"ok", false}, {"reason", "adapter-enumeration-failed"}, {"status", status}};
    bool enabled = false; unsigned answered = 0, unknown = 0;
    for (ULONG index = 0; index < args.NumAdapters; ++index) {
        const auto handle = adapters.values[index].hAdapter;
        if (!handle) { ++unknown; continue; }
        D3DKMT_QUERYADAPTERINFO info{}; info.hAdapter = handle;
        D3DKMT_ADAPTERTYPE type{};
        info.Type = KMTQAITYPE_ADAPTERTYPE; info.pPrivateDriverData = &type; info.PrivateDriverDataSize = sizeof(type);
        if (query(&info) >= 0 && type.SoftwareDevice) continue;
        D3DKMT_WDDM_2_7_CAPS caps{};
        info.Type = KMTQAITYPE_WDDM_2_7_CAPS; info.pPrivateDriverData = &caps; info.PrivateDriverDataSize = sizeof(caps);
        if (query(&info) >= 0) { enabled = enabled || caps.HwSchEnabled; ++answered; continue; }
        // Older WDDM drivers cannot enable this feature. A failed modern probe
        // stays unknown instead of being reported as OFF.
        D3DKMT_DRIVERVERSION version{};
        info.Type = KMTQAITYPE_DRIVERVERSION; info.pPrivateDriverData = &version; info.PrivateDriverDataSize = sizeof(version);
        if (query(&info) >= 0 && version < KMT_DRIVERVERSION_WDDM_2_7) ++answered;
        else ++unknown;
    }
    if (enabled || (answered && !unknown)) return {{"ok", true}, {"enabled", enabled}, {"source", "windows-gpu-state"}};
    return {{"ok", false}, {"reason", "gpu-state-unavailable"}};
}
}

// Compile the actual native RTSS client with isolated process/file shims.
#include <windows.h>
#include <tlhelp32.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <atomic>
#include <string>
#include <vector>
#include <cwchar>
#include <cctype>
#include <iostream>
#include "json.hpp"
using json=nlohmann::json;
namespace fspath=std::filesystem;
static std::wstring exe_dir(){wchar_t p[MAX_PATH]{};GetModuleFileNameW(nullptr,p,MAX_PATH);*std::wcsrchr(p,L'\\')=0;return p;}
static bool file_exists(const std::wstring& p){return fspath::is_regular_file(p);}
static std::string W2U(const std::wstring& p){int n=WideCharToMultiByte(CP_UTF8,0,p.c_str(),-1,nullptr,0,nullptr,nullptr);std::string s(n,0);WideCharToMultiByte(CP_UTF8,0,p.c_str(),-1,s.data(),n,nullptr,nullptr);s.pop_back();return s;}
static std::string ascii_lower(std::string s){for(auto& c:s)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return s;}
// Test equality shim only; production main.cpp supplies the real SHA-256 function.
static std::string sha256File(const std::wstring& p){std::ifstream f(fspath::path(p),std::ios::binary);return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};}
static bool sameFinalPath(const std::wstring& a,const std::wstring& b){return !a.empty()&&!b.empty()&&_wcsicmp(fspath::weakly_canonical(a).c_str(),fspath::weakly_canonical(b).c_str())==0;}
static std::wstring processImagePath(DWORD pid){HANDLE h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!h)return {};wchar_t p[MAX_PATH]{};DWORD n=MAX_PATH;bool ok=QueryFullProcessImageNameW(h,0,p,&n)!=FALSE;CloseHandle(h);return ok?std::wstring(p,n):std::wstring{};}
static bool sgRunExeSync(const std::wstring& exe,const std::wstring& args,DWORD ms){
    auto command=L"\""+exe+L"\" "+args;std::vector<wchar_t>b(command.begin(),command.end());b.push_back(0);
    STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};
    if(!CreateProcessW(nullptr,b.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi))return false;
    const auto wait=WaitForSingleObject(pi.hProcess,ms);if(wait==WAIT_TIMEOUT){TerminateProcess(pi.hProcess,ERROR_TIMEOUT);WaitForSingleObject(pi.hProcess,1000);}
    DWORD code=STILL_ACTIVE;GetExitCodeProcess(pi.hProcess,&code);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
    return wait==WAIT_OBJECT_0&&code==0;
}
#include "../../native/rtss_client.h"
static void require(bool ok,const char* label){if(!ok)throw std::runtime_error(label);std::cout<<"PASS "<<label<<"\n";}
static void write(const fspath::path& p,const std::string& text){std::ofstream f(p,std::ios::binary);f<<text;}
int wmain(int argc,wchar_t**argv){
 try {
    // On-machine diagnostics use only window IPC/the actual client, never a
    // mock host, DLL injection, local hook loading, or automatic restarts.
    if(argc == 3 && !std::wcscmp(argv[1], L"--live-status")) {
        const std::wstring root = argv[2];
        RtssBridgeSearch search{root};
        EnumWindows(nativeRtssFindBridge, reinterpret_cast<LPARAM>(&search));
        auto status = search.window ? json{{"ok", true}, {"bridgeState", "loaded"}} : nativeRtssMissingBridge(root);
        status["bridgeLoaded"] = search.window != nullptr;
        status["rtssDirectory"] = W2U(root);
        if(search.window) {
            DWORD pid = 0;
            GetWindowThreadProcessId(search.window, &pid);
            status["rtssPid"] = pid;
            ymcc::rtss::Packet packet{};
            DWORD_PTR reply = 0;
            status["bridgeResponding"] = nativeRtssBridgeMessage(search.window, packet, reply) &&
                reply == static_cast<DWORD_PTR>(ymcc::rtss::Reply::Complete);
        }
        std::cout << status.dump(2) << "\n";
        return 0;
    }
    if(argc == 4 && !std::wcscmp(argv[1], L"--live-load")) {
        const auto result = nativeRtssLoadOverlay(argv[2], W2U(argv[3]));
        std::cout << result.dump(2) << "\n";
        return result.value("ok", false) ? 0 : 1;
    }
    if(argc!=3)return 2;
    const std::wstring root=argv[1],assets=argv[2];
    // Exercise the real "deployed after RTSS started" failure without touching
    // the installed RTSS or restarting any process. Disk configuration must
    // never count as a successful live load or persist the requested Layout.
    const auto unloaded = fspath::path(root).parent_path() / L"unloaded-probe";
    fspath::create_directories(unloaded / L"Plugins/Client");
    fspath::create_directories(unloaded / L"Profiles");
    const auto unloadedPlugin = unloaded / L"Plugins/Client/YMCCOverlayBridge.dll";
    const auto unloadedConfig = unloaded / L"Profiles/Config";
    const auto unloadedLayout = unloaded / L"Plugins/Client/OverlayEditor.cfg";
    // Previous probe output may remain between runs; normalize only fixtures.
    fspath::remove(unloadedPlugin);
    write(unloadedConfig, "[Plugins]\r\nYMCCOverlayBridge.dll=0\r\n");
    write(unloadedLayout, "[Settings]\r\nLayout=original.ovl\r\n");
    const auto beforeUnavailable = sha256File(unloadedLayout.wstring());
    auto missing = nativeRtssLoadOverlay(unloaded.wstring(), "YeManOBS-L-1.ovl");
    require(!missing["ok"] && missing["bridgeState"] == "not_installed" && !missing["bridgeInstalled"], "missing bridge file diagnosed separately");
    write(unloadedPlugin, "diagnostic fixture, never loaded");
    missing = nativeRtssLoadOverlay(unloaded.wstring(), "YeManOBS-L-1.ovl");
    require(!missing["ok"] && missing["bridgeState"] == "disabled" && !missing["bridgeEnabled"], "installed but disabled bridge diagnosed separately");
    WritePrivateProfileStringW(L"Plugins", L"YMCCOverlayBridge.dll", L"1", unloadedConfig.c_str());
    missing = nativeRtssLoadOverlay(unloaded.wstring(), "YeManOBS-L-1.ovl");
    require(!missing["ok"] && missing["bridgeState"] == "enabled_not_loaded" && missing["bridgeInstalled"] && missing["bridgeEnabled"], "enabled on disk is not loaded in running RTSS");
    require(missing["errorCode"] == "RTSS_BRIDGE_UNAVAILABLE", "structured bridge readiness error");
    require(sha256File(unloadedLayout.wstring()) == beforeUnavailable, "unavailable bridge does not persist requested layout");
    // A clean RTSS install has no YMCC plugin, overlays or even Profiles.
    // Validate the production preparation function on a private fixture only.
    const auto fresh = fspath::path(root).parent_path() /
        (L"fresh-install-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    fspath::create_directories(fresh / L"Plugins/Client");
    write(fresh / L"RTSS.exe", "installation marker, never launched");
    write(fresh / L"Plugins/Client/OverlayEditor.dll", "official plugin marker, never loaded");
    const auto retainedSource = exe_dir() + L"\\YMCCOverlayBridge.dll";
    const auto retainedHash = sha256File(retainedSource);
    const auto deployed = fresh / L"Plugins/Client/YMCCOverlayBridge.dll";
    require(!file_exists(deployed.wstring()) && !fspath::exists(fresh / L"Profiles"), "fresh install initially has no bridge or profiles");
    require(nativeRtssPrepareOverlay(fresh.wstring(), assets)["ok"], "first startup installs missing bridge and config");
    require(file_exists(deployed.wstring()) && sha256File(deployed.wstring()) == retainedHash, "installed bridge equals retained YMCC source");
    require(sha256File(retainedSource) == retainedHash, "automatic installation keeps source DLL beside YMCC");
    const auto freshConfig = fresh / L"Profiles/Config";
    require(GetPrivateProfileIntW(L"Plugins", L"YMCCOverlayBridge.dll", 0, freshConfig.c_str()) == 1 &&
        GetPrivateProfileIntW(L"Plugins", L"OverlayEditor.dll", 0, freshConfig.c_str()) == 1, "first startup enables both RTSS plugins");
    for(const auto* name : {L"YeManOBS-W-1.ovl", L"YeManOBS-L-1.ovl", L"YeManOBS-JJ-1.ovl", L"Empty.ovl"}) {
        require(sha256File((fresh / L"Plugins/Client/Overlays" / name).wstring()) ==
            sha256File((fspath::path(assets) / L"RTSS-Overlays" / name).wstring()), "first startup supplies complete shipped template");
    }
    const auto custom = fresh / L"Plugins/Client/Overlays/YeManOBS-W-1.ovl";
    write(custom, "[General]\r\nLayers=1\r\n[Layer0]\r\nText=user-customized\r\n");
    write(fresh / L"Plugins/Client/OverlayEditor.cfg", "[Settings]\r\nLayout=user-custom.ovl\r\nSMART=0\r\n");
    write(fresh / L"Profiles/Global", "[Framerate]\r\nLimit=144\r\n[OSD]\r\nZoomRatio=3\r\n");
    WritePrivateProfileStringW(L"Plugins", L"HotkeyHandler.dll", L"0", freshConfig.c_str());
    WritePrivateProfileStringW(L"Other", L"Keep", L"7", freshConfig.c_str());
    const auto customHash = sha256File(custom.wstring());
    const auto layoutHash = sha256File((fresh / L"Plugins/Client/OverlayEditor.cfg").wstring());
    const auto globalHash = sha256File((fresh / L"Profiles/Global").wstring());
    const auto dllTime = fspath::last_write_time(deployed);
    require(nativeRtssPrepareOverlay(fresh.wstring(), assets)["ok"], "repeat startup preparation is safe");
    require(fspath::last_write_time(deployed) == dllTime, "identical plugin is not replaced on repeat startup");
    require(sha256File(custom.wstring()) == customHash, "automatic installation preserves customized template");
    require(sha256File((fresh / L"Plugins/Client/OverlayEditor.cfg").wstring()) == layoutHash &&
        sha256File((fresh / L"Profiles/Global").wstring()) == globalHash, "startup preserves Layout FPS zoom and editor settings");
    require(GetPrivateProfileIntW(L"Plugins", L"HotkeyHandler.dll", -1, freshConfig.c_str()) == 0 &&
        GetPrivateProfileIntW(L"Other", L"Keep", 0, freshConfig.c_str()) == 7, "plugin enabling preserves unrelated configuration");
    require(fspath::remove(deployed), "simulate deleted plugin only in private fixture");
    require(nativeRtssPrepareOverlay(fresh.wstring(), assets)["ok"] && file_exists(deployed.wstring()), "subsequent YMCC startup repairs deleted bridge");
    require(nativeRtssPrepareOverlay(root,assets)["ok"],"prepare plugin and four missing templates");
    // Regression: file equality is NOT wire compatibility. Append a harmless
    // trailer to the retained source while the unmodified deployed DLL remains
    // loaded by our private mock RTSS. The old replacement path must fail, but
    // production preparation + live load must continue without changing it.
    const auto livePlugin = fspath::path(root) / L"Plugins/Client/YMCCOverlayBridge.dll";
    const auto liveConfig = fspath::path(root) / L"Profiles/Config";
    write(liveConfig, "[Plugins]\r\nYMCCOverlayBridge.dll=0\r\n[Other]\r\nKeep=9\r\n");
    const auto liveConfigHash = sha256File(liveConfig.wstring());
    const auto livePluginHash = sha256File(livePlugin.wstring());
    const auto livePluginTime = fspath::last_write_time(livePlugin);
    write(retainedSource, retainedHash + "different-build-trailer");
    require(!rtssCopyAssetAtomic(retainedSource, livePlugin.wstring()), "loaded different-build DLL rejects replacement in regression fixture");
    HANDLE configLock = CreateFileW(liveConfig.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    require(configLock != INVALID_HANDLE_VALUE, "live plugin enable configuration locked against writes");
    auto livePrepare = nativeRtssPrepareOverlay(root, assets);
    CloseHandle(configLock);
    require(livePrepare["ok"] && livePrepare["bridgeState"] == "loaded_compatible" && livePrepare["bridgeMaintenanceDeferred"], "compatible loaded bridge bypasses binary equality and update gate");
    require(sha256File(livePlugin.wstring()) == livePluginHash && fspath::last_write_time(livePlugin) == livePluginTime, "live preparation never replaces or touches loaded plugin");
    require(sha256File(liveConfig.wstring()) == liveConfigHash, "live preparation does not rewrite plugin enable flags");
    require(nativeRtssLoadOverlay(root, "YeManOBS-L-1.ovl")["ok"], "different-build source does not prevent a complete live layout switch");
    const auto sourceBackup = retainedSource + L".probe-backup";
    fspath::rename(retainedSource, sourceBackup);
    livePrepare = nativeRtssPrepareOverlay(root, assets);
    const auto noSourceInstall = nativeRtssPrepareOverlay(fresh.wstring(), assets);
    fspath::rename(sourceBackup, retainedSource);
    write(retainedSource, retainedHash);
    require(livePrepare["ok"], "missing maintenance source does not disable a compatible loaded bridge");
    require(!noSourceInstall["ok"] && noSourceInstall["errorCode"] == "RTSS_BRIDGE_SOURCE_MISSING", "missing source still fails real installation");
    const auto incompatibleMarker = fspath::path(root) / L"mock-incompatible-bridge";
    write(incompatibleMarker, "1");
    livePrepare = nativeRtssPrepareOverlay(root, assets);
    fspath::remove(incompatibleMarker);
    require(!livePrepare["ok"] && livePrepare["errorCode"] == "RTSS_BRIDGE_INCOMPATIBLE", "incompatible bridge is rejected without unsafe DLL replacement");
    const auto slowBridgeMarker = fspath::path(root) / L"mock-slow-bridge";
    write(slowBridgeMarker, "1");
    const auto pingStart = GetTickCount64();
    livePrepare = nativeRtssPrepareOverlay(root, assets);
    const auto pingElapsed = GetTickCount64() - pingStart;
    fspath::remove(slowBridgeMarker);
    require(!livePrepare["ok"] && livePrepare["errorCode"] == "RTSS_BRIDGE_UNRESPONSIVE" && pingElapsed < 1500, "unresponsive handshake returns promptly instead of blocking switch");
    Sleep(1000);
    require(nativeRtssPrepareOverlay(root, assets)["ok"] && nativeRtssLoadOverlay(root, "Empty.ovl")["ok"], "switch recovers after handshake timeout without restarting RTSS");
    write(deployed, "old offline bridge contents");
    require(nativeRtssPrepareOverlay(fresh.wstring(), assets)["ok"] && sha256File(deployed.wstring()) == retainedHash, "offline preparation updates a different bridge from retained source");
    HANDLE lockedPlugin = CreateFileW(deployed.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    require(lockedPlugin != INVALID_HANDLE_VALUE, "private offline DLL lock established");
    write(retainedSource, retainedHash + "locked-install-trailer");
    const auto lockedInstall = nativeRtssPrepareOverlay(fresh.wstring(), assets);
    CloseHandle(lockedPlugin);
    write(retainedSource, retainedHash);
    require(!lockedInstall["ok"] && lockedInstall["errorCode"] == "RTSS_BRIDGE_INSTALL_FAILED" && lockedInstall.contains("path") && lockedInstall.value("win32Error", 0) != 0, "unavailable bridge keeps actual installation error code and target path");
    require(!nativeRtssLoadOverlay(root,"../../evil.ovl")["ok"],"reject arbitrary paths");
    for(int i=0;i<25;++i)for(const auto*layout:{"YeManOBS-W-1.ovl","YeManOBS-L-1.ovl","YeManOBS-JJ-1.ovl","Empty.ovl"}){
        const auto r=nativeRtssLoadOverlay(root,layout);require(r["ok"],layout);
        const auto state=root+L"\\Plugins\\Client\\mock-state.ini";
        wchar_t current[64]{};GetPrivateProfileStringW(L"Mock",L"Layout",L"",current,64,state.c_str());
        require(W2U(current)==layout,"ACK after mocked official queued rebuild");
        const auto templatePath = root + L"\\Plugins\\Client\\Overlays\\" + current;
        const int expected = GetPrivateProfileIntW(L"General", L"Layers", -1, templatePath.c_str());
        require(GetPrivateProfileIntW(L"Mock", L"Layers", -2, state.c_str()) == expected, "all new layers rebuilt (no stale extent fixture)");
    }
    const auto global=root+L"\\Profiles\\Global";
    require(GetPrivateProfileIntW(L"OSD",L"EnableBgnd",-1,global.c_str())==1,"restore original shadow on off");
    require(GetPrivateProfileIntW(L"Framerate",L"Limit",-1,global.c_str())==120,"preserve FPS");
    require(GetPrivateProfileIntW(L"OSD",L"ZoomRatio",-1,global.c_str())==2,"preserve zoom");
    write(fspath::path(root)/L"mock-slow-editor","1");
    require(nativeRtssLoadOverlay(root,"YeManOBS-W-1.ovl")["ok"],"slow official rebuild waits for completion, no repeated Load");
    fspath::remove(fspath::path(root)/L"mock-slow-editor");
    const auto stalled = fspath::path(root) / L"mock-stall-editor";
    const auto mockState = (fspath::path(root) / L"Plugins/Client/mock-state.ini").wstring();
    const int updatesBeforeStall = GetPrivateProfileIntW(L"Mock", L"Updates", -1, mockState.c_str());
    write(stalled, "1");
    const auto loadStart = GetTickCount64();
    const auto timedOutLoad = nativeRtssLoadOverlay(root, "YeManOBS-W-1.ovl");
    const auto loadElapsed = GetTickCount64() - loadStart;
    fspath::remove(stalled);
    require(!timedOutLoad["ok"] && loadElapsed < 5000, "stalled layout rebuild returns bounded timeout instead of hanging");
    Sleep(1200);
    require(GetPrivateProfileIntW(L"Mock", L"Updates", -1, mockState.c_str()) == updatesBeforeStall + 1, "timed out load is never automatically resent");
    require(nativeRtssPrepareOverlay(root, assets)["ok"] && nativeRtssLoadOverlay(root, "Empty.ovl")["ok"], "later switch recovers after load timeout without restarting host");
    write(fspath::path(root)/L"mock-fault-editor","1");
    require(!nativeRtssLoadOverlay(root,"YeManOBS-W-1.ovl")["ok"],"editor export exception isolated/rejected");
    fspath::remove(fspath::path(root)/L"mock-fault-editor");
    require(nativeRtssLoadOverlay(root,"YeManOBS-JJ-1.ovl")["ok"],"recover after rejected official export");
    require(GetPrivateProfileIntW(L"OSD",L"EnableBgnd",-1,global.c_str())==0,"custom overlay does not inherit black Global shadow");
    const auto path=fspath::path(root)/L"Plugins/Client/Overlays/YeManOBS-L-1.ovl";
    const auto old=sha256File(path.wstring());write(path,"[General]\nLayers=2\n[Layer0]\nText=broken\n");
    require(!nativeRtssLoadOverlay(root,"YeManOBS-L-1.ovl")["ok"],"reject incomplete template before load");
    write(path,old);
    require(nativeRtssLoadOverlay(root,"Empty.ovl")["ok"],"off after damaged template rejection");
    require(nativeRtssReloadProfiles(root+L"\\RTSSHooks64.dll")["ok"],"isolated profile helper ABI");
    write(fspath::path(root)/L"mock-fault-sdk","1");
    require(!nativeRtssReloadProfiles(root+L"\\RTSSHooks64.dll")["ok"],"faulted helper does not crash parent");
    fspath::remove(fspath::path(root)/L"mock-fault-sdk");
    require(nativeRtssReloadProfiles(root+L"\\RTSSHooks64.dll")["ok"],"recover after helper exception");
    RtssBridgeSearch search{root};EnumWindows(nativeRtssFindBridge,reinterpret_cast<LPARAM>(&search));
    DWORD pid=0;GetWindowThreadProcessId(search.window,&pid);
    HANDLE host=OpenProcess(SYNCHRONIZE,FALSE,pid);
    require(host&&WaitForSingleObject(host,0)==WAIT_TIMEOUT,"same RTSS host remains alive after 100 switches");
    if(host)CloseHandle(host);
    std::cout<<"RTSS_CROSS_PROCESS_SELFTEST_OK\n";
    return 0;
 }catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}
}

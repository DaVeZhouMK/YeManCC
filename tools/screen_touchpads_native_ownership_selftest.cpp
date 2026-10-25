#define YMCC_SCREEN_NATIVE_INPUT_CANDIDATE 1
#include "screen_touchpads_native_topology_test_host.h"
int wmain(int argc,wchar_t** argv) {
    if(argc==3&&std::wstring(argv[1])==L"--target")return targetMain(argv[2]);if(argc!=2)return 2;const HWND foreground=GetForegroundWindow();OwnTarget target;bool ok=true;
    Json result={{"ok",false},{"systemInputInjected",false},{"configurationWrites",0},{"virtualTargetStarted",false},{"productDeployed",false},{"cases",Json::array()}};
    auto check=[&](const char* name,bool passed){ok=ok&&passed;result["cases"].push_back({{"name",name},{"ok",passed}});};
    try {if(!target.start())throw std::runtime_error("owned-target-unavailable");winrt::init_apartment(winrt::apartment_type::single_threaded);
        ymcc::screeninput::Runtime runtime;ymcc::screeninput::Surface rejected;const auto foreignStyle=GetWindowLongPtrW(target.window,GWL_EXSTYLE);
        check("reject-other-process-HWND",!rejected.attach(runtime,target.window,0,nullptr,nullptr));check("rejected-foreign-HWND-style-unchanged",GetWindowLongPtrW(target.window,GWL_EXSTYLE)==foreignStyle);
        bool mtaRejected=false;std::thread mta([&]{winrt::init_apartment(winrt::apartment_type::multi_threaded);ymcc::screeninput::Runtime wrong;mtaRejected=!wrong.ensure()&&!wrong.compositor;winrt::uninit_apartment();});mta.join();check("reject-MTA-input-runtime",mtaRejected);
        dryRun=true;setContext("disabled",false);Config cfg=profileDefault(0);cfg.enabled=true;cfg.layout=Layout::Single;cfg.single=Mode::Mouse;initialize(GetModuleHandleW(nullptr),nullptr,cfg);refresh();
        const HWND root=windows[0];const auto rootStyle=GetWindowLongPtrW(root,GWL_EXSTYLE);setNativeInputCandidate(true);check("attach-own-UI-HWND",nativeInputSync(0,true,false));const HWND child=nativeInputSurfaces[0].child();
        check("only-own-child-is-reparented",GetParent(child)==root&&GetWindowThreadProcessId(child,nullptr)==GetCurrentThreadId());
        bool wrongSync=false,wrongCloseSafe=false;std::thread wrongThread([&]{wrongSync=!nativeInputSurfaces[0].sync()&&nativeInputSurfaces[0].error==RPC_E_WRONG_THREAD;nativeInputSurfaces[0].close();wrongCloseSafe=nativeInputSurfaces[0].root()==root;});wrongThread.join();
        check("reject-wrong-thread-sync",wrongSync);check("wrong-thread-close-does-not-destroy-owned-HWND",wrongCloseSafe&&IsWindow(child));check("UI-thread-can-resync-after-rejection",nativeInputSync(0,true,false));
        setNativeInputCandidate(false);check("disable-restores-original-window-style",GetWindowLongPtrW(root,GWL_EXSTYLE)==rootStyle);check("disable-destroys-composition-child",!IsWindow(child));
        check("reattach-after-disable",(setNativeInputCandidate(true),nativeInputSync(0,true,false)));shutdown();setNativeInputCandidate(false);
        check("shutdown-destroys-owned-surface",!windows[0]&&!nativeInputSurfaces[0].root());
    }catch(const std::exception& e){ok=false;result["failure"]=e.what();shutdown();}catch(const winrt::hresult_error& e){ok=false;result["failure"]=(long)e.code();shutdown();}
    result["ownedTargetTerminal"]=target.stop();ok=ok&&result["ownedTargetTerminal"].get<bool>();result["ok"]=ok;if(foreground&&IsWindow(foreground))SetForegroundWindow(foreground);
    std::ofstream file(std::filesystem::path(argv[1]),std::ios::binary);file<<result.dump(2);std::cout<<"SURFACE_OWNERSHIP passed="<<ok<<" checks="<<result["cases"].size()<<"\n";return ok&&file.good()?0:2;
}
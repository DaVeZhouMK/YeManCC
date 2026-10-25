#define NOMINMAX
#include "mouse_backend_process.h"
#include "process_efficiency.h"
#include "mouse_mode_com_apartment.h"
#include <iostream>
#include <stdexcept>
#include <filesystem>
static int cases = 0;
static void need(bool value, const char* label) { if (!value) throw std::runtime_error(label); ++cases; }
int wmain(int argc, wchar_t** argv) {
    if (argc != 4) return 3;
    const std::wstring owned=argv[1], foreign=argv[2], exits=argv[3];
    struct Cleanup {
        std::wstring a,b;
        ~Cleanup(){ymcc::mouseProcess::set(a,false);ymcc::mouseProcess::set(b,false);}
    } cleanup{owned,foreign};
    try {
        APTTYPE apartmentType; APTTYPEQUALIFIER qualifier;
        need(CoGetApartmentType(&apartmentType,&qualifier)==CO_E_NOTINITIALIZED,"fixture-starts-without-com");
        {ymcc::mouseMode::ComApartment first;need(first.ready()&&SUCCEEDED(first.status),"worker-com-initialized");
         {ymcc::mouseMode::ComApartment second;need(second.ready()&&second.status==S_FALSE,"existing-mta-reference-balanced");}
         need(SUCCEEDED(CoGetApartmentType(&apartmentType,&qualifier))&&apartmentType==APTTYPE_MTA,"outer-mta-survives-inner-guard");}
        need(CoGetApartmentType(&apartmentType,&qualifier)==CO_E_NOTINITIALIZED,"worker-com-reference-released");
        need(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"fixture-sta-created");
        {ymcc::mouseMode::ComApartment existing;need(existing.ready()&&existing.status==RPC_E_CHANGED_MODE,"existing-sta-is-usable");}
        need(SUCCEEDED(CoGetApartmentType(&apartmentType,&qualifier))&&(apartmentType==APTTYPE_STA||apartmentType==APTTYPE_MAINSTA),"existing-sta-not-uninitialized");
        CoUninitialize();
        need(CoGetApartmentType(&apartmentType,&qualifier)==CO_E_NOTINITIALIZED,"fixture-sta-reference-released");
        using namespace ymcc::mouseProcess;
        need(observe(owned).known && !observe(owned).running,"initially-stopped");
        auto value=set(owned,true,&ymcc::processEfficiency::createProcess); need(value.ok && value.running && value.known,"direct-start-live");
        auto first=inspect(owned);need(first.processes.size()==1,"single-instance-started");
        const DWORD pid=GetProcessId(first.processes[0].value);
        value=set(owned,true); auto again=inspect(owned);
        need(value.ok && again.processes.size()==1 && GetProcessId(again.processes[0].value)==pid,"explicit-on-is-idempotent");
        need(set(foreign,true).ok,"foreign-same-basename-started");
        need(inspect(owned).processes.size()==1 && inspect(foreign).processes.size()==1,"exact-path-isolation");
        value=set(owned,false);need(value.ok && !value.running,"confirmed-stop");
        need(observe(foreign).running,"foreign-same-name-survives-stop");
        need(set(owned,false).ok,"explicit-off-is-idempotent");
        value=set(exits,true);need(!value.ok && !value.running,"immediate-exit-is-not-success");
        value=set(owned+L".missing",true);need(!value.ok && !value.running,"missing-executable-is-not-success");
        need(set(owned,true).ok,"restart-after-stop");
        need(set(owned,false).ok && set(foreign,false).ok,"isolated-children-cleaned");
        std::cout << "{\"status\":\"PASS\",\"cases\":" << cases << ",\"boundary\":\"isolated-owned-win32-processes\"}";
        return 0;
    } catch(const std::exception& error) {
        std::cout << "{\"status\":\"FAIL\",\"error\":\"" << error.what() << "\"}";
        return 1;
    }
}

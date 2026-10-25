#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#include "steam_ui_mode.h"
#include <iostream>
int wmain(int argc, wchar_t** argv) {
    using namespace ymcc; using Json=nlohmann::json;
    steamsession::Observer observer;
    if(argc==2 && std::wstring(argv[1])==L"--live-read-only") {
        if(!observer.start([](bool){},[]{return false;}))return 2;
        const auto end=GetTickCount64()+3000;
        while(observer.steamPresence()<0&&GetTickCount64()<end)Sleep(10);
        const auto state=steamui::read(observer); observer.stop();
        std::cout<<state.dump(2)<<std::endl;return state.value("known",false)?0:3;
    }
    int passed=0; auto expect=[&](bool ok,const char* label){if(!ok)throw std::runtime_error(label);++passed;};
    expect(std::wstring(steamui::uriFor("desktop"))==L"steam://close/bigpicture","desktop URI is not quit");
    expect(std::wstring(steamui::uriFor("bigpicture"))==L"steam://open/bigpicture","bigpicture URI");
    try {steamui::uriFor("shutdown");throw std::runtime_error("unchecked URI");}catch(const std::runtime_error& e){expect(std::string(e.what())=="Invalid Steam UI mode","arbitrary URI rejected");}
    const auto unknown=steamui::read(observer);
    expect(!unknown.value("known",true),"unstarted observer unknown");
    expect(unknown.value("mode",std::string{})=="unknown","unknown never desktop");
    Json valid={{"mode","desktop"},{"pid",uint32_t(123)},{"birth","456"},{"userInitiated",true}};
    expect(!steamui::set(observer,valid).value("ok",true),"absent session never launches");
    for(const auto& mode:{"desktop","bigpicture"}) for(const auto& flag:std::vector<Json>{nullptr,false,"true",1,Json::object()}) {
        auto request=valid;request["mode"]=mode;request["userInitiated"]=flag;
        bool rejected=false;try{steamui::set(observer,request);}catch(const std::runtime_error& error){rejected=std::string(error.what()).find("explicit user action")!=std::string::npos;}
        expect(rejected,"missing/malformed manual intent blocks both native mode directions before process lookup");
    }
    auto legacy=valid;legacy.erase("userInitiated");bool legacyRejected=false;try{steamui::set(observer,legacy);}catch(const std::runtime_error&){legacyRejected=true;}
    expect(legacyRejected,"legacy unmarked mode request fails closed without a fallback");
    for(const auto& malformed:std::vector<Json>{Json::object(),Json{{"mode","shutdown"},{"pid",uint32_t(123)},{"birth","456"}},Json{{"mode","desktop"},{"pid",-1},{"birth","456"}},Json{{"mode","desktop"},{"pid","123"},{"birth","456"}},Json{{"mode","desktop"},{"pid",uint32_t(123)},{"birth",456}},Json{{"mode","desktop"},{"pid",uint32_t(123)},{"birth","456"},{"uri","steam://quit"}}}) {
        bool rejected=false;try {steamui::set(observer,malformed);}catch(const std::runtime_error&){rejected=true;}expect(rejected,"malformed switch rejected");
    }
    std::cout<<Json{{"ok",true},{"passed",passed},{"realSteamOperations",0}}.dump()<<std::endl;
}

#include <initguid.h>
#include "../native/fxsound_recovery.h"
#include "../native/fxsound_control.h"
#include <iostream>
#include <stdexcept>
#include <functional>
#include <iomanip>
using namespace ymcc::fxsound;
static int checks = 0;
static void check(bool ok, const char* message) { ++checks; if (!ok) throw std::runtime_error(message); }
static Snapshot sample() {
    Snapshot s; s.valid = true; s.process = ProcessState::stopped;
    s.devices = {{L"fx",L"FxSound Speakers (FxSound Audio Enhancer)",true,false,1},
                 {L"speakers",L"Speakers",false,true,1}, {L"phones",L"Headphones",false,true,3},
                 {L"virtual",L"Virtual Cable",false,false,1}};
    s.defaults = {L"fx",L"fx",L"phones"}; s.preferred = {L"speakers"}; return s;
}
struct MemoryPort {
    Snapshot state = sample();
    std::vector<unsigned> writes;
    std::function<void(MemoryPort&)> beforeRead;
    int reads = 0; bool fail = false;
    Snapshot read() { ++reads; if (beforeRead) beforeRead(*this); return state; }
    SetResult setDefault(unsigned role, const std::wstring&, const std::wstring& target) {
        writes.push_back(role); if (fail) return {false,5}; state.defaults[role] = target; return {true,0};
    }
};
static std::vector<Attempt> confirm(Recovery& r, MemoryPort& p, std::uint64_t start = 0) {
    std::vector<Attempt> result;
    for (std::uint64_t t = start; t <= start + 3000; t += 500) {
        auto batch = r.step(p,t,true); result.insert(result.end(),batch.begin(),batch.end());
    }
    return result;
}
static void tests() {
    {
        Recovery r; MemoryPort p;
        for (unsigned t = 0; t <= 2500; t += 500) check(r.step(p,t,true).empty(),"must wait 3 seconds");
        check(r.step(p,2999,true).empty(),"2999ms is too early");
        const auto result = r.step(p,3000,true);
        check(result.size()==2 && result[0].ok && result[1].ok,"repair console and multimedia once");
        check(p.state.defaults[0]==L"speakers" && p.state.defaults[1]==L"speakers","restore recorded system default");
        check(p.state.defaults[2]==L"phones" && p.writes.size()==2,"preserve real communications default");
        check(r.finished,"successful startup ends detection");
        const auto reads=p.reads;p.state.defaults={L"fx",L"fx",L"fx"};
        for(unsigned t=3500;t<=20000;t+=500)check(r.step(p,t,true).empty(),"later FxSound use must never trigger recovery");
        check(p.reads==reads && p.writes.size()==2,"no reads or writes after startup");
    }
    for (auto state : {ProcessState::running, ProcessState::unknown}) {
        Recovery r; MemoryPort p; p.state.process=state;
        check(confirm(r,p).empty() && p.writes.empty() && r.finished,"running/unknown at launch ends detection");
        const auto reads=p.reads;p.state.process=ProcessState::stopped;
        check(confirm(r,p,6000).empty()&&p.reads==reads,"later FxSound exit never rearms detection");
    }
    {
        Recovery r; MemoryPort p;p.state.defaults={L"speakers",L"speakers",L"phones"};
        check(confirm(r,p).empty()&&r.finished,"normal output at launch ends detection");
        const auto reads=p.reads;p.state.defaults={L"fx",L"fx",L"phones"};
        check(confirm(r,p,5000).empty()&&p.reads==reads,"changing to FxSound later never triggers repair");
    }
    {
        Recovery r;MemoryPort p;r.step(p,0,true);r.step(p,500,true);
        p.state.process=ProcessState::running;check(r.step(p,1000,true).empty()&&r.finished,"launch during countdown cancels");
        const auto reads=p.reads;p.state.process=ProcessState::stopped;
        check(confirm(r,p,1500).empty()&&p.reads==reads&&p.writes.empty(),"cancelled startup never rearms");
    }
    {
        Recovery r;MemoryPort p;r.step(p,0,true);r.step(p,500,true);r.step(p,1000,false);
        const auto reads=p.reads;
        check(confirm(r,p,1500).empty()&&p.reads==reads&&p.writes.empty(),"OFF then ON cannot rearm this launch");
    }
    {
        Recovery r;MemoryPort p;r.step(p,0,false);
        check(confirm(r,p,500).empty()&&p.reads==0,"enabling after disabled startup waits for next launch");
    }
    {
        Recovery r;MemoryPort p;r.step(p,0,true);r.step(p,500,true);
        check(r.step(p,60000,true).empty()&&r.finished,"sleep gap cancels startup instead of repairing on resume");
        const auto reads=p.reads;check(confirm(r,p,60500).empty()&&p.reads==reads,"resume never rearms startup");
    }
    {
        Recovery r;MemoryPort p;p.beforeRead=[](MemoryPort& m){if(m.reads==8)m.state.process=ProcessState::running;};
        check(confirm(r,p).empty()&&p.writes.empty()&&r.finished,"FxSound starts at commit boundary");
    }
    {
        Recovery r;MemoryPort p;p.beforeRead=[](MemoryPort& m){if(m.reads==8)m.state.defaults={L"phones",L"phones",L"phones"};};
        check(confirm(r,p).empty()&&p.writes.empty(),"user changes output at commit boundary");
    }
    {
        Recovery r;MemoryPort p;r.step(p,0,true);p.state.defaults={L"phones",L"phones",L"phones"};
        check(r.step(p,500,true).empty()&&r.finished,"user changes output during countdown");
        const auto reads=p.reads;p.state.defaults={L"fx",L"fx",L"phones"};
        check(confirm(r,p,1000).empty()&&p.reads==reads,"output changes never restart confirmation");
    }
    {
        Recovery r;MemoryPort p;p.state.preferred={L"missing",L"virtual",L"fx"};
        const auto result=confirm(r,p);
        check(result.size()==2&&result[0].target==L"phones","ignore missing/virtual records and prefer available headphones");
    }
    {
        Recovery r;MemoryPort p;p.state.devices.resize(1);
        const auto result=confirm(r,p);
        check(result.size()==2&&!result[0].ok&&result[0].target.empty()&&p.writes.empty(),"no physical output never writes");
        const auto reads=p.reads;check(confirm(r,p,5000).empty()&&p.reads==reads,"no-target failure never retries");
    }
    {
        Recovery r;MemoryPort p;p.state.valid=false;
        check(confirm(r,p).empty()&&p.writes.empty()&&r.finished,"enumeration failure cancels startup");
    }
    {
        Recovery r;MemoryPort p;p.fail=true;const auto result=confirm(r,p);
        check(result.size()==2&&!result[0].ok&&p.writes.size()==2,"failed writes have explicit receipts");
        const auto reads=p.reads;p.fail=false;
        check(confirm(r,p,100000).empty()&&p.reads==reads&&p.writes.size()==2,"write failure never retries later");
    }
    {
        Recovery r;MemoryPort p;p.state.defaults={L"speakers",L"speakers",L"fx"};const auto result=confirm(r,p);
        check(result.size()==1&&result[0].role==2,"only orphaned communications default is repaired");
    }
    {
        Recovery r;MemoryPort p;r.step(p,0,true);p.state.devices.push_back({L"fx2",L"FxSound",true,false,1});
        p.state.defaults={L"fx2",L"fx2",L"phones"};
        check(r.step(p,500,true).empty()&&r.finished,"different FxSound endpoint cancels startup");
    }
    {
        Recovery r;MemoryPort p;r.step(p,1000,true);
        check(r.step(p,500,true).empty()&&r.finished,"clock rollback cancels startup");
    }
    {
        Recovery r;MemoryPort p;p.state.defaults={L"fx",L"fx",L"fx"};
        const auto result=confirm(r,p);check(result.size()==3&&p.writes.size()==3,"all three orphaned roles repair in one startup transaction");
    }
}
static void silentControlTests() {
    const std::string before="<?xml version=\"1.0\"?><PROPERTIES>\n<VALUE name=\"preset\" val=\"General\"/>\n<VALUE name=\"power\" val=\"0\"/>\n<VALUE name=\"run_minimized\" val=\"0\"/>\n<VALUE name=\"version\" val=\"1.0.0.0\"/>\n<VALUE name=\"output_device_id\" val=\"player-speaker\"/>\n<VALUE name=\"balance\" val=\"-7.25\"/>\n<VALUE name=\"future_setting\" val=\"opaque&amp;content\"/>\n</PROPERTIES>";
    std::string after;std::wstring reason;
    check(rewriteConfig(before,"1.2.6.0",after,reason),"silent config rewrites valid XML");
    for(const auto* literal : {"<VALUE name=\"output_device_id\" val=\"player-speaker\"/>","<VALUE name=\"balance\" val=\"-7.25\"/>","<VALUE name=\"future_setting\" val=\"opaque&amp;content\"/>"})check(after.find(literal)!=std::string::npos,"unrelated configuration remains byte-identical");
    check(after.find("name=\"power\" val=\"1\"")!=std::string::npos,"effects enabled only through config");
    check(after.find("name=\"run_minimized\" val=\"1\"")!=std::string::npos,"hidden startup requested");
    check(after.find("name=\"hide_notifications\" val=\"1\"")!=std::string::npos,"FxSound notifications suppressed");
    check(after.find("name=\"version\" val=\"1.2.6.0\"")!=std::string::npos,"version-change foreground path suppressed");
    std::string again;check(rewriteConfig(after,"1.2.6.0",again,reason)&&again==after,"repeated ON config is idempotent");
    check(!rewriteConfig("<PROPERTIES><VALUE", "1.2.6.0",again,reason),"damaged config is not reset");
    check(!rewriteConfig("<!DOCTYPE PROPERTIES [<!ENTITY x SYSTEM 'file:///bad'>]><PROPERTIES>&x;</PROPERTIES>","1.2.6.0",again,reason),"external entities are rejected");
    check(rewriteConfig("","1.2.6.0",again,reason)&&again.find("survey_displayed")!=std::string::npos&&again.find("output_device_id")==std::string::npos,"fresh seed has no source-machine speaker ID");
    auto folder=std::filesystem::temp_directory_path()/(L"ymcc-fxsound-unit-"+std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(folder);
    const auto backup=folder/L"backup.fac",target=folder/L"user"/presetFile;
    {std::ofstream out(backup,std::ios::binary);out<<"opaque preset bytes";}
    bool copied=false;check(supplementPreset(target,{folder/L"missing.fac",backup},copied,reason)&&copied,"missing preset uses available backup");
    {std::ofstream out(target,std::ios::binary);out<<"player edited opaque bytes";}
    copied=false;check(supplementPreset(target,{backup},copied,reason)&&!copied,"existing edited preset is never replaced");
    {std::ifstream in(target,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(in)),{});check(bytes=="player edited opaque bytes","existing preset bytes remain unchanged");}
    std::filesystem::remove(target);copied=false;check(supplementPreset(target,{backup},copied,reason)&&copied,"deletion is the only reset trigger");
    const auto config=folder/L"FxSound.settings";
    std::string fileBytes;bool exists=false;
    check(readConfigFile(config,fileBytes,exists,reason)&&!exists,"only missing config is reported absent");
    check(prepareConfigAt(config,"1.2.6.0",[]{return true;},reason),"missing settings are actually seeded atomically");
    check(readConfigFile(config,fileBytes,exists,reason)&&exists&&fileBytes.find("output_device_id")==std::string::npos,"new settings omit source-machine hardware IDs");
    {std::ofstream out(config,std::ios::binary);out<<before;}
    HANDLE locked=CreateFileW(config.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    check(locked!=INVALID_HANDLE_VALUE,"exclusive config lock acquired");
    check(!readConfigFile(config,fileBytes,exists,reason),"unreadable config is never treated as absent");
    check(!prepareConfigAt(config,"1.2.6.0",[]{return true;},reason),"locked settings are not replaced or seeded");
    CloseHandle(locked);
    check(readConfigFile(config,fileBytes,exists,reason)&&fileBytes==before,"locked settings retain every original byte");
    {std::ofstream out(config,std::ios::binary|std::ios::trunc);}
    check(!prepareConfigAt(config,"1.2.6.0",[]{return true;},reason),"existing empty settings are not silently reset");
    check(readConfigFile(config,fileBytes,exists,reason)&&exists&&fileBytes.empty(),"empty existing settings remain empty");
    {std::ofstream out(config,std::ios::binary);out<<before;}
    int checkpoint=0;
    check(!prepareConfigAt(config,"1.2.6.0",[&]{if(++checkpoint==2){std::ofstream out(config,std::ios::binary);out<<"external update";}return true;},reason),"concurrent config replacement cancels commit");
    check(readConfigFile(config,fileBytes,exists,reason)&&fileBytes=="external update","concurrent writer's bytes survive");
    std::filesystem::remove(config);checkpoint=0;
    check(!prepareConfigAt(config,"1.2.6.0",[&]{if(++checkpoint==2){std::ofstream out(config,std::ios::binary);}return true;},reason),"new empty file created by another owner cancels missing-file seed");
    check(readConfigFile(config,fileBytes,exists,reason)&&exists&&fileBytes.empty(),"concurrent empty file is not replaced");
    std::filesystem::remove(config);
    check(!prepareConfigAt(config,"1.2.6.0",[]{return false;},reason),"external process startup cancels settings write");
    check(!std::filesystem::exists(config),"process startup refusal creates no configuration");
    auto temp=config;temp+=L".ymcc-"+std::to_wstring(GetCurrentProcessId())+L".tmp";
    check(!std::filesystem::exists(temp),"all refused commits release their temporary file");
    // Delete only the three explicitly created test files, never a recursive root.
    std::filesystem::remove(target);std::filesystem::remove(backup);std::filesystem::remove(target.parent_path());std::filesystem::remove(folder);
}
static std::string utf8(const std::wstring& value) {
    if(value.empty())return {};
    const int size=WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    std::string result(size,'\0');WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),result.data(),size,nullptr,nullptr);return result;
}
int main(int argc,char** argv) {
    try {
        if(argc>1 && std::string(argv[1])=="--inventory") {
            if(FAILED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)))return 2;
            {
                ComPtr<IMMDeviceEnumerator> e;
                if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&e))))return 3;
                std::atomic<bool> disabled{false};WindowsPort port(e.Get(),disabled);const auto s=port.read();
                std::cout<<"READ_ONLY valid="<<s.valid<<" process="<<static_cast<int>(s.process)<<"\n";
                for(unsigned r=0;r<3;++r)std::cout<<"role="<<r<<" endpoint="<<utf8(s.defaults[r])<<"\n";
                for(const auto& d:s.devices)std::cout<<"device="<<utf8(d.id)<<" fx="<<d.fx<<" usable="<<d.usable<<" form="<<d.form<<" name="<<utf8(d.name)<<"\n";
                for(const auto& id:s.preferred)std::cout<<"recorded-default="<<utf8(id)<<"\n";
                check(s.valid,"read-only inventory failed");
            }
            CoUninitialize();return 0;
        }
        CoInitializeEx(nullptr,COINIT_MULTITHREADED);silentControlTests();CoUninitialize();tests();std::cout<<"PASS fxsound recovery + silent control: "<<checks<<" assertions; memory audio/process ports and isolated files only\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}
}

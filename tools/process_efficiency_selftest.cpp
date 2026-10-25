#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>
#include <stdexcept>
#include <iostream>
#include "../native/process_efficiency.h"
namespace efficiency=ymcc::processEfficiency;
unsigned checks=0;
void require(bool test,const char* reason){++checks;if(!test)throw std::runtime_error(reason);}
struct Fake {
    efficiency::Throttling stored{};DWORD current= NORMAL_PRIORITY_CLASS;unsigned sets=0,priorities=0;bool failQuery=false,failSet=false,failPriority=false,ignoreSet=false;
    bool query(HANDLE,efficiency::Throttling& state){if(failQuery)return false;state=stored;return true;}
    bool setQos(HANDLE,const efficiency::Throttling& state){++sets;if(failSet)return false;if(!ignoreSet)stored=state;return true;}
    DWORD priority(HANDLE){return current;}
    bool setPriority(HANDLE){++priorities;if(failPriority)return false;current=IDLE_PRIORITY_CLASS;return true;}
    DWORD error(){return ERROR_ACCESS_DENIED;}
};
int wmain(int argc,wchar_t** argv){
    try{
        if(argc==2&&std::wstring(argv[1])==L"--child"){
            efficiency::Throttling state{};const bool query=GetProcessInformation(GetCurrentProcess(),ProcessPowerThrottling,&state,sizeof(state))!=FALSE;
            std::printf("{\"priority\":%lu,\"qosQuery\":%s,\"controlMask\":%lu,\"stateMask\":%lu}\n",GetPriorityClass(GetCurrentProcess()),query?"true":"false",state.ControlMask,state.StateMask);
            return GetPriorityClass(GetCurrentProcess())==NORMAL_PRIORITY_CLASS?20:GetPriorityClass(GetCurrentProcess())==IDLE_PRIORITY_CLASS&&(state.StateMask&1)?21:22;
        }
        for(unsigned flags=0;flags<8;++flags)for(unsigned failure=0;failure<6;++failure){
            Fake ops;ops.stored.ControlMask=flags;ops.stored.StateMask=flags;ops.failQuery=failure==1;ops.failSet=failure==2;ops.failPriority=failure==3;ops.ignoreSet=failure==4;if(failure==5)ops.current=IDLE_PRIORITY_CLASS;
            const auto result=efficiency::apply(ops,reinterpret_cast<HANDLE>(1));
            require(result.priority==!ops.failPriority,"priority result");
            if(!ops.failQuery)require((ops.stored.ControlMask&6)==(flags&6)&&(ops.stored.StateMask&6)==(flags&6),"unrelated QoS flags changed");
            if(!ops.failQuery&&!ops.failSet&&!ops.ignoreSet)require(result.qos,"QoS should be confirmed");
            const auto before=ops.sets;efficiency::apply(ops,reinterpret_cast<HANDLE>(1));
            if(result.qos)require(ops.sets==before,"already applied policy wrote again");
        }
        const wchar_t* owner=L"C:\\SOFT\\YeMan\\YeManCC\\YeManCC.exe";
        for(const auto* path:{L"C:\\SOFT\\YeMan\\YeManCC\\YeManCC.exe",L"C:\\SOFT\\YeMan\\PowerControl\\fan-host\\YeManFanHost.exe",L"c:/soft/yeman/PowerControl/battery-host/YeManBatteryHost.exe",L"C:\\SOFT\\YeMan\\CustomSteamLibrary\\SteamArtworkLab.exe"})require(efficiency::ownedImage(path,owner),"owned task omitted");
        for(const auto* path:{L"C:\\Games\\YeManInputHost.exe",L"C:\\SOFT\\YeManOther\\YeManInputHost.exe",L"C:\\SOFT\\YeMan\\game.exe",L"C:\\Program Files (x86)\\Steam\\steam.exe",L"C:\\Windows\\System32\\notepad.exe"})require(!efficiency::ownedImage(path,owner),"external process classified as owned");
        SetLastError(1234);const auto applied=efficiency::applyCurrent();require(GetLastError()==1234,"last error changed");
        require(applied.qos&&applied.priority,"real process efficiency did not apply");
        efficiency::Throttling state{};require(GetProcessInformation(GetCurrentProcess(),ProcessPowerThrottling,&state,sizeof(state))!=FALSE&&(state.ControlMask&1)&&(state.StateMask&1),"real EcoQoS readback");
        require(GetPriorityClass(GetCurrentProcess())==IDLE_PRIORITY_CLASS,"real priority readback");
        if(argc==2){
            wchar_t self[32768]{};GetModuleFileNameW(nullptr,self,32768);
            const std::wstring own=argv[1];
            for(unsigned i=0;i<2;++i){
                const std::wstring path=i?own:std::wstring(self);std::wstring command=L"\""+path+L"\" --child";
                STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};
                require(efficiency::createProcess(path.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)!=FALSE,"fixture child launch");
                require(GetPriorityClass(pi.hProcess)==(i?DWORD(IDLE_PRIORITY_CLASS):DWORD(NORMAL_PRIORITY_CLASS)),"child priority inheritance scope");
                efficiency::Throttling childState{};
                require(GetProcessInformation(pi.hProcess,ProcessPowerThrottling,&childState,sizeof(childState))!=FALSE,"child QoS readback");
                require(bool(childState.StateMask&1)==bool(i),"external task inherited EcoQoS or owned task missing EcoQoS");
                ResumeThread(pi.hThread);require(WaitForSingleObject(pi.hProcess,3000)==WAIT_OBJECT_0,"child did not exit");
                DWORD exit=0;GetExitCodeProcess(pi.hProcess,&exit);require(exit==(i?21u:20u),"child initial state incorrect");CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
            }
        }
        std::printf("{\"pass\":true,\"checks\":%u,\"realPriority\":%lu,\"controlMask\":%lu,\"stateMask\":%lu,\"hardwareAccess\":false}\n",checks,GetPriorityClass(GetCurrentProcess()),state.ControlMask,state.StateMask);
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
#pragma once
#include <array>
#include <string_view>
#include "json.hpp"

namespace ymcc::trainerSteamLaunch {
// Match only the shipped frontend's PowerShell file invocation. Other hidden
// workers and explicit interactive requests never need a foreground read.
template<class MatchesScript,class MatchesApp>
bool needsLegacyForeground(const std::string& program,const nlohmann::json& args,
    const MatchesScript& matchesScript,const MatchesApp& matchesApp) {
    constexpr std::array<std::string_view,5> prefix={"-NoProfile","-NonInteractive","-ExecutionPolicy","Bypass","-File"};
    constexpr std::array<std::string_view,15> keys={"-Mode","-AppDirectory","-CacheRoot","-ResultPath","-RequestId",
        "-TrainerPath","-TrainerFolder","-ExpectedSha256","-TrainerGameName","-TrainerOrigin","-GcmResourceId",
        "-GameExecutablePath","-GameProcessId","-GameProcessCreated","-ExpectedGameSha256"};
    if(program!="powershell.exe"||!args.is_array()||args.size()<16||args.size()>6+2*keys.size()||args.size()%2)return false;
    for(size_t i=0;i<prefix.size();++i)
        if(!args[i].is_string()||args[i].get_ref<const std::string&>()!=prefix[i])return false;
    if(!args[5].is_string()||!matchesScript(args[5].get_ref<const std::string&>()))return false;
    std::array<bool,keys.size()> seen{};
    for(size_t i=6;i<args.size();i+=2){
        if(!args[i].is_string()||!args[i+1].is_string())return false;
        const std::string_view key=args[i].get_ref<const std::string&>();
        size_t index=0;
        while(index<seen.size()&&key!=keys[index])++index;
        if(index==seen.size()||seen[index])return false;
        seen[index]=true;
        if(index==0&&args[i+1]!="Trainer")return false;
        if(index==1&&!matchesApp(args[i+1].get_ref<const std::string&>()))return false;
    }
    return seen[0]&&seen[1]&&seen[2]&&seen[3]&&seen[4];
}
} // namespace ymcc::trainerSteamLaunch

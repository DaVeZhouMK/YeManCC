#pragma once
// Early, private same-image worker entry. Must precede singleton/settings/devices.
#include "decky_binding_worker.h"
#include "decky_binding_process_owner.h"
#include "decky_resource_worker.h"
#include "decky_context_cleanup_worker.h"
namespace ymcc::deckybinding::entry {
using CleanupHook=std::function<bool(const nlohmann::json&,HANDLE,const std::function<bool(const nlohmann::json&)>&,int&)>;
inline bool tryRunCommandLine(const wchar_t* commandLine,int& exitCode,const CleanupHook& cleanup={}){
    int argc=0;auto** argv=CommandLineToArgvW(commandLine,&argc);
    if(!argv){exitCode=ERROR_INVALID_PARAMETER;return true;}
    const auto mode=ChildChannel::classify(argc,argv);
    if(mode==ChildMode::ordinary){LocalFree(argv);return false;}
    if(mode==ChildMode::invalid){LocalFree(argv);exitCode=ERROR_INVALID_PARAMETER;return true;}
    ChildChannel channel;
    const auto opened=channel.openArguments(argc,argv);LocalFree(argv);
    if(opened!=ChildMode::worker){exitCode=ERROR_INVALID_PARAMETER;return true;}
    try{
        const auto request=channel.readFrame(7000);
        if(!request){exitCode=ERROR_INVALID_DATA;return true;}
        const std::string bytes(request.payload.begin(),request.payload.end());
        const auto emit=[&](const nlohmann::json& packet){
            const auto text=packet.dump();return static_cast<bool>(channel.writeFrame(std::vector<std::uint8_t>(text.begin(),text.end()),250));
        };
        const auto operation=nlohmann::json::parse(bytes);
        if(ymcc::deckyresources::worker::tryRun(operation,channel.cancelEvent(),emit,exitCode))return true;
        if(cleanup&&cleanup(operation,channel.cancelEvent(),emit,exitCode))return true;
        exitCode=worker::run(bytes,channel.cancelEvent(),[&](const nlohmann::json& packet){
            const auto text=packet.dump();
            const std::vector<std::uint8_t> payload(text.begin(),text.end());
            return static_cast<bool>(channel.writeFrame(payload,2500));
        });
    }catch(...){exitCode=ERROR_UNHANDLED_EXCEPTION;}
    return true;
}
} // namespace ymcc::deckybinding::entry

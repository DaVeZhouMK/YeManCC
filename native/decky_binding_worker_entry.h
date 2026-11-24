#pragma once
// Early, private same-image worker entry. Must precede singleton/settings/devices.
#include "decky_binding_worker.h"
#include "decky_binding_process_owner.h"
#include "decky_resource_worker.h"
#include "decky_context_cleanup_worker.h"
#include "decky_listener_proxy.h"
#include "steam_document_finite_worker.h"
#include <optional>
namespace ymcc::deckybinding::entry {
using CleanupHook=std::function<bool(const nlohmann::json&,HANDLE,const std::function<bool(const nlohmann::json&)>&,int&)>;
// The retained-repair fixture alone exercises old, unmarked cleanup frames.
// This policy is compiled into that fixture; no process input can select it.
template<bool AllowLegacyCleanupFixture=false>
inline bool tryRunCommandLine(const wchar_t* commandLine,int& exitCode,const CleanupHook& cleanup={}){
    int argc=0;auto** argv=CommandLineToArgvW(commandLine,&argc);
    if(!argv){exitCode=ERROR_INVALID_PARAMETER;return true;}
    const auto mode=ChildChannel::classify(argc,argv);
    if(mode==ChildMode::ordinary){LocalFree(argv);return false;}
    if(mode==ChildMode::invalid){LocalFree(argv);exitCode=ERROR_INVALID_PARAMETER;return true;}
    auto sharedChannel=std::make_shared<ChildChannel>();auto& channel=*sharedChannel;
    auto writes=std::make_shared<std::timed_mutex>();
    const auto opened=channel.openArguments(argc,argv);LocalFree(argv);
    if(opened!=ChildMode::worker){exitCode=ERROR_INVALID_PARAMETER;return true;}
    try{
        const auto request=channel.readFrame(7000);
        if(!request){exitCode=ERROR_INVALID_DATA;return true;}
        const std::string bytes(request.payload.begin(),request.payload.end());
        const auto send=[&](const nlohmann::json& packet,DWORD timeout){
            const auto end=GetTickCount64()+timeout;
            std::unique_lock<std::timed_mutex> lock(*writes,std::defer_lock);
            if(!lock.try_lock_for(std::chrono::milliseconds(timeout))||!listenerproxy::left(end))return false;
            const auto text=packet.dump();return static_cast<bool>(channel.writeFrame({text.begin(),text.end()},listenerproxy::left(end)));
        };
        const auto emit=[&](const nlohmann::json& packet){
            return send(packet,250);
        };
        const auto operation=nlohmann::json::parse(bytes);
        // Install only after the original channel has authenticated. A resource
        // operation without Steam identity still runs; any attempted acquisition
        // is denied by its empty bound identity rather than falling back locally.
        worker::Request identity;
        const auto kind=operation.value("schema",std::string{});
        std::optional<steamdocuments::finite::Request> documents;
        if(kind==worker::schema){
            identity=worker::parse(bytes);
            if(!identity.delegatedIdentity){
                exitCode=2;
                send({{"schema",kind},{"id",identity.id},{"generation",std::to_string(identity.generation)},
                    {"kind","result"},{"detail",{{"ok",false},{"mutated",false},{"reason","mirror-bootstrap-shared-identity-required"}}}},2500);
                return true;
            }
        }
        else if(kind==steamdocuments::finite::schema)documents.emplace(steamdocuments::finite::parse(bytes));
        else if(kind==deckyrepair::cleanupworker::schema||kind==deckyrepair::cleanupworker::lifecycleSchema){
            const auto request=deckyrepair::cleanupworker::parse(operation);
            if constexpr(!AllowLegacyCleanupFixture){
                if(!request.identity.delegatedIdentity){
                    exitCode=2;
                    emit({{"schema",kind},{"id",request.id},{"generation",request.generation},
                        {"detail",{{"ok",false},{"mutated",false},{"reason","decky-cleanup-worker-shared-identity-required"}}}});
                    return true;
                }
            }
            identity=request.identity;
        }
        const auto& steam=documents?documents->steam:identity.steam;
        auto proxy=std::make_shared<listenerproxy::Child>(sharedChannel,writes,operation.value("id",std::string{}),operation.value("generation",std::string{}),steam.root,steam.pid);
        steamlive::listenersource::Delegated source([proxy](const std::wstring& root,DWORD pid,ULONGLONG end){return proxy->read(root,pid,end);});
        steamcommand::RemoteScope commandScope([proxy](steamcommand::Effect effect,unsigned,bool completed,bool accepted){
            return proxy->command(effect,completed,accepted,GetTickCount64()+2500);
        });
        if(documents){exitCode=steamdocuments::finite::run(*documents,channel.cancelEvent(),emit);return true;}
        if(ymcc::deckyresources::worker::tryRun(operation,channel.cancelEvent(),emit,exitCode))return true;
        if(cleanup&&cleanup(operation,channel.cancelEvent(),emit,exitCode))return true;
        exitCode=worker::run(bytes,channel.cancelEvent(),[&](const nlohmann::json& packet){
            return send(packet,2500);
        });
    }catch(...){exitCode=ERROR_UNHANDLED_EXCEPTION;}
    return true;
}
} // namespace ymcc::deckybinding::entry

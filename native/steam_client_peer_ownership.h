#pragma once
#include "steam_information_client.h"
#include "process_inventory.h"
#include <memory>
namespace ymcc::steaminfo {
struct Ancestry {
    std::vector<std::unique_ptr<Peer>> parents;
    bool live()const{return std::all_of(parents.begin(),parents.end(),[](const auto& peer){return peer->live();});}
};
enum class LibraryCaller { HostOnly, HostOrWorker };
inline bool authorizeLibraryCaller(const Peer& provider,const Peer& caller,Ancestry& held,LibraryCaller allowed,
        std::shared_ptr<const processInventory::Snapshot>* receipt=nullptr){
    if(!provider.sameUser(caller))return false;
    const auto root=std::filesystem::path(provider.path).parent_path();
    const auto host=pathKey((root/L"CustomSteamLibrary"/L"CustomSteamLibrary.exe").wstring());
    const auto worker=pathKey((root/L"CustomSteamLibrary"/L"SteamArtworkLab.exe").wstring());
    const auto callerPath=pathKey(caller.path);
    const bool callerIsHost=callerPath==host;
    if(!callerIsHost&&(allowed!=LibraryCaller::HostOrWorker||callerPath!=worker))return false;
    auto table=processInventory::read(processInventory::resolveQuery(),processInventory::currentTime,0);
    if(!table||table->status!=processInventory::Status::Complete)return false;
    const auto accept=[&] {
        if(!provider.live()||!caller.live()||!held.live())return false;
        if(receipt)*receipt=table;
        return true;
    };
    DWORD childPid=caller.pid;ULONGLONG childBirth=caller.birth;
    for(unsigned depth=0;depth<4;++depth){
        const auto row=std::find_if(table->rows.begin(),table->rows.end(),[&](const auto& row){return row.pid==childPid;});
        if(row==table->rows.end()||!row->parentPid||row->parentPid==childPid)return false;
        if(row->parentPid==provider.pid)return provider.birth<=childBirth&&accept();
        auto parent=std::make_unique<Peer>();if(!parent->load(row->parentPid)||!parent->sameUser(provider)||parent->birth>childBirth)return false;
        // A standalone installed Host may be started by Explorer. Hold its
        // actual direct parent, but metadata does not require main ancestry.
        const bool reachedHost=childPid==caller.pid?callerIsHost:
            (!held.parents.empty()&&pathKey(held.parents.back()->path)==host);
        if(reachedHost){held.parents.push_back(std::move(parent));return accept();}
        const auto name=pathKey(parent->path);if(name!=host&&name!=worker)return false;
        childPid=parent->pid;childBirth=parent->birth;held.parents.push_back(std::move(parent));
    }
    return false;
}
} // namespace ymcc::steaminfo

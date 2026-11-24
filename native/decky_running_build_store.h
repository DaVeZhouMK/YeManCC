#pragma once
#include "decky_running_build_recovery.h"
#include "decky_build_recovery_io.h"
namespace ymcc::deckyupgrade::runningstore {
using Json=nlohmann::json;
struct State {
    std::wstring root,path,backup;std::string normalized;running::Build build;
    updateconfig::Image primary,secondary;Json history;
};
// This can only deny. Require both protected copies to already describe the
// executing build, with no observation to publish and the requested slot spent.
inline bool publishedSlotSpent(const updateconfig::Image& primary,const updateconfig::Image& backup,
                               const running::Build& build,const std::string& exe,bool reopen){
    if(primary.kind!=updateconfig::Kind::ready||backup.kind!=updateconfig::Kind::ready||primary.value!=backup.value)return false;
    const auto& history=primary.value;
    if(!history.is_object()||running::observe(history,build,exe)!=history)return false;
    return !history.at("builds").at(build.id).at(reopen?"bpm":"page").is_null();
}
inline bool slotAlreadySpent(const std::wstring& exe,const std::string& version,bool reopen,const std::function<bool()>& current){
    try{
        if(!buildidentity::available||!current())return false;
        store::Lease lease(exe);if(!lease.owned)return false;
        const auto root=updaterecovery::directory();
        const auto disk=updateconfig::read((std::filesystem::path(exe).parent_path()/L"native-build-identity.json").wstring(),4096);
        if(!store::receiptImage(disk)||std::string(buildidentity::marker())!=std::string(buildidentity::prefix)+disk.value.at("nativeBuildId").get<std::string>())return false;
        const running::Build build{disk.value.at("nativeBuildId").get<std::string>(),disk.value.at("nativeSha256").get<std::string>(),version};
        const auto path=updaterecovery::pathFor(root,exe)+L".decky-running-builds.json";
        const auto primary=store::protectedRead(root,path);
        const auto backup=store::protectedRead(root,path+L".bak");
        return publishedSlotSpent(primary,backup,build,updaterecovery::utf8(updaterecovery::normalized(exe)),reopen)&&current();
    }catch(...){return false;}
}
// Optional stage points only to fixed literals; no allocation, logging or new IO.
// The caller serializes after its settings guard is released. Existing predicates
// and current() calls stay in their original order; diagnostics grant no authority.
inline bool load(State& state,const std::wstring& exe,const std::string& version,
                 const std::function<std::string(const std::wstring&)>& digest,const std::function<bool()>& current,const char** stage=nullptr){
    if(stage)*stage="native-marker";
    if(!buildidentity::available||!current())return false;
    if(stage)*stage="protected-directory";
    state.root=updaterecovery::directory();
    if(!updaterecovery::secureDirectory(state.root,true))return false;
    if(stage)*stage="native-receipt";
    const auto disk=updateconfig::read((std::filesystem::path(exe).parent_path()/L"native-build-identity.json").wstring(),4096);
    if(!store::receiptImage(disk)||std::string(buildidentity::marker())!=std::string(buildidentity::prefix)+disk.value.at("nativeBuildId").get<std::string>())return false;
    if(stage)*stage="native-image";
    const auto actual=digest(exe);if(actual!=disk.value.at("nativeSha256").get_ref<const std::string&>()||!current())return false;
    if(stage)*stage="native-file";
    const auto attrs=GetFileAttributesW(exe.c_str());if(attrs==INVALID_FILE_ATTRIBUTES||(attrs&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))return false;
    if(stage)*stage="native-build";
    state.build={disk.value.at("nativeBuildId").get<std::string>(),actual,version};if(!running::build(state.build))return false;
    state.normalized=updaterecovery::utf8(updaterecovery::normalized(exe));
    state.path=updaterecovery::pathFor(state.root,exe)+L".decky-running-builds.json";state.backup=state.path+L".bak";
    if(stage)*stage="history-read";
    state.primary=store::protectedRead(state.root,state.path);state.secondary=store::protectedRead(state.root,state.backup);
    const auto usable=[](const auto& x){return x.kind==updateconfig::Kind::ready||x.kind==updateconfig::Kind::missing;};
    if(!usable(state.primary)||!usable(state.secondary))return false;
    if(stage)*stage="history-merge";
    state.history=running::reconcile(state.primary.kind==updateconfig::Kind::missing?Json::object():state.primary.value,
        state.secondary.kind==updateconfig::Kind::missing?Json::object():state.secondary.value,state.build,state.normalized);
    if(state.history.is_null())return false;
    if(stage)*stage="build-observe";
    state.history=running::observe(std::move(state.history),state.build,state.normalized);
    return !state.history.is_null()&&current();
}
inline bool save(const State& state,const Json& next,const std::function<bool()>& current,const char** stage=nullptr){
    if(stage)*stage="history-value";
    if(!running::valid(next)||!current())return false;
    const bool mainSame=state.primary.kind==updateconfig::Kind::ready&&state.primary.value==next;
    const bool backupSame=state.secondary.kind==updateconfig::Kind::ready&&state.secondary.value==next;
    if(stage)*stage="history-primary";
    if(!mainSame&&!store::publish(state.root,state.path,state.primary,next,current))return false;
    if(stage)*stage="history-backup";
    if(!backupSame&&!store::publish(state.root,state.backup,state.secondary,next,current))return false;
    if(stage)*stage="history-readback";
    for(const auto* path:{&state.path,&state.backup}){const auto saved=store::protectedRead(state.root,*path);
        if(saved.kind!=updateconfig::Kind::ready||saved.value!=next)return false;}
    if(stage)*stage="history-current";
    return current();
}
// Original startup invokes this ONCE per actual main process. Observation is
// installation metadata only; never a Steam or business-setting action. It
// records healthy/intermediate builds too, so later A-B rollback cannot refill.
inline bool observe(const std::wstring& exe,const std::string& version,
    const std::function<std::string(const std::wstring&)>& digest,const std::function<bool()>& current){
    try{store::Lease lease(exe);if(!lease.owned)return false;State state;
        return load(state,exe,version,digest,current)&&save(state,state.history,current);
    }catch(...){return false;}
}
// Keep the diagnostic pointer chain out of the huge main TU inliner (MSVC /O2).
// This cold recovery reservation still executes every original predicate and IO.
__declspec(noinline) inline bool reserve(const std::wstring& exe,const std::string& version,bool reopen,const std::string& steam,const Json& prior,const Json& next,
    const std::function<std::string(const std::wstring&)>& digest,const std::function<bool()>& current,const char** stage=nullptr){
    if(stage)*stage="lease";
    try{store::Lease lease(exe);if(!lease.owned)return false;State state;if(!load(state,exe,version,digest,current,stage))return false;
        if(stage)*stage="grant";
        const auto reserved=running::reserve(state.history,state.build,state.normalized,reopen,steam,prior,next);
        // Even refusal retains newly observed/retired build facts. Dispatch
        // requires BOTH durable copies, preserving partial commit consumption.
        if(!save(state,reserved.is_null()?state.history:reserved,current,stage))return false;
        if(stage)*stage=reserved.is_null()?"grant":"final-current";
        const bool accepted=!reserved.is_null()&&current();
        if(accepted&&stage)*stage="reserved";
        return accepted;
    }catch(...){return false;}
}
} // namespace ymcc::deckyupgrade::runningstore

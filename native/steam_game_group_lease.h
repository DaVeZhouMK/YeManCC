#pragma once
#include "steam_game_process_facts.h"
#include "steam_game_role_contract.h"
#include <array>
#include <span>
#include <cstdio>

namespace ymcc::steamnative { class ReadScope; }
namespace ymcc::steamgamefacts {
// PRIVATE CANDIDATE. Request-local exact objects plus one source observation.
// This is process/topology evidence, never account, native-record, UI, power,
// historical-reference or publication authority. Existing final gates remain.
struct ProcessGroupMemberSeed {
    std::uint32_t pid=0;
    std::string path,at;
    unsigned sourceLine=0;
};
struct ProcessGroupSeed {
    std::uint32_t appid=0,selectedPid=0;
    std::uint64_t fullGameId=0,sourceRevision=0,consoleRevision=0;
    std::vector<ProcessGroupMemberSeed> members;
};
enum class ProcessGroupRejection {
    None, SourceUnknown, SelectionMismatch, InvalidMember, ProcessUnavailable,
    BirthWindowMismatch, PathOrSessionMismatch, ParentUnavailable, InvalidTopology,
    ContextChanged, PreparationFailed
};
struct ProcessGroupCapture {
    std::shared_ptr<ProcessGroupLease> lease;
    ProcessGroupRejection rejection=ProcessGroupRejection::SourceUnknown;
    std::uint32_t rejectedPid=0;
};
namespace group_detail {
inline std::wstring utf16(const std::string& value,bool requireAbsolute=true){
    if(value.empty()||value.size()>131068||value.find('\0')!=value.npos)return {};
    const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);
    if(count<=0||count>32767)return {};
    std::wstring out(static_cast<std::size_t>(count),L'\0');
    if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),out.data(),count)!=count)return {};
    const bool drive=out.size()>3&&out[1]==L':'&&(out[2]==L'\\'||out[2]==L'/');
    const bool unc=out.size()>4&&out[0]==L'\\'&&out[1]==L'\\';
    return !requireAbsolute||drive||unc?out:std::wstring{};
}
inline bool scriptPairToolImage(const std::wstring& spelling,const std::wstring& image,std::size_t toolSlash){
    if(spelling.size()<=4||_wcsicmp(spelling.c_str()+spelling.size()-4,L".exe")!=0)return false;
    if(spelling.find_first_of(L"\\/:")!=std::wstring::npos)return samePath(spelling,image);
    return samePath(spelling,image.substr(toolSlash+1));
}
inline bool scriptPairToolCommand(const std::wstring& spelling,const std::wstring& command,
    const std::wstring& image,std::size_t toolSlash){
    if(spelling.empty()||spelling!=command||spelling.front()==L' '||spelling.front()==L'\t')return false;
    int count=0;const auto args=CommandLineToArgvW(command.c_str(),&count);
    const auto freeArguments=[](LPWSTR* value) noexcept { LocalFree(value); };
    const std::unique_ptr<LPWSTR,decltype(freeArguments)> ownedArguments(args,freeArguments);
    return args&&count>1&&scriptPairToolImage(args[0],image,toolSlash);
}
inline bool scriptPairChildTool(const std::string& logPath,const ProcessWitness& tool,
    const ProcessWitness& game,DWORD parent,HANDLE process=nullptr){
    if(parent!=game.pid||tool.created<=game.created)return false;
    const auto gameSlash=game.image.find_last_of(L"\\/"),toolSlash=tool.image.find_last_of(L"\\/");
    if(gameSlash==std::wstring::npos||toolSlash==std::wstring::npos||
        !samePath(game.image.substr(0,gameSlash),tool.image.substr(0,toolSlash))||samePath(game.image,tool.image))return false;
    const auto spelling=utf16(logPath,false);
    if(scriptPairToolImage(spelling,tool.image,toolSlash))return true;
    // An unquoted Steam tracker payload can include the tool's arguments.
    // Require the held process's exact command line before using argv[0].
    if(!process||spelling.find_first_of(L" \t")==std::wstring::npos)return false;
    const auto command=detail::commandLine(process);
    return command&&scriptPairToolCommand(spelling,*command,tool.image,toolSlash);
}
inline std::string localSecond(std::uint64_t value){
    if(!value)return {};ULARGE_INTEGER raw{};raw.QuadPart=value;
    const FILETIME utc{raw.LowPart,raw.HighPart};FILETIME local{};SYSTEMTIME at{};
    if(!FileTimeToLocalFileTime(&utc,&local)||!FileTimeToSystemTime(&local,&at))return {};
    char text[32]{};const int count=std::snprintf(text,sizeof(text),"%04u-%02u-%02u %02u:%02u:%02u",
        static_cast<unsigned>(at.wYear),static_cast<unsigned>(at.wMonth),static_cast<unsigned>(at.wDay),
        static_cast<unsigned>(at.wHour),static_cast<unsigned>(at.wMinute),static_cast<unsigned>(at.wSecond));
    return count==19?std::string(text,19):std::string{};
}
inline bool launchWindow(std::uint64_t birth,const std::string& at){
    if(!birth||at.size()!=19)return false;
    const auto first=localSecond(birth>20000000ULL?birth-20000000ULL:0);
    const auto last=localSecond(birth<=(std::numeric_limits<std::uint64_t>::max)()-30000000ULL?birth+30000000ULL:birth);
    return !first.empty()&&!last.empty()&&first<=at&&at<=last;
}
}
class ProcessGroupLease final {
    friend class ProcessOwner;
    friend class BoundQualifiedRead;
    friend class GroupNativeRoleBinding;
    friend class MainRunningGroupRoleIssuer;
    friend class RunningLinkNativeRoleObservation;
    const std::shared_ptr<ProcessLease> selected_;
    const ProcessGroupSeed source_;
    const ymcc::steamgameroles::Graph graph_;
    const std::array<ProcessWitness,32> witnesses_;
    // selected_/Request owns node0; these own exactly the remaining nodes.
    const std::vector<detail::PinnedProcess> otherMembers_;
    std::array<HANDLE,33> liveness_{};
    const DWORD livenessCount_;
    mutable std::atomic<bool> revoked_{false};
    mutable std::atomic<std::shared_ptr<RequestCancellation>> cancellationSignal_{};
    // Defined by the qualified read composition, using this actual Request.
    bool nativeReadScopeCurrent(ymcc::steamnative::ReadScope& scope)const noexcept;
    ProcessGroupLease(std::shared_ptr<ProcessLease> selected,ProcessGroupSeed source,
        ymcc::steamgameroles::Graph graph,std::array<ProcessWitness,32> witnesses,
        std::vector<detail::PinnedProcess> members)
        :selected_(std::move(selected)),source_(std::move(source)),graph_(graph),
         witnesses_(std::move(witnesses)),otherMembers_(std::move(members)),
         livenessCount_(static_cast<DWORD>(otherMembers_.size()+2)){
        liveness_[0]=selected_->request_->gameHandle_.value;
        liveness_[1]=selected_->request_->steamHandle_.value;
        for(std::size_t i=0;i<otherMembers_.size();++i)liveness_[i+2]=otherMembers_[i].handle.value;
    }
public:
    ProcessGroupLease(const ProcessGroupLease&)=delete;
    ProcessGroupLease& operator=(const ProcessGroupLease&)=delete;
    void cancel()const noexcept{
        revoked_.store(true,std::memory_order_release);
        if(const auto held=cancellationSignal_.load(std::memory_order_acquire))held->cancel();
    }
    std::shared_ptr<RequestCancellation> groupCancellation()const noexcept{
        return requestCancellationSignal(cancellationSignal_,revoked_);
    }
    std::shared_ptr<RequestCancellation> processCancellation()const noexcept{
        return selected_->request_->cancellation();
    }
    // Original selected+Steam only. This does not establish group currentness
    // or revive a revoked group; unused helper lifetime is intentionally absent.
    bool originalProcessesCurrent()const noexcept{
        return selected_->processCurrent();
    }
    bool processCurrent()const noexcept{
        if(revoked_.load(std::memory_order_acquire))return false;
        try{
            const auto& request=selected_->request_;
            const auto publication=request->observer_->identitySnapshot();
            // One nonblocking kernel wait over at most 33 held objects. Do not
            // repeat per-process image/birth/parent queries on a hot guard.
            const bool current=GetCurrentProcessId()==request->creatorPid_&&
                !request->cancelled_.load(std::memory_order_acquire)&&
                request->sameProcessPublication(publication)&&
                request->observer_->sameIdentityEpoch(request->expected_)&&
                livenessCount_>=2&&livenessCount_<=liveness_.size()&&
                WaitForMultipleObjects(livenessCount_,liveness_.data(),FALSE,0)==WAIT_TIMEOUT&&
                !request->cancelled_.load(std::memory_order_acquire);
            if(!current){cancel();return false;}
            return !revoked_.load(std::memory_order_acquire);
        }catch(...){cancel();return false;}
    }
    // Borrowed exact held members beyond selected+Steam. Keep this lease
    // alive throughout the wait; do not close or replace these handles. This
    // exposes liveness only and grants no native-role or action authority.
    std::span<const HANDLE> additionalProcessLiveness()const noexcept{
        return {liveness_.data()+2,static_cast<std::size_t>(livenessCount_-2)};
    }
    const ProcessWitness& selectedGame()const noexcept{return selected_->gameWitness();}
    const ProcessWitness& steam()const noexcept{return selected_->steamWitness();}
    const ProcessGroupSeed& source()const noexcept{return source_;}
    const ymcc::steamgameroles::Graph& graph()const noexcept{return graph_;}
    const ProcessWitness* witness(std::uint32_t pid)const noexcept{
        for(std::uint32_t i=0;i<graph_.count;++i)if(witnesses_[i].pid==pid)return &witnesses_[i];
        return nullptr;
    }
    using Inspection=decltype(ymcc::steamgameroles::inspect(std::declval<const ymcc::steamgameroles::Graph&>(),0u,0u));
    std::optional<Inspection> inspectNativeRoles(std::uint32_t primary,std::uint32_t foreground)const noexcept{
        if(!processCurrent())return {};
        const auto result=ymcc::steamgameroles::inspect(graph_,primary,foreground);
        if(!processCurrent())return {};
        return result;
    }
};

inline ProcessGroupCapture ProcessOwner::acquireGroup(const std::shared_ptr<ProcessLease>& selected,
    const ProcessGroupSeed& source,DWORD nativePrimaryPid)const{
    ProcessGroupCapture out;
    const auto refuse=[&](ProcessGroupRejection why,std::uint32_t pid=0){out.rejection=why;out.rejectedPid=pid;return out;};
    try{
        if(!selected||!selected->processCurrent())return refuse(ProcessGroupRejection::ContextChanged);
        if(!source.sourceRevision||!source.consoleRevision||!exactGameId(source.appid,source.fullGameId)||
            source.members.empty()||source.members.size()>32)return out;
        if(source.selectedPid!=selected->gameWitness().pid)return refuse(ProcessGroupRejection::SelectionMismatch);
        const auto& request=selected->request_;
        if(!request->observer_->sameIdentityEpoch(request->expected_))return refuse(ProcessGroupRejection::ContextChanged);
        // Steam can put a system VBS launcher and the game's own child tools in
        // one shortcut row set. Prove those tools belong to the selected game,
        // but keep them out of the launcher/game role tuple.
        const auto launcher=std::find_if(source.members.begin(),source.members.end(),[&](const auto& row){return row.pid==nativePrimaryPid;});
        const bool scriptPair=source.members.size()>2&&(source.fullGameId&0xffffffffULL)==0x02000000ULL&&
            nativePrimaryPid&&nativePrimaryPid!=source.selectedPid&&launcher!=source.members.end()&&
            detail::windowsScriptHostImage(group_detail::utf16(launcher->path));
        ymcc::steamgameroles::Graph graph;graph.count=scriptPair?2u:static_cast<std::uint32_t>(source.members.size());graph.selectedPid=source.selectedPid;
        std::array<ProcessWitness,32> witnesses{};std::vector<detail::PinnedProcess> members;
        members.reserve(source.members.size()-1);
        unsigned selectedRows=0;for(const auto& seed:source.members)if(seed.pid==source.selectedPid)++selectedRows;
        if(selectedRows!=1)return refuse(ProcessGroupRejection::SelectionMismatch);
        std::uint32_t next=1;
        for(const auto& seed:source.members){
            out.rejectedPid=seed.pid;
            if(!seed.pid||seed.pid==selected->steamWitness().pid||!seed.sourceLine||seed.at.empty())return refuse(ProcessGroupRejection::InvalidMember,seed.pid);
            const bool helper=scriptPair&&seed.pid!=source.selectedPid&&seed.pid!=nativePrimaryPid;
            const auto path=group_detail::utf16(seed.path);
            if(path.empty()&&!helper)return refuse(ProcessGroupRejection::InvalidMember,seed.pid);
            for(std::uint32_t i=0;i<next&&i<graph.count;++i)if(witnesses[i].pid==seed.pid)return refuse(ProcessGroupRejection::InvalidMember,seed.pid);
            const bool original=seed.pid==source.selectedPid;
            const std::uint32_t index=original||helper?0:next++;
            if(index>=graph.count)return refuse(ProcessGroupRejection::InvalidMember,seed.pid);
            HANDLE handle=nullptr;detail::PinnedProcess observed;
            if(original){
                observed.witness=selected->gameWitness();observed.windowsSession=selected->windowsSession();
                handle=request->gameHandle_.value;
                if(!samePath(path,selected->selectedGameWitness().image)&&!samePath(path,observed.witness.image))
                    return refuse(ProcessGroupRejection::PathOrSessionMismatch,seed.pid);
            }else{
                observed.handle=detail::Handle(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,seed.pid));
                handle=observed.handle.value;
                if(!detail::alive(handle))return refuse(ProcessGroupRejection::ProcessUnavailable,seed.pid);
                observed.witness={GetProcessId(handle),detail::birth(handle),detail::image(handle)};
                if(!ProcessIdToSessionId(seed.pid,&observed.windowsSession))return refuse(ProcessGroupRejection::PathOrSessionMismatch,seed.pid);
            }
            if(!detail::complete(observed.witness)||observed.witness.pid!=seed.pid||
                observed.windowsSession!=selected->windowsSession()||
                observed.witness.created<selected->steamWitness().created||!detail::alive(handle))
                return refuse(ProcessGroupRejection::PathOrSessionMismatch,seed.pid);
            if(!group_detail::launchWindow(observed.witness.created,seed.at))return refuse(ProcessGroupRejection::BirthWindowMismatch,seed.pid);
            const auto parent=detail::parent(handle);
            if(!parent||parent==seed.pid)return refuse(ProcessGroupRejection::ParentUnavailable,seed.pid);
            if(helper){
                if(!group_detail::scriptPairChildTool(seed.path,observed.witness,selected->gameWitness(),parent,handle)||
                    !selected->processCurrent()||!detail::alive(handle))
                    return refuse(ProcessGroupRejection::InvalidTopology,seed.pid);
                continue;
            }
            if(!samePath(path,observed.witness.image))return refuse(ProcessGroupRejection::PathOrSessionMismatch,seed.pid);
            if(scriptPair&&((original&&parent!=nativePrimaryPid)||(!original&&
                detail::scriptHostVbsOperand(handle,observed.witness.image,selected->steamWitness().pid).empty())))
                return refuse(ProcessGroupRejection::InvalidTopology,seed.pid);
            graph.members[index]={seed.pid,observed.windowsSession,parent,observed.witness.created};
            witnesses[index]=observed.witness;
            if(!original)members.push_back(std::move(observed));
            if(!selected->processCurrent())return refuse(ProcessGroupRejection::ContextChanged);
        }
        const auto topology=ymcc::steamgameroles::inspect(graph,source.selectedPid,source.selectedPid);
        if(topology.state!=ymcc::steamgameroles::State::ExactSelected&&
            topology.state!=ymcc::steamgameroles::State::VerifiedGroupRoles)
            return refuse(ProcessGroupRejection::InvalidTopology);
        auto lease=std::shared_ptr<ProcessGroupLease>(new ProcessGroupLease(selected,source,graph,std::move(witnesses),std::move(members)));
        if(!lease->processCurrent())return refuse(ProcessGroupRejection::ContextChanged);
        out.lease=std::move(lease);out.rejection=ProcessGroupRejection::None;out.rejectedPid=0;return out;
    }catch(...){return refuse(ProcessGroupRejection::PreparationFailed,out.rejectedPid);}
}
}

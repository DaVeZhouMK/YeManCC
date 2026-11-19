namespace ymcc::deckybinding::clientdetail {
struct ReaderRetainedFixtureAccess {
    static nlohmann::json snapshot(){
        std::lock_guard lock(EventReader::retainedMutex_);
        auto rows=nlohmann::json::array();
        for(auto* s=EventReader::retained_;s;s=s->retainedNext)
            rows.push_back({{"state",std::to_string(reinterpret_cast<std::uintptr_t>(s))},
                {"handles",unsigned(s->pipe.value!=nullptr)+unsigned(s->ready.value!=nullptr)},
                {"pending",s->pending},{"cleanupError",s->cleanupError}});
        return rows;
    }
    template<class Reader=EventReader>static void parkPending(EventReader& reader){
        auto empty=std::make_unique<EventReader::State>();auto old=std::move(reader.state_);reader.state_=std::move(empty);
        if constexpr(requires{Reader::retain(old);})Reader::retain(old);
        else {std::lock_guard lock(EventReader::retainedMutex_);old->retainedNext=EventReader::retained_;EventReader::retained_=old.release();}
    }
    static void hold(HANDLE entered,HANDLE release,bool state){
        if(!state){std::lock_guard lock(EventReader::retainedMutex_);SetEvent(entered);WaitForSingleObject(release,2000);return;}
        EventReader::State* selected=nullptr;{std::lock_guard lock(EventReader::retainedMutex_);selected=EventReader::retained_;}
        if(!selected)return;std::lock_guard lock(selected->mutex);SetEvent(entered);WaitForSingleObject(release,2000);
    }
};
}
namespace ymcc::deckybinding::fixture::retainedreader {
using Reader=clientdetail::EventReader;using Access=clientdetail::ReaderRetainedFixtureAccess;
inline Json measurements=Json::array();
inline HANDLE readPipe(Owner& owner){DWORD error=0;const auto h=owner.copyReadPipe(&error);need(h&&error==0,"retained-fixture-pipe");return h;}
inline std::string parkProtected(Owner& owner,HANDLE& event){
    Reader reader;DWORD error=99;need(reader.attach(readPipe(owner),&error)&&error==0,"retained-reader-attach");event=reader.event();
    need(event&&SetHandleInformation(event,HANDLE_FLAG_PROTECT_FROM_CLOSE,HANDLE_FLAG_PROTECT_FROM_CLOSE),"retained-reader-protect");
    need(!reader.stop(200),"real-protected-reader-close-did-not-fail");const auto status=reader.snapshot();
    need(status.handles==1&&status.pendingIo==0&&status.error==ERROR_INVALID_HANDLE&&!status.fullyDestroyed,"real-protected-reader-facts");
    return std::to_string(reinterpret_cast<std::uintptr_t>(event));
}
inline void transient(Owner& owner){
    HANDLE event=nullptr;parkProtected(owner,event);const auto parked=Access::snapshot();need(parked.size()==1,"retained-reader-was-not-parked");
    need(SetHandleInformation(event,HANDLE_FLAG_PROTECT_FROM_CLOSE,0),"retained-reader-unprotect");
    Reader next;DWORD error=99;need(next.attach(readPipe(owner),&error)&&error==0,"retained-next-attach");
    const auto after=Access::snapshot();measurements.push_back({{"kind","actual-protected-close-then-new-attach"},{"before",parked},{"after",after}});
    need(after.empty(),"retained-reader-not-reaped-on-next-attach");
    need(next.stop(200)&&next.snapshot().fullyDestroyed,"retained-next-cleanup");
}
inline void fairness(Owner& owner){
    HANDLE first=nullptr,second=nullptr;parkProtected(owner,first);parkProtected(owner,second);
    const auto initial=Access::snapshot();need(initial.size()==2,"retained-fifo-preparation");
    need(SetHandleInformation(second,HANDLE_FLAG_PROTECT_FROM_CLOSE,0),"retained-fifo-second-unprotect");
    Reader invalid;DWORD error=0;need(!invalid.attach(nullptr,&error)&&error==ERROR_INVALID_HANDLE,"retained-housekeeping-changed-attach-error");
    const auto rotated=Access::snapshot();need(rotated.size()==2&&rotated[0]["state"]==initial[1]["state"]&&rotated[1]["state"]==initial[0]["state"],"retained-head-did-not-rotate-one-state");
    Reader next;need(next.attach(readPipe(owner)),"retained-fifo-attach");const auto remaining=Access::snapshot();
    need(remaining.size()==1&&remaining[0]["state"]==initial[0]["state"]&&remaining[0]["handles"]==1,"protected-head-starved-later-completed-reader");
    need(next.stop(200),"retained-fifo-next-stop");need(SetHandleInformation(first,HANDLE_FLAG_PROTECT_FROM_CLOSE,0),"retained-fifo-first-unprotect");
    Reader final;need(final.attach(readPipe(owner))&&Access::snapshot().empty()&&final.stop(200),"retained-fifo-final-cleanup");
    measurements.push_back({{"kind","one-state-fifo-permanent-refusal-fairness"},{"initial",initial},{"rotated",rotated},{"remaining",remaining},{"attachErrorPreserved",true}});
}
inline void busy(Owner& owner,bool state){
    HANDLE event=nullptr;parkProtected(owner,event);need(SetHandleInformation(event,HANDLE_FLAG_PROTECT_FROM_CLOSE,0),"retained-busy-unprotect");
    detail::Handle entered{CreateEventW(nullptr,TRUE,FALSE,nullptr)},release{CreateEventW(nullptr,TRUE,FALSE,nullptr)},done{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    std::thread holder([&]{Access::hold(entered.value,release.value,state);});
    const bool acquired=WaitForSingleObject(entered.value,1000)==WAIT_OBJECT_0;
    Reader next;bool attached=false;std::thread caller([&]{attached=next.attach(readPipe(owner));SetEvent(done.value);});
    const auto began=GetTickCount64();const bool beforeRelease=WaitForSingleObject(done.value,200)==WAIT_OBJECT_0;const auto elapsed=GetTickCount64()-began;
    SetEvent(release.value);holder.join();caller.join();
    measurements.push_back({{"kind",state?"retired-state-mutex-busy":"retirement-list-mutex-busy"},{"finishedBeforeFixtureRelease",beforeRelease},{"elapsedMs",elapsed},{"queueAfterRelease",Access::snapshot()}});
    need(acquired&&attached&&beforeRelease,"retained-cleanup-blocked-current-attach-before-release");need(next.stop(200),"retained-busy-current-stop");
    Reader final;need(final.attach(readPipe(owner))&&Access::snapshot().empty()&&final.stop(200),"retained-busy-later-cleanup");
}
inline void pending(){
    Owner owner;need(bool(owner.start()),"retained-pending-owner-start");
    {
        Reader pending;need(pending.attach(readPipe(owner))&&pending.snapshot().pendingIo==1,"retained-real-pending-read-missing");
        Access::parkPending(pending); // Explicit retirement handoff input, not a natural CancelIoEx refusal.
        Reader next;need(next.attach(readPipe(owner)),"retained-pending-next-attach");const auto held=Access::snapshot();
        measurements.push_back({{"kind","actual-pending-io-retirement-handoff"},{"held",held}});
        need(held.size()==1&&held[0]["pending"]==true&&held[0]["handles"]==2,"inflight-overlapped-storage-was-released");
        need(next.stop(200),"retained-pending-next-stop");
    }
    need(owner.stop().fullyDestroyed,"retained-pending-owner-stop");
    Owner fresh;need(bool(fresh.start()),"retained-completed-owner-start");Reader next;
    need(next.attach(readPipe(fresh))&&Access::snapshot().empty(),"late-real-io-completion-not-reaped");
    need(next.stop(200)&&fresh.stop().fullyDestroyed,"retained-late-completion-resources");
    measurements.push_back({{"kind","peer-exit-completes-original-overlapped"},{"queue",Access::snapshot()},{"freshOwner",fresh.snapshot().fullyDestroyed}});
}
inline Json run(){
    need(Access::snapshot().empty(),"retained-reader-original-state-not-empty");
    {Owner owner;need(bool(owner.start()),"retained-reader-owner-start");transient(owner);fairness(owner);busy(owner,false);busy(owner,true);need(owner.stop().fullyDestroyed,"retained-reader-owner-return");}
    pending();need(Access::snapshot().empty(),"retained-reader-final-queue-not-empty");return {{"completed",true},{"measurements",measurements}};
}
}

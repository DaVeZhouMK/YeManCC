    struct HistoricalStartupRetained {
        const TrackerSteamReceipt steam;
        const TrackerOwnerCurrent retainedScopeCurrent;
        const std::shared_ptr<std::atomic<bool>> epoch,retained=std::make_shared<std::atomic<bool>>(true);
        const std::shared_ptr<const StartupParentObjectEvidence> pending;
        std::shared_ptr<const HistoricalExitedParentEvidence> historical;
        const ULONGLONG capturedAt=GetTickCount64();
        std::atomic<bool> sourceBound{false};
        std::uint64_t boundGameId=0;
        unsigned boundLine=0;
        std::string boundExact;
        HistoricalStartupRetained(TrackerSteamReceipt s,std::shared_ptr<std::atomic<bool>> e,std::shared_ptr<const StartupParentObjectEvidence> h,TrackerOwnerCurrent scope)
            :steam(std::move(s)),retainedScopeCurrent(std::move(scope)),epoch(std::move(e)),pending(std::move(h)){}
        bool current()const noexcept{return epoch&&epoch->load(std::memory_order_acquire)&&retained->load(std::memory_order_acquire)&&pending&&pending->childCurrent()&&(sourceBound.load(std::memory_order_acquire)||GetTickCount64()-capturedAt<15000);}
    };
    std::vector<std::shared_ptr<HistoricalStartupRetained>> historicalStartupParents_;
    static bool historicalSourceNativePath(const std::string& path,const std::wstring& expected){
        auto value=group_detail::utf16(path);
        if(value.rfind(L"\\\\?\\",0)==0)value.erase(0,4);
        if(value.size()<3||value[1]!=L':'||(value[2]!=L'\\'&&value[2]!=L'/')||value.find(L'\0')!=std::wstring::npos)return false;
        const wchar_t drive[]{value[0],L':',L'\0'};wchar_t target[32768]{};
        const auto count=QueryDosDeviceW(drive,target,static_cast<DWORD>(std::size(target)));
        if(!count)return false;
        std::wstring native=target;native+=value.substr(2);std::replace(native.begin(),native.end(),L'/',L'\\');
        return _wcsicmp(native.c_str(),expected.c_str())==0;
    }
public:
    // Event-only, at most four exact object pairs on the existing source owner.
    // No target election or GameID is available at this early capture boundary.
    bool captureHistoricalWindowStartup(const SteamLogSourceIdentity& key,ymcc::steamsession::WindowProcessCache& cache,
        DWORD pid,DWORD event,const TrackerOwnerCurrent& admit,const TrackerOwnerCurrent& retainedCurrent){
        startupdiagnostic::Scope trace(pid,startupdiagnostic::Stage::Retain,"retain-context-unavailable");
        try{
            if(!tracker_detail::ownerCurrent(retainedCurrent)||!key||event!=EVENT_OBJECT_CREATE||!MainTrackerFactsAdapter::sourceCurrent(*key)||!startupContextCurrent(key->facts(),admit))return false;
            const auto generation=startupReadGeneration();if(!generation)return false;
            const auto epoch=startupEpoch(key->facts(),generation);if(!epoch)return false;
            // Capacity is decided before any new child duplication/parent open.
            // Move retired owners out of the mutex before their HANDLE release.
            std::array<std::shared_ptr<HistoricalStartupRetained>,4> scopeSnapshot{},expiredScopes{};
            std::size_t scopeCount=0,expiredCount=0;
            {std::lock_guard lock(startupMutex_);
                if(startupReadGeneration_!=generation||startupEpoch_!=epoch||!epoch->load(std::memory_order_acquire))return false;
                for(const auto& entry:historicalStartupParents_){if(scopeCount>=scopeSnapshot.size())return false;scopeSnapshot[scopeCount++]=entry;}
            }
            // Caller predicates are value-owned pure scope checks and are never
            // invoked under the source ledger mutex, including expired cleanup.
            for(std::size_t i=0;i<scopeCount;++i)if(!tracker_detail::ownerCurrent(scopeSnapshot[i]->retainedScopeCurrent))expiredScopes[expiredCount++]=scopeSnapshot[i];
            std::vector<std::shared_ptr<HistoricalStartupRetained>> capacityRetired;capacityRetired.reserve(4);
            {std::lock_guard lock(startupMutex_);
                if(startupReadGeneration_!=generation||startupEpoch_!=epoch||!epoch->load(std::memory_order_acquire))return false;
                for(auto it=historicalStartupParents_.begin();it!=historicalStartupParents_.end();){
                    if(!(*it)->current()||std::find(expiredScopes.begin(),expiredScopes.begin()+expiredCount,*it)!=expiredScopes.begin()+expiredCount){(*it)->retained->store(false,std::memory_order_release);capacityRetired.push_back(std::move(*it));it=historicalStartupParents_.erase(it);}else ++it;
                }
                if(historicalStartupParents_.size()>=4){trace.reached("retain-capacity-before-open");return false;}
            }
            trace.reached("retain-parent-object-unavailable");
            const auto historical=processParentEvidenceOwner().captureStartupWindowParent(cache,pid,event);
            if(!historical||historical->windowsSession()!=key->facts().windowsSession||historical->parentCreated()<key->facts().birth||
                !historical->childCurrent()||!epoch->load(std::memory_order_acquire)||!startupContextCurrent(key->facts(),admit)||!MainTrackerFactsAdapter::sourceCurrent(*key))return false;
            auto candidate=std::make_shared<HistoricalStartupRetained>(key->facts(),epoch,historical,retainedCurrent);
            std::vector<std::shared_ptr<HistoricalStartupRetained>> retired;
            bool accepted=false;
            {std::lock_guard lock(startupMutex_);
                if(startupReadGeneration_!=generation||startupEpoch_!=epoch||!epoch->load(std::memory_order_acquire))return false;
                for(auto it=historicalStartupParents_.begin();it!=historicalStartupParents_.end();){
                    if(!(*it)->current()){(*it)->retained->store(false,std::memory_order_release);retired.push_back(std::move(*it));it=historicalStartupParents_.erase(it);}else ++it;
                }
                const auto& child=historical->child()->witness();
                for(const auto& entry:historicalStartupParents_){const auto& old=entry->pending->child()->witness();if(old.pid==child.pid&&old.created==child.created)return false;}
                if(historicalStartupParents_.size()<4){historicalStartupParents_.push_back(std::move(candidate));accepted=true;}
            }
            trace.reached(accepted?"retain-parent-object-accepted":"retain-raced-capacity");return accepted;
        }catch(...){return false;}
    }
private:
    std::shared_ptr<const HistoricalStartupHandoffEvidence> historicalStartupHandoffFromSource(const SteamLogSourceIdentity& key,
        const std::shared_ptr<ProcessLease>& selected,const TrackerOwnerCurrent& admit,
        const std::shared_ptr<const SteamLogSourceView>& source,std::uint64_t generation){
        startupdiagnostic::Scope trace(selected?selected->gameWitness().pid:0,startupdiagnostic::Stage::Join,"join-source-or-context-unavailable");
        try{
            std::unique_lock capture(startupCaptureMutex_,std::try_to_lock);
            if(!capture.owns_lock()||!key||!selected)return {};
            const auto steam=key->facts();
            const auto current=[&]{return selected->processCurrent()&&selected->accountEpoch()==steam.accountEpoch&&selected->steamWitness().pid==steam.pid&&
                selected->steamWitness().created==steam.birth&&selected->windowsSession()==steam.windowsSession&&samePath(selected->steamRoot(),steam.root)&&
                MainTrackerFactsAdapter::sourceCurrent(*key)&&startupContextCurrent(steam,admit);};
            if(!current()||!source||source->steam!=steam)return {};
            std::shared_ptr<std::atomic<bool>> epoch;std::vector<std::shared_ptr<HistoricalStartupRetained>> entries;
            {std::lock_guard lock(startupMutex_);if(!generation||generation!=startupReadGeneration_||startupSteam_!=steam||!startupEpoch_||!startupEpoch_->load()||
                !source->observation.sourceRevision||source->observation.sourceRevision<startupSourceFrontier_)return {};
                startupSourceFrontier_=source->observation.sourceRevision;epoch=startupEpoch_;entries=historicalStartupParents_;}
            if(entries.empty()){trace.reached("join-no-retained-parent");return {};}
            if(!source->complete||source->diagnosticOnly||source->kind!=SteamLogKind::GameProcess||source->policy!=SteamLogReadPolicy::CompleteOnly||
                !source->runningInventory.complete||!source->lifetimeInventory.complete){refuseIncompleteStartupSource(epoch,generation,*source);return {};}
            std::shared_ptr<const HistoricalStartupHandoffEvidence> result;trace.reached("join-selected-child-not-retained");
            for(const auto& entry:entries){
                if(entry->steam!=steam||entry->epoch!=epoch||!entry->current()||!tracker_detail::ownerCurrent(entry->retainedScopeCurrent))continue;
                const auto& child=entry->pending->child()->witness();const auto& game=selected->gameWitness();
                if(child.pid!=game.pid||child.created!=game.created||!samePath(child.image,game.image))continue;
                const auto& parent=entry->pending;trace.reached("join-parent-source-row-missing");
                std::optional<ymcc::sleepsteam::TrackedProcessRow> matched;
                bool conflict=false;unsigned line=0;
                for(const auto& text:source->lines){
                    ++line;
                    if(text.size()<22||text.compare(22,6,"AppID ")!=0||text.find(" adding PID ",22)==std::string::npos)continue;
                    const auto rows=ymcc::sleepsteam::trackedProcessInventory(std::vector<std::string>{text},ymcc::sleepsteam::TrackingScope::ProcessLifetime);
                    for(const auto& row:rows.rows){
                        if(row.pid!=parent->parentPid()||!group_detail::launchWindow(parent->parentCreated(),row.at))continue;
                        if(row.identityConflict||!row.fullGameIdKnown||!exactGameId(row.appid,row.fullGameId)||
                            !historicalSourceNativePath(row.path,parent->parentNativeImage())){conflict=true;continue;}
                        if(matched){conflict=true;continue;}matched=row;matched->sourceLine=line;
                    }
                }
                if(conflict||!matched){if(conflict)trace.reached("join-parent-source-conflict");if(conflict||entry->boundLine)entry->retained->store(false,std::memory_order_release);continue;}
                const auto earliest=group_detail::localSecond(parent->parentCreated()>20000000ULL?parent->parentCreated()-20000000ULL:0);
                const auto latest=group_detail::localSecond(parent->parentCreated()<=(std::numeric_limits<std::uint64_t>::max)()-30000000ULL?parent->parentCreated()+30000000ULL:parent->parentCreated());
                const auto binding=source->binding(parent->parentPid(),matched->path,earliest,latest,ymcc::sleepsteam::TrackingScope::ProcessLifetime);
                if(binding.status!="tracked"&&binding.status!="removed"){entry->retained->store(false,std::memory_order_release);continue;}
                const auto& exact=source->lines[matched->sourceLine-1];
                {std::lock_guard lock(startupMutex_);
                    if(generation!=startupReadGeneration_||epoch!=startupEpoch_||!entry->current()||source->observation.sourceRevision<startupSourceFrontier_)return {};
                    if(entry->boundLine&&(entry->boundLine!=matched->sourceLine||entry->boundGameId!=matched->fullGameId||entry->boundExact!=exact)){entry->retained->store(false,std::memory_order_release);continue;}
                    entry->boundLine=matched->sourceLine;entry->boundGameId=matched->fullGameId;entry->boundExact=exact;entry->sourceBound.store(true,std::memory_order_release);
                }
                // Complete source + actual selected child can retain this pending
                // object across slow initialization, without issuing exited proof.
                if(!entry->historical)entry->historical=processParentEvidenceOwner().completeStartupWindowParent(entry->pending);
                const auto& history=entry->historical;if(!history){trace.reached("join-source-bound-parent-pending");continue;}
                if(result)return {};
                result=std::shared_ptr<const HistoricalStartupHandoffEvidence>(new HistoricalStartupHandoffEvidence(history,steam,entry->retainedScopeCurrent,epoch,entry->retained,
                    source->observation.sourceRevision,matched->fullGameId,matched->appid,matched->sourceLine,matched->path,exact,binding.status=="removed"));
            }
            if(current()&&epoch->load(std::memory_order_acquire)&&result&&result->retainedChildCurrent()){trace.reached("join-complete-history-proof");return result;}return {};
        }catch(...){return {};}
    }
public:
    // Explicit original source demand, identical source owner and selection
    // lease. Fresh producer composition above reuses its existing bytes instead.
    std::shared_ptr<const HistoricalStartupHandoffEvidence> observeHistoricalStartupHandoff(const SteamLogSourceIdentity& key,
        const std::shared_ptr<ProcessLease>& selected,const TrackerOwnerCurrent& admit){
        if(!key||!selected||!selected->processCurrent()||!startupContextCurrent(key->facts(),admit))return {};
        {std::lock_guard lock(startupMutex_);if(startupSteam_!=key->facts()||historicalStartupParents_.empty())return {};}
        const auto generation=startupReadGeneration();if(!generation)return {};
        const auto current=[&]{return selected->processCurrent()&&MainTrackerFactsAdapter::sourceCurrent(*key)&&startupContextCurrent(key->facts(),admit);};
        const auto source=readLogSource(key,SteamLogKind::GameProcess,SteamLogReadPolicy::CompleteOnly,current,selected);
        return historicalStartupHandoffFromSource(key,selected,admit,source,generation);
    }
    bool historicalStartupHandoffCurrent(const std::shared_ptr<const HistoricalStartupHandoffEvidence>& evidence,
        const std::shared_ptr<ProcessLease>& selected,const TrackerOwnerCurrent& admit)const noexcept{
        try{
            if(!evidence||!selected||!evidence->retainedChildCurrent())return false;
            const auto& child=evidence->historicalParent()->child()->witness();const auto& steam=evidence->steam();
            return selected->processCurrent()&&selected->accountEpoch()==steam.accountEpoch&&child.pid==selected->gameWitness().pid&&
                child.created==selected->gameWitness().created&&samePath(child.image,selected->gameWitness().image)&&
                selected->steamWitness().pid==steam.pid&&selected->steamWitness().created==steam.birth&&
                selected->windowsSession()==steam.windowsSession&&samePath(selected->steamRoot(),steam.root)&&
                startupContextCurrent(steam,admit)&&evidence->retainedChildCurrent();
        }catch(...){return false;}
    }
private:

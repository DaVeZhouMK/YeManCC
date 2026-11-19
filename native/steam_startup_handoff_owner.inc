    // One bounded source ledger belongs to the existing Tracker owner. It
    // retains exact launch objects, never a target election or action slot.
    std::mutex startupCaptureMutex_,startupMutex_;
    TrackerSteamReceipt startupSteam_;
    std::uint64_t startupReadGeneration_=1,startupSourceFrontier_=0;
    std::uint64_t startupReadGeneration(){std::lock_guard lock(startupMutex_);return startupReadGeneration_;}
    void advanceStartupRetirement() noexcept {startupReadGeneration_=!startupReadGeneration_||startupReadGeneration_==(std::numeric_limits<std::uint64_t>::max)()?0:startupReadGeneration_+1;}
    std::shared_ptr<std::atomic<bool>> startupEpoch_;
    std::vector<std::shared_ptr<const StartupLaunchEvidence>> startupLaunches_;
    bool startupContextCurrent(const TrackerSteamReceipt& steam,const TrackerOwnerCurrent& admit) const noexcept {
        try {
            return steam.present&&steam.rootKnown&&steam.windowsSessionKnown&&steam.pid&&steam.birth&&
                steam.processEpoch&&steam.accountEpoch&&!steam.root.empty()&&
                tracker_detail::ownerCurrent(admit)&&
                trackerSteamReceipt(mainReadTailPort().identitySnapshot())==steam;
        }catch(...){return false;}
    }
    std::shared_ptr<std::atomic<bool>> startupEpoch(const TrackerSteamReceipt& steam,std::uint64_t generation) {
        {std::lock_guard lock(startupMutex_);
            if(!generation||startupReadGeneration_!=generation)return {};
            if(startupEpoch_&&startupEpoch_->load(std::memory_order_acquire)&&startupSteam_==steam)return startupEpoch_;}
        auto candidate=std::make_shared<std::atomic<bool>>(true);
        std::vector<std::shared_ptr<const StartupLaunchEvidence>> retired;std::vector<std::shared_ptr<HistoricalStartupRetained>> historicalRetired;
        std::shared_ptr<std::atomic<bool>> epoch;
        {std::lock_guard lock(startupMutex_);
            if(!generation||startupReadGeneration_!=generation)return {};
            if(!startupEpoch_||!startupEpoch_->load(std::memory_order_acquire)||startupSteam_!=steam){
                if(startupEpoch_)startupEpoch_->store(false,std::memory_order_release);
                retired.swap(startupLaunches_);historicalRetired.swap(historicalStartupParents_);startupSteam_=steam;startupEpoch_=std::move(candidate);
            }
            epoch=startupEpoch_;
        }
        return epoch; // Old exact process objects release after startupMutex_.
    }
    void refuseIncompleteStartupSource(const std::shared_ptr<std::atomic<bool>>& epoch,
        std::uint64_t generation,const SteamLogSourceView& source) noexcept {
        try {
            std::vector<std::shared_ptr<const StartupLaunchEvidence>> retired;
            std::vector<std::shared_ptr<HistoricalStartupRetained>> historicalRetired;
            std::array<std::shared_ptr<HistoricalStartupRetained>,4> released{};
            std::size_t releasedCount=0;
            {std::lock_guard lock(startupMutex_);
                const auto revision=source.observation.sourceRevision;
                if(!generation||startupReadGeneration_!=generation||!epoch||startupEpoch_!=epoch||!epoch->load(std::memory_order_acquire)||
                    source.steam!=startupSteam_||(revision&&revision<startupSourceFrontier_))return;
                if(revision>startupSourceFrontier_)startupSourceFrontier_=revision;
                advanceStartupRetirement();
                for(const auto& launch:startupLaunches_)launch->retained_->store(false,std::memory_order_release);
                retired.swap(startupLaunches_);
                // A failed log read revokes all source claims. Unbound Windows
                // objects have no claim yet and still keep their original TTL.
                const bool keepUnbound=startupReadGeneration_&&source.kind==SteamLogKind::GameProcess&&
                    source.policy==SteamLogReadPolicy::CompleteOnly&&!source.diagnosticOnly&&historicalStartupParents_.size()<=released.size();
                if(!keepUnbound){
                    for(const auto& entry:historicalStartupParents_)entry->retained->store(false,std::memory_order_release);
                    historicalRetired.swap(historicalStartupParents_);
                }
                for(auto it=historicalStartupParents_.begin();it!=historicalStartupParents_.end();){
                    const auto& entry=*it;
                    if(!entry->boundLine&&!entry->sourceBound.load(std::memory_order_acquire)&&
                        entry->retained->load(std::memory_order_acquire)){++it;continue;}
                    entry->retained->store(false,std::memory_order_release);
                    released[releasedCount++]=std::move(*it);it=historicalStartupParents_.erase(it);
                }
                if(historicalStartupParents_.empty()){
                    epoch->store(false,std::memory_order_release);startupEpoch_.reset();startupSteam_={};
                }
            }
        }catch(...){}
    }
    static bool startupSourceMatches(const StartupLaunchEvidence& launch,const SteamLogSourceView& source,bool* removed=nullptr) {
        if(removed)*removed=false;
        if(!source.complete||source.diagnosticOnly||!source.lifetimeInventory.complete||!source.runningInventory.complete||
            source.kind!=SteamLogKind::GameProcess||source.policy!=SteamLogReadPolicy::CompleteOnly||
            !launch.line_||launch.line_>source.lines.size()||source.lines[launch.line_-1]!=launch.exactLine_)return false;
        const auto& object=launch.launcher_->witness();
        unsigned rows=0;
        for(const auto& row:source.lifetimeInventory.rows)if(row.pid==object.pid){
            ++rows;
            if(row.identityConflict||!row.fullGameIdKnown||row.appid!=launch.appid_||row.fullGameId!=launch.fullGameId_||
                row.sourceLine!=launch.line_||row.at!=launch.at_||
                ymcc::sleepsteam::pathKey(row.path)!=ymcc::sleepsteam::pathKey(launch.sourcePath_))return false;
        }
        if(rows>1)return false;
        const auto earliest=group_detail::localSecond(object.created>20000000ULL?object.created-20000000ULL:0);
        const auto latest=group_detail::localSecond(object.created<=(std::numeric_limits<std::uint64_t>::max)()-30000000ULL?object.created+30000000ULL:object.created);
        const auto binding=source.binding(object.pid,launch.sourcePath_,earliest,latest,ymcc::sleepsteam::TrackingScope::ProcessLifetime);
        if(binding.status=="tracked")return rows==1&&binding.appid==launch.appid_&&binding.line==launch.line_&&binding.at==launch.at_&&binding.exactPath;
        if(binding.status!="removed"||rows)return false;
        if(removed)*removed=true;
        return true; // Explicit historical removal, never current tracked health.
    }
public:
    void retireStartupLaunches() noexcept {
        try {
            std::vector<std::shared_ptr<const StartupLaunchEvidence>> retired;std::vector<std::shared_ptr<HistoricalStartupRetained>> historicalRetired;
            {std::lock_guard lock(startupMutex_);
                advanceStartupRetirement();if(startupEpoch_)startupEpoch_->store(false,std::memory_order_release);
                retired.swap(startupLaunches_);historicalRetired.swap(historicalStartupParents_);startupEpoch_.reset();startupSteam_={};}
        }catch(...){}
    }
private:
    StartupLaunchCapture captureStartupFromSource(const SteamLogSourceIdentity& key,const TrackerOwnerCurrent& admit,const std::shared_ptr<const SteamLogSourceView>& source,std::uint64_t generation) {
        StartupLaunchCapture result;
        try {
            std::unique_lock capture(startupCaptureMutex_,std::try_to_lock);
            if(!capture.owns_lock()){result.status=StartupLaunchCapture::Status::Busy;return result;}
            if(!key||!MainTrackerFactsAdapter::sourceCurrent(*key)||!startupContextCurrent(key->facts(),admit))return result;
            const auto steam=key->facts();const auto epoch=startupEpoch(steam,generation);
            if(!epoch){result.status=StartupLaunchCapture::Status::ContextChanged;return result;}
            const auto current=[&]{return epoch->load(std::memory_order_acquire)&&MainTrackerFactsAdapter::sourceCurrent(*key)&&startupContextCurrent(steam,admit);};
            if(!source||source->steam!=steam)return result;
            if(!current()){result.status=StartupLaunchCapture::Status::ContextChanged;return result;}
            if(!source->complete||source->diagnosticOnly||!source->runningInventory.complete||!source->lifetimeInventory.complete){refuseIncompleteStartupSource(epoch,generation,*source);result.status=StartupLaunchCapture::Status::SourceIncomplete;return result;}
            result.sourceRevision=source->observation.sourceRevision;
            std::vector<std::shared_ptr<const StartupLaunchEvidence>> previous,next;
            {std::lock_guard lock(startupMutex_);if(startupReadGeneration_!=generation||!source->observation.sourceRevision||source->observation.sourceRevision<startupSourceFrontier_||startupEpoch_!=epoch||!epoch->load()){result.status=StartupLaunchCapture::Status::ContextChanged;return result;}startupSourceFrontier_=source->observation.sourceRevision;previous=startupLaunches_;}
            next.reserve(32);
            for(const auto& old:previous)if(old->sourceEpochRetained()&&startupSourceMatches(*old,*source))next.push_back(old);
            for(const auto& row:source->runningInventory.rows){
                ++result.considered;
                if(!current()){result.status=StartupLaunchCapture::Status::ContextChanged;return result;}
                if(!row.pid||row.pid==steam.pid||row.identityConflict||!row.fullGameIdKnown||!exactGameId(row.appid,row.fullGameId)||
                    !row.sourceLine||row.sourceLine>source->lines.size())continue;
                unsigned count=0;for(const auto& other:source->lifetimeInventory.rows)if(other.pid==row.pid)++count;
                if(count!=1)continue;
                const auto reused=std::find_if(next.begin(),next.end(),[&](const auto& old){return old->launcher_->witness().pid==row.pid&&
                    old->appid_==row.appid&&old->fullGameId_==row.fullGameId&&old->line_==row.sourceLine&&old->at_==row.at&&
                    ymcc::sleepsteam::pathKey(old->sourcePath_)==ymcc::sleepsteam::pathKey(row.path);});
                if(reused!=next.end()){++result.reused;continue;}
                if(next.size()>=32){result.status=StartupLaunchCapture::Status::Capacity;return result;}
                detail::Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,row.pid));
                const auto object=processParentEvidenceOwner().capture(process.value);
                const auto path=group_detail::utf16(row.path);
                if(!object||path.empty()||!samePath(object->witness().image,path)||object->windowsSession()!=steam.windowsSession||
                    object->witness().created<steam.birth||!group_detail::launchWindow(object->witness().created,row.at)||!object->stillAlive())continue;
                auto launch=std::shared_ptr<const StartupLaunchEvidence>(new StartupLaunchEvidence(steam,epoch,object,
                    source->observation.sourceRevision,row.appid,row.fullGameId,row.sourceLine,row.at,row.path,source->lines[row.sourceLine-1]));
                if(!startupSourceMatches(*launch,*source)||!current()){result.status=StartupLaunchCapture::Status::ContextChanged;return result;}
                next.push_back(std::move(launch));++result.captured;
            }
            if(!current()){result.status=StartupLaunchCapture::Status::ContextChanged;return result;}
            result.retained=static_cast<unsigned>(next.size());
            {std::lock_guard lock(startupMutex_);
                if(startupReadGeneration_!=generation||source->observation.sourceRevision<startupSourceFrontier_||startupEpoch_!=epoch||!epoch->load(std::memory_order_acquire)){result.status=StartupLaunchCapture::Status::ContextChanged;return result;}
                for(const auto& old:startupLaunches_)if(std::find(next.begin(),next.end(),old)==next.end())old->retained_->store(false,std::memory_order_release);
                startupLaunches_.swap(next);
            }
            result.status=current()?StartupLaunchCapture::Status::Observed:StartupLaunchCapture::Status::ContextChanged;
            return result;
        }catch(...){return result;}
    }
public:
    StartupLaunchCapture captureStartupLaunches(const SteamLogSourceIdentity& key,const TrackerOwnerCurrent& admit) {
        StartupLaunchCapture refused;
        if(!key||!MainTrackerFactsAdapter::sourceCurrent(*key)||!startupContextCurrent(key->facts(),admit))return refused;
        const auto current=[&]{return MainTrackerFactsAdapter::sourceCurrent(*key)&&startupContextCurrent(key->facts(),admit);};
        const auto generation=startupReadGeneration();if(!generation)return refused;
        const auto source=readLogSource(key,SteamLogKind::GameProcess,SteamLogReadPolicy::CompleteOnly,current);
        return captureStartupFromSource(key,admit,source,generation);
    }
private:
    std::shared_ptr<const StartupHandoffEvidence> startupHandoffFromSource(const SteamLogSourceIdentity& key,
        const std::shared_ptr<ProcessLease>& selected,const TrackerOwnerCurrent& admit,const std::shared_ptr<const SteamLogSourceView>& source,std::uint64_t generation) {
        try {
            std::unique_lock capture(startupCaptureMutex_,std::try_to_lock);
            if(!capture.owns_lock()||!key||!selected||!MainTrackerFactsAdapter::sourceCurrent(*key))return {};
            const auto steam=key->facts();
            const auto current=[&]{return selected->processCurrent()&&selected->accountEpoch()==steam.accountEpoch&&
                selected->steamWitness().pid==steam.pid&&selected->steamWitness().created==steam.birth&&
                selected->windowsSession()==steam.windowsSession&&samePath(selected->steamRoot(),steam.root)&&
                MainTrackerFactsAdapter::sourceCurrent(*key)&&startupContextCurrent(steam,admit);};
            if(!current())return {};
            std::shared_ptr<std::atomic<bool>> epoch;std::vector<std::shared_ptr<const StartupLaunchEvidence>> launches;
            {std::lock_guard lock(startupMutex_);if(!generation||startupReadGeneration_!=generation||!source||!source->observation.sourceRevision||source->observation.sourceRevision<startupSourceFrontier_||startupSteam_!=steam||!startupEpoch_||!startupEpoch_->load(std::memory_order_acquire))return {};startupSourceFrontier_=source->observation.sourceRevision;epoch=startupEpoch_;launches=startupLaunches_;}
            if(launches.empty())return {}; // No extra source read without retained original evidence.
            const auto child=processParentEvidenceOwner().captureGame(selected);
            if(!child||!current()||!epoch->load(std::memory_order_acquire))return {};
            if(!source||source->steam!=steam)return {};
            if(!current()||!epoch->load(std::memory_order_acquire))return {};
            if(!source->complete||source->diagnosticOnly||!source->runningInventory.complete||!source->lifetimeInventory.complete){refuseIncompleteStartupSource(epoch,generation,*source);return {};}
            std::shared_ptr<const StartupHandoffEvidence> result;
            for(const auto& launch:launches){
                bool removed=false;
                if(launch->epoch_!=epoch||!launch->sourceEpochRetained()||source->observation.sourceRevision<launch->sourceRevision_)continue;
                if(!startupSourceMatches(*launch,*source,&removed)){launch->retained_->store(false,std::memory_order_release);continue;}
                const auto relation=processParentEvidenceOwner().relate(child,launch->launcher_);
                if(!relation)continue;
                if(result)return {}; // More than one source relation never elects an identity.
                result=std::shared_ptr<const StartupHandoffEvidence>(new StartupHandoffEvidence(launch,relation,source->observation.sourceRevision,removed));
            }
            return current()&&epoch->load(std::memory_order_acquire)&&(!result||result->retainedChildCurrent())?result:std::shared_ptr<const StartupHandoffEvidence>{};
        }catch(...){return {};}
    }
public:
    std::shared_ptr<const StartupHandoffEvidence> observeStartupHandoff(const SteamLogSourceIdentity& key,
        const std::shared_ptr<ProcessLease>& selected,const TrackerOwnerCurrent& admit) {
        if(!key||!selected||!selected->processCurrent()||!startupContextCurrent(key->facts(),admit))return {};
        {std::lock_guard lock(startupMutex_);if(startupSteam_!=key->facts()||startupLaunches_.empty())return {};}
        const auto current=[&]{return selected->processCurrent()&&startupContextCurrent(key->facts(),admit);};
        const auto generation=startupReadGeneration();if(!generation)return {};
        const auto source=readLogSource(key,SteamLogKind::GameProcess,SteamLogReadPolicy::CompleteOnly,current,selected);
        return startupHandoffFromSource(key,selected,admit,source,generation);
    }
    bool startupHandoffCurrent(const std::shared_ptr<const StartupHandoffEvidence>& evidence,
        const std::shared_ptr<ProcessLease>& selected,const TrackerOwnerCurrent& admit) const noexcept {
        try {
            if(!evidence||!selected||!evidence->retainedChildCurrent())return false;
            const auto& child=evidence->relation_->child()->witness();const auto& steam=evidence->launch_->steam_;
            return child.pid==selected->gameWitness().pid&&child.created==selected->gameWitness().created&&
                samePath(child.image,selected->gameWitness().image)&&selected->processCurrent()&&selected->accountEpoch()==steam.accountEpoch&&
                selected->steamWitness().pid==steam.pid&&selected->steamWitness().created==steam.birth&&
                selected->windowsSession()==steam.windowsSession&&samePath(selected->steamRoot(),steam.root)&&
                startupContextCurrent(steam,admit)&&evidence->retainedChildCurrent();
        }catch(...){return false;}
    }
private:

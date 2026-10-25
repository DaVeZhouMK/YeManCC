#pragma once
// Included inside screenpads. UI-thread-only WinEvent observation; no input hook,
// injection, device command, worker or timer. Periodic cursor movement is separate.
namespace cursorvisibility {
inline constexpr UINT kMessage=WM_APP+180;
inline HWINEVENTHOOK hook=nullptr,retiredHook=nullptr;
inline DWORD retiredOwnerThread=0;
inline HWND target=nullptr;
inline DWORD ownerThread=0,startedTick=0,lastError=0,lastEvent=0,lastEventTick=0,lastUnhookError=0;
inline ULONG_PTR generation=0;
inline bool attempted=false,pending=false;
inline unsigned long long notifications=0,coalesced=0,stale=0,posts=0,unhookFailures=0;
inline unsigned long long queries=0,edgeUpdates=0,unchanged=0,positionDeferrals=0,hiddenQueryDeferrals=0;
// Source thread IDs come from the event payload, not this out-of-context
// callback's current thread. Keep them separate from later cursor queries.
// Resolve only the first occurrence of up to eight source TIDs per hook
// generation, on the existing dispatch message (never in this callback/MOVE).
// A near-delivery owner observation is not the original cursor API caller;
// event delivery/dispatch can be delayed and a TID can exit or be reused.
struct SourceOwnerObservation {
    unsigned long long sequence=0;ULONG_PTR generation=0;
    DWORD sourceThread=0,event=0,eventTick=0,observedTick=0,resolvedTick=0,processId=0,error=0,timesError=0;
    unsigned long long threadCreated=0,threadExited=0;
    bool queried=false,timesKnown=false;
};
struct SourceOwnerSlot {DWORD thread=0;unsigned long long sequence=0;};
inline std::array<SourceOwnerObservation,16> sourceOwners{};
inline std::array<SourceOwnerSlot,8> sourceOwnerCache{};
inline unsigned long long sourceOwnerCount=0,sourceOwnerLimitHits=0;
inline size_t sourceOwnerCacheCount=0;
inline ULONG_PTR sourceOwnerCacheGeneration=0;
inline bool sourceOwnerQueriesPending=false;
inline unsigned long long observeSourceOwner(DWORD sourceThread,DWORD event,DWORD tick) {
    if(!sourceThread)return 0;
    if(sourceOwnerCacheGeneration!=generation) {
        sourceOwnerCacheGeneration=generation;sourceOwnerCacheCount=0;sourceOwnerQueriesPending=false;
    }
    for(size_t i=0;i<sourceOwnerCacheCount;++i)if(sourceOwnerCache[i].thread==sourceThread)return sourceOwnerCache[i].sequence;
    if(sourceOwnerCacheCount==sourceOwnerCache.size()){++sourceOwnerLimitHits;return 0;}
    const auto sequence=++sourceOwnerCount;
    auto& item=sourceOwners[(sequence-1)%sourceOwners.size()];item=SourceOwnerObservation{};
    item.sequence=sequence;item.generation=generation;item.sourceThread=sourceThread;
    item.event=event;item.eventTick=tick;item.observedTick=GetTickCount();
    sourceOwnerCache[sourceOwnerCacheCount++]={sourceThread,sequence};sourceOwnerQueriesPending=true;
    return sequence;
}
inline void resolveSourceOwners() {
    if(!sourceOwnerQueriesPending)return;
    sourceOwnerQueriesPending=false;
    const DWORD savedError=GetLastError(),resolvedTick=GetTickCount();
    for(auto& item:sourceOwners) {
        if(!item.sequence || item.generation!=generation || item.queried)continue;
        item.queried=true;item.resolvedTick=resolvedTick;
        SetLastError(ERROR_SUCCESS);
        HANDLE thread=OpenThread(THREAD_QUERY_LIMITED_INFORMATION,FALSE,item.sourceThread);
        if(!thread){item.error=GetLastError();continue;}
        item.processId=GetProcessIdOfThread(thread);
        if(!item.processId)item.error=GetLastError();
        FILETIME created{},exited{},kernel{},user{};SetLastError(ERROR_SUCCESS);
        item.timesKnown=GetThreadTimes(thread,&created,&exited,&kernel,&user)!=FALSE;
        if(!item.timesKnown)item.timesError=GetLastError();
        item.threadCreated=(static_cast<unsigned long long>(created.dwHighDateTime)<<32)|created.dwLowDateTime;
        item.threadExited=(static_cast<unsigned long long>(exited.dwHighDateTime)<<32)|exited.dwLowDateTime;
        CloseHandle(thread);
    }
    SetLastError(savedError);
}
inline Json sourceOwnerState() {
    Json entries=Json::array();const auto begin=sourceOwnerCount>sourceOwners.size()?sourceOwnerCount-sourceOwners.size():0;
    for(auto i=begin;i<sourceOwnerCount;++i){const auto& item=sourceOwners[i%sourceOwners.size()];
        entries.push_back({{"sequence",item.sequence},{"generation",item.generation},{"sourceThreadId",item.sourceThread},
            {"firstEvent",item.event},{"firstEventTick32",item.eventTick},{"firstObservedTick32",item.observedTick},
            {"queryAttempted",item.queried},{"queriedAtTick32",item.resolvedTick},
            {"eventToObservationMs",static_cast<DWORD>(item.observedTick-item.eventTick)},
            {"observationToQueryMs",item.queried?Json(static_cast<DWORD>(item.resolvedTick-item.observedTick)):Json(nullptr)},
            {"processIdAtQuery",item.processId},{"error",item.error},{"threadTimesError",item.timesError},
            {"threadCreationTimeKnown",item.timesKnown},{"threadCreationFileTime",std::to_string(item.threadCreated)},{"threadExitFileTime",std::to_string(item.threadExited)}});}
    return {{"version",1},{"capacity",sourceOwners.size()},{"total",sourceOwnerCount},{"perGenerationThreadLimit",sourceOwnerCache.size()},
        {"limitHits",sourceOwnerLimitHits},{"queriesPending",sourceOwnerQueriesPending},{"entries",entries},
        {"historicalEventProcessProven",false},{"cursorApiCallerProven",false},
        {"scope","First source-TID delivery per hook generation, owner queried on existing dispatch. TID reuse/delivery delay remain possible; later events referencing this observation do not prove unchanged ownership."}};
}
struct EventTrace {DWORD event=0,sourceThread=0,observerThread=0,tick=0;ULONG_PTR generation=0;unsigned long long sourceOwnerSequence=0;};
inline std::array<EventTrace,64> eventTrace{};
inline unsigned long long eventTraceCount=0;
inline Json eventTraceState() {
    Json entries=Json::array();const auto begin=eventTraceCount>eventTrace.size()?eventTraceCount-eventTrace.size():0;
    for(auto i=begin;i<eventTraceCount;++i){const auto& e=eventTrace[i%eventTrace.size()];
        entries.push_back({{"sequence",i+1},{"event",e.event},{"sourceThreadId",e.sourceThread},{"observerThreadId",e.observerThread},{"sourceOwnerObservationSequence",e.sourceOwnerSequence},
            {"eventTick32",e.tick},{"generation",e.generation},{"sourceIsObserverThread",e.sourceThread!=0&&e.sourceThread==e.observerThread}});}
    return {{"version",2},{"capacity",eventTrace.size()},{"total",eventTraceCount},{"entries",entries},
        {"scope","WinEvent payload thread only; not hidden-API caller identity or a binding to later GetCursorInfo samples"}};
}
// Explicit log export only. Resolve at most eight distinct event TIDs after
// capturing state; never open thread handles on callbacks or ordinary state().
// TIDs can exit/reuse, so this is current ownership, not historical identity.
inline Json eventThreadOwnersAtExport(const Json& capturedTrace) {
    std::array<DWORD,8> tids{};size_t count=0;unsigned omitted=0;
    Json owners=Json::array();const auto tick=GetTickCount64();
    if(capturedTrace.contains("entries"))for(const auto& event:capturedTrace["entries"]) {
        const DWORD tid=event.value("sourceThreadId",0u);if(!tid)continue;
        bool known=false;for(size_t i=0;i<count;++i)if(tids[i]==tid){known=true;break;}
        if(known)continue;if(count==tids.size()){++omitted;continue;}tids[count++]=tid;
        DWORD pid=0,error=0;FILETIME created{},exited{},kernel{},user{};bool timesKnown=false;
        HANDLE thread=OpenThread(THREAD_QUERY_LIMITED_INFORMATION,FALSE,tid);
        if(thread){pid=GetProcessIdOfThread(thread);if(!pid)error=GetLastError();
            timesKnown=GetThreadTimes(thread,&created,&exited,&kernel,&user)!=FALSE;CloseHandle(thread);
        }else error=GetLastError();
        const unsigned long long creation=(static_cast<unsigned long long>(created.dwHighDateTime)<<32)|created.dwLowDateTime;
        owners.push_back({{"threadId",tid},{"resolved",pid!=0},{"processIdAtExport",pid},{"sameProcessAtExport",pid!=0&&pid==GetCurrentProcessId()},
            {"error",error},{"threadCreationTimeKnown",timesKnown},{"threadCreationFileTime",creation}});
    }
    return {{"version",1},{"lateResolution",true},{"historicalEventProcessProven",false},{"resolvedAtTick",tick},
        {"uniqueThreadLimit",tids.size()},{"omittedUnresolvedEntries",omitted},{"owners",owners},
        {"scope","Current limited thread query at explicit export; exited/reused TIDs may not identify the historical event producer or cursor API caller"}};
}
inline void CALLBACK onEvent(HWINEVENTHOOK observed,DWORD event,HWND,LONG object,LONG child,DWORD sourceThread,DWORD tick) {
    // Cursor is a global accessibility object without an associated HWND.
    // Our layered windows raise OBJID_WINDOW notifications, not OBJID_CURSOR.
    if(observed!=hook || !hook || GetCurrentThreadId()!=ownerThread ||
       object!=OBJID_CURSOR || child!=0 || (event!=EVENT_OBJECT_SHOW && event!=EVENT_OBJECT_HIDE))return;
    if(static_cast<LONG>(tick-startedTick)<0){++stale;return;}
    ++notifications;lastEvent=event;lastEventTick=tick;
    const auto sourceOwnerSequence=observeSourceOwner(sourceThread,event,tick);
    eventTrace[eventTraceCount%eventTrace.size()]={event,sourceThread,ownerThread,tick,generation,sourceOwnerSequence};++eventTraceCount;
    if(pending){++coalesced;return;}
    pending=true;
    if(target && PostMessageW(target,kMessage,(WPARAM)generation,0)){++posts;return;}
    lastError=GetLastError();pending=false;
}
inline bool releaseRetiredHook() {
    if(!retiredHook)return true;
    if(GetCurrentThreadId()!=retiredOwnerThread){lastUnhookError=ERROR_INVALID_THREAD_ID;++unhookFailures;return false;}
    SetLastError(ERROR_SUCCESS);
    if(!UnhookWinEvent(retiredHook)) {
        lastUnhookError=GetLastError();if(!lastUnhookError)lastUnhookError=ERROR_GEN_FAILURE;
        ++unhookFailures;return false;
    }
    retiredHook=nullptr;retiredOwnerThread=0;lastUnhookError=0;return true;
}
inline void stop() {
    if(!attempted && !hook && !pending && !target)return;
    // UnhookWinEvent must run on its installation thread. Current production
    // callers are UI-owned; reject a future incorrect caller before losing state.
    if(ownerThread && GetCurrentThreadId()!=ownerThread){lastUnhookError=ERROR_INVALID_THREAD_ID;++unhookFailures;return;}
    // Invalidate callbacks/messages before unhooking; never retry input ownership.
    ++generation;pending=false;sourceOwnerQueriesPending=false;target=nullptr;attempted=false;
    if(hook) {
        retiredHook=hook;retiredOwnerThread=ownerThread;hook=nullptr;
        releaseRetiredHook(); // A failed handle remains gated off and available for cleanup.
    }
    ownerThread=0;
}
inline void start(HWND dispatch) {
    if(!dispatch){stop();return;}
    // Keep the first pad window for this session: alternating two-pad MOVE events
    // must not reinstall the observer. Configuration/teardown stops before closing it.
    if(attempted)return; // A failed registration also waits for a new session.
    stop();if(hook)return;
    attempted=true;
    // Retry failed cleanup once on a new session, never on each MOVE, and never
    // install a second observer while the previous one may still be registered.
    if(!releaseRetiredHook()){lastError=lastUnhookError;return;}
    target=dispatch;ownerThread=GetCurrentThreadId();startedTick=GetTickCount();
    ++generation;pending=false;lastError=0;
    SetLastError(ERROR_SUCCESS);
    hook=SetWinEventHook(EVENT_OBJECT_SHOW,EVENT_OBJECT_HIDE,nullptr,onEvent,0,0,WINEVENT_OUTOFCONTEXT);
    if(!hook){lastError=GetLastError();if(!lastError)lastError=ERROR_NOT_SUPPORTED;}
}
inline bool consume(HWND receiver,WPARAM postedGeneration) {
    if(!hook || receiver!=target || postedGeneration!=generation || GetCurrentThreadId()!=ownerThread){++stale;return false;}
    pending=false;resolveSourceOwners();return true;
}
inline Json state() {
    return {{"registered",hook!=nullptr},{"attempted",attempted},{"pending",pending},{"generation",generation},
        {"notifications",notifications},{"coalesced",coalesced},{"stale",stale},{"posts",posts},{"eventTrace",eventTraceState()},{"sourceOwnerObservations",sourceOwnerState()},
        {"queries",queries},{"edgeUpdates",edgeUpdates},{"unchanged",unchanged},{"positionDeferrals",positionDeferrals},{"hiddenQueryDeferrals",hiddenQueryDeferrals},
        {"lastEvent",lastEvent},{"lastEventTick",lastEventTick},{"error",lastError},
        {"unhookFailures",unhookFailures},{"lastUnhookError",lastUnhookError},{"cleanupPending",retiredHook!=nullptr}};
}
} // namespace cursorvisibility

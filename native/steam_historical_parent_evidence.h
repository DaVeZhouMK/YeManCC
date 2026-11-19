#pragma once
#include "steam_process_parent_evidence.h"
#include "steam_window_event_budget.h"
#include <string>
#include <memory>
#include <utility>
namespace ymcc::steamgamefacts {
// Historical object facts only. No Steam/GameID, live group, health or authority.
// No caller-supplied PID, path, birth or exit witness enters this issuer.
// Earliest real window event may precede the parent's exit. Retain the exact
// parent object then; only completeStartupWindowParent can issue exited proof.
class StartupParentObjectEvidence final {
    friend class ProcessParentEvidenceOwner;
    const std::shared_ptr<const ProcessObjectEvidence> child_;
    const std::shared_ptr<detail::Handle> parent_;
    const DWORD parentPid_,windowsSession_,creatorPid_;
    const std::uint64_t born_;
    const std::wstring parentNativeImage_,childNativeImage_;
    StartupParentObjectEvidence(std::shared_ptr<const ProcessObjectEvidence> c,std::shared_ptr<detail::Handle> p,
        DWORD pid,DWORD session,DWORD creator,std::uint64_t born,std::wstring parentImage,std::wstring childImage)
        :child_(std::move(c)),parent_(std::move(p)),parentPid_(pid),windowsSession_(session),creatorPid_(creator),born_(born),
         parentNativeImage_(std::move(parentImage)),childNativeImage_(std::move(childImage)){}
public:
    const std::shared_ptr<const ProcessObjectEvidence>& child()const noexcept{return child_;}
    DWORD windowsSession()const noexcept{return windowsSession_;}
    std::uint64_t parentCreated()const noexcept{return born_;}
    DWORD parentPid()const noexcept{return parentPid_;}
    const std::wstring& parentNativeImage()const noexcept{return parentNativeImage_;}
    bool childCurrent()const noexcept{return GetCurrentProcessId()==creatorPid_&&child_&&child_->stillAlive();}
};
class HistoricalExitedParentEvidence final {
    friend class ProcessParentEvidenceOwner;
    const std::shared_ptr<const ProcessObjectEvidence> child_;
    const std::shared_ptr<detail::Handle> parent_;
    const DWORD parentPid_,windowsSession_,creatorPid_;
    const std::uint64_t born_,exited_;
    const std::wstring parentNativeImage_,childNativeImage_;
    HistoricalExitedParentEvidence(std::shared_ptr<const ProcessObjectEvidence> child,std::shared_ptr<detail::Handle> parent,
        DWORD pid,DWORD session,DWORD creator,std::uint64_t born,std::uint64_t exited,
        std::wstring parentImage,std::wstring childImage) noexcept
        :child_(std::move(child)),parent_(std::move(parent)),parentPid_(pid),windowsSession_(session),creatorPid_(creator),
        born_(born),exited_(exited),parentNativeImage_(std::move(parentImage)),childNativeImage_(std::move(childImage)){}
public:
    HistoricalExitedParentEvidence(const HistoricalExitedParentEvidence&)=delete;
    HistoricalExitedParentEvidence& operator=(const HistoricalExitedParentEvidence&)=delete;
    const std::shared_ptr<const ProcessObjectEvidence>& child()const noexcept{return child_;}
    DWORD parentPid()const noexcept{return parentPid_;}
    DWORD windowsSession()const noexcept{return windowsSession_;}
    std::uint64_t parentCreated()const noexcept{return born_;}
    std::uint64_t parentExitedAt()const noexcept{return exited_;}
    const std::wstring& parentNativeImage()const noexcept{return parentNativeImage_;}
    const std::wstring& childNativeImage()const noexcept{return childNativeImage_;}
    bool parentExited()const noexcept{return GetCurrentProcessId()==creatorPid_&&parent_&&parent_->value&&WaitForSingleObject(parent_->value,0)==WAIT_OBJECT_0;}
    bool childStillAlive()const noexcept{return child_&&child_->stillAlive();}
    bool relationCurrent()const noexcept{return parentExited()&&childStillAlive();}
};
inline std::shared_ptr<const HistoricalExitedParentEvidence> ProcessParentEvidenceOwner::captureHistoricalParent(
    std::shared_ptr<const ProcessObjectEvidence> childInput)const noexcept {
    struct PreserveError {DWORD value=GetLastError();~PreserveError(){SetLastError(value);}} preserve;
    auto child=std::move(childInput); // Retire all object references before restoring LastError.
    try{
        if(!child)return {};
        child=child->weak_from_this().lock();
        const DWORD creator=GetCurrentProcessId();
        if(!child||!creator||child->creatorPid_!=creator||!child->stillAlive())return {};
        const auto parentPid=detail::parent(child->handle_.value);
        if(!parentPid||parentPid==child->witness_.pid)return {};
        detail::Handle parent(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,parentPid));
        if(!parent.value||WaitForSingleObject(parent.value,0)!=WAIT_OBJECT_0||GetProcessId(parent.value)!=parentPid)return {};
        FILETIME born{},exit{},kernel{},user{};
        if(!GetProcessTimes(parent.value,&born,&exit,&kernel,&user))return {};
        const auto created=(std::uint64_t(born.dwHighDateTime)<<32)|born.dwLowDateTime;
        const auto exited=(std::uint64_t(exit.dwHighDateTime)<<32)|exit.dwLowDateTime;
        const auto childBorn=child->witness_.created;
        if(!created||!exited||created>=childBorn||childBorn>exited)return {};
        const auto nativeImage=[](HANDLE object){
            std::wstring image(32768,L'\0');DWORD count=static_cast<DWORD>(image.size());
            if(!QueryFullProcessImageNameW(object,PROCESS_NAME_NATIVE,image.data(),&count)||!count||count>=image.size())return std::wstring{};
            image.resize(count);if(image.find(L'\0')!=std::wstring::npos)return std::wstring{};return image;
        };
        const auto parentImage=nativeImage(parent.value),childImage=nativeImage(child->handle_.value);
        if(parentImage.empty()||childImage.empty())return {};
        DWORD session=0;
        if(!ProcessIdToSessionId(parentPid,&session)||session!=child->windowsSession_||
            WaitForSingleObject(parent.value,0)!=WAIT_OBJECT_0||!child->stillAlive()||detail::parent(child->handle_.value)!=parentPid)return {};
        auto result=std::shared_ptr<const HistoricalExitedParentEvidence>(new HistoricalExitedParentEvidence(
            std::move(child),std::make_shared<detail::Handle>(std::move(parent)),parentPid,session,creator,created,exited,parentImage,childImage));
        return result->relationCurrent()?result:std::shared_ptr<const HistoricalExitedParentEvidence>{};
    }catch(...){return {};}
}


inline std::shared_ptr<const StartupParentObjectEvidence> ProcessParentEvidenceOwner::captureStartupWindowParent(
    ymcc::steamsession::WindowProcessCache& cache,DWORD pid,DWORD event)const noexcept {
    struct PreserveError {DWORD value=GetLastError();~PreserveError(){SetLastError(value);}} preserve;
    startupdiagnostic::Scope trace(pid,startupdiagnostic::Stage::Parent,"parent-cache-entry-unavailable");
    try {
        if(event!=EVENT_OBJECT_CREATE||!pid)return {};
        const auto found=cache.entries_.find(pid);if(found==cache.entries_.end())return {};
        auto& entry=found->second;trace.reached("parent-create-already-attempted-or-child-unavailable");
        if(entry.kind||entry.historicalCreateAttempted||!entry.process||WaitForSingleObject(entry.process,0)!=WAIT_TIMEOUT)return {};
        entry.historicalCreateAttempted=true;
        trace.reached("parent-child-capture-unavailable");const auto child=capture(entry.process);if(!child)return {};
        trace.reached("parent-pid-unavailable");const auto parentPid=detail::parent(child->handle_.value);if(!parentPid||parentPid==pid)return {};
        trace.reached("parent-open-unavailable");detail::Handle opened(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,parentPid));
        auto parent=std::make_shared<detail::Handle>(std::move(opened));
        if(!parent->value||GetProcessId(parent->value)!=parentPid)return {};
        trace.reached("parent-times-unavailable");FILETIME born{},exited{},kernel{},user{};if(!GetProcessTimes(parent->value,&born,&exited,&kernel,&user))return {};
        const auto created=(std::uint64_t(born.dwHighDateTime)<<32)|born.dwLowDateTime;
        trace.reached("parent-child-birth-order-invalid");if(!created||created>=child->witness_.created)return {};
        const auto nativeImage=[](HANDLE object){std::wstring value(32768,L'\0');DWORD n=static_cast<DWORD>(value.size());if(!QueryFullProcessImageNameW(object,PROCESS_NAME_NATIVE,value.data(),&n)||!n||n>=value.size())return std::wstring{};value.resize(n);if(value.find(L'\0')!=std::wstring::npos)return std::wstring{};return value;};
        trace.reached("parent-native-images-unavailable");const auto parentImage=nativeImage(parent->value),childImage=nativeImage(child->handle_.value);if(parentImage.empty()||childImage.empty())return {};
        trace.reached("parent-session-or-child-changed");DWORD session=0;if(!ProcessIdToSessionId(parentPid,&session)||session!=child->windowsSession_||!child->stillAlive()||detail::parent(child->handle_.value)!=parentPid)return {};
        const auto state=WaitForSingleObject(parent->value,0);if(state!=WAIT_TIMEOUT&&state!=WAIT_OBJECT_0)return {};
        auto result=std::shared_ptr<const StartupParentObjectEvidence>(new StartupParentObjectEvidence(child,std::move(parent),parentPid,session,GetCurrentProcessId(),created,parentImage,childImage));
        trace.reached(state==WAIT_TIMEOUT?"parent-live-object-retained":"parent-exited-object-retained");return result;
    }catch(...){trace.reached("parent-capture-exception");return {};}
}
inline std::shared_ptr<const HistoricalExitedParentEvidence> ProcessParentEvidenceOwner::completeStartupWindowParent(
    const std::shared_ptr<const StartupParentObjectEvidence>& pending)const noexcept {
    struct PreserveError {DWORD value=GetLastError();~PreserveError(){SetLastError(value);}} preserve;
    try {
        if(!pending||!pending->childCurrent()||!pending->parent_||!pending->parent_->value||WaitForSingleObject(pending->parent_->value,0)!=WAIT_OBJECT_0)return {};
        FILETIME born{},exited{},kernel{},user{};if(!GetProcessTimes(pending->parent_->value,&born,&exited,&kernel,&user))return {};
        const auto created=(std::uint64_t(born.dwHighDateTime)<<32)|born.dwLowDateTime,exit=(std::uint64_t(exited.dwHighDateTime)<<32)|exited.dwLowDateTime;
        const auto childBorn=pending->child_->witness_.created;
        if(created!=pending->born_||!exit||created>=childBorn||childBorn>exit||!pending->childCurrent())return {};
        auto result=std::shared_ptr<const HistoricalExitedParentEvidence>(new HistoricalExitedParentEvidence(pending->child_,pending->parent_,pending->parentPid_,pending->windowsSession_,pending->creatorPid_,created,exit,pending->parentNativeImage_,pending->childNativeImage_));
        return result->relationCurrent()?result:std::shared_ptr<const HistoricalExitedParentEvidence>{};
    }catch(...){return {};}
}

inline std::shared_ptr<const HistoricalExitedParentEvidence> ProcessParentEvidenceOwner::captureHistoricalWindowParent(
    ymcc::steamsession::WindowProcessCache& cache,DWORD pid,DWORD event)const noexcept {
    struct PreserveError {DWORD value=GetLastError();~PreserveError(){SetLastError(value);}} preserve;
    try{
        if(event!=EVENT_OBJECT_CREATE||!pid)return {};
        const auto found=cache.entries_.find(pid);
        if(found==cache.entries_.end())return {};
        auto& entry=found->second;
        if(entry.kind||entry.historicalCreateAttempted||!entry.process||WaitForSingleObject(entry.process,0)!=WAIT_TIMEOUT)return {};
        entry.historicalCreateAttempted=true;
        // Borrow only for this synchronous issuer call; the returned evidence owns
        // original child/parent objects independently of the window cache.
        const auto child=capture(entry.process);
        return child?captureHistoricalParent(child):std::shared_ptr<const HistoricalExitedParentEvidence>{};
    }catch(...){return {};}
}
}

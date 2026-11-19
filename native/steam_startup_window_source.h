#pragma once
#include "steam_information_client.h"
#include "steam_session_observer.h"
#include "steam_startup_window_facts.h"
#include "process_inventory.h"
#include <memory>
#include <mutex>
#include <functional>
namespace ymcc::steaminfo {
// Main-only acquisition, using Observer's existing one-slot executor. A read
// returns immediately; no synchronous window/process search runs on the UI
// thread and no new worker/timer/handle belongs to this source.
class StartupWindowSource final {
    using Identity=std::shared_ptr<const steamsession::IdentitySnapshot>;
    struct Work;
    struct State {
        std::mutex mutex;
        Identity identity;
        StartupWindowFacts facts;
        ULONGLONG completedAt=0;
        std::weak_ptr<Work> pending;
    };
    std::shared_ptr<State> state_=std::make_shared<State>();
    static bool equal(const Identity& a,const Identity& b) noexcept {
        return a&&b&&a->processEpoch==b->processEpoch&&a->accountEpoch==b->accountEpoch&&
            a->session==b->session&&a->presence==b->presence&&a->rootReadError==b->rootReadError&&
            a->windowsSessionKnown&&b->windowsSessionKnown&&a->windowsSession==b->windowsSession;
    }
    struct Work {
        std::shared_ptr<State> state;
        Identity identity;
        steamsession::IdentityReadView view;
        StartupWindowFacts result;
        bool completed=false;
        const bool reuseOwnerForRead;
        std::unique_ptr<Peer> rootProcess;
        std::shared_ptr<const processInventory::Snapshot> topology;
        bool topologyRead=false;
        ULONGLONG topologyBeforeBirth=0;
        ULONGLONG end=0;
        Work(std::shared_ptr<State> s,Identity i,steamsession::IdentityReadView v,ULONGLONG now,bool readOnly)
            :state(std::move(s)),identity(std::move(i)),view(std::move(v)),reuseOwnerForRead(readOnly),end(now+1000){}
        bool current()const noexcept {
            return GetTickCount64()<end&&(!rootProcess||rootProcess->live())&&StartupWindowSource::equal(identity,view.identitySnapshot());
        }
        bool belongsToRoot(const Peer& peer) {
            if(!rootProcess||!rootProcess->sameUser(peer))return false;
            if(peer.pid==rootProcess->pid)return peer.birth==rootProcess->birth;
            if(peer.birth<rootProcess->birth)return false;
            if(!topologyRead){
                topologyRead=true;
                FILETIME before{};GetSystemTimeAsFileTime(&before);
                topologyBeforeBirth=(ULONGLONG(before.dwHighDateTime)<<32)|before.dwLowDateTime;
                // One fresh acquisition from the existing shared process owner,
                // on the existing fact executor only. Never infer ancestry from
                // an incomplete table or a candidate born after its acquisition.
                topology=processInventory::read(processInventory::resolveQuery(),processInventory::currentTime,0);
            }
            if(!topology||topology->status!=processInventory::Status::Complete||peer.birth>topologyBeforeBirth)return false;
            const auto root=pathKey(identity->session.root);
            DWORD childPid=peer.pid;ULONGLONG childBirth=peer.birth;
            std::vector<std::unique_ptr<Peer>> held;
            for(unsigned depth=0;depth<8&&current();++depth){
                const auto row=std::find_if(topology->rows.begin(),topology->rows.end(),[&](const auto& r){return r.pid==childPid;});
                if(row==topology->rows.end()||!row->parentPid||row->parentPid==childPid)return false;
                if(row->parentPid==rootProcess->pid)
                    return rootProcess->birth<=childBirth&&rootProcess->live()&&peer.live()&&
                        std::all_of(held.begin(),held.end(),[](const auto& p){return p->live();});
                auto parent=std::make_unique<Peer>();
                if(!parent->load(row->parentPid)||!rootProcess->sameUser(*parent)||
                    parent->birth>childBirth||parent->birth<rootProcess->birth)return false;
                const auto image=pathKey(parent->path);
                if(!image.starts_with(root+L"\\bin\\cef\\")||std::filesystem::path(image).filename()!=L"steamwebhelper.exe")return false;
                childPid=parent->pid;childBirth=parent->birth;held.push_back(std::move(parent));
            }
            return false;
        }
        bool inspect(HWND window) noexcept {
            try {
                if(!current()||!window||!IsWindow(window)||!IsWindowVisible(window))return false;
                wchar_t cls[128]{};if(!GetClassNameW(window,cls,128)||wcscmp(cls,L"SDL_app"))return false;
                DWORD pid=0;if(!GetWindowThreadProcessId(window,&pid)||!pid)return false;
                Peer windowProcess;
                const Peer* peer=rootProcess.get();
                if(!peer)return false;
                // The read-only job already verified this exact live process.
                // A helper or an action still goes through the original checks.
                if(!reuseOwnerForRead||pid!=peer->pid){
                    if(!windowProcess.load(pid)||windowProcess.session!=identity->windowsSession)return false;
                    const auto root=pathKey(identity->session.root),image=pathKey(windowProcess.path);
                    const bool owned=image==root+L"\\steam.exe"||
                        (image.starts_with(root+L"\\bin\\cef\\")&&std::filesystem::path(image).filename()==L"steamwebhelper.exe");
                    if(!owned||!belongsToRoot(windowProcess))return false;
                    peer=&windowProcess;
                }
                RECT rect{};if(!GetWindowRect(window,&rect))return false;
                if(!startupWindowCandidate(true,true,true,true,
                    (GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_APPWINDOW)!=0,
                    (GetWindowLongPtrW(window,GWL_STYLE)&WS_THICKFRAME)!=0,
                    rect.right-rect.left,rect.bottom-rect.top))return false;
                DWORD finalPid=0;GetWindowThreadProcessId(window,&finalPid);
                if(finalPid!=pid||!peer->live()||!current()||!IsWindowVisible(window))return false;
                result.known=true;result.window=reinterpret_cast<std::uintptr_t>(window);result.pid=pid;
                result.processBirth=peer->birth;result.windowsSession=identity->windowsSession;
                result.root=utf8(identity->session.root);result.processEpoch=identity->processEpoch;
                result.accountEpoch=identity->accountEpoch;result.observedAt=GetTickCount64();return true;
            }catch(...){return false;}
        }
        void run()noexcept {
            if(!current())return;
            try{
                auto owner=std::make_unique<Peer>();
                if(!owner->load(identity->session.pid)||owner->birth!=identity->session.created||
                    owner->session!=identity->windowsSession||pathKey(owner->path)!=pathKey(identity->session.root)+L"\\steam.exe"||!current())return;
                rootProcess=std::move(owner);
            }catch(...){return;}
            bool observed=inspect(GetForegroundWindow());
            if(!observed){
                const bool enumerated=EnumWindows([](HWND window,LPARAM data)->BOOL{
                    auto& work=*reinterpret_cast<Work*>(data);
                    return work.current()&&!work.inspect(window);
                },reinterpret_cast<LPARAM>(this))!=FALSE;
                observed=enumerated||result.known;
            }
            completed=observed&&current();
            if(completed){
                result.completed=true;result.observedAt=GetTickCount64();
                result.processEpoch=identity->processEpoch;result.accountEpoch=identity->accountEpoch;
                result.windowsSession=identity->windowsSession;result.root=utf8(identity->session.root);
            }
        }
        void publish()noexcept {
            if(!completed||!current())return;
            std::unique_lock lock(state->mutex,std::try_to_lock);if(!lock.owns_lock())return;
            if(state->pending.lock().get()!=this||!current())return;
            state->identity=identity;state->facts=std::move(result);
            state->completedAt=GetTickCount64();state->pending.reset();
        }
    };
public:
    using Post=std::function<bool(std::function<void()>,std::function<void()>)>;
    static bool focus(const Identity& identity,steamsession::IdentityReadView view,
            const std::function<bool()>& current,const std::function<bool()>& claim)noexcept{
        try{
            if(!identity||identity->presence!=1||!identity->session.pid||!identity->session.created||identity->session.root.empty()||!current||!claim||!current())return false;
            Work work(std::make_shared<State>(),identity,std::move(view),GetTickCount64(),false);work.run();
            if(!work.completed||!work.result.known||!work.current()||!current())return false;
            Peer target;if(!target.load(work.result.pid)||target.birth!=work.result.processBirth||!work.rootProcess||
                !work.rootProcess->sameUser(target))return false;
            const auto window=reinterpret_cast<HWND>(static_cast<std::uintptr_t>(work.result.window));
            const auto ready=[&]{DWORD pid=0;return work.current()&&current()&&target.live()&&
                GetWindowThreadProcessId(window,&pid)&&pid==target.pid&&IsWindowVisible(window);};
            if(!ready()||!claim()||!ready())return false;
            if(IsIconic(window)){
                if(!ready())return false;
                ShowWindow(window,SW_RESTORE);
            }
            if(!ready())return false;
            SetForegroundWindow(window);
            return GetForegroundWindow()==window;
        }catch(...){return false;}
    }
    StartupWindowFacts read(const Identity& identity,steamsession::IdentityReadView view,const Post& post)noexcept {
        try {
            if(!identity||identity->presence!=1||!identity->session.pid||!identity->session.created||
                identity->session.root.empty()||identity->rootReadError!=ERROR_SUCCESS||!identity->windowsSessionKnown||
                !equal(identity,view.identitySnapshot())||!post)return {};
            const auto now=GetTickCount64();std::shared_ptr<Work> work;
            {
                std::unique_lock lock(state_->mutex,std::try_to_lock);if(!lock.owns_lock())return {};
                if(equal(identity,state_->identity)&&state_->completedAt&&now>=state_->completedAt&&now-state_->completedAt<=250)
                    return state_->facts;
                if(!state_->pending.expired())return {};
                work=std::make_shared<Work>(state_,identity,std::move(view),now,true);state_->pending=work;
            }
            // Both callbacks own Work; a canceled or rejected slot releases it,
            // leaving only an expired weak token, so no pending flag can wedge.
            (void)post([work]{work->run();},[work]{work->publish();});
            return {};
        }catch(...){return {};}
    }
};
}

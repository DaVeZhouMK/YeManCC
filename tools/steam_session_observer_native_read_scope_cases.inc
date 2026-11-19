// Native read-scope cases for the original isolated Observer fixture.
#include <type_traits>
#include <thread>
#include <utility>
namespace NativeReadScopeFixture {
using Scope=ymcc::steamnative::ReadScope;
using Power=ymcc::sleeppower::SharedPowerV1;
using Lease=ymcc::steamsession::NativeReadDelegation;
using Registration=ymcc::sleeppower::RevocationMirrors::Registration;
static unsigned assertions=0;
static void require(bool ok,const char* reason){++assertions;if(!ok)throw std::runtime_error(reason);}
static bool signalled(HANDLE h){return h&&WaitForSingleObject(h,0)==WAIT_OBJECT_0;}
static bool quiet(HANDLE h){return h&&WaitForSingleObject(h,0)==WAIT_TIMEOUT;}
template<class T,class=void>struct hasResolvedAccount:std::false_type{};
template<class T>struct hasResolvedAccount<T,std::void_t<decltype(std::declval<T&>().resolvedAccount())>>:std::true_type{};
template<class T,class=void>struct hasLiveUiAccount:std::false_type{};
template<class T>struct hasLiveUiAccount<T,std::void_t<decltype(std::declval<T&>().liveUiAccount())>>:std::true_type{};
template<class T,class=void>struct hasNativeQualified:std::false_type{};
template<class T>struct hasNativeQualified<T,std::void_t<decltype(std::declval<T&>().nativeQualified())>>:std::true_type{};
struct OwnedHandle{
    HANDLE value=nullptr;explicit OwnedHandle(HANDLE h=nullptr):value(h){}
    ~OwnedHandle(){if(value)CloseHandle(value);}
    OwnedHandle(const OwnedHandle&)=delete;
};
struct NativePowerProducer{
    std::mutex mutex;Power state;ymcc::sleeppower::RevocationMirrors mirrors;bool raceAfterRegistration=false;
    NativePowerProducer(){state.generation=71;state.publicationRevision=10;state.observedNotificationSequence=8;state.suspendSerial=4;state.phase=0;state.flags=ymcc::sleeppower::kKnown|ymcc::sleeppower::kHardwareAllowed;}
    void pending(){{std::lock_guard lock(mutex);state.flags|=ymcc::sleeppower::kNotificationPending;++state.publicationRevision;}mirrors.revoke();}
    void ready(){std::lock_guard lock(mutex);state.flags&=~ymcc::sleeppower::kNotificationPending;state.phase=0;++state.publicationRevision;}
    void forwardDiagnostic(){std::lock_guard lock(mutex);++state.publicationRevision;}
};
static NativePowerProducer* activePower=nullptr;
// Pure complete Native projection: no IO/discovery, cancellation side effects,
// or Steam/account proof. State changes are explicit producer operations.
static int __cdecl readPower(Power* output,std::uint32_t bytes){
    if(!activePower||!output||bytes!=sizeof(Power))return 0;
    std::lock_guard lock(activePower->mutex);*output=activePower->state;return 1;
}
static bool capturePower(Power& output)noexcept{return ymcc::sleeppower::capturePower(&readPower,output);}
static bool currentPower(const Power& captured)noexcept{return ymcc::sleeppower::powerCurrent(&readPower,captured);}
static Registration observePower(const Power& captured)noexcept{
    if(!activePower)return {};
    auto result=activePower->mirrors.observe(&readPower,captured);
    // Actual registration is revoked after registration and before factory
    // final checks. Reader itself has no mutation hooks.
    if(activePower->raceAfterRegistration){
        activePower->raceAfterRegistration=false;activePower->pending();activePower->ready();
    }
    return result;
}
static ymcc::steamsource::NativePowerAccess access(){return {&capturePower,&currentPower,&observePower};}
struct Context{
    Registry registry;Child steam;Observer observer;NativePowerProducer power;
    std::shared_ptr<const IdentitySnapshot> initial;
    Context(const std::wstring& root,const std::wstring& child)
        :registry(contextRegistryPath+L"\\NativeReadScope"),steam(child),observer(registry.path){
        if(activePower)throw std::runtime_error("overlapping power fixture");activePower=&power;
        try{
            registry.root(root);registry.number(L"ActiveUser",42);registry.number(L"pid",steam.pi.dwProcessId);
            require(observer.start([](bool){},[]{return false;}),"actual Observer start");
            require(until([&]{const auto r=observer.identitySnapshot();return r&&r->presence==1&&r->session.pid==steam.pi.dwProcessId&&r->session.account==42&&r->windowsSessionKnown&&r->processEpoch&&r->accountEpoch;}),"actual live publisher receipt");
            initial=observer.identitySnapshot();
        }catch(...){observer.stop();activePower=nullptr;throw;}
    }
    ~Context(){observer.stop();activePower=nullptr;}
    std::shared_ptr<const IdentitySnapshot> receipt(){return observer.identitySnapshot();}
    std::shared_ptr<Scope> scope(){
        auto result=ymcc::steamnative::acquireReadScope(observer,receipt(),access());
        require(bool(result),"real pinned scope acquired");return result;
    }
    void account(DWORD value){
        const auto previous=receipt();registry.number(L"ActiveUser",value);
        require(until([&]{const auto r=receipt();return r&&r->session.account==value&&r->accountReadError==ERROR_SUCCESS&&r->accountEpoch>previous->accountEpoch;}),"actual account epoch publication");
    }
    void deleteAccount(){
        HKEY key=nullptr;require(RegOpenKeyExW(HKEY_CURRENT_USER,(registry.path+L"\\ActiveProcess").c_str(),0,KEY_SET_VALUE,&key)==ERROR_SUCCESS,"open private registry");
        const auto status=RegDeleteValueW(key,L"ActiveUser");RegCloseKey(key);require(status==ERROR_SUCCESS,"delete private account value");
        require(until([&]{const auto r=receipt();return r&&r->accountReadError!=ERROR_SUCCESS&&r->accountEpoch>initial->accountEpoch;}),"actual account read error publication");
    }
};
static void closedApi(const std::wstring& root,const std::wstring& child){
    Context f(root,child);auto scope=f.scope();auto native=f.observer.delegateNativeRead(f.receipt());
    require(native&&native->current(),"closed native delegation");
    require(!std::is_default_constructible_v<Lease>&&!hasResolvedAccount<Lease>::value&&!hasLiveUiAccount<Lease>::value&&!hasNativeQualified<Lease>::value,"native delegation has no account/UI/native-qualified authority API");
    require(!hasResolvedAccount<Scope>::value&&!hasLiveUiAccount<Scope>::value&&!hasNativeQualified<Scope>::value,"scope has no account/UI/native-qualified authority API");
    const auto reads=registryReads.load(),queries=nativeReadProcessQueries.load();
    bool healthy=true;for(unsigned i=0;i<1001;++i)if(!native->current()||!scope->current())healthy=false;
    require(healthy&&registryReads.load()==reads&&nativeReadProcessQueries.load()==queries,"1001 current calls have zero registry/process discovery");
    require(quiet(scope->identityCancellationEvent())&&quiet(scope->powerCancellationEvent()),"both borrowed events quiet");
}
static void strictInputsAndMetadata(const std::wstring& root,const std::wstring& child){
    Context f(root,child);const auto actual=f.receipt();
    require(!f.observer.delegateNativeRead({})&&!ymcc::steamnative::acquireReadScope(f.observer,{},access()),"empty expected refused");
    const auto reject=[&](auto mutate){
        auto bad=std::make_shared<IdentitySnapshot>(*actual);mutate(*bad);
        require(!f.observer.delegateNativeRead(bad)&&!ymcc::steamnative::acquireReadScope(f.observer,bad,access()),"mismatched published boundary refused");
    };
    reject([](auto& s){++s.processEpoch;});reject([](auto& s){s.processEpoch=0;});
    reject([](auto& s){++s.accountEpoch;});reject([](auto& s){s.accountEpoch=0;});
    reject([](auto& s){++s.session.pid;});reject([](auto& s){++s.session.created;});
    reject([](auto& s){s.session.root+=L"\\foreign";});
    reject([](auto& s){++s.windowsSession;});reject([](auto& s){s.windowsSessionKnown=false;});
    reject([](auto& s){s.rootReadError=ERROR_ACCESS_DENIED;});
    auto forged=std::make_shared<IdentitySnapshot>(*actual);forged->revision+=1000000;forged->observedAt+=1000000;
    const auto leaseBefore=f.receipt();auto lease=f.observer.delegateNativeRead(forged);const auto leaseAfter=f.receipt();
    const auto scopeBefore=f.receipt();auto scope=ymcc::steamnative::acquireReadScope(f.observer,forged,access());const auto scopeAfter=f.receipt();
    require(lease&&scope&&lease->current()&&scope->current(),"caller diagnostics are not authority inputs");
    const auto actuallyPublished=[](const IdentitySnapshot& held,const auto& earlier,const auto& later){
        const auto chosen=&held==earlier.get()?earlier:(&held==later.get()?later:std::shared_ptr<const IdentitySnapshot>{});
        return chosen&&held.revision==chosen->revision&&held.observedAt==chosen->observedAt;
    };
    require(&lease->expected()!=forged.get()&&actuallyPublished(lease->expected(),leaseBefore,leaseAfter),"lease retains exact actual publisher object and metadata");
    require(&scope->expected()!=forged.get()&&actuallyPublished(scope->expected(),scopeBefore,scopeAfter),"scope retains exact actual publisher object and metadata");
    auto missing=access();missing.capture=nullptr;require(!ymcc::steamnative::acquireReadScope(f.observer,actual,missing),"missing capture refused");
    missing=access();missing.current=nullptr;require(!ymcc::steamnative::acquireReadScope(f.observer,actual,missing),"missing current refused");
    missing=access();missing.observe=nullptr;require(!ymcc::steamnative::acquireReadScope(f.observer,actual,missing),"missing observe refused");
}
static void sameEpochRefresh(const std::wstring& root,const std::wstring& child){
    Context f(root,child);auto held=f.scope();const auto original=f.receipt();
    auto native=f.observer.delegateNativeRead(original);auto ui=f.observer.delegateIdentity(original,123,true);require(native&&ui,"native and original UI delegates");
    require(f.observer.requestIdentityRefresh()&&until([&]{const auto r=f.receipt();return r&&r->revision>original->revision;}),"real same-epoch refresh");
    const auto fresh=f.receipt();require(fresh->processEpoch==original->processEpoch&&fresh->accountEpoch==original->accountEpoch,"refresh retains actual epochs");
    require(held->current()&&native->current()&&ui->current()&&quiet(native->cancellationEvent()),"same epoch does not revoke");
    auto next=ymcc::steamnative::acquireReadScope(f.observer,original,access());require(next&&next->current(),"older same-epoch receipt can acquire");
}
static void accountAba(const std::wstring& root,const std::wstring& child){
    // Single-case mutation target: this setup and this case only.
    Context f(root,child);const auto before=f.receipt();auto scope=f.scope();
    auto native=f.observer.delegateNativeRead(before);auto ui=f.observer.delegateIdentity(before,123,true);
    auto candidate=f.observer.delegateIdentity(before,42,false);require(native&&ui&&candidate,"actual original and native delegates");
    f.account(43);const auto changed=f.receipt();
    require(changed->processEpoch==before->processEpoch&&changed->accountEpoch>before->accountEpoch,"actual account transition retains process epoch");
    // Check the actual Observer event BEFORE composite current can cancel.
    require(signalled(native->cancellationEvent())&&!native->current(),"actual Observer signals native account revocation");
    require(ui->current()&&!candidate->current()&&signalled(candidate->cancellationEvent()),"original UI/candidate policy unchanged");
    require(!scope->current()&&signalled(scope->identityCancellationEvent()),"old composite scope rejects changed account");
    f.account(42);
    require(f.receipt()->accountEpoch>changed->accountEpoch&&!native->current()&&signalled(native->cancellationEvent()),"account ABA never revives old native event");
    require(!scope->current()&&!f.observer.delegateNativeRead(before),"old scope and receipt stay revoked after ABA");
    auto renewed=f.scope();require(renewed->current()&&ui->current(),"only fresh receipt admits new native work");
}
static void zeroCandidate(const std::wstring& root,const std::wstring& child){
    Context f(root,child);f.account(0);const auto zero=f.receipt();auto scope=f.scope();auto native=f.observer.delegateNativeRead(zero);
    require(zero->accountReadError==ERROR_SUCCESS&&zero->session.account==0&&!zero->registryAccountCandidateKnown()&&!zero->currentRegistryAccountKnown(),"readable zero remains unknown account");
    require(native&&native->current()&&scope->current()&&scope->expected().session.account==0,"zero candidate admits read-only qualification");
    require(!f.observer.delegateIdentity(zero,0,true)&&!f.observer.delegateIdentity(zero,42,false),"zero candidate does not acquire original account authority");
}
static void unknownCandidate(const std::wstring& root,const std::wstring& child){
    Context f(root,child);auto old=f.observer.delegateNativeRead(f.receipt());f.deleteAccount();
    const auto unknown=f.receipt();auto native=f.observer.delegateNativeRead(unknown);auto scope=f.scope();
    require(old&&signalled(old->cancellationEvent())&&!old->current(),"account read error revokes prior actual lease");
    require(unknown->accountReadError!=ERROR_SUCCESS&&!unknown->registryAccountCandidateKnown()&&!unknown->currentRegistryAccountKnown(),"read failure remains unknown");
    require(native&&native->current()&&scope->current()&&scope->expected().accountReadError==unknown->accountReadError,"unknown actual candidate admits read-only scope");
}
static void readErrorRecovery(const std::wstring& root,const std::wstring& child){
    Context f(root,child);auto prior=f.scope();const auto before=f.receipt();f.deleteAccount();const auto error=f.receipt();
    require(error->accountEpoch>before->accountEpoch&&signalled(prior->identityCancellationEvent())&&!prior->current(),"actual read error sticky cancels prior scope");
    auto unknown=f.scope();f.account(42);
    require(!prior->current()&&!unknown->current()&&signalled(unknown->identityCancellationEvent()),"readability recovery does not revive either prior scope");
    auto fresh=f.scope();require(fresh->current()&&!f.observer.delegateNativeRead(before),"new recovered receipt required");
}
static void registeredPidEpoch(const std::wstring& root,const std::wstring& child){
    Context f(root,child);auto scope=f.scope();auto native=f.observer.delegateNativeRead(f.receipt());const auto before=f.receipt();
    f.registry.number(L"pid",0);
    require(until([&]{const auto r=f.receipt();return r&&r->registeredPid==0&&r->accountEpoch>before->accountEpoch;}),"actual registered PID transition");
    const auto zero=f.receipt();require(zero->session.pid==before->session.pid&&zero->session.created==before->session.created&&zero->processEpoch==before->processEpoch,"actual Steam tuple retained across zero registration");
    require(native&&signalled(native->cancellationEvent())&&!native->current()&&!scope->current(),"registered PID/account epoch revokes native");
    auto zeroScope=f.scope();f.registry.number(L"pid",f.steam.pi.dwProcessId);
    require(until([&]{const auto r=f.receipt();return r&&r->registeredPid==f.steam.pi.dwProcessId&&r->accountEpoch>zero->accountEpoch;}),"actual registered PID ABA publication");
    require(!native->current()&&!scope->current()&&!zeroScope->current(),"registered PID ABA never revives old work");
}
static void observerStop(const std::wstring& root,const std::wstring& child){
    Context f(root,child);const auto before=f.receipt();auto native=f.observer.delegateNativeRead(before);auto scope=f.scope();
    f.observer.stop();require(native&&signalled(native->cancellationEvent())&&!native->current()&&!scope->current(),"observerStop revokes actual native and scope");
    require(!f.observer.delegateNativeRead(before),"stopped observer refuses retained receipt");
    require(f.observer.start([](bool){},[]{return false;})&&until([&]{const auto r=f.receipt();return r&&r->presence==1&&r->session.pid==f.steam.pi.dwProcessId&&r->processEpoch>before->processEpoch;}),"observer restart issues actual new epoch");
    auto fresh=f.scope();require(fresh->current()&&!native->current()&&!scope->current()&&!f.observer.delegateNativeRead(before),"restart cannot revive old native scope");
}
static void registrationRace(const std::wstring& root,const std::wstring& child){
    Context f(root,child);const auto expected=f.receipt();f.power.raceAfterRegistration=true;
    auto raced=ymcc::steamnative::acquireReadScope(f.observer,expected,access());
    require(!raced&&!f.power.raceAfterRegistration,"actual registration revoked before factory final check");
    Power ready;require(capturePower(ready)&&ready.generation==71,"race returns Ready in same generation");
    auto fresh=f.scope();require(fresh->current(),"new registration allowed after race");
}
static void pendingReadySticky(const std::wstring& root,const std::wstring& child){
    Context f(root,child);auto old=f.scope();const auto generation=old->power().generation;
    f.power.pending();require(signalled(old->powerCancellationEvent()),"same generation pending immediately signals actual registration");
    require(!old->current()&&!ymcc::steamnative::acquireReadScope(f.observer,f.receipt(),access()),"pending refuses held and new scope");
    f.power.ready();require(!old->current()&&signalled(old->powerCancellationEvent()),"Ready never resets cancelled event");
    auto fresh=f.scope();require(fresh->current()&&fresh->power().generation==generation,"fresh scope uses same authoritative generation");
}
static void forwardRevision(const std::wstring& root,const std::wstring& child){
    Context f(root,child);auto scope=f.scope();const auto original=scope->power();
    f.power.forwardDiagnostic();Power now;require(capturePower(now)&&now.publicationRevision>original.publicationRevision&&now.generation==original.generation,"forward diagnostic publication only");
    require(scope->current()&&quiet(scope->identityCancellationEvent())&&quiet(scope->powerCancellationEvent()),"forward revision does not revoke");
    require(scope->power().publicationRevision==original.publicationRevision,"immutable captured power stays unchanged");
}
static void callerCancellation(const std::wstring& root,const std::wstring& child){
    Context f(root,child);auto scope=f.scope();auto sibling=f.scope();auto native=f.observer.delegateNativeRead(f.receipt());
    scope->cancel();scope->cancel();
    require(!scope->current()&&signalled(scope->identityCancellationEvent())&&signalled(scope->powerCancellationEvent()),"caller cancellation completes both sticky signals");
    require(sibling->current()&&native&&native->current()&&quiet(sibling->identityCancellationEvent())&&quiet(sibling->powerCancellationEvent()),"caller cancellation stays request local");
    f.power.forwardDiagnostic();f.observer.requestIdentityRefresh();require(!scope->current(),"healthy diagnostics cannot revive caller cancellation");
}
static void sharedCallbackLifetime(const std::wstring& root,const std::wstring& child){
    Context f(root,child);auto scope=f.scope();std::weak_ptr<Scope> weak=scope;
    const HANDLE identity=scope->identityCancellationEvent(),power=scope->powerCancellationEvent();
    OwnedHandle identityDuplicate,powerDuplicate;
    require(DuplicateHandle(GetCurrentProcess(),identity,GetCurrentProcess(),&identityDuplicate.value,SYNCHRONIZE,FALSE,0)&&
        DuplicateHandle(GetCurrentProcess(),power,GetCurrentProcess(),&powerDuplicate.value,SYNCHRONIZE,FALSE,0),"observation-only duplicates retained");
    OwnedHandle entered(CreateEventW(nullptr,TRUE,FALSE,nullptr)),release(CreateEventW(nullptr,TRUE,FALSE,nullptr)),done(CreateEventW(nullptr,TRUE,FALSE,nullptr));
    require(entered.value&&release.value&&done.value,"bounded callback synchronization handles");
    std::function<bool()> callback=[held=scope](){return !held->current()&&signalled(held->identityCancellationEvent())&&signalled(held->powerCancellationEvent());};
    std::atomic<bool> accepted{false};
    std::thread worker([retained=callback,&entered,&release,&done,&accepted]()mutable{
        SetEvent(entered.value);if(WaitForSingleObject(release.value,3000)==WAIT_OBJECT_0)accepted.store(retained(),std::memory_order_release);
        retained={};SetEvent(done.value);
    });
    const bool started=WaitForSingleObject(entered.value,3000)==WAIT_OBJECT_0;scope.reset();callback={};
    const bool heldAfterCallerReturn=!weak.expired()&&quiet(identity)&&quiet(power);
    f.power.pending();const bool originalPowerSignalled=signalled(power);
    SetEvent(release.value);const bool completed=WaitForSingleObject(done.value,3000)==WAIT_OBJECT_0;worker.join();
    require(started&&heldAfterCallerReturn,"shared callback retains original borrowed handles after caller return");
    require(originalPowerSignalled&&completed&&accepted.load(std::memory_order_acquire),"callback observes cancellation before releasing scope");
    require(weak.expired()&&signalled(identityDuplicate.value)&&signalled(powerDuplicate.value),"callback completion releases scope; duplicates stay signalled");
}
static void actualSteamExit(const std::wstring& root,const std::wstring& child){
    Context f(root,child);auto scope=f.scope();auto native=f.observer.delegateNativeRead(f.receipt());const auto actual=f.receipt();
    auto pin=ymcc::steamgamefacts::detail::pin({actual->session.pid,actual->session.created,actual->session.root+L"\\steam.exe"});
    require(pin&&pin->windowsSession==actual->windowsSession&&ymcc::steamgamefacts::detail::alive(pin->handle.value),"actual original detail::pin");
    f.steam.finish();require(WaitForSingleObject(f.steam.pi.hProcess,0)==WAIT_OBJECT_0&&!ymcc::steamgamefacts::detail::alive(pin->handle.value),"actual fixture Steam child exits");
    require(until([&]{return native&&!native->current()&&signalled(native->cancellationEvent());}),"actual Observer process-exit event revokes native");
    require(!scope->current()&&signalled(scope->identityCancellationEvent())&&signalled(scope->powerCancellationEvent()),"composite scope revoked after actual exit");
    require(!ymcc::steamnative::acquireReadScope(f.observer,actual,access()),"exited actual process cannot be repinned");
}
// Additional boundaries in the original NativeReadScope Observer fixture.
static Registration observeObservationPower(const Power& captured)noexcept{
    return activePower?activePower->mirrors.observeRead(&readPower,captured):Registration{};
}
struct ObservationFixture {
    Registry registry;Child steam;NativePowerProducer power;
    std::wstring root,gamePath;std::unique_ptr<Child> game;
    ObservationFixture(const std::wstring& folder,const std::wstring& child)
        :registry(contextRegistryPath),steam(child),root(folder),gamePath(folder+L"\\observation-game.exe"){
        if(activePower)throw std::runtime_error("overlapping actual observation fixture");activePower=&power;
        try{
            registry.root(root);registry.number(L"ActiveUser",42);registry.number(L"pid",steam.pi.dwProcessId);
            require(g_steamSettingsObserver.start([](bool){},[]{return false;}),"actual common Observer starts");
            require(until([&]{auto r=g_steamSettingsObserver.identitySnapshot();return r&&r->presence==1&&r->session.pid==steam.pi.dwProcessId&&r->windowsSessionKnown&&r->session.account==42;}),"actual common Observer publishes Steam");
            // One immutable child image per fresh fixture directory; subsequent
            // exact cases share the same bytes but own fresh held processes.
            if(!CopyFileW(child.c_str(),gamePath.c_str(),TRUE)&&GetLastError()!=ERROR_FILE_EXISTS)throw std::runtime_error("observation game image copy");
            game=std::make_unique<Child>(gamePath);
            std::filesystem::create_directories(root+L"\\logs");seed();
        }catch(...){g_steamSettingsObserver.stop();activePower=nullptr;throw;}
    }
    ~ObservationFixture(){fixtureFreshReadHook={};g_powerLifecycle=PowerLifecycle::Ready;g_steamSettingsObserver.stop();activePower=nullptr;}
    SleepSteamRecovery::Session client(){const auto r=g_steamSettingsObserver.identitySnapshot();SleepSteamRecovery::Session v;v.pid=r->session.pid;v.created=r->session.created;v.root=r->session.root;return v;}
    SleepSteamRecovery::Game target(){FILETIME b{},e{},k{},u{};if(!GetProcessTimes(game->pi.hProcess,&b,&e,&k,&u))throw std::runtime_error("real game birth");return {game->pi.dwProcessId,(ULONGLONG(b.dwHighDateTime)<<32)|b.dwLowDateTime,gamePath};}
    void write(const std::wstring& path,const std::string& bytes){std::ofstream out(std::filesystem::path(path),std::ios::binary|std::ios::trunc);out<<bytes;if(!out)throw std::runtime_error("actual observation log write");}
    void seed(){const auto t=target();const auto at=SleepSteamRecovery::localSecond(t.created);const auto id=std::to_string((std::uint64_t(2399932135U)<<32)|0x02000000ULL);
        write(root+L"\\logs\\gameprocess_log.txt","["+at+"] AppID "+id+" adding PID "+std::to_string(t.pid)+" as a tracked process \""+W2U(t.path)+"\"\n");
        write(root+L"\\logs\\console_log.txt","["+at+"] Game process added : AppID "+id+" fixture, ProcID "+std::to_string(t.pid)+", IP 0.0.0.0:0\n");}
    std::shared_ptr<Scope> observe(){return ymcc::steamnative::acquireReadScope(g_steamSettingsObserver,g_steamSettingsObserver.identitySnapshot(),access(),ymcc::steamnative::ReadPurpose::ObserveOriginal,{&readPower,&observeObservationPower});}
};
static void observeDuringSuspend(const std::wstring& root,const std::wstring& child){
    ObservationFixture f(root,child);f.power.pending();g_powerLifecycle=PowerLifecycle::Suspending;
    auto scope=f.observe();require(scope&&scope->current(),"observation admits actual identity outside Ready");
    require(!ymcc::sleeppower::powerAdmitted(scope->power())&&quiet(scope->powerCancellationEvent())&&quiet(scope->identityCancellationEvent()),"observation has read cancellation event but no Ready stamp");
    const auto client=f.client();const auto game=f.target();NativeDetectedGame selected;selected.pid=game.pid;selected.processCreated=game.created;selected.path=game.path;
    require(!nativeGameFactScope(selected,g_gameRulesEpoch.load(),currentPowerGeneration())&&!SteamGameLink::tracking(client,game).complete,"original game scope and GameLink Ready gates stay strict");
    const auto beforeGame=fixtureFreshGameReads,beforeConsole=fixtureFreshConsoleReads;
    const auto actual=nativeSteamNativeObservationTracking(client,game,[&]{return scope->current();});
    require(actual.complete&&actual.binding.status=="tracked"&&actual.binding.exactPath&&actual.gameId=="10307630032478011392"&&actual.primary.value("originalAllowed",false)&&actual.primary.value("nativePrimaryVerified",false),"actual nonReady public source pair preserves exact full GameID and native-primary join");
    require(fixtureFreshGameReads==beforeGame+1&&fixtureFreshConsoleReads==beforeConsole+1,"one actual game and console bounded source observation");
    require(!ymcc::steamnative::acquireReadScope(g_steamSettingsObserver,g_steamSettingsObserver.identitySnapshot(),access())&&!ymcc::steamnative::acquireReadScope(g_steamSettingsObserver,g_steamSettingsObserver.identitySnapshot(),access(),ymcc::steamnative::ReadPurpose::PrepareMutation),"default and explicit mutation captures reject actual nonReady power");
}
static void observeIdentityRevocation(const std::wstring& root,const std::wstring& child){
    ObservationFixture f(root,child);f.power.pending();g_powerLifecycle=PowerLifecycle::Suspending;auto scope=f.observe();require(scope&&scope->current(),"read-only scope before actual account change");
    bool changed=false;fixtureFreshReadHook=[&](const std::wstring& file){if(changed||!file.ends_with(L"gameprocess_log.txt"))return;changed=true;f.registry.number(L"ActiveUser",43);require(until([&]{auto r=g_steamSettingsObserver.identitySnapshot();return r&&r->session.account==43;}),"actual account publication while source is held");};
    const auto value=nativeSteamNativeObservationTracking(f.client(),f.target(),[&]{return scope->current();});fixtureFreshReadHook={};
    require(changed&&!value.complete&&!scope->current()&&signalled(scope->identityCancellationEvent()),"read-only source cannot publish after actual account revocation");
}
static void observeHeldGameExit(const std::wstring& root,const std::wstring& child){
    ObservationFixture f(root,child);f.power.pending();g_powerLifecycle=PowerLifecycle::Resuming;auto scope=f.observe();require(scope&&scope->current(),"observation identity before game exit");
    fixtureFreshReadHook=[&](const std::wstring& file){if(file.ends_with(L"gameprocess_log.txt"))f.game->finish();};
    const auto value=nativeSteamNativeObservationTracking(f.client(),f.target(),[&]{return scope->current();});fixtureFreshReadHook={};
    require(!value.complete&&scope->current()&&WaitForSingleObject(f.game->pi.hProcess,0)==WAIT_OBJECT_0,"actual held game exit rejects source even with live Steam identity scope");
}
static void observeMissingConsole(const std::wstring& root,const std::wstring& child){
    ObservationFixture f(root,child);f.power.pending();g_powerLifecycle=PowerLifecycle::Suspending;auto scope=f.observe();require(scope&&DeleteFileW((root+L"\\logs\\console_log.txt").c_str()),"actual missing console setup");
    const auto value=nativeSteamNativeObservationTracking(f.client(),f.target(),[&]{return scope->current();});
    require(value.complete&&value.binding.status=="tracked"&&value.primary.value("originalAllowed",false)&&!value.primary.value("nativePrimaryVerified",true),"read-only actual missing-console fallback remains explicitly unverified");
    const auto beforeGame=fixtureFreshGameReads,beforeConsole=fixtureFreshConsoleReads;auto denied=nativeSteamNativeObservationTracking(f.client(),f.target(),[](){return false;});
    require(!denied.complete&&fixtureFreshGameReads==beforeGame&&fixtureFreshConsoleReads==beforeConsole,"cancelled observation has no source read or fallback");
}

static void observePowerStampSticky(const std::wstring& root,const std::wstring& child){
    ObservationFixture f(root,child);f.power.pending();g_powerLifecycle=PowerLifecycle::Suspending;auto scope=f.observe();require(scope&&scope->current(),"read-only original Native stamp");
    f.power.forwardDiagnostic();require(scope->current(),"same stamp diagnostic revision does not revoke observation");
    {std::lock_guard lock(f.power.mutex);f.power.state.phase=2;++f.power.state.observedNotificationSequence;++f.power.state.publicationRevision;}
    f.power.mirrors.revoke();require(signalled(scope->powerCancellationEvent()),"original Native mirror signals nonReady observation cancellation");
    {std::lock_guard lock(f.power.mutex);f.power.state.phase=0;++f.power.state.publicationRevision;}
    require(!scope->current()&&signalled(scope->powerCancellationEvent()),"Ready cannot revive read-only operation from another Native sequence");
}
static void observePowerUnknownSticky(const std::wstring& root,const std::wstring& child){
    ObservationFixture f(root,child);f.power.pending();g_powerLifecycle=PowerLifecycle::Suspending;auto scope=f.observe();require(scope&&scope->current(),"observation before Native becomes unknown");
    {std::lock_guard lock(f.power.mutex);f.power.state.flags=0;++f.power.state.publicationRevision;}
    require(!scope->current()&&signalled(scope->powerCancellationEvent()),"unknown unique Native reader fails closed");
    {std::lock_guard lock(f.power.mutex);f.power.state.flags=ymcc::sleeppower::kKnown|ymcc::sleeppower::kNotificationPending;++f.power.state.publicationRevision;}
    require(!scope->current(),"later known state does not revive old read observation");
}
struct Case{const char* name;void(*run)(const std::wstring&,const std::wstring&);};
#define NATIVE_READ_SCOPE_CASE(name,fn) {name,&fn}
static const Case cases[]={
    NATIVE_READ_SCOPE_CASE("native-read-observation-power-stamp-sticky",observePowerStampSticky),
    NATIVE_READ_SCOPE_CASE("native-read-observation-power-unknown-sticky",observePowerUnknownSticky),
    NATIVE_READ_SCOPE_CASE("native-read-observation-suspending-actual-public-source",observeDuringSuspend),
    NATIVE_READ_SCOPE_CASE("native-read-observation-actual-account-revocation",observeIdentityRevocation),
    NATIVE_READ_SCOPE_CASE("native-read-observation-actual-held-game-exit",observeHeldGameExit),
    NATIVE_READ_SCOPE_CASE("native-read-observation-missing-console-and-cancel",observeMissingConsole),

    NATIVE_READ_SCOPE_CASE("native-read-closed-api-no-ui-pure-current",closedApi),
    NATIVE_READ_SCOPE_CASE("native-read-strict-tuple-epochs-session-publisher-metadata",strictInputsAndMetadata),
    NATIVE_READ_SCOPE_CASE("native-read-same-epoch-refresh",sameEpochRefresh),
    NATIVE_READ_SCOPE_CASE("native-read-account-aba-sticky",accountAba),
    NATIVE_READ_SCOPE_CASE("native-read-zero-account-candidate",zeroCandidate),
    NATIVE_READ_SCOPE_CASE("native-read-unknown-account-candidate",unknownCandidate),
    NATIVE_READ_SCOPE_CASE("native-read-account-error-recovery-sticky",readErrorRecovery),
    NATIVE_READ_SCOPE_CASE("native-read-registered-pid-epoch-sticky",registeredPidEpoch),
    NATIVE_READ_SCOPE_CASE("native-read-observer-stop-restart",observerStop),
    NATIVE_READ_SCOPE_CASE("native-read-power-registration-race",registrationRace),
    NATIVE_READ_SCOPE_CASE("native-read-power-pending-ready-sticky",pendingReadySticky),
    NATIVE_READ_SCOPE_CASE("native-read-forward-diagnostic-revision",forwardRevision),
    NATIVE_READ_SCOPE_CASE("native-read-caller-cancel-local-sticky",callerCancellation),
    NATIVE_READ_SCOPE_CASE("native-read-shared-callback-borrowed-lifetime",sharedCallbackLifetime),
    NATIVE_READ_SCOPE_CASE("native-read-actual-steam-exit-held-pin",actualSteamExit)
};
#undef NATIVE_READ_SCOPE_CASE
static bool knownCase(const std::string& name){for(const auto& row:cases)if(name==row.name)return true;return false;}
static int run(const std::wstring& root,const std::wstring& child,const std::string& filter,bool observationOnly=false){
    json checks=json::array();unsigned failures=0;
    for(const auto& row:cases){
        if(observationOnly&&std::string_view(row.name).find("native-read-observation-")!=0)continue;
        if(!filter.empty()&&filter!=row.name)continue;
        const auto before=assertions,start=static_cast<unsigned>(GetTickCount64());std::string error;
        try{row.run(root,child);}catch(const std::exception& e){error=e.what();}catch(...){error="non-standard fixture exception";}
        const bool pass=error.empty();if(!pass)++failures;
        checks.push_back({{"name",row.name},{"pass",pass},{"assertions",assertions-before},{"elapsedMs",static_cast<unsigned>(GetTickCount64())-start},{"error",error}});
    }
    std::cout<<"NATIVE_READ_SCOPE_JSON "<<json({{"selection","native-read-scope"},{"caseFilter",filter},{"checks",checks},{"failures",failures},
        {"caseCount",checks.size()},{"assertions",assertions},{"realSteamOperations",0},{"actualObserver",true},{"actualFixtureSteamChild",true},
        {"powerProducer","pure-reader-with-original-RevocationMirrors"},{"fullOriginalObserverSuiteRun",false}}).dump()<<std::endl;
    return failures?2:0;
}
} // namespace NativeReadScopeFixture

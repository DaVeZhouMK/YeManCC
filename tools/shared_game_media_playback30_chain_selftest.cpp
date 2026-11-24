#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../native/shared_game_media_download_supervisor.h"
#include "../native/power_source_channel.h"
#include <iostream>

using namespace ymcc::media;
using namespace ymcc::media::downloads;
using namespace ymcc::sleeppower;

static RevocationMirrors mirrors;
static remote::NativeRemoteTransport* pipeOwner=nullptr;
static std::atomic<unsigned> powerStep{1};
static int __cdecl readPower(SharedPowerV1* value,uint32_t bytes) {
    if(!value||bytes!=sizeof(*value))return 0;
    const auto step=powerStep.load();
    *value=SharedPowerV1{};value->generation=step;value->publicationRevision=step;
    value->phase=step==2?1:0;value->flags=step==2?kKnown:kKnown|kHardwareAllowed;return 1;
}
static remote::NativeRemoteTransport* transport() noexcept{return pipeOwner;}
static void expect(bool condition,const char* name){if(!condition)throw std::runtime_error(name);}
template<class Predicate> static bool waitFor(DWORD budget,Predicate predicate){
    const auto started=GetTickCount64();
    do{if(predicate())return true;Sleep(30);}while(GetTickCount64()-started<budget);
    return predicate();
}

int wmain(int argc,wchar_t** argv) {
    try {
        const auto mode=argc==4?std::wstring_view(argv[3]):std::wstring_view{};
        const bool cpuShutdown=mode==L"--cpu-shutdown";
        const bool idleWait=mode==L"--idle-wait";
        const bool launchRecovery=mode==L"--launch-recovery",launchBudget=mode==L"--launch-budget";
        const bool replacePending=mode==L"--replace-pending",replaceActive=mode==L"--replace-active";
        expect(argc==3||cpuShutdown||idleWait||launchRecovery||launchBudget||replacePending||replaceActive,"fresh data root, source and optional fixture mode required");
        const auto app=fs::path(ymcc::steaminfo::transfer::module()).parent_path();
        const auto child=app/L"CustomSteamLibrary"/L"CustomSteamLibrary.exe";
        const fs::path root=fs::absolute(argv[1]);
        expect(!fs::exists(root)&&root.parent_path()==app,"fixture data must be fresh and adjacent to fixture executable");
        expect(fs::is_regular_file(child),"build the real child first");
        expect(SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",root.c_str())!=FALSE,"set isolated cache root");
        fs::create_directories(root);
        const auto source=ymcc::powerSource::refresh(true);
        expect(source.known&&source.ac==1&&source.status.SystemStatusFlag!=1,"fixture requires measured AC and saver OFF");
        expect(ymcc::powerSource::channel::startPublisher()&&ymcc::powerSource::channel::publish(source),"publish physical power on the existing channel");
        remote::NativeProvider provider(&readPower,mirrors);remote::NativeRemoteTransport pipe(provider);pipeOwner=&pipe;
        static const ymcc::sleeppower::task::Access access{&readPower,&mirrors,&transport};
        expect(ymcc::sleeppower::task::bind(access),"bind real shared-sleep transport");
        Store store(root);
        Json request={{"appId",570},{"slot","dynamic"}};
        request["bindingRevision"]=store.registerEntry(request)["bindingRevision"];
        const auto original=store.publish(request,fs::absolute(argv[2]),true);
        const auto claim=Store::playback30Claim(original);
        expect(!claim.empty(),"fixture source must need cadence verification or conversion");
        std::atomic<double> cpu{0};
        std::atomic<unsigned> acChecks{0};
        Limits limits;limits.startup=0;limits.monitor=200;
        struct Attempt {std::string id;int code;std::string stage;DWORD error;uint64_t tick;bool retry;};
        std::array<Attempt,4> attempts{};
        std::atomic<size_t> attemptCount{0};
        Controller controller(root,child,[](const Json&){},limits,ResourcePorts{[&]{++acChecks;return 1;},[&](uint64_t){return std::optional<double>{cpu.load()};},[]{return false;},
            [&](const std::string& id,int code,const char* stage,DWORD error,uint64_t,bool retry){
                const auto index=attemptCount.load(std::memory_order_relaxed);
                if(index<attempts.size())attempts[index]={id,code,stage,error,GetTickCount64(),retry};
                attemptCount.store(index+1,std::memory_order_release);
            }});
        controller.background({{"enabled",false},{"pauseReason",""},{"cpuThreshold",15}});
        const auto selected=[&]{return store.playback30Selection(store.resolve(request));};
        const auto originalCurrent=[&]{const auto entry=store.resolve(request);return entry.value("bindingRevision",uint64_t{0})==original.value("bindingRevision",uint64_t{1})&&
            entry["asset"]["id"]==original["asset"]["id"]&&fs::is_regular_file(store.assetPath(entry["asset"]));};
        const auto staging=store.playback30Staging(std::string(32,'a')).parent_path();
        const auto activeStage=[&]{
            std::error_code error;
            for(fs::directory_iterator it(staging,error),end;!error&&it!=end;it.increment(error)){
                if(it->path().extension()==L".mp4"&&it->is_regular_file(error))return it->path();
            }
            return fs::path{};
        };
        const auto setIdle=[&](bool idle){controller.background({{"enabled",false},{"pauseReason",idle?"":"game-running"},{"cpuThreshold",15},{"playback30Idle",idle}});};
        if(replacePending||replaceActive){
            setIdle(false);
            controller.requestPlayback30(claim);
            if(replaceActive){
                setIdle(true);
                expect(waitFor(15000,[&]{return !activeStage().empty();}),"old worker starts before source replacement");
            }
            request["bindingRevision"]=original["bindingRevision"];
            const auto replacement=store.publish(request,fs::absolute(argv[2]),true);
            const auto replacementClaim=Store::playback30Claim(replacement);
            expect(!replacementClaim.empty()&&replacementClaim["id"]!=claim["id"],"a new binding has its own conversion claim");
            controller.requestPlayback30(replacementClaim);
            for(unsigned i=0;i<8;++i)controller.requestPlayback30(claim); // Delayed old reads cannot take back the slot.
            if(replaceActive){
                setIdle(false);
                expect(waitFor(4500,[&]{return attemptCount.load()==1&&activeStage().empty();}),"idle withdrawal retires the old worker");
                const auto& old=attempts[0];
                std::cout<<Json({{"retiredOldId",old.id==stringField(claim,"id")},{"code",old.code},{"retry",old.retry}}).dump()<<std::endl;
                expect(old.id==stringField(claim,"id")&&old.code==17&&!old.retry,"withdrawal must not requeue a replaced source");
            }
            Json otherRequest={{"appId",730},{"slot","dynamic"}};
            otherRequest["bindingRevision"]=store.registerEntry(otherRequest)["bindingRevision"];
            const auto other=store.publish(otherRequest,fs::absolute(argv[2]),true);
            const auto otherClaim=Store::playback30Claim(other);
            expect(!otherClaim.empty(),"unrelated game needs its own derivative");
            controller.requestPlayback30(otherClaim);
            const size_t retired=replaceActive?1:0,expected=retired+2;
            expect(attemptCount.load()==retired,"no extra worker before idle grant");
            setIdle(true);
            expect(waitFor(replaceActive?150000:28000,[&]{return attemptCount.load()>=expected;}),"the replacement and unrelated game finish");
            const auto staleStarted=waitFor(4500,[&]{return attemptCount.load()>expected;});
            auto rows=Json::array();
            for(size_t i=0;i<(std::min)(attemptCount.load(),attempts.size());++i){
                const auto& item=attempts[i];
                rows.push_back({{"asset",item.id==stringField(claim,"id")?"old":item.id==stringField(replacementClaim,"id")?"replacement":item.id==stringField(otherClaim,"id")?"other":"unknown"},{"code",item.code},{"stage",item.stage},{"retry",item.retry}});
            }
            const auto current=store.resolve(request),variant=store.playback30Selection(current);
            const auto otherVariant=store.playback30Selection(store.resolve(otherRequest));
            const auto ready30=[&](const Json& asset){
                if(asset.empty())return false;
                const auto cadence=frameProbe::inspectMp4(store.assetPath(asset));
                return cadence&&!cadence.exceeds30()&&cadence.averageFps()>=29.99;
            };
            const auto currentSource=current["asset"]["id"]==replacement["asset"]["id"];
            const auto outputsReady=ready30(variant)&&ready30(otherVariant);
            const auto autoDownload=controller.snapshot().value("enabled",true);
            expect(waitFor(4500,[&]{return controller.finishForExit()&&activeStage().empty()&&SourceJob::reap()==0;}),"worker and staging retire naturally");
            std::cout<<Json({{"attempts",rows},{"staleStarted",staleStarted},{"currentSource",currentSource},{"outputsReady",outputsReady},{"autoDownload",autoDownload}}).dump()<<std::endl;
            expect(currentSource&&outputsReady&&!autoDownload,"new and unrelated material verified at 30 FPS without changing download policy");
            expect(!staleStarted&&rows.size()==expected&&rows[retired]["code"]==20&&rows[retired+1]["code"]==20,"superseded source must not start an extra worker");
            return 0;
        }
        if(idleWait){
            setIdle(false);
            controller.requestPlayback30(claim);
            const auto heldAt=GetTickCount64();
            Sleep(24500); // Includes both the first due time and one legacy retry.
            const auto blockedChecks=acChecks.load();
            const auto heldMs=GetTickCount64()-heldAt;
            expect(selected().empty()&&originalCurrent()&&activeStage().empty()&&attemptCount.load()==0,
                "a game-running policy leaves the original intact and never starts the converter");
            const auto admitted=GetTickCount64();
            setIdle(true);
            expect(waitFor(12000,[&]{return !activeStage().empty();}),"policy wake starts eligible work without another request");
            const auto startAfterGrantMs=GetTickCount64()-admitted;
            Json variant;
            expect(waitFor(23000,[&]{variant=selected();return !variant.empty();}),"real worker publishes after returning to idle");
            const auto cadence=frameProbe::inspectMp4(store.assetPath(variant));
            expect(cadence&&cadence.averageFps()>=29.99&&!cadence.exceeds30()&&originalCurrent(),"valid 30 FPS derivative and unchanged original");
            expect(waitFor(5000,[&]{return attemptCount.load()>0;}),"terminal attempt receipt precedes owner shutdown");
            const auto& attempt=attempts[0];
            std::cout<<Json({{"attempts",attemptCount.load()},{"code",attempt.code},{"stage",attempt.stage},
                {"retry",attempt.retry},{"autoDownload",controller.snapshot().value("enabled",true)}}).dump()<<std::endl;
            expect(attemptCount.load()==1&&attempt.code==20&&attempt.stage=="child-exit"&&!attempt.retry&&
                !controller.snapshot().value("enabled",true),"one successful worker; auto download stays off");
            expect(waitFor(4500,[&]{return controller.finishForExit()&&activeStage().empty()&&SourceJob::reap()==0;}),"owner and worker retire naturally");
            std::cout<<Json({{"ok",blockedChecks==0},{"blockedAcChecks",blockedChecks},{"heldMs",heldMs},
                {"startAfterGrantMs",startAfterGrantMs},{"fps",cadence.averageFps()},{"frames",cadence.samples},{"variant",variant["id"]}}).dump()<<'\n';
            expect(blockedChecks==0,"no resource probes while waiting for the idle-policy event");
            return 0;
        }
        if(launchRecovery||launchBudget){
            // An exclusive open keeps the exact packaged helper from being
            // mapped by CreateProcess. The fixture never changes its bytes.
            ymcc::steaminfo::transfer::Handle withheld(CreateFileW(child.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr));
            expect(withheld.value!=INVALID_HANDLE_VALUE,"hold isolated child image");
            const auto originalId=original["asset"]["id"].get<std::string>();
            controller.requestPlayback30(claim);setIdle(true);
            expect(waitFor(12000,[&]{return attemptCount.load()>=1;}),"observe real launch rejection");
            const auto& first=attempts[0];
            expect(first.id==originalId&&first.stage=="create-process"&&first.code==24&&first.error==ERROR_SHARING_VIOLATION,"real sharing violation before child start");
            expect(selected().empty()&&originalCurrent(),"first failure leaves original undisturbed");
            std::cout<<Json({{"firstCode",first.code},{"win32",first.error},{"retry",first.retry}}).dump()<<std::endl;
            if(launchRecovery){
                expect(CloseHandle(std::exchange(withheld.value,nullptr))!=FALSE,"release isolated child image");
                expect(waitFor(39500,[&]{return attemptCount.load()>=2;}),"transient launch failure recovers without another selection");
                const auto& second=attempts[1];const auto variant=selected();
                expect(first.retry&&second.id==originalId&&second.code==20&&second.stage=="child-exit"&&!second.retry,"retry ran the exact packaged child once");
                expect(second.tick-first.tick>=19000,"recovery observes the slow retry interval");
                expect(!variant.empty()&&originalCurrent(),"retry publishes derivative while preserving original");
                const auto cadence=frameProbe::inspectMp4(store.assetPath(variant));
                expect(cadence&&cadence.averageFps()>=29.99&&!cadence.exceeds30(),"retry output verified 30 FPS");
                std::cout<<Json({{"ok",true},{"code",second.code},{"retryPublishedMs",second.tick-first.tick},
                    {"fps",cadence.averageFps()},{"frames",cadence.samples},{"variant",variant["id"]}}).dump()<<'\n';
            }else{
                expect(waitFor(30000,[&]{return attemptCount.load()>=2;}),"launch failure gets one attempt after backoff");
                const auto& second=attempts[1];
                expect(first.retry&&second.code==24&&second.error==ERROR_SHARING_VIOLATION&&!second.retry,"second failure exhausts transient retry");
                expect(CloseHandle(std::exchange(withheld.value,nullptr))!=FALSE,"release after terminal failure");
                controller.requestPlayback30(claim);
                expect(!waitFor(22000,[&]{return attemptCount.load()>2;}),"no third launch during terminal cooldown even on reselection");
                expect(selected().empty()&&originalCurrent(),"terminal failure cannot replace the source");
                std::cout<<Json({{"ok",true},{"attempts",attemptCount.load()},{"retryFailedMs",second.tick-first.tick}}).dump()<<'\n';
            }
            expect(waitFor(4500,[&]{return controller.finishForExit()&&activeStage().empty()&&SourceJob::reap()==0;}),"attempts retire jobs and leave no staging");
            expect(controller.snapshot().value("enabled",true)==false,"retry does not enable automatic downloads");
            return 0;
        }
        controller.requestPlayback30(claim);
        Sleep(3400);
        expect(selected().empty()&&pipe.pipeName().empty(),"old renderer/no idle proof cannot start child");
        controller.background({{"enabled",false},{"pauseReason","game-running"},{"playback30Idle",true}});
        expect(selected().empty()&&pipe.pipeName().empty(),"game running is not an idle grant");
        const auto started=GetTickCount64();setIdle(true);
        fs::path first,second;
        expect(waitFor(15000,[&]{first=activeStage();return !first.empty();}),"idle grant starts real child staging");
        expect(selected().empty()&&originalCurrent(),"original is still usable while transcode is running");
        if(cpuShutdown){
            // Hold the same idle grant throughout; only the supervisor's CPU
            // observation can cause the first cancellation.
            const auto loadAt=GetTickCount64();cpu.store(91.0);
            expect(waitFor(4500,[&]{return activeStage().empty();}),"CPU veto retires the running child's staging file");
            const auto cpuCancelMs=GetTickCount64()-loadAt;
            expect(selected().empty()&&originalCurrent(),"CPU veto publishes nothing");
            expect(!waitFor(22000,[&]{return !activeStage().empty();}),"CPU veto inhibits retry even after its backoff expires");
            const auto calmAt=GetTickCount64();cpu.store(0.0);
            expect(waitFor(33500,[&]{second=activeStage();return !second.empty()&&second!=first;}),"calm CPU permits a fresh child without an extra busy backoff");
            const auto retryMs=GetTickCount64()-calmAt;
            expect(retryMs>=CpuGate::calmWindowMs,"low usage still has to clear the original calm window");
            const auto stopAt=GetTickCount64();
            expect(waitFor(4500,[&]{return controller.finishForExit()&&activeStage().empty()&&SourceJob::reap()==0;}),"exit retires the active converter and its staging file");
            expect(selected().empty()&&originalCurrent(),"exit publishes nothing and preserves the original");
            std::cout<<Json({{"ok",true},{"cpuCancelMs",cpuCancelMs},{"retryMs",retryMs},
                {"shutdownMs",GetTickCount64()-stopAt},{"protocolProcess",true}}).dump()<<'\n';
            return 0;
        }
        const auto withdraw=GetTickCount64();setIdle(false);
        expect(waitFor(4500,[&]{return activeStage().empty();}),"game-start revocation returns the exact staging file");
        const auto gameCancelMs=GetTickCount64()-withdraw;
        expect(selected().empty()&&originalCurrent(),"game-start revocation publishes nothing");
        setIdle(true);
        expect(waitFor(33000,[&]{second=activeStage();return !second.empty()&&second!=first;}),"same source retries with a new staging owner after idle returns");
        const auto sleepAt=GetTickCount64();powerStep.store(2);mirrors.revoke();
        expect(waitFor(4500,[&]{return activeStage().empty();}),"shared sleep revocation returns the second staging file");
        const auto sleepCancelMs=GetTickCount64()-sleepAt;
        expect(selected().empty()&&originalCurrent(),"shared sleep revocation publishes nothing");
        powerStep.store(3);setIdle(false);setIdle(true);
        Json reduced;
        expect(waitFor(39000,[&]{reduced=selected();return !reduced.empty();}),"real child retries after shared Ready");
        expect(!reduced.empty()&&!pipe.pipeName().empty(),"real headless child publishes after known idle despite download OFF");
        const auto cadence=frameProbe::inspectMp4(store.assetPath(reduced));
        expect(cadence&&cadence.averageFps()>=29.99&&!cadence.exceeds30(),"verify produced output by its own MP4 sample table");
        expect(originalCurrent(),"real worker did not change editor source or binding revision");
        expect(controller.snapshot().value("enabled",true)==false,"no automatic downloads enabled by conversion");
        const auto stopAt=GetTickCount64();
        while(!controller.finishForExit()&&GetTickCount64()-stopAt<3000)Sleep(20);
        expect(controller.finishForExit()&&SourceJob::reap()==0&&activeStage().empty(),"real child Jobs, watcher and staging returned");
        std::cout<<Json({{"ok",true},{"elapsedMs",GetTickCount64()-started},{"fps",cadence.averageFps()},
            {"frames",cadence.samples},{"variant",reduced["id"]},{"protocolProcess",true},
            {"gameCancelMs",gameCancelMs},{"sleepCancelMs",sleepCancelMs}}).dump()<<'\n';
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

// Real same-image Owner -> private entry -> production child refusal consumer.
#include "../native/decky_sidebar_diagnostics.h"
// No successful Steam scope is admitted and no CDP/installed application is used.
#include "../native/decky_binding_worker_entry.h"
#include <fstream>
#include <iostream>
namespace {
using J=nlohmann::json;
using namespace ymcc::deckybinding;
void need(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
J request(std::uint64_t generation){return{{"schema",worker::schema},{"id",std::string(32,'a')},{"generation",std::to_string(generation)},{"steam",{{"root","C:/source-fixture/Steam"},{"pid",GetCurrentProcessId()},{"created","123456789012345678"},{"account",12345678},{"windowsSession",0}}},{"bootstrap",{{"endpoint","ws://127.0.0.1:1/mirror"},{"token",std::string(64,'b')},{"runId",std::string(32,'c')}}},{"requireFreshContext",true},{"retainedFrontendOnly",false}};}
std::vector<std::uint8_t> bytes(const std::string& value){return{value.begin(),value.end()};}
J state(const Snapshot& s){return{{"processId",s.processId},{"exitCode",s.exitCode},{"handles",s.handles},{"activeCalls",s.activeCalls},{"pendingIo",s.pendingIo},{"jobActiveProcesses",s.jobActiveProcesses},{"childExited",s.childExited},{"fullyDestroyed",s.fullyDestroyed}};}
}
int main(int argc,char** argv){
    int workerExit=0;if(ymcc::deckybinding::entry::tryRunCommandLine(GetCommandLineW(),workerExit))return workerExit;
    if(argc!=2)return 3;J cases=J::array();
    const auto test=[&](const char* name,const auto& body){try{auto row=body();row["name"]=name;row["passed"]=true;cases.push_back(std::move(row));}catch(const std::exception& e){cases.push_back({{"name",name},{"passed",false},{"reason",e.what()}});}};
    test("actual-worker-refuses-unqualified-session-with-correlated-packet",[]{
        Owner owner;need(static_cast<bool>(owner.start()),"owner-start");const auto generation=owner.snapshot().generation;
        need(static_cast<bool>(owner.writeFrame(bytes(request(generation).dump()),1500,generation)),"request-send");
        const auto reply=owner.readFrame(2500,generation);need(static_cast<bool>(reply),"worker-refusal-read");
        const auto packet=J::parse(reply.payload);need(packet["schema"]==worker::schema&&packet["id"]==std::string(32,'a')&&packet["generation"]==std::to_string(generation)&&packet["kind"]=="result"&&packet["detail"]["ok"]==false&&packet["detail"]["mutated"]==false&&packet["detail"]["reason"]=="mirror-bootstrap-session-changed","bad-worker-receipt");
        need(packet.dump().find(std::string(64,'b'))==std::string::npos,"private-token-echoed");
        need(packet["detail"].value("sessionCheck",std::string{})=="steam-process-or-birth","session-check-not-reported");
        const auto diagnostic=ymcc::deckydiag::record("bootstrap",packet["detail"]);
        need(diagnostic.value("sessionCheck",std::string{})=="steam-process-or-birth","session-check-filtered-out");
        need(!ymcc::deckydiag::record("runtime",packet["detail"]).contains("sessionCheck"),"session-check-wrong-log-boundary");
        need(!ymcc::deckydiag::record("bootstrap",{{"sessionCheck","private-unrecognized-value"},{"token","private"}}).contains("sessionCheck"),"session-check-untrusted-value");
        const auto stopped=owner.stop();const auto after=owner.snapshot();
        need(stopped.fullyDestroyed&&after.handles==0&&after.pendingIo==0&&after.activeCalls==0&&after.jobActiveProcesses==0&&after.childExited,"worker-resources-not-destroyed");
        return J{{"reply",packet},{"atExit",state(after)},{"sameExecutableChild",true}};
    });
    test("malformed-input-exits-child-without-ordinary-startup-or-private-echo",[]{
        Owner owner;need(static_cast<bool>(owner.start()),"owner-start");const auto generation=owner.snapshot().generation;
        need(static_cast<bool>(owner.writeFrame(bytes("private-malformed-input"),1500,generation)),"request-send");
        const auto reply=owner.readFrame(2500,generation);need(!reply,"malformed-input-echoed");
        const auto stopped=owner.stop();const auto after=owner.snapshot();need(stopped.fullyDestroyed&&after.handles==0&&after.pendingIo==0&&after.jobActiveProcesses==0&&after.childExited,"malformed-child-residual");
        return J{{"replyCode",static_cast<int>(reply.code)},{"atExit",state(after)}};
    });
    test("loader-environment-and-exit-keep-only-bounded-facts",[]{
        const J input{{"scope","loader-environment"},{"bootstrapStage","loader-temp-prepared"},{"tempPathUnits",154u},{"tempShortRoot",true},{"rawPath","private"},{"token","private"}};
        const auto env=ymcc::deckydiag::record("bootstrap-watch",input);
        need(env.value("tempPathUnits",0u)==154&&env.value("tempShortRoot",false),"loader-temp-facts-filtered-out");
        need(!env.contains("rawPath")&&!env.contains("token"),"loader-temp-private-data-logged");
        const auto exit=ymcc::deckydiag::record("bootstrap-watch",{{"scope","loader-exit"},{"bootstrapStage","loader-exited"},{"loaderUptimeMs",1720000ull},{"crashFailures",1u}});
        need(exit.value("loaderUptimeMs",0ull)==1720000&&exit.value("crashFailures",0u)==1,"loader-exit-facts-filtered-out");
        for(const auto& bad:J::array({nullptr,true,"123",-1,32768u}))
            need(!ymcc::deckydiag::record("bootstrap-watch",{{"scope","loader-environment"},{"bootstrapStage","loader-temp-prepared"},{"tempPathUnits",bad}}).contains("tempPathUnits"),"unbounded-temp-fact-logged");
        need(!ymcc::deckydiag::record("runtime",input).contains("tempPathUnits"),"loader-temp-wrong-event");
        need(!ymcc::deckydiag::record("bootstrap-watch",{{"scope","private"},{"bootstrapStage","loader-temp-prepared"},{"tempPathUnits",154u}}).contains("tempPathUnits"),"loader-temp-wrong-scope");
        return J{{"environment",env},{"exit",exit},{"onlyBoundedFacts",true}};
    });
    test("mode-signal-loader-versions-survive-only-bounded-log-fields",[]{
        const auto record=ymcc::deckydiag::record("steam-mode-signal",{{"observedLoaderVersion","v3.3.0"},{"installedLoaderVersion","v3.3.1"},{"token","private"},{"endpoint","private"}});
        need(record.value("observedLoaderVersion",std::string{})=="v3.3.0"&&record.value("installedLoaderVersion",std::string{})=="v3.3.1","loader-versions-were-filtered-out");
        need(!record.contains("token")&&!record.contains("endpoint"),"untrusted-fields-entered-log");
        need(!ymcc::deckydiag::record("runtime",{{"observedLoaderVersion","v3.3.0"}}).contains("observedLoaderVersion"),"loader-version-wrong-boundary");
        for(const auto& bad:J::array({nullptr,true,42,"v3.3.1 private","v3..1","3.3.1",std::string(100,'9')}))
            need(!ymcc::deckydiag::record("steam-mode-signal",{{"observedLoaderVersion",bad}}).contains("observedLoaderVersion"),"invalid-version-was-logged");
        return J{{"record",record},{"onlyBoundedFacts",true}};
    });
    test("offline-ui-provenance-is-strict-and-cannot-be-smuggled-by-bootstrap",[]{
        auto value=request(1);value["steam"]["accountSource"]="verified-steam-ui";
        const auto parsed=worker::parse(value.dump());need(parsed.liveUiAccount&&parsed.steam.account!=0,"valid-offline-source-refused");
        const auto log=ymcc::deckydiag::record("steam-session-check",{{"stage","binding-identity-verified"},{"uiAccountConfirmed",true},{"accountSource","verified-steam-ui"},{"observedAccount",123},{"token","private"}});
        need(log.value("stage",std::string{})=="binding-identity-verified"&&log.value("uiAccountConfirmed",false)&&log.value("accountSource",std::string{})=="verified-steam-ui","offline-proof-diagnostic-lost");
        need(!log.contains("observedAccount")&&!log.contains("token"),"private-account-or-token-logged");
        for(const auto& bad:std::vector<J>{nullptr,true,42,"cached-last-user","registry"}){
            auto invalid=value;invalid["steam"]["accountSource"]=bad;bool refused=false;
            try{worker::parse(invalid.dump());}catch(...){refused=true;}need(refused,"untrusted-account-source-admitted");
        }
        auto zero=value;zero["steam"]["account"]=0;bool denied=false;try{worker::parse(zero.dump());}catch(...){denied=true;}need(denied,"anonymous-offline-account-admitted");
        auto smuggled=request(1);smuggled["bootstrap"]["account"]=123;J failure;
        const auto code=worker::run(smuggled.dump(),nullptr,[&](const J& packet){failure=packet;return true;});
        need(code==2&&failure["detail"]["mutated"]==false&&failure["detail"]["reason"]=="mirror-bootstrap-worker-request-invalid","bootstrap-could-nominate-account");
        return J{{"nonzeroTypedAccount",true},{"malformedSourcesRefused",true},{"bootstrapNominationRefusedBeforeMutation",true}};
    });
    test("closed-generation-cannot-reopen-production-worker",[]{
        Owner owner;const auto generation=owner.snapshot().generation;need(owner.stop().fullyDestroyed,"initial-stop");need(!owner.start(),"stopped-owner-reopened");need(!owner.writeFrame(bytes(request(generation).dump()),100,generation),"late-request-accepted");const auto after=owner.snapshot();need(!after.started&&after.handles==0&&after.childExited,"late-operation-created-resources");return J{{"atExit",state(after)}};
    });
    const auto passed=std::count_if(cases.begin(),cases.end(),[](const J& row){return row.at("passed").get<bool>();});const bool ok=static_cast<std::size_t>(passed)==cases.size();
    const J result={{"schema","ymcc.binding-owned-worker-real-entry.v1"},{"passed",ok},{"tests",cases.size()},{"passedCount",passed},{"cases",cases},{"productionOwnerEntryWorkerExecuted",true},{"sameImageChildrenOnly",true},{"successfulSteamBootstrapTested",false},{"realSteamOrDeviceActions",0},{"runtimeCallbackMigrationComplete",false},{"hostReleasePassed",false}};
    std::ofstream out(std::filesystem::absolute(argv[1]),std::ios::binary);out<<result.dump(2)<<'\n';out.close();std::cout<<"OWNED_WORKER cases="<<cases.size()<<" passed="<<passed<<'\n';return ok?0:2;
}

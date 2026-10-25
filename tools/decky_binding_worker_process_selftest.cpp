// Real same-image Owner -> private entry -> production child refusal consumer.
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
    test("closed-generation-cannot-reopen-production-worker",[]{
        Owner owner;const auto generation=owner.snapshot().generation;need(owner.stop().fullyDestroyed,"initial-stop");need(!owner.start(),"stopped-owner-reopened");need(!owner.writeFrame(bytes(request(generation).dump()),100,generation),"late-request-accepted");const auto after=owner.snapshot();need(!after.started&&after.handles==0&&after.childExited,"late-operation-created-resources");return J{{"atExit",state(after)}};
    });
    const auto passed=std::count_if(cases.begin(),cases.end(),[](const J& row){return row.at("passed").get<bool>();});const bool ok=static_cast<std::size_t>(passed)==cases.size();
    const J result={{"schema","ymcc.binding-owned-worker-real-entry.v1"},{"passed",ok},{"tests",cases.size()},{"passedCount",passed},{"cases",cases},{"productionOwnerEntryWorkerExecuted",true},{"sameImageChildrenOnly",true},{"successfulSteamBootstrapTested",false},{"realSteamOrDeviceActions",0},{"runtimeCallbackMigrationComplete",false},{"hostReleasePassed",false}};
    std::ofstream out(std::filesystem::absolute(argv[1]),std::ios::binary);out<<result.dump(2)<<'\n';out.close();std::cout<<"OWNED_WORKER cases="<<cases.size()<<" passed="<<passed<<'\n';return ok?0:2;
}

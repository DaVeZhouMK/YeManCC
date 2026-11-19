#define NOMINMAX
#include <windows.h>
#include <wincrypt.h>
#include <thread>
#include <iostream>
#include <fstream>
#include <new>
#include "adaptation_process_export.h"
#include "log_ai_index.h"
#pragma comment(lib,"advapi32.lib")
using json=nlohmann::json;
namespace fs=std::filesystem;
namespace fspath=std::filesystem;
static json checks=json::array();
static void check(const char* name,bool pass){checks.push_back({{"name",name},{"pass",pass}});}
static thread_local bool blockAllocation=false,trackAllocations=false;
static thread_local unsigned long long allocations=0;
static std::atomic<bool> allocationEntered{false},allocationRelease{false};
void* operator new(size_t size){
    if(trackAllocations)++allocations;
    if(blockAllocation){allocationEntered.store(true);while(!allocationRelease.load())std::this_thread::yield();}
    if(auto* p=std::malloc(size?size:1))return p;throw std::bad_alloc();
}
void* operator new[](size_t size){return ::operator new(size);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}
void operator delete[](void* p,size_t)noexcept{std::free(p);}
static std::string bytes(const fs::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
static json rows(const fs::path& path){json result=json::array();std::ifstream in(path);std::string line;while(std::getline(in,line))result.push_back(json::parse(line));return result;}
static std::string digest(const std::wstring& path){
    HCRYPTPROV provider=0;HCRYPTHASH hash=0;
    if(!CryptAcquireContextW(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)||!CryptCreateHash(provider,CALG_SHA_256,0,0,&hash))throw std::runtime_error("fixture SHA init");
    const auto data=bytes(path);bool ok=CryptHashData(hash,reinterpret_cast<const BYTE*>(data.data()),static_cast<DWORD>(data.size()),0)!=FALSE;
    BYTE result[32]{};DWORD size=32;ok=ok&&CryptGetHashParam(hash,HP_HASHVAL,result,&size,0);CryptDestroyHash(hash);CryptReleaseContext(provider,0);
    if(!ok)throw std::runtime_error("fixture SHA failed");const char* hex="0123456789abcdef";std::string text;for(auto b:result){text.push_back(hex[b>>4]);text.push_back(hex[b&15]);}return text;
}
using ymcc::adaptlog::Fact;
using ymcc::adaptlog::BasicFacts;
static Fact fact(unsigned type,const json& sample){return {{{"model","fixture"}},{{"type",type}},sample,"virtual-step-witness"};}
static ymcc::adaptlog::VirtualFacts& virtualAdaptationFacts(){static ymcc::adaptlog::VirtualFacts value;return value;}
static ymcc::adaptlog::Journal journal(1024,4096,32768);
[[maybe_unused]] static json virtualAdaptationStatus(){return {{"facts",virtualAdaptationFacts().status()},{"journal",journal.status()}};}

static ymcc::adaptlog::Facts& modelAdaptationFacts(){static ymcc::adaptlog::Facts value;return value;}
static ymcc::adaptlog::Signals& modelAdaptationSignals(){static ymcc::adaptlog::Signals value;return value;}
static int modelExportCases(const fs::path& root){
    using namespace ymcc::adaptlog;
    // MODEL_EXPORT_BEFORE_CONTROL
    const auto history=root/L"model-adaptation.log";const std::string historyBefore(64,'H');
    {std::ofstream out(history,std::ios::binary);out<<historyBefore;}
    Journal full(64,1,64);
    const auto omitted=full.recordResult(history,{{"model","fixture"}},"first",{{"type",0}},json::object(),"fixture");
    check("model-export-actual-full-journal-omits-without-changing-history",omitted==RecordResult::omitted&&full.status()["budgetFull"]==true&&bytes(history)==historyBefore);
    for(unsigned i=0;i<3;++i)modelAdaptationFacts().offer({{{"model","fixture-A"}},{{"type",i}},{{"first",i},{"consumerActionProven",false}},"fixture-model-fact"});
    for(unsigned i=0;i<50000;++i){Signal value;const unsigned type=i%3;
        value.type={type==0?2u:type==1?3u:32u,type,0xB2u,0x21u+type,512u,1ull<<type};value.type.reportId=7;
        value.rawBytes=512;value.raw.fill(static_cast<BYTE>(i));value.raw[0]=static_cast<BYTE>(7+type);
        value.pressed=(i&1)!=0;value.mapped=type!=2;modelAdaptationSignals().offer(value);}
    check("model-export-50000-signal-observations-retain-three-first-types",modelAdaptationSignals().status()["knownTypes"]==3);
    const auto originalSignals=modelAdaptationSignals().snapshotForExport();
    trackAllocations=true;allocations=0;const auto zeroCopy=modelAdaptationSignals().snapshotForExport();trackAllocations=false;
    check("model-export-signal-snapshot-no-json-copy-or-heap-allocation",zeroCopy.available&&zeroCopy.known==3&&allocations==0);
    size_t index=0;Fact capturedFact;while(modelAdaptationFacts().take(index,capturedFact)){
        const auto result=full.recordResult(history,capturedFact.model,capturedFact.category.c_str(),capturedFact.identity,capturedFact.sample,"fixture");
        modelAdaptationFacts().finish(index,result!=RecordResult::retry);
    }
    Signal capturedSignal;while(modelAdaptationSignals().take(index,capturedSignal)){
        const auto result=full.recordResult(history,json::object(),"signal",{{"code",capturedSignal.type.code}},json::object(),"fixture");
        modelAdaptationSignals().finish(index,result!=RecordResult::retry);
    }
    const auto factsBefore=modelAdaptationFacts().status(),signalsBefore=modelAdaptationSignals().status(),journalBefore=full.status();
    auto invoke=[&](const fs::path& directory){
        const std::wstring dataDir=directory.wstring();json availability={{"modelAdaptation",json::object()},{"files",json::array()}};
        std::vector<fs::path> included;const auto addFile=[&](const std::wstring& path){included.emplace_back(path);};
        // ACTUAL_MODEL_EXPORT_BLOCK
        availability["fixtureReadableRowsBeforeScopeEnd"]=json::array();
        for(const auto& path:included)availability["fixtureReadableRowsBeforeScopeEnd"].push_back(rows(path).size());
        return std::make_pair(availability,std::move(included));
    };
    const auto receipts=invoke(root);
    // Consume the registered paths while the actual block's RAII objects are alive.
    // The returned paths are never reused after that scope has ended.
    check("model-export-actual-main-block-registers-both-fresh-receipts",receipts.first["modelAdaptation"]["currentProcessFactsExport"]["saved"]==true&&
        receipts.first["modelAdaptation"]["currentProcessSignalsExport"]["saved"]==true&&receipts.second.size()==2&&receipts.first["files"].empty()&&
        receipts.first["fixtureReadableRowsBeforeScopeEnd"]==json::array({5,5}));
    const auto factsExport=exportRetainedFacts(root,modelAdaptationFacts(),ProcessExportKind::modelFacts);
    const auto signalsExport=exportRetainedSignals(root,modelAdaptationSignals());
    const auto factsRows=rows(factsExport.file),signalRows=rows(signalsExport.file);
    check("model-export-facts-survive-omitted-drain-with-original-model-sample-clock",factsRows.size()==5&&factsRows[1]["uniqueKey"]["model"]["model"]=="fixture-A"&&
        factsRows[1]["firstSample"]["first"]==0&&factsRows[1]["queuePhaseAtSnapshot"]=="drain-resolved"&&factsRows[1]["capturedTickMs"].is_number_unsigned());
    bool original=true;for(unsigned i=0;i<3;++i){const auto& source=*originalSignals.entries[i].value;const auto& row=signalRows[i+1];
        const auto& raw=row["firstSample"];const auto& type=row["retainedType"];const std::string hex=raw["firstRawReportHex"];
        original&=hex.size()==1024&&raw["rawBytes"]==512&&row["capturedTickMs"]==source.capturedAt&&type["source"]==source.type.source&&
            type["profile"]==source.type.profile&&type["reportId"]==7&&type["canonicalMask"]==source.type.canonicalMask&&
            raw["pressedFlag"]==source.pressed&&raw["mappedFlag"]==source.mapped&&row["modelAtCaptureKnown"]==false&&!row.contains("uniqueKey");
        for(unsigned byte=0;byte<source.rawBytes;++byte)original&=std::stoul(hex.substr(byte*2,2),nullptr,16)==source.raw[byte];
    }
    check("model-export-raw-512-byte-first-reports-types-flags-and-clocks-exact",signalRows.size()==5&&original);
    check("model-export-model-binding-unknown-and-source-coverage-never-granted",signalRows[1]["modelAtCaptureKnown"]==false&&
        signalRows[1]["modelBindingScope"].get<std::string>().find("do-not-backfill")!=std::string::npos&&
        signalRows.back()["retainedSnapshotRowsComplete"]==true&&signalRows.back()["sourceFirstWitnessCoverageComplete"]==false&&signalRows.back()["completeHistory"]==false);
    check("model-export-does-not-drain-change-counters-or-touch-permanent-history",modelAdaptationFacts().status()==factsBefore&&
        modelAdaptationSignals().status()==signalsBefore&&full.status()==journalBefore&&bytes(history)==historyBefore);
    const auto failed=invoke(root/L"missing-parent");
    check("model-export-main-failure-adds-explicit-omissions-never-old-files",failed.second.empty()&&failed.first["files"].size()==2&&
        failed.first["modelAdaptation"]["currentProcessFactsExport"]["saved"]==false&&failed.first["modelAdaptation"]["currentProcessSignalsExport"]["saved"]==false);
    Signals busy;allocationEntered=false;allocationRelease=false;
    std::thread holder([&]{blockAllocation=true;(void)busy.status();blockAllocation=false;});
    while(!allocationEntered.load())std::this_thread::yield();
    const auto busyExport=exportRetainedSignals(root,busy);allocationRelease=true;holder.join();const auto busyRows=rows(busyExport.file);
    check("model-export-busy-signal-store-is-unknown-not-empty",busyExport.receipt["saved"]==true&&busyRows[0]["snapshotAvailable"]==false&&
        busyRows[0]["knownFactsAtSnapshot"].is_null()&&busyRows.back()["retainedSnapshotRowsComplete"]==false);
    Signals bounded;for(unsigned i=0;i<129;++i){Signal value;value.type.code=i;bounded.offer(value);}
    const auto boundedExport=exportRetainedSignals(root,bounded);const auto boundedRows=rows(boundedExport.file);
    check("model-export-capacity-drops-independent-of-complete-retained-snapshot",boundedRows.size()==130&&boundedRows[0]["capacity"]==128&&
        boundedRows[0]["capacityDropsAtSnapshot"]==1&&boundedRows.back()["writtenFacts"]==128&&boundedRows.back()["sourceFirstWitnessCoverageComplete"]==false);
    Signals bad;Signal invalid;invalid.type.code=1;invalid.rawBytes=513;bad.offer(invalid);invalid.type.code=2;invalid.rawBytes=1;bad.offer(invalid);
    const auto badExport=exportRetainedSignals(root,bad);const auto badRows=rows(badExport.file);
    check("model-export-invalid-raw-length-explicit-omission-later-type-kept",badRows.size()==3&&badRows[1]["slotOrdinal"]==2&&
        badRows.back()["omittedFacts"]==1&&badRows.back()["omissions"][0]["slotOrdinal"]==1&&badRows.back()["retainedSnapshotRowsComplete"]==false);
    fs::path owned;{const auto transient=exportRetainedSignals(root,bounded);owned=transient.file;}
    check("model-export-cleans-only-owned-temporary-after-consumption",!fs::exists(owned)&&!fs::exists(owned.parent_path()));
    const auto invalidKind=exportRetainedFacts(root,modelAdaptationFacts(),static_cast<ProcessExportKind>(99));
    check("model-export-unknown-kind-does-not-create-or-reuse-payload",invalidKind.receipt["saved"]==false&&invalidKind.file.empty()&&invalidKind.receipt["file"].is_null());
    fs::copy_file(factsExport.file,root/modelFactsExportName);fs::copy_file(signalsExport.file,root/modelSignalsExportName);
    const std::vector<std::pair<std::wstring,std::wstring>> entries={{root.wstring(),modelFactsExportName},{root.wstring(),modelSignalsExportName}};
    const auto factsInspection=ymcc::logexport::stagedFileEvidence(root/modelFactsExportName,"model-adaptation-session.jsonl");
    const auto signalsInspection=ymcc::logexport::stagedFileEvidence(root/modelSignalsExportName,"model-adaptation-signals-session.jsonl");
    check("model-export-actual-ai-index-and-resident-lanes-recognize-both-streams",ymcc::logexport::writeAiLogIndex(root,entries,digest)&&
        std::string(ymcc::logexport::aiEvidenceLane("model-adaptation-session.jsonl"))=="resident"&&
        std::string(ymcc::logexport::aiEvidenceLane("model-adaptation-signals-session.jsonl"))=="resident"&&
        ymcc::logexport::aiReadingGuide().contains("modelAdaptationProcessExports")&&
        factsInspection["format"]=="json-lines"&&signalsInspection["format"]=="json-lines"&&
        factsInspection["validJsonRows"]==5&&signalsInspection["validJsonRows"]==5&&
        factsInspection["invalidJsonRows"]==0&&signalsInspection["invalidJsonRows"]==0);
    unsigned failures=0;for(const auto& item:checks)failures+=!item["pass"].get<bool>();
    std::cout<<json{{"selectedCase","adaptation-facts"},{"boundarySelection","current-model-process-export"},{"checks",checks},{"failures",failures},
        {"permanentHistoryUnchanged",bytes(history)==historyBefore},{"productOrHardwareExecuted",false},{"fixtureRoot",root.string()}}.dump(2)<<'\n';
    return failures?3:0;
}

static json rejectedDescriptor(const json& value,size_t reason){
    const auto hex=value["rejectedOfferEvidence"]["witnesses"][reason]["prefixHex"].get<std::string>();
    std::string text;for(size_t i=0;i<hex.size();i+=2)text.push_back(static_cast<char>(std::stoul(hex.substr(i,2),nullptr,16)));
    return json::parse(text);
}
static int factsRejectedCases(const fs::path& root,bool control){
    using namespace ymcc::adaptlog;
    BasicFacts<1> full;full.offer(fact(1,{{"originalSample",true}}));
    unsigned builds=0;full.offerLazy({{"model","fixture"}},{{"type",2}},"rejected-capacity",[&]{++builds;return json{{"mustNotBuild",true}};});
    const auto first=full.status();
    check("facts-first-capacity-rejection-descriptor-is-present",first.contains("rejectedOfferEvidence"));
    if(control||!first.contains("rejectedOfferEvidence")){
        const bool pass=checks[0]["pass"].get<bool>();std::cout<<json{{"checks",checks},{"failures",pass?0:1}}.dump()<<'\n';return pass?0:3;
    }
    const auto& evidence=first["rejectedOfferEvidence"];
    check("facts-capacity-rejection-keeps-original-admission-and-lazy-builder",builds==0&&first["knownFacts"]==1&&first["pendingFacts"]==1&&first["capacityDrops"]==1);
    check("facts-two-fixed-reasons-and-absent-busy-witness",evidence["maximumWitnesses"]==2&&evidence["witnesses"].size()==2&&evidence["witnesses"][0]["state"]=="not-observed"&&evidence["witnesses"][1]["reason"]=="capacity");
    check("facts-rejected-descriptor-retains-existing-type-without-sample",rejectedDescriptor(first,1)==json{{"category","rejected-capacity"},{"identity",{{"type",2}}},{"model",{{"model","fixture"}}}}&&evidence["sampleRetained"]==false);
    check("facts-no-synthetic-clock-lifetime-or-atomic-counter-proof",evidence["attemptTickKnown"]==false&&evidence["attemptTickMs"].is_null()&&evidence["writerLifetimeKnown"]==false&&evidence["historyComplete"]==false&&evidence["snapshotAtomicWithCounters"]==false);
    std::vector<Fact> repeats;repeats.reserve(50000);for(unsigned i=0;i<50000;++i)repeats.push_back(fact(100+i,json::object()));
    allocations=0;trackAllocations=true;for(auto& value:repeats)full.offer(std::move(value));trackAllocations=false;
    const auto repeated=full.status();
    check("facts-50000-capacity-rejections-add-no-allocation-over-original-teardown",allocations==100000&&repeated["capacityDrops"]==50001&&repeated["rejectedOfferEvidence"]==evidence);
    check("facts-offers-never-write-files",fs::is_empty(root));
    BasicFacts<1> busy;busy.offer(fact(1,{{"original",true}}));
    std::vector<Fact> attempts;attempts.reserve(1001);for(unsigned i=0;i<1001;++i)attempts.push_back(fact(77+i,json{{"notRetained",true}}));
    allocationEntered.store(false);allocationRelease.store(false);
    std::thread holding([&]{blockAllocation=true;(void)busy.status();blockAllocation=false;});
    struct Joined {std::thread& thread;~Joined(){allocationRelease.store(true);if(thread.joinable())thread.join();}} joined{holding};
    const auto until=GetTickCount64()+5000;
    while(!allocationEntered.load()&&GetTickCount64()<until)std::this_thread::yield();
    if(!allocationEntered.load())throw std::runtime_error("fixture mutex holder not entered");
    allocations=0;trackAllocations=true;for(auto& value:attempts)busy.offer(std::move(value));trackAllocations=false;
    allocationRelease.store(true);holding.join();
    const auto busyStatus=busy.status();
    check("facts-real-other-thread-lock-refusal-retains-original-teardown-allocation-budget",allocations==2000&&busyStatus["busyDrops"]==1001&&busyStatus["knownFacts"]==1&&busyStatus["pendingFacts"]==1);
    check("facts-busy-first-descriptor-is-original-attempt-and-never-overwritten",rejectedDescriptor(busyStatus,0)["identity"]["type"]==77&&busyStatus["rejectedOfferEvidence"]["witnesses"][1]["state"]=="not-observed");
    size_t index=0;Fact taken;const bool got=busy.take(index,taken);if(got)busy.finish(index,true);
    check("facts-rejected-descriptor-does-not-queue-or-replace-accepted-fact",got&&taken.identity["type"]==1&&taken.sample["original"]==true&&busy.status()["pendingFacts"]==0&&busy.snapshotForExport().known==1);
    BasicFacts<1> large;large.offer(fact(1,json::object()));
    const json largeIdentity={{"unicode","证据"},{"value",std::string(20000,'X')}};
    large.offer({{{"model","fixture"}},largeIdentity,json::object(),"large-rejected"});
    const auto largeStatus=large.status();const auto& largeWitness=largeStatus["rejectedOfferEvidence"]["witnesses"][1];
    const auto encoded=json{{"model",{{"model","fixture"}}},{"identity",largeIdentity},{"category","large-rejected"}}.dump();
    std::string expectedHex;const char* digits="0123456789abcdef";for(size_t i=0;i<2048;++i){const auto v=static_cast<unsigned char>(encoded[i]);expectedHex+=digits[v>>4];expectedHex+=digits[v&15];}
    check("facts-export-prefix-is-exact-and-capped-at2048-original-bytes",largeWitness["descriptorBytes"]==encoded.size()&&largeWitness["prefixBytes"]==2048&&largeWitness["prefixHex"]==expectedHex&&largeWitness["descriptorComplete"]==false);
    BasicFacts<1> invalid;invalid.offer(fact(1,json::object()));invalid.offer({json::object(),{{"bad",std::string(1,'\xFF')}},json::object(),"invalid"});
    const auto invalidStatus=invalid.status();
    check("facts-invalid-descriptor-does-not-replace-original-status",invalidStatus["capacityDrops"]==1&&invalidStatus["knownFacts"]==1&&invalidStatus["rejectedOfferEvidence"]["witnesses"][1]["state"]=="descriptor-serialization-failed");
    const auto exported=exportRetainedFacts(root,busy);const auto exportedRows=rows(exported.file);
    check("facts-export-retains-original-one-sample-and-explicit-source-gap",exported.receipt["saved"]==true&&exportedRows.size()==3&&exportedRows[0]["busyDropsAtSnapshot"]==1001&&exportedRows[2]["sourceFirstWitnessCoverageComplete"]==false);
    const auto badKind=exportRetainedFacts(root,modelAdaptationFacts(),static_cast<ProcessExportKind>(99));
    check("facts-original-invalid-kind-argument-still-rejects-before-export",badKind.receipt["saved"]==false&&badKind.file.empty()&&badKind.receipt["file"].is_null());
    json availability={{"virtualAdaptation",{{"file","virtual-adaptation.log"},{"facts",busyStatus}}},
                       {"modelAdaptation",{{"schema","ymcc.model-adaptation-status/1"},{"file","model-adaptation.log"},{"facts",repeated}}}};
    {std::ofstream out(root/L"actual-status.json",std::ios::binary);out<<availability.dump()<<'\n';}
    {std::ofstream out(root/L"actual-large-status.json",std::ios::binary);out<<json{{"virtualAdaptation",{{"file","virtual-adaptation.log"},{"facts",largeStatus}}}}.dump()<<'\n';}
    unsigned failures=0;for(const auto& c:checks)failures+=!c["pass"].get<bool>();
    std::cout<<json{{"checks",checks},{"failures",failures},{"scope","actual BasicFacts with private allocation-held mutex and real other thread;no product or devices"},{"factsSize",sizeof(BasicFacts<1>)}}.dump(2)<<'\n';return failures?3:0;
}

static int factsRejectedResumeCases([[maybe_unused]] const fs::path& root){
    using namespace ymcc::adaptlog;
    BasicFacts<1> full;full.offer(fact(1,json::object()));
    full.offerLazy({{"model","fixture"}},{{"type",2}},"rejected-capacity",[]{return json::object();});
    std::vector<Fact> repeats;repeats.reserve(50000);for(unsigned i=0;i<50000;++i)repeats.push_back(fact(77+i,json::object()));
    allocations=0;trackAllocations=true;for(auto& value:repeats)full.offer(std::move(value));trackAllocations=false;
    const auto capacityAllocations=allocations;const auto fullStatus=full.status();
    check("facts-capacity-rejections-add-no-allocation-over-original-json-teardown",capacityAllocations==100000&&fullStatus["capacityDrops"]==50001&&rejectedDescriptor(fullStatus,1)["identity"]["type"]==2);
    BasicFacts<1> busy;busy.offer(fact(1,json::object()));
    std::vector<Fact> attempts;attempts.reserve(1001);for(unsigned i=0;i<1001;++i)attempts.push_back(fact(77+i,json::object()));
    allocationEntered.store(false);allocationRelease.store(false);
    std::thread holding([&]{blockAllocation=true;(void)busy.status();blockAllocation=false;});
    struct Joined {std::thread& thread;~Joined(){allocationRelease.store(true);if(thread.joinable())thread.join();}} joined{holding};
    auto deadline=GetTickCount64()+5000;while(!allocationEntered.load()&&GetTickCount64()<deadline)std::this_thread::yield();
    if(!allocationEntered.load())throw std::runtime_error("resume holder not entered");
    allocations=0;trackAllocations=true;for(auto& value:attempts)busy.offer(std::move(value));trackAllocations=false;
    const auto busyAllocations=allocations;allocationRelease.store(true);holding.join();const auto busyStatus=busy.status();
    check("facts-busy-rejections-add-no-allocation-and-first-move-delays-two-teardowns",busyAllocations==2000&&busyStatus["busyDrops"]==1001&&rejectedDescriptor(busyStatus,0)["identity"]["type"]==77);
    BasicFacts<1> raced;raced.offer(fact(1,json::object()));
    std::vector<Fact> descriptors;for(unsigned i=0;i<8;++i)descriptors.push_back({{{"producer",10+i}},{{"type",10+i}},json::object(),"producer-"+std::to_string(10+i)});
    allocationEntered.store(false);allocationRelease.store(false);
    std::thread raceHolder([&]{blockAllocation=true;(void)raced.status();blockAllocation=false;});
    Joined raceJoined{raceHolder};deadline=GetTickCount64()+5000;
    while(!allocationEntered.load()&&GetTickCount64()<deadline)std::this_thread::yield();
    if(!allocationEntered.load())throw std::runtime_error("race holder not entered");
    std::atomic<unsigned> ready{0};std::atomic<bool> go{false};std::vector<std::thread> producers;
    for(unsigned i=0;i<8;++i)producers.emplace_back([&,i]{ready.fetch_add(1);while(!go.load())std::this_thread::yield();raced.offer(std::move(descriptors[i]));});
    deadline=GetTickCount64()+5000;while(ready.load()!=8&&GetTickCount64()<deadline)std::this_thread::yield();
    go.store(true);for(auto& thread:producers)thread.join();allocationRelease.store(true);raceHolder.join();
    const auto racedStatus=raced.status();const auto chosen=rejectedDescriptor(racedStatus,0);const auto type=chosen["identity"]["type"].get<unsigned>();
    check("facts-eight-real-concurrent-refusals-publish-one-coherent-descriptor",ready.load()==8&&racedStatus["busyDrops"]==8&&racedStatus["knownFacts"]==1&&racedStatus["pendingFacts"]==1&&type>=10&&type<18&&chosen["model"]["producer"]==type&&chosen["category"]=="producer-"+std::to_string(type));
    unsigned failures=0;for(const auto& c:checks)failures+=!c["pass"].get<bool>();
    std::cout<<json{{"checks",checks},{"failures",failures},{"capacityAllocations",capacityAllocations},{"busyAllocations",busyAllocations},
        {"allocationScope","existing JSON teardown included;compare original exact-input baseline,not absolute zero"},
        {"scope","two corrected failed allocation checks plus one new actual concurrent offer boundary;previous12 not rerun"}}.dump(2)<<'\n';return failures?3:0;
}

static ymcc::adaptlog::Signal signalFor(unsigned seed){
    ymcc::adaptlog::Signal value;value.type={seed,seed+1,seed+2,seed+3,seed+4,0x8000000000000000ull|seed,seed+5};
    value.rawBytes=512;value.raw.fill(static_cast<unsigned char>(seed));value.pressed=true;value.mapped=true;return value;
}
static json signalType(const ymcc::adaptlog::SignalType& t){return {{"source",t.source},{"profile",t.profile},{"command",t.command},
    {"code",t.code},{"shape",t.shape},{"canonicalMask",t.canonicalMask},{"reportId",t.reportId}};}
static int signalsRejectedCases(const fs::path& root,bool control){
    using namespace ymcc::adaptlog;
    Signals full;for(unsigned i=0;i<signalCapacity;++i)full.offer(signalFor(i));
    full.offer(signalFor(1000));const auto first=full.status();
    check("signals-first-capacity-refusal-type-is-present",first.contains("rejectedTypeEvidence"));
    if(control||!first.contains("rejectedTypeEvidence")){
        const bool pass=checks[0]["pass"].get<bool>();std::cout<<json{{"checks",checks},{"failures",pass?0:1}}.dump()<<'\n';return pass?0:3;
    }
    const auto& evidence=first["rejectedTypeEvidence"];
    check("signals-two-fixed-reasons-and-original-capacity-admission",first["knownTypes"]==128&&first["pendingTypes"]==128&&first["capacityDrops"]==1&&
        evidence["maximumWitnesses"]==2&&evidence["witnesses"].size()==2&&evidence["witnesses"][0]["state"]=="not-observed"&&
        evidence["witnesses"][1]["reason"]=="capacity"&&evidence["witnesses"][1]["type"]==signalType(signalFor(1000).type));
    auto repeat=signalFor(1001);allocations=0;trackAllocations=true;
    for(unsigned i=0;i<50000;++i){repeat.type=signalFor(i+2000).type;full.offer(repeat);}
    trackAllocations=false;const auto capacityAllocations=allocations;const auto after=full.status();
    check("signals-50000-capacity-refusals-zero-allocation-and-immutable-first-type",capacityAllocations==0&&after["capacityDrops"]==50001&&after["rejectedTypeEvidence"]==evidence);
    full.offer(signalFor(0));const auto duplicate=full.status();
    check("signals-known-duplicate-at-capacity-does-not-count-refusal",duplicate["capacityDrops"]==50001&&duplicate["knownTypes"]==128);
    check("signals-evidence-no-report-clock-lifetime-or-atomic-counter-claim",evidence["rawReportRetained"]==false&&evidence["attemptTickKnown"]==false&&
        evidence["writerLifetimeKnown"]==false&&evidence["historyComplete"]==false&&evidence["snapshotAtomicWithCounters"]==false&&evidence["witnesses"][1].size()==3);
    Signals busy;busy.offer(signalFor(7));allocationEntered.store(false);allocationRelease.store(false);
    std::thread holder([&]{blockAllocation=true;(void)busy.status();blockAllocation=false;});
    struct ReleaseJoin {std::thread& t;~ReleaseJoin(){allocationRelease.store(true);if(t.joinable())t.join();}} release{holder};
    auto deadline=GetTickCount64()+5000;while(!allocationEntered.load()&&GetTickCount64()<deadline)std::this_thread::yield();
    if(!allocationEntered.load())throw std::runtime_error("signals fixture lock holder not entered");
    allocations=0;trackAllocations=true;for(unsigned i=0;i<1001;++i)busy.offer(signalFor(i+77));trackAllocations=false;
    const auto busyAllocations=allocations;allocationRelease.store(true);holder.join();const auto busyStatus=busy.status();
    check("signals-real-other-thread-busy-refusals-zero-allocation-original-counters",busyAllocations==0&&busyStatus["contentionDrops"]==1001&&busyStatus["capacityDrops"]==0&&busyStatus["knownTypes"]==1&&busyStatus["pendingTypes"]==1);
    check("signals-first-busy-type-kept-separate-from-capacity",busyStatus["rejectedTypeEvidence"]["witnesses"][0]["type"]==signalType(signalFor(77).type)&&busyStatus["rejectedTypeEvidence"]["witnesses"][1]["state"]=="not-observed");
    size_t index=0;Signal taken;const bool got=busy.take(index,taken);if(got)busy.finish(index,true);
    check("signals-rejected-type-does-not-queue-or-replace-first-report",got&&taken.type==signalFor(7).type&&taken.raw==signalFor(7).raw&&taken.rawBytes==512&&taken.pressed&&taken.mapped&&!busy.pending()&&busy.snapshotForExport().known==1);
    Signals raced;allocationEntered.store(false);allocationRelease.store(false);
    std::thread raceHolder([&]{blockAllocation=true;(void)raced.status();blockAllocation=false;});ReleaseJoin raceRelease{raceHolder};
    deadline=GetTickCount64()+5000;while(!allocationEntered.load()&&GetTickCount64()<deadline)std::this_thread::yield();
    if(!allocationEntered.load())throw std::runtime_error("signals race holder not entered");
    std::atomic<unsigned> ready{0};std::atomic<bool> go{false};std::array<std::thread,8> producers;
    struct JoinAll {std::array<std::thread,8>& ts;std::atomic<bool>& go;~JoinAll(){go.store(true);for(auto& t:ts)if(t.joinable())t.join();}} joinAll{producers,go};
    for(unsigned i=0;i<8;++i)producers[i]=std::thread([&,i]{ready.fetch_add(1);while(!go.load())std::this_thread::yield();raced.offer(signalFor(i+400));});
    deadline=GetTickCount64()+5000;while(ready.load()!=8&&GetTickCount64()<deadline)std::this_thread::yield();go.store(true);
    for(auto& t:producers)t.join();allocationRelease.store(true);raceHolder.join();const auto raceStatus=raced.status();
    const auto chosen=raceStatus["rejectedTypeEvidence"]["witnesses"][0]["type"];bool coherent=false;
    for(unsigned i=0;i<8;++i)coherent|=chosen==signalType(signalFor(i+400).type);
    check("signals-eight-producers-publish-one-complete-original-type",coherent&&raceStatus["contentionDrops"]==8&&raceStatus["knownTypes"]==0&&raceStatus["pendingTypes"]==0&&raceStatus["rejectedTypeEvidence"]["witnesses"][0]["state"]=="retained");
    check("signals-offers-never-create-files",fs::is_empty(root));
    const auto exported=exportRetainedSignals(root,busy);const auto exportedRows=rows(exported.file);
    check("signals-original-export-keeps-accepted-row-and-explicit-source-gap",exported.receipt["saved"]==true&&exportedRows.size()==3&&
        exportedRows[1]["retainedType"]==signalType(signalFor(7).type)&&exportedRows.back()["sourceFirstWitnessCoverageComplete"]==false);
    const json availability={{"modelAdaptation",{{"schema","ymcc.model-adaptation-status/1"},{"file","model-adaptation.log"},{"signals",busyStatus}}}};
    {std::ofstream out(root/L"actual-status.json",std::ios::binary);out<<availability.dump()<<'\n';}
    {std::ofstream out(root/L"capacity-status.json",std::ios::binary);out<<json{{"modelAdaptation",{{"schema","ymcc.model-adaptation-status/1"},{"file","model-adaptation.log"},{"signals",after}}}}.dump()<<'\n';}
    unsigned failures=0;for(const auto& c:checks)failures+=!c["pass"].get<bool>();
    std::cout<<json{{"checks",checks},{"failures",failures},{"capacityOfferAllocations",capacityAllocations},{"busyOfferAllocations",busyAllocations},
        {"signalsBytes",sizeof(Signals)},{"scope","actual Signals/header/export;private threads and files;no devices or full writer"}}.dump(2)<<'\n';return failures?3:0;
}

int wmain(int argc,wchar_t**argv){try{
    if(argc!=2&&argc!=3)return 2;const fs::path root(argv[1]);fs::create_directories(root);
    using namespace ymcc::adaptlog;
    if(argc==3&&std::wstring(argv[2])==L"--model-only")return modelExportCases(root);
    if(argc==3&&std::wstring(argv[2])==L"--signals-rejected-only")return signalsRejectedCases(root,false);
    if(argc==3&&std::wstring(argv[2])==L"--signals-rejected-control")return signalsRejectedCases(root,true);
    if(argc==3&&std::wstring(argv[2])==L"--facts-rejected-only")return factsRejectedCases(root,false);
    if(argc==3&&std::wstring(argv[2])==L"--facts-rejected-resume")return factsRejectedResumeCases(root);
    if(argc==3&&std::wstring(argv[2])==L"--facts-rejected-control")return factsRejectedCases(root,true);
    if(argc==3){
        if(std::wstring(argv[2])!=L"--index-only")return 2;
        BasicFacts<2> selected;selected.offer(fact(1,{{"indexWitness",true}}));
        const auto current=exportRetainedFacts(root,selected);fs::copy_file(current.file,root/processExportName);
        const std::vector<std::pair<std::wstring,std::wstring>> selectedEntries={{root.wstring(),processExportName}};
        check("fallback-actual-ai-index-over-exported-bytes",ymcc::logexport::writeAiLogIndex(root,selectedEntries,digest));
        const bool passed=checks[0]["pass"].get<bool>();
        std::cout<<json{{"selectedCase","adaptation-facts"},{"boundarySelection","current-process-export-index"},{"checks",checks},{"failures",passed?0:1},{"productOrHardwareExecuted",false},{"fixtureRoot",root.string()}}.dump(2)<<'\n';return passed?0:3;
    }

    const auto history=root/L"virtual-adaptation.log";
    const std::string historyBefore(950,'H');{std::ofstream out(history,std::ios::binary);out<<historyBefore;}
    const auto omitted=journal.recordResult(history,{{"model","fixture"}},"virtual-step-witness",{{"type",0}},{{"payload",std::string(2000,'X')}},"2000-01-01T00:00:00");
    check("fallback-before-full-journal-still-omits-new-witness",omitted==RecordResult::omitted&&journal.status()["budgetFull"]==true&&journal.status()["committedThisProcess"]==0&&bytes(history)==historyBefore);
    const json original={{"observedFrom","input-host-cleanup-evidence"},{"producer",{{"closingRequestId",17},{"localCleanupResult","failure"}}},{"count",1}};
    auto& facts=virtualAdaptationFacts();for(unsigned i=0;i<50000;++i)facts.offerLazy({{"model","fixture"}},{{"type",i%3}},"virtual-step-witness",[&]{return original;});
    size_t index=0;Fact value;bool drains=true;
    while(facts.take(index,value)){const auto result=journal.recordResult(history,value.model,value.category.c_str(),value.identity,value.sample,"now");drains&=result==RecordResult::omitted;facts.finish(index,result!=RecordResult::retry);}
    const auto before=facts.status();
    check("fallback-50000-repeats-retain-three-types-after-omitted-drain",drains&&before["knownFacts"]==3&&before["pendingFacts"]==0&&journal.status()["budgetOmissions"]==4);
    json availability={{"virtualAdaptation",json::object()},{"files",json::array()}};std::vector<fs::path> included;
    const std::wstring dataDir=root.wstring();
    const auto addFile=[&](const std::wstring& path){included.emplace_back(path);};
    // ACTUAL_EXPORT_BLOCK
    const auto actual=rows(virtualProcessExport.file);
    check("fallback-actual-export-handler-adds-current-file-and-explicit-receipt",included.size()==1&&included[0]==virtualProcessExport.file&&availability["virtualAdaptation"]["currentProcessExport"]["saved"]==true);
    check("fallback-current-samples-survive-permanent-byte-budget",actual.size()==5&&actual[0]["record"]=="process_begin"&&actual.back()["record"]=="process_end"&&actual.back()["writtenFacts"]==3&&actual.back()["omittedFacts"]==0&&actual.back()["retainedSnapshotRowsComplete"]==true);
    bool originals=true;for(unsigned i=1;i<=3;++i)originals&=actual[i]["firstSample"]==original&&actual[i]["queuePhaseAtSnapshot"]=="drain-resolved"&&actual[i]["journalPersistence"]=="not-inferred;drain-resolved-can-mean-budget-omitted";
    const auto& writer=ymcc::sleepfactlog::writerIdentity();
    check("fallback-original-samples-writer-and-clocks-have-separated-meaning",originals&&actual[0]["writerPid"]==GetCurrentProcessId()&&actual[0]["writerProcessCreation100ns"]==std::to_string(writer.creation100ns)&&actual[1]["capturedTickMs"].is_number_unsigned());
    check("fallback-export-does-not-drain-or-append-permanent-history",facts.status()==before&&bytes(history)==historyBefore&&journal.status()["committedThisProcess"]==0);
    fs::copy_file(virtualProcessExport.file,root/processExportName);
    const auto staged=ymcc::logexport::stagedFileEvidence(root/processExportName,"virtual-adaptation-session.jsonl");
    check("fallback-native-file-inspection-and-ai-lane-accept-jsonl",staged["format"]=="json-lines"&&staged["validJsonRows"]==5&&staged["invalidJsonRows"]==0&&std::string(ymcc::logexport::aiEvidenceLane("virtual-adaptation-session.jsonl"))=="resident");
    const std::vector<std::pair<std::wstring,std::wstring>> entries={{root.wstring(),processExportName}};
    check("fallback-actual-ai-index-over-exported-bytes",ymcc::logexport::writeAiLogIndex(root,entries,digest));
    check("fallback-guide-describes-ram-coverage-and-durability",ymcc::logexport::aiReadingGuide().contains("virtualAdaptationProcessExport"));

    BasicFacts<8> phases;phases.offer(fact(1,{{"pending",true}}));phases.offer(fact(2,{{"inFlight",true}}));Fact temp;size_t first=0;phases.take(first,temp);
    auto phaseExport=exportRetainedFacts(root,phases);auto phaseRows=rows(phaseExport.file);
    check("fallback-pending-and-inflight-kept-without-settling-queue",phaseRows[1]["queuePhaseAtSnapshot"]=="in-flight"&&phaseRows[2]["queuePhaseAtSnapshot"]=="pending"&&phases.status()["pendingFacts"]==2);
    auto view=phases.snapshotForExport();phases.offer(fact(3,{{"afterSnapshot",true}}));
    check("fallback-immutable-view-survives-append-and-captures-cutoff",view.available&&view.known==2&&view.entries[0].value->identity["type"]==1&&phases.snapshotForExport().known==3);
    trackAllocations=true;allocations=0;const auto zeroCopy=phases.snapshotForExport();trackAllocations=false;
    check("fallback-snapshot-view-does-not-copy-json-or-allocate",zeroCopy.known==3&&allocations==0);
    fs::path transient;{auto firstExport=exportRetainedFacts(root,phases);auto secondExport=exportRetainedFacts(root,phases);transient=firstExport.file;check("fallback-concurrent-exports-have-independent-owned-paths",firstExport.file!=secondExport.file&&fs::exists(firstExport.file)&&fs::exists(secondExport.file));}
    check("fallback-snapshot-temp-lifetime-cleans-only-after-consumption",!fs::exists(transient)&&!fs::exists(transient.parent_path()));
    {auto moved=exportRetainedFacts(root,phases);const auto owned=moved.file;auto destination=std::move(moved);check("fallback-moving-owned-snapshot-keeps-source-alive-for-consumer",fs::exists(owned)&&destination.receipt["saved"]==true);}
    const auto unavailable=exportRetainedFacts(root/L"missing-parent",phases);
    check("fallback-creation-failure-never-adds-old-file",unavailable.receipt["saved"]==false&&unavailable.file.empty()&&bytes(root/processExportName)==bytes(virtualProcessExport.file));

    BasicFacts<8> busy;
    std::thread holding([&]{blockAllocation=true;(void)busy.status();blockAllocation=false;});
    while(!allocationEntered.load())std::this_thread::yield();
    auto busyExport=exportRetainedFacts(root,busy);allocationRelease.store(true);holding.join();const auto busyRows=rows(busyExport.file);
    check("fallback-lock-busy-is-explicit-unknown-not-zero-types",busyExport.receipt["saved"]==true&&busyExport.receipt["snapshotAvailable"]==false&&busyRows[0]["knownFactsAtSnapshot"].is_null()&&busyRows[0]["capacityDropsAtSnapshot"].is_null()&&busyRows.back()["retainedSnapshotRowsComplete"]==false);
    BasicFacts<8> bad;bad.offer(fact(1,{{"large",std::string(40000,'X')}}));bad.offer(fact(2,{{"invalid",std::string(1,'\xFF')}}));bad.offer(fact(3,{{"afterBad",true}}));
    const auto badExport=exportRetainedFacts(root,bad);const auto badRows=rows(badExport.file);
    check("fallback-oversize-and-invalid-sample-gaps-do-not-hide-later-fact",badRows.size()==3&&badRows[1]["slotOrdinal"]==3&&badRows.back()["omittedFacts"]==2&&badRows.back()["omissions"][0]["reason"]=="row-byte-budget"&&badRows.back()["omissions"][1]["reason"]=="sample-serialization-failed");
    BasicFacts<512> saturated;for(unsigned i=0;i<512;++i)saturated.offer(fact(i,{{"payload",std::string(24000,'B')}}));saturated.offer(fact(999,json::object()));
    const auto large=exportRetainedFacts(root,saturated);const auto largeRows=rows(large.file);const auto footer=largeRows.back();const auto largeBytes=fs::file_size(large.file);
    bool lineLimits=true;for(size_t i=1;i+1<largeRows.size();++i)lineLimits&=largeRows[i].dump().size()+1<=processExportMaxRowBytes;
    check("fallback-four-MiB-and-32-KiB-limits-preserve-all-omission-ordinals",largeBytes<=processExportMaxBytes&&largeBytes==large.receipt["bytes"]&&footer["writtenFacts"].get<unsigned>()+footer["omittedFacts"].get<unsigned>()==512&&footer["omittedFacts"].get<unsigned>()>0&&footer["retainedSnapshotRowsComplete"]==false&&lineLimits);
    check("fallback-source-drops-remain-separate-from-export-row-coverage",largeRows[0]["capacityDropsAtSnapshot"]==1&&footer["sourceFirstWitnessCoverageComplete"]==false&&saturated.status()["knownFacts"]==512);
    BasicFacts<8> empty;const auto emptyExport=exportRetainedFacts(root,empty);const auto emptyRows=rows(emptyExport.file);
    check("fallback-empty-available-snapshot-is-distinct-from-busy",emptyRows[0]["snapshotAvailable"]==true&&emptyRows[0]["knownFactsAtSnapshot"]==0&&emptyRows.back()["retainedSnapshotRowsComplete"]==true&&emptyRows.back()["completeHistory"]==false);
    unsigned failures=0;for(const auto& c:checks)failures+=!c["pass"].get<bool>();std::cout<<json{{"selectedCase","adaptation-facts"},{"boundarySelection","current-process-export"},{"checks",checks},{"failures",failures},{"productOrHardwareExecuted",false},{"permanentHistoryUnchanged",bytes(history)==historyBefore},{"fixtureRoot",root.string()},{"largeExportBytes",largeBytes}}.dump(2)<<'\n';return failures?3:0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}

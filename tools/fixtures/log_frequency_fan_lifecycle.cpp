#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <thread>
#include <set>
#include <stdexcept>
#include <atomic>
#include <cstdlib>
#include <new>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>
#include "json.hpp"
#include "log_snapshot_state.h"
#include "log_jsonl_boundary.h"
using json=nlohmann::json;
namespace fspath=std::filesystem;
static bool fixtureCountAllocation=false;
static size_t fixtureLargestAllocation=0,fixtureMeasuredProjectionMaxAllocation=0;
void* operator new(std::size_t size){if(fixtureCountAllocation&&size>fixtureLargestAllocation)fixtureLargestAllocation=size;if(void* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
static bool fixtureUnknownBirth=false;
static unsigned fixtureTimeQueries=0,fixtureRotations=0;
static BOOL fixtureProcessTimes(HANDLE process,LPFILETIME creation,LPFILETIME exit,LPFILETIME kernel,LPFILETIME user){
    ++fixtureTimeQueries;
    if(fixtureUnknownBirth){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    return ::GetProcessTimes(process,creation,exit,kernel,user);
}
#define GetProcessTimes fixtureProcessTimes
#include "sleep_fact_log_record.h"
#undef GetProcessTimes
static std::wstring fixtureRoot;
static std::mutex g_fanLogMtx;
static std::wstring yemancc_data_dir(){return fixtureRoot;}
static std::string nativeLifecycleTimestamp(){return "2000-01-01T00:00:00.000";}
static void rotateOneLogFile(const std::wstring&){++fixtureRotations;}
// ACTUAL_FIELDS
// ACTUAL_CLASSIFIER
// ACTUAL_PROJECTOR
namespace ymcc::logging {
static unsigned fanBoundaryOpenCalls=0;
[[maybe_unused]] static bool countedFanBoundaryOpen(std::ofstream& out,const fspath::path& path){++fanBoundaryOpenCalls;return openJsonLineAppend(out,path);}
}
// Instrument the existing helper call only; its unchanged body still executes.
#define openJsonLineAppend countedFanBoundaryOpen
// ACTUAL_WRITER
#undef openJsonLineAppend
static std::vector<json> readRows(){
    std::ifstream in(fspath::path(fixtureRoot)/L"fan-lifecycle.log",std::ios::binary);
    std::vector<json> rows;std::string line;
    while(std::getline(in,line))if(!line.empty())rows.push_back(json::parse(line));
    return rows;
}
static json checks=json::array();
static void check(const char* name,bool passed){checks.push_back({{"name",name},{"pass",passed}});}
static std::string fanFileBytes(){
    std::ifstream in(fspath::path(fixtureRoot)/L"fan-lifecycle.log",std::ios::binary);
    return {std::istreambuf_iterator<char>(in),{}};
}
static void fanSeed(const std::string& bytes){
    std::ofstream out(fspath::path(fixtureRoot)/L"fan-lifecycle.log",std::ios::binary|std::ios::trunc);
    out.write(bytes.data(),bytes.size());out.close();if(!out)throw std::runtime_error("private seed failed");
}
static void fanBoundaryCases(const fspath::path& base,bool control){
    const auto use=[&](const wchar_t* name){fixtureRoot=(base/name).wstring();fspath::create_directories(fixtureRoot);};
    const std::string fragment="{\"event\":\"interrupted\",\"x\":";
    use(L"fragment");fanSeed(fragment);
    const json payload={{"reason","current"},{"nested",{{"unknown",17}}},{"generation",7}};const auto original=payload;
    appendFanLifecycleLog("new-current",payload);const auto first=fanFileBytes();
    bool nextParsed=false;
    if(first.rfind(fragment+"\n",0)==0){
        const auto row=json::parse(first.substr(fragment.size()+1),nullptr,false);
        nextParsed=row.is_object()&&row["event"]=="new-current"&&row["reason"]=="current";
    }
    check("fan-reopen-fragment-retained-and-next-receipt-separate",nextParsed);
    if(control)return;
    check("fan-reopen-producer-fields-and-birth-remain-original",payload==original&&json::parse(first.substr(fragment.size()+1))["nested"]["unknown"]==17&&json::parse(first.substr(fragment.size()+1))["writerProcessCreation100ns"].is_string());
    appendFanLifecycleLog("second-current",{{"n",2}});
    const auto second=fanFileBytes();
    check("fan-clean-reopen-appends-without-extra-empty-lines",second.rfind(first,0)==0&&std::count(second.begin(),second.end(),'\n')==3);
    use(L"valid-no-lf");const std::string prior="{\"event\":\"old-valid\",\"n\":1}";fanSeed(prior);
    appendFanLifecycleLog("after-valid",{{"n",2}});auto rows=readRows();
    check("fan-complete-json-without-lf-keeps-two-parseable-rows",rows.size()==2&&rows[0]["n"]==1&&rows[1]["n"]==2&&fanFileBytes().rfind(prior+"\n",0)==0);
    use(L"empty");appendFanLifecycleLog("empty-first",{{"n",1}});
    check("fan-new-file-has-one-row-and-no-leading-lf",readRows().size()==1&&fanFileBytes().front()=='{');
    use(L"lf");fanSeed(prior+"\n");appendFanLifecycleLog("after-lf",json::object());
    check("fan-existing-lf-is-not-doubled",readRows().size()==2&&fanFileBytes().rfind(prior+"\n{",0)==0);
    use(L"crlf");fanSeed(prior+"\r\n");appendFanLifecycleLog("after-crlf",json::object());
    check("fan-existing-crlf-bytes-are-not-normalized",readRows().size()==2&&fanFileBytes().rfind(prior+"\r\n{",0)==0);
    use(L"read-denied");fanSeed(fragment);const auto path=fspath::path(fixtureRoot)/L"fan-lifecycle.log";
    HANDLE writeOnly=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(writeOnly==INVALID_HANDLE_VALUE)throw std::runtime_error("private tail-denial handle failed");
    appendFanLifecycleLog("refused-no-replay",{{"n",1}});LARGE_INTEGER size{};const bool sizeKnown=GetFileSizeEx(writeOnly,&size)!=FALSE;const bool handleReturned=CloseHandle(writeOnly)!=FALSE;
    check("fan-unreadable-tail-does-not-append-or-rewrite-history",sizeKnown&&size.QuadPart==static_cast<LONGLONG>(fragment.size())&&handleReturned&&fanFileBytes()==fragment);
    HANDLE exclusive=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    const bool exclusiveReturned=exclusive!=INVALID_HANDLE_VALUE&&CloseHandle(exclusive)!=FALSE;
    check("fan-failed-tail-inspection-closes-owned-stream",exclusiveReturned);
    appendFanLifecycleLog("new-after-denial",{{"n",2}});const auto recovered=fanFileBytes();
    const auto row=json::parse(recovered.substr(fragment.size()+1),nullptr,false);
    check("fan-next-valid-call-recovers-without-replaying-refused-receipt",recovered.rfind(fragment+"\n",0)==0&&row.is_object()&&row["event"]=="new-after-denial"&&recovered.find("refused-no-replay")==std::string::npos);
    use(L"serialized");const auto opens=ymcc::logging::fanBoundaryOpenCalls;
    std::thread a([]{for(unsigned i=0;i<200;++i)appendFanLifecycleLog("thread-a",{{"n",i}});});
    std::thread b([]{for(unsigned i=0;i<200;++i)appendFanLifecycleLog("thread-b",{{"n",i}});});
    a.join();b.join();rows=readRows();std::set<std::pair<std::string,unsigned>> identities;
    for(const auto& value:rows)identities.emplace(value["event"].get<std::string>(),value["n"].get<unsigned>());
    check("fan-original-stream-lock-keeps400-complete-unique-receipts",rows.size()==400&&identities.size()==400);
    check("fan-helper-called-once-per-original-open-with-one-process-birth-cache",ymcc::logging::fanBoundaryOpenCalls-opens==400&&fixtureTimeQueries==1);
}

int wmain(int argc,wchar_t** argv){
    if(argc!=4)return 2;
    const std::wstring mode=argv[2],selection=argv[3];
    if((mode!=L"known"&&mode!=L"unknown") ||
        (selection!=L"all"&&selection!=L"boundary-control"&&selection!=L"birth-control"&&selection!=L"text-only"&&selection!=L"text-control"&&selection!=L"copy-control"&&selection!=L"boundary-only"&&selection!=L"reopen-control"))return 2;
    fixtureUnknownBirth=mode==L"unknown";fixtureRoot=argv[1];
    if(fspath::exists(fixtureRoot))return 2;
    fspath::create_directories(fixtureRoot);
    if(selection==L"boundary-only"||selection==L"reopen-control"){
        fanBoundaryCases(fspath::path(fixtureRoot),selection==L"reopen-control");
        unsigned failures=0;for(const auto& row:checks)failures+=!row["pass"].get<bool>();
        std::cout<<json{{"checks",checks},{"failures",failures},{"rows",selection==L"reopen-control"?json(nullptr):json(readRows().size())},
            {"cachedTimeQueries",fixtureTimeQueries},{"boundaryOpenCalls",ymcc::logging::fanBoundaryOpenCalls},
            {"scope","original full Fan writer/shared boundary helper;real isolated files/threads/own-cache;controlled rotation/timestamp;no device or business"}}.dump(2)<<std::endl;
        return failures?3:0;
    }
    const auto offer=[](const char* boundary,unsigned generation){
        appendFanLifecycleLog("lifecycle.power-boundary-supersede",projectFanLifecycleEvidence({
            {"event",boundary},{"caller","fanHost-lifecycle"},{"generation",generation},
            {"intentRevision",4},{"state","suspended"},{"secret","must-not-project"}}));
    };
    offer("suspending",7);auto rows=readRows();
    if(selection==L"boundary-control"){
        check("original-boundary-survives-projection-and-real-writer",rows.size()==1&&rows[0].value("producerEvent","")=="suspending");
    }else if(selection==L"birth-control"){
        check("writer-birth-is-cached-own-process-value",rows.size()==1&&rows[0].contains("writerProcessCreation100ns")&&rows[0]["writerProcessCreation100ns"].is_string());
    }else if(selection==L"text-only"||selection==L"text-control"||selection==L"copy-control"){
        const std::string chinese="\xe4\xb8\xad",emoji="\xf0\x9f\x99\x82",accent="\xc3\xa9";
        if(selection!=L"copy-control"){
            const auto baseline=rows.size();
            appendFanLifecycleLog("lifecycle.resume-wait-terminal-failed",projectFanLifecycleEvidence({
                {"reason",std::string(47,'x')+chinese+"tail"},{"state","failed"},{"generation",7},{"ok",false}}));
            rows=readRows();
            check("valid-multibyte-cut-keeps-one-complete-terminal-receipt",rows.size()==baseline+1&&rows.back()["event"]=="lifecycle.resume-wait-terminal-failed"&&rows.back()["state"]=="failed"&&rows.back()["ok"]==false);
            if(selection==L"text-only"){
                check("three-byte-crossing-retains-maximal-complete-prefix",rows.back()["reason"]==std::string(47,'x'));
                const auto emit=[](const std::string& text){appendFanLifecycleLog("text-boundary",projectFanLifecycleEvidence({{"reason",text},{"caller","fanHost-lifecycle"},{"generation",9}}));return readRows().back();};
                const auto a=emit(std::string(45,'x')+emoji+"tail");
                check("four-byte-crossing-keeps-valid-prefix-and-original-fields",a["reason"]==std::string(45,'x')&&a["generation"]==9&&a["caller"]=="fanHost-lifecycle");
                check("two-byte-crossing-keeps-valid-prefix",emit(std::string(47,'x')+accent+"tail")["reason"]==std::string(47,'x'));
                std::string exact;for(unsigned i=0;i<16;++i)exact+=chinese;
                check("exact-48-byte-unicode-prefix-is-kept",emit(exact+emoji)["reason"]==exact&&emit(exact)["reason"]==exact);
                const std::string shortText=chinese+emoji+std::string("\0z",2);
                check("short-unicode-and-embedded-null-round-trip",emit(shortText)["reason"]==shortText);
                check("empty-ascii-and-48-byte-cap-stay-unchanged",emit("")["reason"]==""&&emit(std::string(100,'a'))["reason"]==std::string(48,'a'));
                bool invalidRetained=true;const auto beforeBad=readRows().size();
                for(const auto& bad:std::vector<std::string>{"\x80","\xc0\xaf","\xed\xa0\x80","\xf4\x90\x80\x80","\xc2","\xe2\x82",std::string(80,'\x80')}){
                    const auto row=emit(bad);invalidRetained&=row["reason"]=="<rejected>"&&row["generation"]==9;
                }
                check("invalid-retained-prefix-is-explicit-rejection-with-one-row-each",invalidRetained&&readRows().size()==beforeBad+7);
                check("discarded-tail-is-not-scanned-or-used-to-reject-prefix",emit(std::string(48,'a')+"\xff")["reason"]==std::string(48,'a'));
                const auto projected=projectFanLifecycleEvidence({{"event",std::string(47,'b')+chinese},{"unknown","secret"},{"detail",json::array()},{"generation",true}});
                check("producer-event-uses-same-safe-cap-and-original-whitelist",projected["producerEvent"]==std::string(47,'b')&&!projected.contains("event")&&!projected.contains("unknown")&&projected["detail"]=="<rejected>"&&projected["generation"]==true);
            }
        }
        if(selection==L"text-only"||selection==L"copy-control"){
            const json large={{"reason",std::string(2*1024*1024,'a')},{"state","failed"}};
            fixtureLargestAllocation=0;fixtureCountAllocation=true;
            const auto bounded=projectFanLifecycleEvidence(large);
            fixtureCountAllocation=false;fixtureMeasuredProjectionMaxAllocation=fixtureLargestAllocation;
            check("large-input-projection-does-not-copy-discarded-megabytes",bounded["reason"]==std::string(48,'a')&&bounded["state"]=="failed"&&fixtureMeasuredProjectionMaxAllocation<4096);
            if(selection==L"text-only"){
                const auto oldCount=readRows().size();appendFanLifecycleLog("valid-after-rejected-text",projectFanLifecycleEvidence({{"reason","valid-next"}}));
                check("next-valid-receipt-recovers-without-replay-or-reclassification",readRows().size()==oldCount+1&&readRows().back()["reason"]=="valid-next"&&fixtureTimeQueries==1);
            }
        }
    }else{
        check("suspending-boundary-and-envelope-event-both-retained",rows.size()==1&&rows[0].value("producerEvent","")=="suspending"&&rows[0]["event"]=="lifecycle.power-boundary-supersede");
        offer("shutdown",8);rows=readRows();
        check("shutdown-boundary-not-inferred-from-suspended-state",rows.size()==2&&rows[1].value("producerEvent","")=="shutdown"&&rows[1]["state"]=="suspended");
        check("whitelist-caller-generation-intent-and-privacy-unchanged",rows[0]["caller"]=="fanHost-lifecycle"&&rows[0]["generation"]==7&&rows[0]["intentRevision"]==4&&!rows[0].contains("secret"));
        FILETIME created{},exited{},kernel{},user{};
        const bool ownBirthKnown=::GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user)!=FALSE;
        const auto expected=fixtureUnknownBirth?json(nullptr):json(std::to_string(ymcc::sleepfactlog::fileTimeValue(created)));
        check("own-writer-pid-and-known-or-null-birth-match",ownBirthKnown&&rows[0]["pid"]==GetCurrentProcessId()&&rows[0].contains("writerProcessCreation100ns")&&rows[0]["writerProcessCreation100ns"]==expected);
        check("one-existing-cache-query-for-two-receipts",fixtureTimeQueries==1);
        auto projected=projectFanLifecycleEvidence({{"event",json::array({"suspending"})},{"reason",json::object()},{"state",std::string(80,'x')},{"session","[redacted]"},{"unknown",17}});
        check("original-cap-and-rejected-types-preserved",projected["producerEvent"]=="<rejected>"&&projected["reason"]=="<rejected>"&&projected["state"]==std::string(48,'x')&&projected["session"]=="[redacted]"&&!projected.contains("unknown"));
        const auto noEvent=projectFanLifecycleEvidence({{"state","ready"},{"producerEvent","forged"}});
        check("missing-producer-event-remains-missing-without-payload-backfill",!noEvent.contains("event")&&!noEvent.contains("producerEvent"));
        appendFanLifecycleLog("ordinary-receipt",{{"nested",{{"unknown",17}}},{"writerProcessCreation100ns","forged"}});rows=readRows();
        check("conflicting-producer-birth-preserved-separately",rows.back()["writerProcessCreation100ns"]==expected&&rows.back()["producerWriterProcessCreation100ns"]=="forged"&&rows.back()["nested"]["unknown"]==17);
        appendFanLifecycleLog(nullptr,json::array());rows=readRows();
        check("legacy-null-event-and-nonobject-fallback-preserved",rows.back()["event"]=="unknown"&&rows.back()["writerProcessCreation100ns"]==expected);
        for(unsigned i=0;i<1000;++i)offer("suspending",10+i);
        rows=readRows();
        check("one-row-per-offer-and-no-extra-identity-queries",rows.size()==1004&&fixtureRotations==1004&&fixtureTimeQueries==1);
        check("resident-whitelist-exact-and-unrelated-events-still-gated",fanLifecycleEvidenceAlwaysLogged("lifecycle.power-boundary-supersede")&&fanLifecycleEvidenceAlwaysLogged("lifecycle.resume-wait-completed")&&!fanLifecycleEvidenceAlwaysLogged("lifecycle.state-changed")&&!fanLifecycleEvidenceAlwaysLogged("lifecycle.power-boundary-supersede-typo"));
        const auto previousRoot=fixtureRoot;fixtureRoot+=L"\\missing\\no-parent";
        appendFanLifecycleLog("sink-failure",{{"ok",false}});fixtureRoot=previousRoot;
        check("failed-sink-does-not-throw-retry-or-touch-existing-rows",readRows().size()==1004&&fixtureRotations==1005&&fixtureTimeQueries==1);
    }
    unsigned failures=0;for(const auto& row:checks)failures+=!row["pass"].get<bool>();
    std::cout<<json{{"checks",checks},{"failures",failures},{"rows",readRows().size()},
        {"processPid",GetCurrentProcessId()},{"unknownBirth",fixtureUnknownBirth},{"cachedTimeQueries",fixtureTimeQueries},
        {"measuredProjectionMaxAllocation",fixtureMeasuredProjectionMaxAllocation},
        {"scope","actual complete Fan classifier/projector/writer and shared identity cache;real private files/self-process;controlled timestamp/rotation;no fan/device/business execution"}}.dump(2)<<std::endl;
    return failures?3:0;
}

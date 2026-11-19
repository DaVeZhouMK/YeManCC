#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <new>
#include <stdexcept>
#include "log_export_evidence.h"
using json=nlohmann::json;namespace fs=std::filesystem;
static std::atomic<long long> heapObjects{0};
static unsigned selectedPoint=0,exceptionKind=0;static bool armed=false,triggered=false;
static size_t failedRequestBytes=0;
void* operator new(size_t size){
    if(armed){armed=false;triggered=true;failedRequestBytes=size;
        if(exceptionKind==1)throw std::runtime_error("fixture non-JSON standard exception");
        if(exceptionKind==2)throw 17;
        throw std::bad_alloc();}
    if(auto p=std::malloc(size?size:1)){++heapObjects;return p;}throw std::bad_alloc();
}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{if(p)--heapObjects;std::free(p);}
void operator delete[](void* p)noexcept{::operator delete(p);}
void operator delete(void* p,size_t)noexcept{::operator delete(p);}
void operator delete[](void* p,size_t)noexcept{::operator delete(p);}
static void stagedFixturePoint(unsigned point){if(point==selectedPoint){selectedPoint=0;armed=true;}}
static const fs::path* shortReadFile=nullptr;
static void stagedFixtureBeforeRead(){if(shortReadFile)fs::resize_file(*shortReadFile,3);}
#include "staged-observed.inc"
#ifdef YMCC_STAGED_BEFORE_CONTROL
#include "staged-before.inc"
#endif
static json checks=json::array(),reports=json::array();
static void check(const char* name,bool pass){checks.push_back({{"name",name},{"pass",pass}});std::cerr<<name<<"="<<pass<<'\n';}
static void put(const fs::path& path,const std::string& data){std::ofstream out(path,std::ios::binary);out.write(data.data(),static_cast<std::streamsize>(data.size()));}
static std::string bytes(const fs::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
static int finish(){unsigned failures=0;for(const auto& row:checks)failures+=!row["pass"].get<bool>();
    std::cout<<json{{"checks",checks},{"failures",failures},{"reports",reports},{"scope","original staged inspector with reversible read and allocation probes;private files only"},
        {"realKernelExhaustionOrPermanentHeapProven",false}}.dump(2)<<'\n';return failures?3:0;}
int wmain(int argc,wchar_t** argv){
    if(argc!=3)return 2;const fs::path root(argv[1]);fs::create_directories(root);const std::wstring_view mode(argv[2]);
    const auto file=root/L"staged.jsonl";const std::string valid=json{{"timestampUtc",std::string(4096,'T')},{"message","valid"}}.dump();
    put(file,valid);(void)ymcc::logexport::stagedFileEvidence(file,"native-lifecycle.log").dump();
    DWORD handlesBefore=0,handlesAfter=0;const bool known=GetProcessHandleCount(GetCurrentProcess(),&handlesBefore)!=FALSE;
    if(mode==L"control"||mode==L"faults"){
        const unsigned firstPoint=mode==L"control"?2:1;
        for(unsigned point=firstPoint;point<=2;++point)for(unsigned kind=0;kind<(mode==L"control"?1u:3u);++kind){
            const auto reportFile=root/(L"fault-"+std::to_wstring(point)+L"-"+std::to_wstring(kind)+L".json");
            const auto objectsBefore=heapObjects.load();bool classified=false,misclassified=false,validKnown=false,invalidKnown=false,partial=false;
            unsigned long long validRows=0,invalidRows=0;
            {
                selectedPoint=point;exceptionKind=kind;armed=false;triggered=false;failedRequestBytes=0;
                const auto result=ymcc::logexport::stagedFileEvidenceMeasured(file,"native-lifecycle.log");selectedPoint=0;armed=false;
                const auto* expected=kind==0?"allocation-exception":kind==1?"standard-exception":"unknown-exception";
                classified=result.value("status","")=="staged-inspection-error"&&result.value("inspectionFailureKind","")==expected&&
                    !result.contains("invalidJsonRows")&&!result.contains("partialOrInvalidTrailingRecord")&&result.value("originalBytesRetained",false);
                validKnown=result.contains("validJsonRows");invalidKnown=result.contains("invalidJsonRows");
                validRows=result.value("validJsonRows",0ull);invalidRows=result.value("invalidJsonRows",0ull);partial=result.value("partialOrInvalidTrailingRecord",false);
                misclassified=invalidRows>0||partial;
                put(reportFile,result.dump(2));
            }
            const bool heapReturned=heapObjects.load()==objectsBefore;const auto fileAfter=bytes(file);
            check("staged-resource-exception-is-not-bad-json-or-tail",triggered&&failedRequestBytes>0&&classified&&!misclassified);
            check("staged-inspection-fault-keeps-source-and-cpp-new-objects",heapReturned&&fileAfter==valid);
            reports.push_back({{"point",point},{"kind",kind},{"resultFile",reportFile.string()},{"triggered",triggered},{"failedRequestBytes",failedRequestBytes},
                {"reportedValidKnown",validKnown},{"reportedValid",validRows},{"reportedInvalidKnown",invalidKnown},{"reportedInvalid",invalidRows},{"reportedPartialTail",partial},{"cppNewObjectsReturned",heapReturned}});
            if(mode==L"control")return finish();
        }
        check("staged-faults-own-handles-return",known&&GetProcessHandleCount(GetCurrentProcess(),&handlesAfter)&&handlesBefore==handlesAfter);
        return finish();
    }
    if(mode==L"format"){
#ifdef YMCC_STAGED_BEFORE_CONTROL
        const std::vector<std::pair<std::string,std::string>> cases={
            {"empty",""},{"valid-tail","{\"time\":\"t\"}"},{"lf","{\"time\":\"t\"}\n"},{"bad-tail","{}\n{broken"},
            {"bad-middle","{broken\n{}\n"},{"bad-utf8",std::string("{\"s\":\"\xff\"}")},
            {"crlf","{}\r\n{}\r\n"},{"nonobject","[]\nnull\n17\n"},{"unicode","{\"s\":\"中文🎮\u2028payload\"}"},
            {"numeric-range","{\"n\":1e999}"},{"large-time",valid},{"line65536","{\"s\":\""+std::string(65528,'x')+"\"}"},
            {"oversize-tail","{\"s\":\""+std::string(65529,'x')+"\"}"},{"tail-window",std::string(8*1024*1024,'x')+"\n{}\n"}};
        unsigned index=0;
        for(const auto& entry:cases){
            const auto caseFile=root/(L"format-"+std::to_wstring(index++)+L".json");put(file,entry.second);
            const auto before=ymcc::logexport::stagedFileEvidenceBefore(file,"native-lifecycle.log");
            const auto actual=ymcc::logexport::stagedFileEvidence(file,"native-lifecycle.log");
            const auto measured=ymcc::logexport::stagedFileEvidenceMeasured(file,"native-lifecycle.log");
            check("staged-normal-format-is-exact-old-and-probe-neutral",actual==before&&measured==actual&&bytes(file)==entry.second);
            put(caseFile,actual.dump(2));reports.push_back({{"case",entry.first},{"resultFile",caseFile.string()}});
        }
        put(file,std::string("opaque\0text",11));const auto opaque=ymcc::logexport::stagedFileEvidence(file,"steam-app.log");
        check("staged-opaque-format-remains-exact",opaque==ymcc::logexport::stagedFileEvidenceBefore(file,"steam-app.log")&&!opaque.contains("invalidJsonRows"));
        const auto missing=root/L"missing.jsonl";
        check("staged-read-error-remains-exact",ymcc::logexport::stagedFileEvidence(missing,"native-lifecycle.log")==ymcc::logexport::stagedFileEvidenceBefore(missing,"native-lifecycle.log"));
        put(file,"{}\n{}\n");shortReadFile=&file;
        const auto shortRead=ymcc::logexport::stagedFileEvidenceMeasured(file,"native-lifecycle.log");shortReadFile=nullptr;
        check("short-read-retains-original-size-but-never-parses-the-prefix",bytes(file)=="{}\n"&&
            shortRead.value("status","")=="staged-read-error"&&shortRead.value("bytes",0)==6&&
            !shortRead.contains("validJsonRows")&&!shortRead.contains("partialOrInvalidTrailingRecord"));
        check("staged-format-own-handles-return",known&&GetProcessHandleCount(GetCurrentProcess(),&handlesAfter)&&handlesBefore==handlesAfter);
        return finish();
#else
        return 4;
#endif
    }
    return 2;
}

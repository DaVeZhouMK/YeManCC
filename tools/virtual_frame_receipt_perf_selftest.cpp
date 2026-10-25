#define NOMINMAX
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <malloc.h>
#include <new>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include "json.hpp"
using json=nlohmann::json;
#define NOINLINE __declspec(noinline)
struct Count{uint64_t ordinaryRequests=0,ordinaryBytes=0,alignedRequests=0,alignedBytes=0;uint64_t requests()const{return ordinaryRequests+alignedRequests;}uint64_t bytes()const{return ordinaryBytes+alignedBytes;}};
static thread_local bool counting=false;static thread_local Count counter;static uint64_t sink=0,assertions=0;
static void account(size_t n,bool aligned){if(!counting)return;if(aligned){++counter.alignedRequests;counter.alignedBytes+=n;}else{++counter.ordinaryRequests;counter.ordinaryBytes+=n;}}
void* operator new(size_t n){auto p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();account(n,false);return p;}void* operator new[](size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}void operator delete(void*p,size_t)noexcept{std::free(p);}void operator delete[](void*p,size_t)noexcept{std::free(p);}
void* operator new(size_t n,std::align_val_t a){auto p=_aligned_malloc(n?n:1,size_t(a));if(!p)throw std::bad_alloc();account(n,true);return p;}void* operator new[](size_t n,std::align_val_t a){return ::operator new(n,a);}void operator delete(void*p,std::align_val_t)noexcept{_aligned_free(p);}void operator delete[](void*p,std::align_val_t)noexcept{_aligned_free(p);}void operator delete(void*p,size_t,std::align_val_t)noexcept{_aligned_free(p);}void operator delete[](void*p,size_t,std::align_val_t)noexcept{_aligned_free(p);}
static void check(bool ok,const char*msg){++assertions;if(!ok)throw std::runtime_error(msg);}
static bool exact(const json&a,const json&b){if(a.type()!=b.type()||a.size()!=b.size())return false;if(a.is_number_float())return std::bit_cast<uint64_t>(a.get<double>())==std::bit_cast<uint64_t>(b.get<double>());if(a.is_object()||a.is_array()){auto x=a.begin(),y=b.begin();for(;x!=a.end();++x,++y)if((a.is_object()&&x.key()!=y.key())||!exact(x.value(),y.value()))return false;return true;}return a==b;}
static std::string dumpResult(const json&v){try{return "ok:"+v.dump();}catch(const json::exception&e){return "error:"+std::to_string(e.id);}}
#include "generated.hpp"
static Input fromJson(const json& r) {
 Input v;v.run=r.at("runId");v.instance=r.at("hostInstanceId");v.persona=r.at("persona");v.epoch=r.at("epoch");
 const auto seq=r.at("commandSequence").get<unsigned long long>();v.command=seq-1;
 v.beforeSample=r.at("sampleSequence");v.afterSample=v.beforeSample;v.tick=r.value("receiptCompletedTick",777ULL);v.accepted=r.at("hostAckAccepted");v.expected=r;return v;
}
static void equivalent(const Input& v,bool field=false) {
 for(int scope=0;scope<3;++scope){auto x=baseline(v,scope);const auto oldCalls=g_personaCalls;const auto oldProof=g_proofCalls;const auto oldFailures=g_commonFrameEvidenceFailures;const auto oldGate=g_gateCalls;
  auto y=candidate(v,scope);check(exact(x,y),"recursive receipt parity");check(dumpResult(x)==dumpResult(y),"dump/exception parity");
  check(oldCalls==g_personaCalls&&oldProof==g_proofCalls&&oldFailures==g_commonFrameEvidenceFailures&&oldGate==g_gateCalls,"helper call order/failure counter changed");
  if(field&&scope==1)check(exact(x,v.expected),"archived receipt values changed");
 }
}
static json read(const char* p){std::ifstream f(p);json j;f>>j;return j;}
static void correctness(const json& rows) {
 for(const auto& row:rows)equivalent(fromJson(row.at("receipt")),true);
 Input seed;
 for(unsigned bits=0;bits<64;++bits){auto v=seed;v.enabled=(bits&1)!=0;v.accepted=(bits&2)!=0;v.effUseMotion=(bits&4)!=0;v.standalone=(bits&8)!=0;v.selectorId=bits&16?"asus-rc73ya":"asus-rc73xa";v.throwPersona=(bits&32)!=0;equivalent(v);}
 for(const auto& text:std::vector<std::string>{"",std::string(1024,'A'),"微星😀\n\"\\",std::string("x\0y",3),std::string("\xc3\x28",2)}){auto v=seed;v.run=v.instance=v.persona=text;equivalent(v);}
 for(unsigned long long n:std::vector<unsigned long long>{0,1,0x7fffffffULL,0xffffffffULL,0x7fffffffffffffffULL,0xffffffffffffffffULL}){auto v=seed;v.command=v.beforeSample=v.epoch=n;v.afterSample=n+1;v.tick=n;equivalent(v);}
 std::mt19937_64 rng(20261015);
 for(unsigned n=0;n<2000;++n){auto v=seed;v.epoch=rng();v.command=rng();v.beforeSample=rng();v.afterSample=rng();v.tick=rng();v.accepted=(rng()&1)!=0;v.effUseMotion=(rng()&1)!=0;v.enabled=n%7!=0;v.persona=n%2?"steamdeck":"dualsense";equivalent(v);}
}
using Builder=json(*)(const Input&,int);
struct Measure{double ns;Count count;};
static Measure measure(const Input& v,bool after,int scope,int batches,bool alloc){Builder fn=after?candidate:baseline;double elapsed=0;Count total{};
 for(int batch=0;batch<batches;++batch){std::vector<Input> prepared(32,v);counter={};counting=alloc;auto start=std::chrono::steady_clock::now();
  for(const auto& x:prepared){auto out=fn(x,(std::min)(scope,2));if(scope==3){auto wire=out.dump();sink+=wire.size();}else sink+=out.size();}
  elapsed+=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count();counting=false;total.ordinaryRequests+=counter.ordinaryRequests;total.ordinaryBytes+=counter.ordinaryBytes;total.alignedRequests+=counter.alignedRequests;total.alignedBytes+=counter.alignedBytes;
 }
 return{elapsed/(32*batches),total};
}
static double median(std::vector<double> x){std::sort(x.begin(),x.end());return x[x.size()/2];}
static json formal(const Input& v,const char* name,int scope,int campaign){measure(v,false,scope,6,false);measure(v,true,scope,6,false);std::vector<double>b,a,p;int wins=0;
 for(int round=0;round<11;++round){double x,y;if(((round+campaign)&1)==0){x=measure(v,false,scope,24,false).ns;y=measure(v,true,scope,24,false).ns;}else{y=measure(v,true,scope,24,false).ns;x=measure(v,false,scope,24,false).ns;}b.push_back(x);a.push_back(y);p.push_back((x-y)/x);wins+=y<x;}
 auto ba=measure(v,false,scope,2,true).count,ca=measure(v,true,scope,2,true).count;double pm=median(p),ratio=(median(b)-median(a))/median(b);bool lower=ca.requests()<ba.requests()&&ca.bytes()<ba.bytes();
 return{{"name",name},{"scope",scope},{"fasterRounds",wins},{"rounds",11},{"pairedMedianReduction",pm},{"ratioOfMediansReduction",ratio},{"beforeMedianNs",median(b)},{"afterMedianNs",median(a)},{"beforeAllocationRequests",ba.requests()/64.0},{"afterAllocationRequests",ca.requests()/64.0},{"beforeRequestedBytes",ba.bytes()/64.0},{"afterRequestedBytes",ca.bytes()/64.0},{"allocationsLower",lower},{"beforeSamplesNs",b},{"afterSamplesNs",a},{"pass",wins>=10&&pm>=.05&&ratio>=.05&&lower}};
}
int main(int argc,char**argv){try{check(argc>=3,"usage");auto rows=read(argv[2]);correctness(rows);
 if(std::string(argv[1])=="verify"){std::cout<<json({{"pass",true},{"assertions",assertions},{"archivedReceiptRows",rows.size()},{"randomCases",2000},{"physicalOrProductExecuted",false}}).dump(2)<<'\n';return 0;}
 check(argc==4&&std::string(argv[1])=="benchmark","benchmark args");int campaign=std::stoi(argv[3]);auto data=json::array();bool pass=true;
 Input first=fromJson(rows.front().at("receipt")),last=fromJson(rows.back().at("receipt")),unicode=first;unicode.run="运行-微星-fixture-😀";unicode.instance="宿主-instance-fixture";unicode.persona="steamdeck-fixture-微星";
 for(auto entry:std::vector<std::pair<const char*,Input>>{{"archived-first-receipt",first},{"archived-last-receipt",last},{"synthetic-Unicode-receipt",unicode}})for(int scope=0;scope<4;++scope){auto row=formal(entry.second,entry.first,scope,campaign);pass=pass&&row.at("pass").get<bool>();data.push_back(std::move(row));}
 std::cout<<json({{"pass",pass},{"campaign",campaign},{"rows",data},{"wholeProcessClaim",false}}).dump(2)<<'\n';return pass?0:2;
 }catch(const std::exception&e){counting=false;std::cerr<<e.what()<<'\n';return 1;}}

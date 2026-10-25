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
static void bind(const Input&v){g_machineIdentity=v.machine;g_hcMotionSelector=v.selector;}
static json baselineReceipt(const Input&v){bind(v);return inputCaptureCandidateMatrixReceipt(v.meta);}
static json candidateReceipt(const Input&v){bind(v);return candidateMatrixReceipt(v.meta);}
static Input fromJson(const json&v){Input i;i.machine.deviceClass=v.at("deviceClass");i.machine.product=v.at("product");i.machine.gyroMatrixId=v.at("gyroMatrixId");i.machine.accelMatrixId=v.at("accelMatrixId");i.selector.matrixSourceSha256=v.at("matrixSourceSha256");const auto&m=v.at("meta");i.meta.gyroFamily=m.at("gyroFamily");i.meta.accelFamily=m.at("accelFamily");i.meta.gyroHcLegacyFields=m.at("gyroHcLegacyFields");i.meta.accelHcLegacyFields=m.at("accelHcLegacyFields");i.expected=v.at("expected");return i;}
static void equivalent(const Input&i,bool expected){auto b=baselineReceipt(i),c=candidateReceipt(i);check(exact(b,c),"receipt recursive parity");check(dumpResult(b)==dumpResult(c),"receipt dump parity");if(expected)check(exact(b,i.expected),"archived receipt mismatch");auto bh=baselineHc(i),ch=candidateHc(i);check(exact(bh,ch),"HC caller parity");check(dumpResult(bh)==dumpResult(ch),"HC caller dump parity");check(exact(bh.at("recipeReceipt"),b),"HC receipt not bound to function result");}
struct Row{std::string name;Input input;};
static std::vector<Row> rowsFrom(const json&j){std::vector<Row>r;for(const auto&v:j)r.push_back({v.at("name"),fromJson(v.at("case"))});return r;}
enum class Scope{Receipt,Hc,ReceiptDump,HcDump};static const char*scopeName(Scope s){return s==Scope::Receipt?"receipt":s==Scope::Hc?"hc-object":s==Scope::ReceiptDump?"receipt-dump":"hc-dump";}
using Builder=json(*)(const Input&);
static json execute(Builder b,const Input&i,Scope s){auto out=b(i);if(s==Scope::ReceiptDump||s==Scope::HcDump){auto wire=out.dump();sink+=wire.size();return json::object();}sink+=out.size();return json::object();}
struct Measurement{double ns=0;Count count;};
static Measurement measure(Builder b,const Input&i,Scope s,int batches,bool alloc=false){double elapsed=0;Count total;uint64_t z=0;for(int k=0;k<batches;++k){std::vector<Input>prepared(32,i);counter={};counting=alloc;auto st=std::chrono::steady_clock::now();for(auto&x:prepared){auto out=b(x);if(s==Scope::ReceiptDump||s==Scope::HcDump){auto wire=out.dump();z+=wire.size();}else z+=out.size();}elapsed+=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-st).count();counting=false;total.ordinaryRequests+=counter.ordinaryRequests;total.ordinaryBytes+=counter.ordinaryBytes;total.alignedRequests+=counter.alignedRequests;total.alignedBytes+=counter.alignedBytes;}sink+=z;return{elapsed/(32*batches),total};}
static double median(std::vector<double>x){std::sort(x.begin(),x.end());return x[x.size()/2];}
static json formal(const Row&r,Scope s,int campaign){measure(baselineReceipt,r.input,s,6);measure(candidateReceipt,r.input,s,6);Builder bb=(s==Scope::Hc||s==Scope::HcDump)?baselineHc:baselineReceipt;Builder cc=(s==Scope::Hc||s==Scope::HcDump)?candidateHc:candidateReceipt;std::vector<double>b,a,p;int wins=0;for(int n=0;n<11;++n){double x,y;if(((n+campaign)&1)==0){x=measure(bb,r.input,s,24).ns;y=measure(cc,r.input,s,24).ns;}else{y=measure(cc,r.input,s,24).ns;x=measure(bb,r.input,s,24).ns;}b.push_back(x);a.push_back(y);p.push_back((x-y)/x);if(y<x)++wins;}double bm=median(b),am=median(a),pm=median(p),ratio=(bm-am)/bm;auto ba=measure(bb,r.input,s,2,true).count,ca=measure(cc,r.input,s,2,true).count;bool al=ca.requests()<ba.requests()&&ca.bytes()<ba.bytes();bool pass=wins>=10&&pm>=.05&&ratio>=.05&&al;return{{"name",r.name},{"scope",scopeName(s)},{"campaign",campaign},{"fasterRounds",wins},{"rounds",11},{"pairedMedianReduction",pm},{"ratioOfMediansReduction",ratio},{"beforeMedianNs",bm},{"afterMedianNs",am},{"beforeAllocationRequests",ba.requests()/64.0},{"candidateAllocationRequests",ca.requests()/64.0},{"beforeRequestedBytes",ba.bytes()/64.0},{"candidateRequestedBytes",ca.bytes()/64.0},{"allocationsLower",al},{"pass",pass},{"beforeSamplesNs",b},{"afterSamplesNs",a},{"pairedReductions",p}};}
static json read(const char*p){std::ifstream f(p);json j;f>>j;return j;}
int main(int argc,char**argv){try{check(argc>=4,"usage");auto selected=read(argv[2]),all=read(argv[3]);auto cases=rowsFrom(selected);for(const auto&v:all)equivalent(fromJson(v.at("case")),true);for(const auto&r:cases)equivalent(r.input,false);auto seed=cases.front().input;for(unsigned n=0;n<1600;++n){auto v=seed;v.meta.gyroFamily=n%3?"winrt":"sensorapi";v.meta.accelFamily=n%5?"winrt":"sensorapi";v.meta.gyroHcLegacyFields=n&1;v.meta.accelHcLegacyFields=n&2;v.machine.product=n%7?"MS-1T91":"unknown-board";v.selector.matrixSourceSha256=std::string(n%2?"digest":"utf8-微星\n\t\\\"");v.machine.gyroMatrixId=std::string(n%4?"gyro-id":"");v.machine.accelMatrixId=std::string(n%6?"accel-id":"");equivalent(v,false);}check(assertions>2301,"correctness coverage");if(std::string(argv[1])=="verify"){std::cout<<json({{"pass",true},{"assertions",assertions},{"allAuthorizedCases",all.size()},{"randomCases",1600},{"productionModified",false},{"hardwareOrProductExecuted",false}}).dump(2)<<'\n';return 0;}check(std::string(argv[1])=="benchmark"&&argc==5,"invalid mode");int campaign=std::stoi(argv[4]);json out=json::array();bool accepted=true;for(const auto&r:cases)for(int s=0;s<4;++s){auto row=formal(r,Scope(s),campaign);accepted=accepted&&row.at("pass").get<bool>();out.push_back(row);}std::cout<<json({{"pass",accepted},{"campaign",campaign},{"rows",out},{"correctnessAssertions",assertions},{"hardwareOrProductExecuted",false},{"wholeProcessClaim",false}}).dump(2)<<'\n';return accepted?0:2;}catch(const std::exception&e){counting=false;std::cerr<<e.what()<<'\n';return 1;}}
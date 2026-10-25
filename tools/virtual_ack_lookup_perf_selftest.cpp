// R29 offline MSVC fixture. Both namespaces and the DOM predicate are extracted
// from the current source into source-bound.h; only lookupField differs.
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <malloc.h>
#include <map>
#include <mutex>
#include <new>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

struct Allocations {
    unsigned long long ordinaryRequests=0, ordinaryBytes=0, alignedRequests=0, alignedBytes=0;
    void add(const Allocations& x) {
        ordinaryRequests+=x.ordinaryRequests; ordinaryBytes+=x.ordinaryBytes;
        alignedRequests+=x.alignedRequests; alignedBytes+=x.alignedBytes;
    }
};
static bool counting=false;
static Allocations allocated;
void* operator new(std::size_t n) {
    if(counting) { ++allocated.ordinaryRequests; allocated.ordinaryBytes+=n; }
    if(void* p=std::malloc(n?n:1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
void operator delete[](void* p,std::size_t) noexcept { std::free(p); }
void* operator new(std::size_t n,std::align_val_t a) {
    if(counting) { ++allocated.alignedRequests; allocated.alignedBytes+=n; }
    if(void* p=_aligned_malloc(n?n:1,static_cast<std::size_t>(a))) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n,std::align_val_t a) { return ::operator new(n,a); }
void operator delete(void* p,std::align_val_t) noexcept { _aligned_free(p); }
void operator delete[](void* p,std::align_val_t) noexcept { _aligned_free(p); }
void operator delete(void* p,std::size_t,std::align_val_t) noexcept { _aligned_free(p); }
void operator delete[](void* p,std::size_t,std::align_val_t) noexcept { _aligned_free(p); }
void* operator new(std::size_t n,const std::nothrow_t&) noexcept { try{return ::operator new(n);}catch(...){return nullptr;} }
void* operator new[](std::size_t n,const std::nothrow_t&) noexcept { return ::operator new(n,std::nothrow); }
void operator delete(void* p,const std::nothrow_t&) noexcept { ::operator delete(p); }
void operator delete[](void* p,const std::nothrow_t&) noexcept { ::operator delete[](p); }
void* operator new(std::size_t n,std::align_val_t a,const std::nothrow_t&) noexcept { try{return ::operator new(n,a);}catch(...){return nullptr;} }
void* operator new[](std::size_t n,std::align_val_t a,const std::nothrow_t&) noexcept { return ::operator new(n,a,std::nothrow); }
void operator delete(void* p,std::align_val_t a,const std::nothrow_t&) noexcept { ::operator delete(p,a); }
void operator delete[](void* p,std::align_val_t a,const std::nothrow_t&) noexcept { ::operator delete[](p,a); }

#include "json.hpp"
using json=nlohmann::json;
#include "source-bound.h"

static const std::string requestId="r29-request-000000000000000041";
static const char* expectedStatus="submitted";
static constexpr unsigned long long sequence=41;
static unsigned long long assertions=0, failures=0, scannerCases=0, lookupCases=0, helperCases=0;
static unsigned long long baselineDomFailures=0,candidateDomFailures=0,differentialFailures=0;
static std::uint64_t outputDigest=1469598103934665603ULL;
static json failureDetails=json::array(),caseOutputs=json::array();
static bool withDom=true;
static volatile unsigned long long sink=0;

static std::string hex(const std::string& bytes) {
    static const char digits[]="0123456789abcdef";
    std::string out; out.reserve(bytes.size()*2);
    for(unsigned char c:bytes) {out+=digits[c>>4];out+=digits[c&15];}
    return out;
}
static void digest(const std::string& text) {
    for(unsigned char c:text) {outputDigest^=c;outputDigest*=1099511628211ULL;}
    outputDigest^=0xff; outputDigest*=1099511628211ULL;
}
static void check(bool pass,const std::string& reason,const std::string& input={}) {
    ++assertions;
    if(pass) return;
    ++failures;
    if(failureDetails.size()<120) failureDetails.push_back({{"reason",reason},{"inputHex",hex(input)}});
}
static void bind() {
    g_inputHostRunId="fixture-run-r29-20261015-source-bound";
    g_inputHostStartupNonce="fixture-startup-r29-20261015-source-bound";
    g_inputHostHostInstanceId="fixture-host-r29-20261015-source-bound";
    g_inputHostEpoch=23;g_inputHostConfigRevision=17;
    g_inputHostBoundPowerGeneration=31;g_powerGeneration.store(43,std::memory_order_relaxed);
    g_realStickTest.virtualTargetPersona="steamdeck";
    g_inputHostBuildPersona.clear();g_inputHostBoundPersona="steamdeck";
}
using Members=std::vector<std::pair<std::string,std::string>>;
static Members members() {
    json values={
        {"protocolVersion",1},{"command","SUBMIT_FRAME"},{"requestId",requestId},
        {"sequence",sequence},{"runId",g_inputHostRunId},{"epoch",g_inputHostEpoch},
        {"powerGeneration",inputHostProtocolPowerGenerationLocked()},
        {"targetId",kInputHostTargetId},{"ownerIdentity",kInputHostOwnerIdentity},
        {"persona",inputHostBoundOrCurrentPersona()},
        {"profileIdentity",inputHostBoundOrCurrentProfileIdentity()},
        {"startupNonce",g_inputHostStartupNonce},{"configRevision",g_inputHostConfigRevision},
        {"configHash",kInputHostProtocolConfigHash},{"assetVerified",true},
        {"lockedCoreSha256",kInputHostLockedCoreSha256},{"lockedCoreMvid",kInputHostLockedCoreMvid},
        {"hostInstanceId",g_inputHostHostInstanceId.empty()?"fixture-new-host-r29-20261015-source-bound":g_inputHostHostInstanceId},
        {"ok",true},{"status",expectedStatus},{"submitElapsedUs",77ULL},{"firstFrame",true}
    };
    Members out;
    for(const auto& field:baseline::kFieldNames) out.emplace_back(field.name,values.at(field.name).dump());
    return out;
}
static std::string wire(const Members& fields,bool rawNames=false) {
    std::string out="{";
    for(const auto& item:fields) {
        if(out.size()>1) out+=',';
        out+=rawNames?'"'+item.first+'"':json(item.first).dump();
        out+=':';out+=item.second;
    }
    out+='}';return out;
}
static std::string detail(const std::string& value,const std::string& key="detail") {
    auto fields=members();fields.emplace_back(key,value);return wire(fields);
}
static std::string replace(const std::string& name,const std::string& value) {
    auto fields=members();
    for(auto& item:fields) if(item.first==name) item.second=value;
    return wire(fields);
}
static json output(const InputHostFastReceipt& x) {
    return {{"used",x.used},{"firstFrame",x.firstFrame},{"submitElapsedUs",x.submitElapsedUs},{"hostInstanceId",x.hostInstanceId}};
}
static bool same(const InputHostFastReceipt& a,const InputHostFastReceipt& b) {
    return a.used==b.used&&a.firstFrame==b.firstFrame&&a.submitElapsedUs==b.submitElapsedUs&&a.hostInstanceId==b.hostInstanceId;
}
static bool domSame(const DomResult& dom,const InputHostFastReceipt& x) {
    return dom.accepted&&dom.firstFrame==x.firstFrame&&dom.submitUs==x.submitElapsedUs&&dom.host==x.hostInstanceId;
}
static void scanCase(const std::string& name,const std::string& text,int expected=-1,bool trace=true) {
    ++scannerCases;
    InputHostFastReceipt before,after;
    if(scannerCases&1) {
        before.used=after.used=true;before.firstFrame=after.firstFrame=true;
        before.submitElapsedUs=after.submitElapsedUs=0xdeadbeefULL;
        before.hostInstanceId=after.hostInstanceId="initial-output-sentinel-unchanged-on-fallback";
    }
    const bool b=baseline::scanSubmitFrameReceipt(text.data(),text.data()+text.size(),expectedStatus,requestId,sequence,before);
    const bool a=candidate::scanSubmitFrameReceipt(text.data(),text.data()+text.size(),expectedStatus,requestId,sequence,after);
    const bool equal=b==a&&same(before,after);
    differentialFailures+=!equal;
    check(equal,name+": full scanner boolean and all four output members",text);
    if(expected>=0) {check(b==(expected!=0),name+": baseline expected acceptance",text);check(a==(expected!=0),name+": candidate expected acceptance",text);}
    json record;
    if(trace) record={{"case",name},{"inputHex",hex(text)},{"baselineAccepted",b},{"candidateAccepted",a},
                      {"baselineOutput",output(before)},{"candidateOutput",output(after)}};
    if(withDom&&(trace||b||a)) {
        const auto dom=actualDomOracle(text.data(),text.data()+text.size(),expectedStatus,requestId,sequence);
        const bool bSafe=!b||domSame(dom,before),aSafe=!a||domSame(dom,after);
        baselineDomFailures+=!bSafe;candidateDomFailures+=!aSafe;
        check(bSafe,name+": baseline fast acceptance requires same actual DOM output",text);
        check(aSafe,name+": candidate fast acceptance requires same actual DOM output",text);
        if(trace) record["dom"]={{"accepted",dom.accepted},{"discarded",dom.discarded},
            {"firstFrame",dom.firstFrame},{"submitElapsedUs",dom.submitUs},{"hostInstanceId",dom.host},{"diff",dom.diff}};
    }
    digest(text);digest(b?"accept":"fallback");digest(a?"accept":"fallback");
    digest(output(before).dump());digest(output(after).dump());
    if(trace) caseOutputs.push_back(std::move(record));
}
static int referenceLookup(const std::string& key) {
    for(const auto& field:baseline::kFieldNames)
        if(static_cast<std::size_t>(field.len)==key.size()&&std::memcmp(key.data(),field.name,key.size())==0) return field.id;
    return -1;
}
static void lookupSpanCase(const std::string& storage,std::size_t start,std::size_t length) {
    ++lookupCases;
    const auto key=storage.substr(start,length);
    const auto* b=storage.data()+start;
    const int expected=referenceLookup(key);
    const int before=baseline::lookupField(b,b+length),after=candidate::lookupField(b,b+length);
    check(before==expected&&after==expected,"bounded span lookup ignores prefix/suffix bytes and terminators",storage);
    digest(key);digest(std::to_string(before));digest(std::to_string(after));
}
static void lookupCase(const std::string& text) {
    ++lookupCases;
    const int expected=referenceLookup(text);
    const int b=baseline::lookupField(text.data(),text.data()+text.size());
    const int a=candidate::lookupField(text.data(),text.data()+text.size());
    check(a==expected&&b==expected,"raw lookup IDs and -1",text);
    digest(text);digest(std::to_string(a));digest(std::to_string(b));
}
static std::ptrdiff_t offset(const char* p,const char* begin) {return p?p-begin:-1;}
static void helperCase(const std::string& text) {
    ++helperCases;const char* p=text.data();const char* end=p+text.size();
    const char *bn=nullptr,*an=nullptr;bool bu=false,au=false;unsigned long long bv=0,av=0;
    bool b=baseline::scanNumber(p,end,bu,bv,bn),a=candidate::scanNumber(p,end,au,av,an);
    check(a==b&&au==bu&&av==bv&&offset(an,p)==offset(bn,p),"source number scanner parity",text);
    bn=an=nullptr;baseline::FieldView bf;candidate::FieldView af;
    b=baseline::captureValue(p,end,bf,bn);a=candidate::captureValue(p,end,af,an);
    check(a==b&&af.kind==bf.kind&&af.escaped==bf.escaped&&af.num==bf.num&&af.boolVal==bf.boolVal&&
          offset(af.s,p)==offset(bf.s,p)&&offset(af.e,p)==offset(bf.e,p)&&offset(an,p)==offset(bn,p),"capture all FieldView members including partial output",text);
    bn=an=nullptr;const char *bs=nullptr,*be=nullptr,*as=nullptr,*ae=nullptr;bool besc=false,aesc=false;
    b=baseline::scanString(p,end,bs,be,besc,bn);a=candidate::scanString(p,end,as,ae,aesc,an);
    check(a==b&&besc==aesc&&offset(bs,p)==offset(as,p)&&offset(be,p)==offset(ae,p)&&offset(bn,p)==offset(an,p),"source string scanner views and escapes",text);
    for(int depth: {0,31,32,33}) {
        bn=an=nullptr;b=baseline::validateValue(p,end,depth,bn);a=candidate::validateValue(p,end,depth,an);
        check(a==b&&offset(an,p)==offset(bn,p),"source recursive validator and next pointer",text);
    }
}
static std::vector<std::string> numberTokens() {
    return {"0","1","-0","-1","2147483647","2147483648","4294967295","4294967296",
        "9007199254740991","9007199254740992","9223372036854775807","9223372036854775808",
        "18446744073709551615","18446744073709551616","184467440737095516150000000000",
        "1.0","-1.0","1e0","1E+0","1e-1","1e308","1.7976931348623157e308",
        "1.7976931348623159e308","1e309","1e9999","-1e9999","0e9999","1e-9999",
        "5e-324","2.2250738585072014e-308","01","00","-01","+1",".1","1.","1e","1e+","1e-","--1","NaN","Infinity","-Infinity","0x10","","-"};
}
static std::vector<std::string> validShapes() {
    return {"null","true","false","0","-1","1.25e2","1e308",R"("")",R"("plain")",
        R"("\"\\\/\b\f\n\r\t")",R"("\u0000\u0041\uFFFF")",R"("\uD800\uDC00")",
        R"("\uDBFF\uDFFF")",json("未知字段-😀").dump(),"[]","{}",
        R"({"nested":[null,true,false,1,-2.5e3,{"x":"\uD83D\uDE00"}]})"};
}
// Predeclared success workload: all metadata values remain provably acceptable
// after the announced R28 conservative numeric fallback. Other valid JSON
// numbers remain in validShapes/numberTokens for correctness and DOM fallback.
static std::vector<std::string> fastShapes() {
    return {"null","true","false","0","18446744073709551615",R"("")",R"("plain")",
        R"("\"\\\/\b\f\n\r\t")",R"("\u0000\u0041\uFFFF")",R"("\uD800\uDC00")",
        R"("\uDBFF\uDFFF")",json("未知字段-😀").dump(),"[]","{}",
        R"({"nested":[null,true,false,1,18446744073709551615,{"x":"\uD83D\uDE00"}]})"};
}
static void corpus() {
    static_assert(baseline::kFieldCount==22&&candidate::kFieldCount==22);
    static_assert(std::is_same_v<decltype(InputHostFastReceipt::submitElapsedUs),unsigned long long>);
    static_assert(std::is_same_v<decltype(InputHostFastReceipt::firstFrame),bool>);
    static_assert(std::is_same_v<decltype(InputHostFastReceipt::hostInstanceId),std::string>);
    static_assert(std::is_same_v<decltype(baseline::FieldView::kind),int>);
    static_assert(std::is_same_v<decltype(baseline::FieldView::s),const char*>);
    static_assert(std::is_same_v<decltype(baseline::FieldView::num),unsigned long long>);
    static_assert(std::is_same_v<decltype(g_inputHostEpoch),unsigned long long>);
    static_assert(std::is_same_v<decltype(g_inputHostRunId),std::string>);
    InputHostFastReceipt defaults;
    check(!defaults.used&&!defaults.firstFrame&&defaults.submitElapsedUs==0&&defaults.hostInstanceId.empty(),"exact receipt defaults");
    baseline::FieldView view;
    check(view.kind==baseline::kMissing&&!view.s&&!view.e&&!view.escaped&&!view.num&&!view.boolVal,"exact FieldView defaults");
    const auto good=wire(members());scanCase("whole-ack-success",good,1);
    auto fields=members();std::reverse(fields.begin(),fields.end());scanCase("reverse-order",wire(fields),1);
    for(std::size_t i=0;i<22;++i) {fields=members();std::rotate(fields.begin(),fields.begin()+i,fields.end());scanCase("rotated-order-"+std::to_string(i),wire(fields),1);}
    std::mt19937_64 rng(20261015);
    for(int i=0;i<64;++i) {fields=members();std::shuffle(fields.begin(),fields.end(),rng);scanCase("shuffled-order-"+std::to_string(i),wire(fields),1);}
    scanCase("outer-whitespace"," \r\n\t"+good+" \t\r\n",1);
    scanCase("pretty-whitespace",json::parse(good).dump(2),1);
    for(std::size_t i=0;i<22;++i) {
        fields=members();const auto name=fields[i].first;fields.erase(fields.begin()+i);
        const bool optional=name=="firstFrame"||name=="submitElapsedUs";
        scanCase("missing-"+name,wire(fields),optional?1:0);
        fields=members();fields.push_back(fields[i]);scanCase("duplicate-"+name,wire(fields),0);
        fields.back().second="null";scanCase("duplicate-null-last-"+name,wire(fields),0);
        fields=members();fields[i].first[0]=static_cast<char>(fields[i].first[0]^0x20);
        scanCase("case-sensitive-"+name,wire(fields),optional?1:0);
        for(const std::string& value:std::vector<std::string>{"null","false","true","0","-1","1.0",R"("")",R"("wrong")","[]","{}"})
            scanCase("type-value-"+name+"-"+hex(value),replace(name,value));
        fields=members();const std::string escaped="\\u00"+hex(std::string(1,fields[i].first[0]))+fields[i].first.substr(1);
        fields[i].first=escaped;scanCase("escaped-key-"+name,wire(fields,true),0);
        // An escaped string in a known member must keep the production fallback.
        if(json::parse(fields[i].second).is_string()) {
            auto plain=json::parse(fields[i].second).get<std::string>();
            if(!plain.empty()) {
                const auto encoded='"'+std::string("\\u00")+hex(plain.substr(0,1))+plain.substr(1)+'"';
                scanCase("escaped-known-value-"+name,replace(name,encoded),0);
            }
        }
    }
    fields=members();fields.erase(std::remove_if(fields.begin(),fields.end(),[](const auto& x){return x.first=="firstFrame"||x.first=="submitElapsedUs";}),fields.end());
    scanCase("both-optional-defaults",wire(fields),1);
    scanCase("elapsed-max",replace("submitElapsedUs","18446744073709551615"),1);
    scanCase("first-frame-false",replace("firstFrame","false"),1);
    scanCase("host-empty",replace("hostInstanceId",R"("")"),0);
    scanCase("host-wrong-instance",replace("hostInstanceId",R"("other-host")"),0);
    for(const auto& value:validShapes()) {helperCase(value);scanCase("unknown-valid-shape-"+hex(value),detail(value));}
    for(const auto& value:fastShapes()) scanCase("unknown-fast-success-shape-"+hex(value),detail(value),1);
    for(const std::string& key:std::vector<std::string>{"","DETAIL","detail","未知-😀",std::string(80,'x')}) scanCase("unknown-key-"+hex(key),detail(R"({"x":[1,true,null]})",key),1);
    fields=members();fields.emplace_back("detail","1");fields.emplace_back("detail",R"({"last":true})");scanCase("duplicate-unknown-key",wire(fields),1);
    for(const auto& value:std::vector<std::string>{R"("\uD800")",R"("\uDC00")",R"("\uD800x")",R"("\uDC00\uD800")",R"("\uD800\u0041")",R"({"\uD800":1})",R"(["\uDC00"])","1e9999","-1e9999",R"({"overflow":[1e9999]})"}) {
        helperCase(value);scanCase("R28-required-safety-"+hex(value),detail(value),0);
    }
    for(const auto& token:numberTokens()) {
        helperCase(token);scanCase("unknown-number-"+hex(token),detail(token));
        for(const std::string name:{"protocolVersion","sequence","epoch","powerGeneration","configRevision","submitElapsedUs"})
            scanCase("known-number-"+name+"-"+hex(token),replace(name,token));
    }
    for(int depth=0;depth<=36;++depth) {
        std::string arr(depth,'[');arr+='0';arr+=std::string(depth,']');
        helperCase(arr);scanCase("array-depth-"+std::to_string(depth),detail(arr),depth<=32?1:0);
        std::string obj="0";for(int i=0;i<depth;++i)obj="{\"x\":"+obj+'}';
        helperCase(obj);scanCase("object-depth-"+std::to_string(depth),detail(obj),depth<=32?1:0);
    }
    for(const auto& value:std::vector<std::string>{R"("\q")",R"("\u12")",R"("\uZZZZ")",R"("unterminated)","[1,]","{\"x\":}","{\"x\" 1}","[true false]","tru","falsex","nullx","[}","{]"}) {
        helperCase(value);scanCase("malformed-unknown-"+hex(value),detail(value),0);
    }
    for(const auto& bytes:std::vector<std::string>{std::string("\0",1),std::string("\x01",1),std::string("\x1f",1),std::string("\x80",1),std::string("\xc0\xaf",2),std::string("\xed\xa0\x80",3),std::string("\xf4\x90\x80\x80",4),std::string("\xff",1),std::string("\xe2\x82",2),std::string("\xc2\xa0",2),std::string("\xf4\x8f\xbf\xbf",4)}) {
        const auto value='"'+bytes+'"';helperCase(value);scanCase("raw-string-bytes-"+hex(bytes),detail(value));
    }
    for(std::size_t n=0;n<good.size();++n)scanCase("truncated-"+std::to_string(n),good.substr(0,n),0);
    for(const std::string& suffix:std::vector<std::string>{"x","{}",",",std::string("\0",1),"\v","\f"})scanCase("trailing-junk-"+hex(suffix),good+suffix,0);
    for(const std::string& value:std::vector<std::string>{"","{}","[]","null","true",R"("string")","{,}","{\"x\":1,}","{\"x\":1}{}"})scanCase("non-ack-"+hex(value),value,0);
    // Every replacement byte, including NUL/non-ASCII, at every name position.
    // Exercise lookup directly AND actual full scanner with a raw unknown member.
    for(const auto& field:baseline::kFieldNames) {
        const std::string name=field.name;lookupCase(name);
        lookupSpanCase("prefix"+name+"suffix",6,name.size());
        lookupSpanCase(name+std::string("\0suffix",7),0,name.size());
        lookupSpanCase(name,1,0);
        for(std::size_t pos=0;pos<name.size();++pos) for(unsigned byte=0;byte<256;++byte) {
            auto mutated=name;mutated[pos]=static_cast<char>(byte);lookupCase(mutated);
            scanCase("every-field-byte-mutation","{\""+mutated+"\":true,"+good.substr(1),-1,false);
            auto replaced=good;
            const auto keyPosition=good.find('"'+name+"\":");
            if(keyPosition==std::string::npos) throw std::runtime_error("source field missing from whole ACK fixture");
            replaced.replace(keyPosition+1,name.size(),mutated);
            scanCase("every-known-field-byte-replacement",replaced,-1,false);
        }
        for(std::size_t n=0;n<name.size();++n)lookupCase(name.substr(0,n));
        for(const auto& suffix:std::vector<std::string>{std::string("\0",1),"x"," ","😀"}) {lookupCase(name+suffix);lookupCase(suffix+name);}
    }
    for(int n=0;n<=128;++n) {lookupCase(std::string(n,'x'));lookupCase(std::string(n,'\0'));lookupCase(std::string(n,static_cast<char>(0xff)));}
    for(int i=0;i<4000;++i) {std::string text(rng()%49,' ');for(char& c:text)c=static_cast<char>(rng()&255);lookupCase(text);}
    for(const std::string& persona:std::vector<std::string>{"xbox360","steamdeck","dualsense","elite","dualsense-edge","dualshock4","unknown"}) {
        g_realStickTest.virtualTargetPersona=persona;g_inputHostBuildPersona.clear();g_inputHostBoundPersona.clear();scanCase("live-persona-"+persona,wire(members()),1);
        g_inputHostBoundPersona=persona;scanCase("bound-persona-"+persona,wire(members()),1);
        g_inputHostBuildPersona=persona;scanCase("build-persona-"+persona,wire(members()),1);
    }
    bind();g_inputHostHostInstanceId.clear();scanCase("unbound-host-first-ack",wire(members()),1);
    bind();g_inputHostBoundPowerGeneration=0;scanCase("power-generation-atomic-fallback",wire(members()),1);
    for(auto n:std::vector<unsigned long long>{0,1,0x7fffffffULL,0xffffffffULL,0x7fffffffffffffffULL,0xffffffffffffffffULL}) {
        bind();g_inputHostEpoch=n;g_inputHostConfigRevision=n;g_inputHostBoundPowerGeneration=n;g_powerGeneration.store(n);scanCase("context-unsigned-boundary-"+std::to_string(n),wire(members()),1);
    }
    bind();
}

using Scanner=bool(*)(const char*,const char*,const char*,const std::string&,unsigned long long,InputHostFastReceipt&);
using Lookup=int(*)(const char*,const char*);
__declspec(noinline) static bool beforeScan(const char* b,const char* e,const char* s,const std::string& r,unsigned long long n,InputHostFastReceipt& out) {
    return baseline::scanSubmitFrameReceipt(b,e,s,r,n,out);
}
__declspec(noinline) static bool afterScan(const char* b,const char* e,const char* s,const std::string& r,unsigned long long n,InputHostFastReceipt& out) {
    return candidate::scanSubmitFrameReceipt(b,e,s,r,n,out);
}
__declspec(noinline) static int beforeLookup(const char* b,const char* e) {return baseline::lookupField(b,e);}
__declspec(noinline) static int afterLookup(const char* b,const char* e) {return candidate::lookupField(b,e);}
struct Workload {std::string name;bool lookup;std::vector<std::string> inputs;};
static std::vector<Workload> workloads() {
    std::vector<Workload> out;auto seed=members();
    out.push_back({"scanner-whole-ack",false,{wire(seed)}});
    Workload alternate{"scanner-alternate-order",false,{}};
    auto f=seed;std::reverse(f.begin(),f.end());alternate.inputs.push_back(wire(f));
    for(int n:{1,7,13}) {f=seed;std::rotate(f.begin(),f.begin()+n,f.end());alternate.inputs.push_back(wire(f));}
    out.push_back(std::move(alternate));
    f=seed;f.erase(std::remove_if(f.begin(),f.end(),[](const auto& x){return x.first=="firstFrame"||x.first=="submitElapsedUs";}),f.end());
    out.push_back({"scanner-optional-defaults",false,{wire(f)}});
    Workload shapes{"scanner-unknown-valid-shapes",false,{}};
    for(const auto& shape:fastShapes())shapes.inputs.push_back(detail(shape));
    out.push_back(std::move(shapes));
    f=seed;for(int i=0;i<32;++i) f.emplace_back("unknown_"+std::to_string(i)+std::string(i%17,'x'),fastShapes()[i%fastShapes().size()]);
    out.push_back({"scanner-unknown-heavy",false,{wire(f)}});
    Workload known{"lookup-known-fields",true,{}};
    for(const auto& field:baseline::kFieldNames)known.inputs.emplace_back(field.name);
    std::mt19937_64 rng(20261015);std::shuffle(known.inputs.begin(),known.inputs.end(),rng);out.push_back(std::move(known));
    Workload unknown{"lookup-unknown-bytes",true,{"","detail","unknown","未知-😀"}};
    for(const auto& field:baseline::kFieldNames) {
        auto text=std::string(field.name);text[text.size()/2]='X';unknown.inputs.push_back(text);
        text=field.name;text.back()='\0';unknown.inputs.push_back(text);
        text=field.name;text[0]=static_cast<char>(0xff);unknown.inputs.push_back(text);
    }
    out.push_back(std::move(unknown));return out;
}
struct Measurement {double ns;Allocations allocations;unsigned long long checksum,operations;};
static Measurement measure(const Workload& w,bool after,int batches,bool count) {
    Scanner scanner=after?afterScan:beforeScan;Lookup lookup=after?afterLookup:beforeLookup;
    unsigned long long checksum=0;std::size_t index=0;allocated={};counting=count;
    const auto start=std::chrono::steady_clock::now();
    for(int batch=0;batch<batches;++batch) for(int n=0;n<32;++n) {
        const std::string& text=w.inputs[index];if(++index==w.inputs.size())index=0;
        if(w.lookup) checksum+=static_cast<unsigned>(lookup(text.data(),text.data()+text.size())+1);
        else {
            InputHostFastReceipt receipt;
            const bool ok=scanner(text.data(),text.data()+text.size(),expectedStatus,requestId,sequence,receipt);
            checksum+=static_cast<unsigned long long>(ok)+receipt.used+receipt.firstFrame+receipt.submitElapsedUs+receipt.hostInstanceId.size();
            if(!receipt.hostInstanceId.empty()) checksum+=static_cast<unsigned char>(receipt.hostInstanceId.front())+static_cast<unsigned char>(receipt.hostInstanceId.back());
        }
    }
    const auto finish=std::chrono::steady_clock::now();counting=false;
    const auto counts=allocated;sink=checksum;
    const auto operations=static_cast<unsigned long long>(32*batches);
    return {std::chrono::duration<double,std::nano>(finish-start).count()/operations,counts,checksum,operations};
}
static double median(std::vector<double> values) {std::sort(values.begin(),values.end());return values[values.size()/2];}
static json counts(const Allocations& c,unsigned long long operations) {
    return {{"ordinaryRequests",c.ordinaryRequests},{"ordinaryRequestedBytes",c.ordinaryBytes},
            {"alignedRequests",c.alignedRequests},{"alignedRequestedBytes",c.alignedBytes},
            {"operations",operations},{"requestsPerOperation",(c.ordinaryRequests+c.alignedRequests)/static_cast<double>(operations)},
            {"requestedBytesPerOperation",(c.ordinaryBytes+c.alignedBytes)/static_cast<double>(operations)}};
}
static json formal(const Workload& w,int campaign) {
    measure(w,false,6,false);measure(w,true,6,false);
    std::vector<double> before,after,paired;json rounds=json::array();int wins=0;bool checksumsAgree=true;
    for(int round=0;round<11;++round) {
        Measurement b,a;const bool baselineFirst=((round+campaign)&1)==0;
        if(baselineFirst) {b=measure(w,false,128,false);a=measure(w,true,128,false);}
        else {a=measure(w,true,128,false);b=measure(w,false,128,false);}
        before.push_back(b.ns);after.push_back(a.ns);paired.push_back((b.ns-a.ns)/b.ns);wins+=a.ns<b.ns;
        checksumsAgree=checksumsAgree&&b.checksum==a.checksum;
        rounds.push_back({{"round",round+1},{"baselineFirst",baselineFirst},{"beforeNsPerOperation",b.ns},
            {"afterNsPerOperation",a.ns},{"pairedReduction",paired.back()},{"beforeChecksum",b.checksum},{"afterChecksum",a.checksum}});
    }
    const auto b=measure(w,false,2,true),a=measure(w,true,2,true);
    const bool noIncrease=a.allocations.ordinaryRequests<=b.allocations.ordinaryRequests&&a.allocations.ordinaryBytes<=b.allocations.ordinaryBytes&&
        a.allocations.alignedRequests<=b.allocations.alignedRequests&&a.allocations.alignedBytes<=b.allocations.alignedBytes;
    checksumsAgree=checksumsAgree&&b.checksum==a.checksum;
    const double pm=median(paired),ratio=1.0-median(after)/median(before);
    return {{"workload",w.name},{"scope",w.lookup?"lookup":"actual-full-scanner"},{"campaign",campaign},
        {"fasterRounds",wins},{"pairedRounds",11},{"medianPairedReduction",pm},{"ratioOfMediansReduction",ratio},
        {"beforeMedianNsPerOperation",median(before)},{"afterMedianNsPerOperation",median(after)},
        {"allocationsBefore",counts(b.allocations,b.operations)},{"allocationsAfter",counts(a.allocations,a.operations)},
        {"allocationRequestsAndBytesDoNotIncrease",noIncrease},{"checksumsAgree",checksumsAgree},{"rounds",rounds},
        {"pass",wins>=10&&pm>=.05&&ratio>=.05&&noIncrease&&checksumsAgree}};
}
int main(int argc,char** argv) {
    try {
        if(argc!=3)throw std::runtime_error("usage: fixture verify dom|no-dom OR fixture benchmark campaign");
        bind();const std::string mode=argv[1];
        if(mode=="verify") {
            withDom=std::string(argv[2])=="dom";corpus();
            json workloadInputs=json::array();
            for(const auto& w:workloads()) {
                json bytes=json::array();
                for(std::size_t i=0;i<w.inputs.size();++i) {
                    const auto& input=w.inputs[i];bytes.push_back(hex(input));
                    if(w.lookup)lookupCase(input);
                    else scanCase("declared-workload-"+w.name+"-"+std::to_string(i),input,1);
                }
                workloadInputs.push_back({{"workload",w.name},{"lookup",w.lookup},{"inputHex",bytes}});
            }
            json report={{"pass",failures==0},{"assertions",assertions},{"failureCount",failures},
                {"scannerCases",scannerCases},{"lookupCases",lookupCases},{"helperCases",helperCases},
                {"differentialFailures",differentialFailures},{"baselineDomSafetyFailures",baselineDomFailures},
                {"candidateDomSafetyFailures",candidateDomFailures},{"actualDomOracleEnabled",withDom},
                {"fullOutputDigestFNV1a64",std::to_string(outputDigest)},{"failures",failureDetails},
                {"caseOutputs",caseOutputs},{"declaredWorkloadInputs",workloadInputs},{"benchmarkExecuted",false},{"physicalOrProductExecuted",false}};
            std::cout<<report.dump(2)<<'\n';return failures?2:0;
        }
        if(mode!="benchmark")throw std::runtime_error("unknown mode");
        const int campaign=std::stoi(argv[2]);if(campaign!=1&&campaign!=2)throw std::runtime_error("campaign must be 1 or 2");
        // The Python authorization gate validates immutable R28 correctness before
        // invoking this mode. This process performs only the predeclared campaign.
        json rows=json::array();bool pass=true;
        for(const auto& w:workloads()) {auto row=formal(w,campaign);pass=pass&&row.at("pass").get<bool>();rows.push_back(std::move(row));}
        std::cout<<json({{"pass",pass},{"campaign",campaign},{"rows",rows},{"wholeProcessCpuClaim",false}}).dump(2)<<'\n';return pass?0:2;
    } catch(const std::exception& error) {counting=false;std::cerr<<error.what()<<'\n';return 1;}
}

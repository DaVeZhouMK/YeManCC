#if defined(YMCC_VENDOR_PROFILE)
#include "../native/steam_original_recovery_engine/vendor/steam_native_record_win32.h"
#else
#include "../native/steam_native_record_win32.h"
#endif
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>
namespace sr=ymcc::steamrecord;
namespace rw=ymcc::steamrecordwin;
using U64=std::uint64_t;
using J=sr::Json;
unsigned checks=0;
void need(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
struct Disk {
    std::vector<unsigned char> bytes;
    IMAGE_NT_HEADERS64 nt{};
    std::vector<IMAGE_SECTION_HEADER> sections;
    explicit Disk(const char* path){
        std::ifstream f(path,std::ios::binary);need(f.good(),"open fixed PE");
        bytes.assign(std::istreambuf_iterator<char>(f),{});
        IMAGE_DOS_HEADER dos{};need(bytes.size()>=sizeof dos,"DOS header");std::memcpy(&dos,bytes.data(),sizeof dos);
        need(dos.e_magic==IMAGE_DOS_SIGNATURE&&dos.e_lfanew>0,"PE DOS signature");
        const auto off=static_cast<std::size_t>(dos.e_lfanew);need(off+sizeof nt<=bytes.size(),"PE bounds");
        std::memcpy(&nt,bytes.data()+off,sizeof nt);need(nt.Signature==IMAGE_NT_SIGNATURE&&nt.OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC,"PE x64");
        const auto start=off+sizeof(DWORD)+sizeof(IMAGE_FILE_HEADER)+nt.FileHeader.SizeOfOptionalHeader;
        need(start+nt.FileHeader.NumberOfSections*sizeof(IMAGE_SECTION_HEADER)<=bytes.size(),"sections bounds");
        sections.resize(nt.FileHeader.NumberOfSections);std::memcpy(sections.data(),bytes.data()+start,sections.size()*sizeof(IMAGE_SECTION_HEADER));
    }
    bool read(U64 rva,void* out,std::size_t n)const{
        for(const auto& s:sections)if(rva>=s.VirtualAddress&&rva-s.VirtualAddress<=s.SizeOfRawData&&n<=s.SizeOfRawData-(rva-s.VirtualAddress)){
            const auto off=s.PointerToRawData+rva-s.VirtualAddress;if(off+n>bytes.size())return false;
            std::memcpy(out,bytes.data()+off,n);return true;
        }
        return false;
    }
};
constexpr U64 BASE=0x140000000,ENGINE=0x220000000,VECTOR=0x230000000,USER=0x240000000,RECORD=0x250000000;
constexpr U64 ACCOUNT=0x0110000155667788,GAME=0xf1234567abc00000;
constexpr unsigned PID=44551,FG=44552;
struct Fixture {
    const Disk& disk;
    sr::Query q{BASE,0x2000000,ACCOUNT,GAME,PID,true};
    std::map<U64,unsigned char> heap;
    std::function<void(U64,std::size_t,unsigned)> hook;
    std::map<U64,unsigned> seen;
    unsigned reads=0,stableCalls=0;
    bool identity=true,identityAfter=true,throwRead=false;
    U64 tamperedCode=0;
    explicit Fixture(const Disk& d,sr::ProfileId id):disk(d){
        q.moduleSize=d.nt.OptionalHeader.SizeOfImage;q.profile=id;
        const auto& p=sr::detail::profileLayout(id);
        put(BASE+p.engineRoot,ENGINE);put(ENGINE,BASE+p.engineVtable);put(ENGINE+p.engineInterfaceOffset,BASE+p.engineInterfaceVtable);
        put(ENGINE+p.userVectorOffset,VECTOR);put(ENGINE+p.userCountOffset,2,4);
        user(0,ACCOUNT^0x100000000ULL);user(1,ACCOUNT);
        put(USER+0x10000+p.recordsOffset,RECORD);put(USER+0x10000+p.recordCountOffset,1,4);
        row(0,GAME,PID,FG);
    }
    void put(U64 a,U64 v,unsigned n=8){for(unsigned i=0;i<n;++i)heap[a+i]=static_cast<unsigned char>(v>>(i*8));}
    void user(unsigned i,U64 account){const auto& p=sr::detail::profileLayout(q.profile);const auto u=USER+0x10000ULL*i;
        put(VECTOR+p.userStride*i+p.userEntryOffset,u);put(u,BASE+p.userVtable);put(u+p.userInterfaceOffset,BASE+p.userInterfaceVtable);put(u+p.accountOffset,account);
    }
    void row(unsigned i,U64 game,unsigned primary,unsigned foreground){
        const auto& p=sr::detail::profileLayout(q.profile);const auto a=RECORD+p.recordStride*i;
        put(a+p.gameIdOffset,game);put(a+p.pidPairOffset,foreground,4);put(a+p.pidPairOffset+4,primary,4);
        put(a+p.predicateFlagsOffset,1,1);put(a+p.predicateFlagsOffset+1,0,1);
        put(a+p.predicateCounterOffset,8,4);put(a+p.predicateAlternateOffset,PID,4);
    }
    J run(){return sr::observe(q,[&](U64 a,void* out,std::size_t n){
        ++reads;const auto occurrence=++seen[a];if(hook)hook(a,n,occurrence);
        if(throwRead)throw std::runtime_error("private exception text");
        bool sparse=true;for(std::size_t i=0;i<n;++i)if(!heap.count(a+i)){sparse=false;break;}
        if(sparse){for(std::size_t i=0;i<n;++i)static_cast<unsigned char*>(out)[i]=heap[a+i];return true;}
        if(a>=BASE&&a-BASE<q.moduleSize&&disk.read(a-BASE,out,n)){
            if(tamperedCode>=a&&tamperedCode-a<n)static_cast<unsigned char*>(out)[tamperedCode-a]^=1;
            return true;
        }
        return false;
    },[&]{++stableCalls;return stableCalls==1?identity:identityAfter;});}
};
void unavailable(const J& j,const char* why){
    need(j.at("available")==false,"negative available");need(j.at("reason")==why,"negative exact reason");
    need(j.at("recordState")=="unknown"&&j.at("originalPrimaryMatched").is_null(),"negative no partial authority");
}
void positive(const J& j,sr::ProfileId id){
    need(j.at("available")==true&&j.at("originalPrimaryMatched")==true,"positive original PID");
    need(j.at("primaryLivenessPid")==PID&&j.at("foregroundComparisonPid")==FG,"distinct PID pair order");
    need(j.at("profileVersion")==sr::profileVersion(id),"profile output selected");
    need(j.at("atomicSnapshot")==false&&j.at("inputVerified")==false&&j.at("continueVerified")==false,"diagnostic authority unchanged");
#if !defined(YMCC_VENDOR_PROFILE)
    need(j.at("predicateInputs").at("available")==true,"root extension preserved and new PE anchors selected");
    need(j.at("predicateInputs").at("failedCheckCounterRaw")==8,"root raw counter");
    need(j.at("predicateInputs").at("profileVersion")==sr::detail::profileLayout(id).predicateVersion,"predicate profile metadata");
#else
    need(!j.contains("predicateInputs"),"vendor difference preserved");
#endif
}
J runProfile(const Disk& disk,sr::ProfileId id){
    const auto& p=sr::detail::profileLayout(id);unsigned cases=0;
    {Fixture f(disk,id);auto j=f.run();positive(j,id);need(!f.seen.count(USER+p.recordsOffset),"other account record header never read");++cases;}
    {Fixture f(disk,id);f.q.profile=id==sr::ProfileId::Legacy1791918634?sr::ProfileId::Public1792618285:sr::ProfileId::Legacy1791918634;
        unavailable(f.run(),"memory-read-failed");++cases;}
    {Fixture f(disk,id);f.tamperedCode=BASE+p.getterRva;unavailable(f.run(),"getter-mismatch");++cases;}
    {Fixture f(disk,id);f.put(ENGINE,BASE+1);unavailable(f.run(),"engine-type-mismatch");++cases;}
    {Fixture f(disk,id);f.put(ENGINE+p.engineInterfaceOffset,BASE+1);unavailable(f.run(),"engine-type-mismatch");++cases;}
    {Fixture f(disk,id);f.put(USER+0x10000,BASE+1);unavailable(f.run(),"user-type-mismatch");++cases;}
    {Fixture f(disk,id);f.put(USER+p.userInterfaceOffset,BASE+1);unavailable(f.run(),"user-type-mismatch");++cases;}
    {Fixture f(disk,id);f.identity=false;unavailable(f.run(),"identity-changed");need(f.reads==0,"identity initial zero reads");++cases;}
    {Fixture f(disk,id);f.identityAfter=false;unavailable(f.run(),"identity-changed");++cases;}
    {Fixture f(disk,id);f.hook=[&](U64 a,std::size_t,unsigned n){if(a==USER+0x10000+p.accountOffset&&n==2)f.put(a,ACCOUNT^1);};unavailable(f.run(),"account-changed");++cases;}
    {Fixture f(disk,id);f.hook=[&](U64 a,std::size_t,unsigned n){if(a==BASE+p.engineRoot&&n==2)f.put(a,ENGINE+1);};unavailable(f.run(),"user-selection-changed");++cases;}
    {Fixture f(disk,id);f.hook=[&](U64 a,std::size_t,unsigned n){if(a==RECORD+p.gameIdOffset&&n==2)f.put(a,GAME^0x100000000ULL);};unavailable(f.run(),"target-gameid-changed");++cases;}
    {Fixture f(disk,id);f.hook=[&](U64 a,std::size_t,unsigned n){if(a==RECORD+p.pidPairOffset&&n==2)f.put(a+4,PID+1,4);};unavailable(f.run(),"target-records-changed");++cases;}
    {Fixture f(disk,id);f.put(USER+0x10000+p.recordCountOffset,257,4);unavailable(f.run(),"record-count-invalid");++cases;}
    {Fixture f(disk,id);f.row(0,GAME^0x100000000ULL,PID,FG);auto j=f.run();need(j.at("available")==true&&j.at("recordState")=="not-observed","full GameID high bits distinguish decoy");++cases;}
    {Fixture f(disk,id);f.row(0,GAME,PID+1,FG);auto j=f.run();need(j.at("available")==true&&j.at("recordState")=="different-primary","wrong original primary explicit");++cases;}
    {Fixture f(disk,id);f.row(1,GAME,PID,FG);f.put(USER+0x10000+p.recordCountOffset,2,4);unavailable(f.run(),"target-record-ambiguous");++cases;}
    {Fixture f(disk,id);f.user(0,ACCOUNT);unavailable(f.run(),"account-ambiguous");++cases;}
    {Fixture f(disk,id);f.q.profile=static_cast<sr::ProfileId>(-1);unavailable(f.run(),"profile-unknown");need(f.reads==0&&f.stableCalls==0,"invalid enum zero reads");++cases;}
    {Fixture f(disk,id);f.q.profileVerified=false;unavailable(f.run(),"profile-unverified");need(f.reads==0,"unverified zero reads");++cases;}
    {Fixture f(disk,id);f.throwRead=true;auto j=f.run();unavailable(j,"memory-read-exception");need(j.dump().find("private exception")==std::string::npos,"exception privacy");++cases;}
#if !defined(YMCC_VENDOR_PROFILE)
    for(const auto rva:p.predicateRvas){Fixture f(disk,id);f.tamperedCode=BASE+rva;auto j=f.run();need(j.at("available")==true,"optional extension keeps core observation");
        need(j.at("predicateInputs").at("available")==false&&j.at("predicateInputs").at("reason")=="predicate-profile-anchor-mismatch","predicate anchor tamper rejected");++cases;}
#endif
    return {{"profile",sr::profileVersion(id)},{"cases",cases},{"passed",true}};
}
void writeFile(const std::wstring& path,const std::string& value){
    rw::Handle h(CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));need(h.valid(),"create own cache fixture");
    DWORD done=0;need(WriteFile(h.h,value.data(),static_cast<DWORD>(value.size()),&done,nullptr)&&done==value.size(),"write own cache fixture");
}
J cacheTests(const std::wstring& path){
    writeFile(path,"owned profile cache test");rw::Context c;c.steamPid=101;c.steamBirth=102;
    rw::Module m{BASE,0x2000000,path};rw::FileStamp stamp;need(rw::fileStamp(path,stamp),"fixture stamp");
    unsigned hashes=0;std::string digest=sr::profileSha256(sr::ProfileId::Public1792618285);
    rw::HashFile hash=[&](const std::wstring&){++hashes;return digest;};rw::DiskProfile selected;
    auto call=[&]{return rw::verifiedDiskProfile(c,m,stamp,hash,&selected);};
    need(call()&&selected.profileId==sr::ProfileId::Public1792618285&&selected.sha256==digest,"cache public SHA and id");
    need(call()&&hashes==1,"no repeated hashing");
    ++c.steamBirth;digest=sr::kClientSha256;need(call()&&hashes==2&&selected.profileId==sr::ProfileId::Legacy1791918634&&selected.sha256==digest,"process birth invalidates");
    ++c.steamPid;need(call()&&hashes==3,"process PID invalidates");
    ++m.base;need(call()&&hashes==4,"module base invalidates");
    ++m.size;need(call()&&hashes==5,"module size invalidates");
    writeFile(path,"a different longer cache fixture");need(rw::fileStamp(path,stamp),"new file stamp");digest=std::string(64,'0');
    need(!call()&&hashes==6&&selected.sha256==digest&&!selected.profileId,"unknown hash closed and retained");
    need(!call()&&hashes==6,"unknown SHA cached without rehash");
    ++c.steamBirth;digest=std::string(sr::kClientSha256).substr(0,8);need(!call()&&hashes==7&&!selected.profileId,"prefix hash rejected");
    ++c.steamBirth;digest=sr::profileSha256(sr::ProfileId::Public1792618285);std::transform(digest.begin(),digest.end(),digest.begin(),[](unsigned char x){return static_cast<char>(std::toupper(x));});
    need(call()&&hashes==8&&selected.profileId==sr::ProfileId::Public1792618285,"normalized full uppercase digest");
    ++c.steamBirth;rw::HashFile mutating=[&](const std::wstring&){++hashes;writeFile(path,"file changed during hash callback with a different size");return std::string(sr::kClientSha256);};
    need(!rw::verifiedDiskProfile(c,m,stamp,mutating,&selected)&&!selected.profileId&&selected.sha256.empty(),"hash file TOCTOU rejected");
    need(rw::fileStamp(path,stamp),"stamp after mutation");need(call()&&hashes==10,"failed hash validation not reused");
    ++c.steamBirth;rw::HashFile throwing=[](const std::wstring&)->std::string{throw std::runtime_error("secret");};
    need(!rw::verifiedDiskProfile(c,m,stamp,throwing,&selected)&&!selected.profileId,"hash exception fail closed");
    return {{"hashCallbackCalls",hashes},{"passed",true},{"scope","own temporary file; synthetic identity; no process opened"}};
}
int main(int argc,char** argv){
    try{
        need(argc==4,"usage: selftest legacy.dll public.dll own-cache-fixture-file");Disk legacy(argv[1]),pub(argv[2]);
        need(sr::Query{}.profile==sr::ProfileId::Legacy1791918634,"default legacy aggregate");
        for(const auto id:{sr::ProfileId::Legacy1791918634,sr::ProfileId::Public1792618285})need(sr::profileIdForSha256(sr::profileSha256(id))==id,"full SHA roundtrip");
        const auto bad=static_cast<sr::ProfileId>(99);need(!sr::profileSha256(bad)&&!sr::profileVersion(bad)&&!rw::profileLabel(bad),"invalid enum metadata no fallback");
        need(!sr::profileIdForSha256("")&&!sr::profileIdForSha256(std::string(64,'0'))&&!sr::profileIdForSha256(std::string(sr::kClientSha256)+"x"),"unknown SHA API closed");
        J out={{"readOnlyTarget",true},{"targetCalls",0},{"targetMemoryWrites",0},{"profiles",J::array({runProfile(legacy,sr::ProfileId::Legacy1791918634),runProfile(pub,sr::ProfileId::Public1792618285)})}};
        const std::string p=argv[3];out["cache"]=cacheTests(std::wstring(p.begin(),p.end()));out["assertions"]=checks;out["passed"]=true;
#if defined(YMCC_VENDOR_PROFILE)
        out["headerVariant"]="engine/vendor";
#else
        out["headerVariant"]="native/root";
#endif
        std::cout<<out.dump(2)<<'\n';return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}

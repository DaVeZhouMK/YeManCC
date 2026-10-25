#include "../native/steam_compatibility_read_contracts.h"
#include "json.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
namespace sc=ymcc::steamcompat;
namespace rd=sc::read_contract_detail;
using U64=std::uint64_t;
using J=nlohmann::json;
unsigned assertions=0,cases=0;
void need(bool ok,const std::string& why){++assertions;if(!ok)throw std::runtime_error(why);}
struct Disk {
    std::vector<unsigned char> raw;
    IMAGE_NT_HEADERS64 nt{};
    std::vector<IMAGE_SECTION_HEADER> sections;
    explicit Disk(const char* path){
        std::ifstream f(path,std::ios::binary);need(f.good(),"open fixed PE");raw.assign(std::istreambuf_iterator<char>(f),{});
        IMAGE_DOS_HEADER dos{};need(raw.size()>=sizeof dos,"DOS bounds");std::memcpy(&dos,raw.data(),sizeof dos);
        need(dos.e_magic==IMAGE_DOS_SIGNATURE&&dos.e_lfanew>0,"DOS signature");
        const auto off=static_cast<std::size_t>(dos.e_lfanew);need(off+sizeof nt<=raw.size(),"NT bounds");std::memcpy(&nt,raw.data()+off,sizeof nt);
        need(nt.Signature==IMAGE_NT_SIGNATURE&&nt.OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC,"PE64 signature");
        const auto first=off+sizeof(DWORD)+sizeof(IMAGE_FILE_HEADER)+nt.FileHeader.SizeOfOptionalHeader;
        need(first+nt.FileHeader.NumberOfSections*sizeof(IMAGE_SECTION_HEADER)<=raw.size(),"section bounds");
        sections.resize(nt.FileHeader.NumberOfSections);std::memcpy(sections.data(),raw.data()+first,sections.size()*sizeof(IMAGE_SECTION_HEADER));
    }
    bool read(U64 a,void* out,std::size_t n)const{
        if(a<nt.OptionalHeader.SizeOfHeaders&&n<=nt.OptionalHeader.SizeOfHeaders-a&&a+n<=raw.size()){
            std::memcpy(out,raw.data()+a,n);return true;
        }
        for(const auto& s:sections)if(a>=s.VirtualAddress&&a-s.VirtualAddress<=s.SizeOfRawData&&n<=s.SizeOfRawData-(a-s.VirtualAddress)){
            const auto off=s.PointerToRawData+a-s.VirtualAddress;if(off+n>raw.size())return false;
            std::memcpy(out,raw.data()+off,n);return true;
        }
        return false;
    }
    unsigned char byte(U64 a)const{unsigned char b=0;need(read(a,&b,1),"read mutation fixture byte");return b;}
};
enum class Kind{Record,Ui};
std::span<const sc::Range> ranges(Kind kind,sc::Layout layout){
    if(kind==Kind::Record)return layout==sc::Layout::Legacy?std::span<const sc::Range>(rd::kRecordLegacyRanges):std::span<const sc::Range>(rd::kRecordPublicRanges);
    return layout==sc::Layout::Legacy?std::span<const sc::Range>(rd::kUiLegacyRanges):std::span<const sc::Range>(rd::kUiPublicRanges);
}
std::span<const rd::PointerOrigin> pointers(Kind kind,sc::Layout layout){
    if(kind==Kind::Record)return layout==sc::Layout::Legacy?std::span<const rd::PointerOrigin>(rd::kRecordLegacyPointers):std::span<const rd::PointerOrigin>(rd::kRecordPublicPointers);
    return layout==sc::Layout::Legacy?std::span<const rd::PointerOrigin>(rd::kUiLegacyPointers):std::span<const rd::PointerOrigin>(rd::kUiPublicPointers);
}
struct Fixture {
    const Disk& disk;Kind kind;sc::Layout layout;U64 base=0,size=0,readCalls=0,readBytes=0,maxRead=0;
    bool fail=false,throws=false;
    std::vector<std::pair<U64,unsigned char>> overlays;
    explicit Fixture(const Disk& d,Kind k,sc::Layout l):disk(d),kind(k),layout(l),base(d.nt.OptionalHeader.ImageBase),size(d.nt.OptionalHeader.SizeOfImage){}
    void flip(U64 a){overlays.emplace_back(a,static_cast<unsigned char>(disk.byte(a)^1));}
    sc::Evidence run(){
        const sc::Image image{[&](U64 a,void* out,std::size_t n){
            ++readCalls;readBytes+=n;maxRead=(std::max)(maxRead,static_cast<U64>(n));
            if(throws)throw std::runtime_error("private callback exception");
            if(fail||!disk.read(a,out,n))return false;
            for(const auto& [at,b]:overlays)if(at>=a&&at-a<n)static_cast<unsigned char*>(out)[at-a]=b;
            return true;
        },base,size};
        return kind==Kind::Record?sc::checkRecordLayout(image,layout):sc::checkUiLayout(image,layout);
    }
};
void rejected(const sc::Evidence& e,const char* reason){++cases;need(!e.matched,"negative must reject");need(std::string(e.reason)==reason,"negative exact reason");}
void accepted(Fixture& f){++cases;const auto e=f.run();need(e.matched,"positive layout contract");
    need(e.rangesChecked==ranges(f.kind,f.layout).size(),"all descriptor ranges read");
    U64 total=0;for(const auto& z:ranges(f.kind,f.layout))total+=z.size;
    need(e.bytesChecked==total&&f.readBytes==total,"entire code/data byte count");need(f.maxRead<=512,"bounded chunk reads");
}
J test(const Disk& disk,Kind kind,sc::Layout layout,U64 versionRva,U64 timestampRva,U64 unrelatedRva){
    const unsigned began=cases;const auto rs=ranges(kind,layout);
    {Fixture f(disk,kind,layout);accepted(f);}
    {Fixture f(disk,kind,layout);f.layout=layout==sc::Layout::Legacy?sc::Layout::Public:sc::Layout::Legacy;rejected(f.run(),"contract-bytes-incompatible");}
    unsigned mutationCases=0;
    // Exercise every descriptor start/interior/end: this catches long function
    // tails and data/RTTI/slot/path changes even when an entry prologue matches.
    for(const auto& z:rs)for(const auto pos:{std::size_t(0),z.size/2,z.size-1}){
        Fixture f(disk,kind,layout);f.flip(z.rva+pos);rejected(f.run(),"contract-bytes-incompatible");++mutationCases;
    }
    for(const auto a:{versionRva,timestampRva,unrelatedRva}){
        need(std::none_of(rs.begin(),rs.end(),[&](const auto& z){return a>=z.rva&&a-z.rva<z.size;}),"independent noncontract mutation RVA");
        Fixture f(disk,kind,layout);f.flip(a);accepted(f);
    }
    {Fixture f(disk,kind,layout);f.flip(versionRva);f.flip(timestampRva);f.flip(unrelatedRva);accepted(f);}
    // Same structure with a different preferred base: expected absolute slots
    // must follow the base while all RVA/code/relative RTTI bytes remain fixed.
    {Fixture f(disk,kind,layout);f.base+=0x1000000;
        for(const auto& p:pointers(kind,layout)){
            const auto a=rs[p.rangeIndex].rva,value=f.base+p.targetRva;
            for(unsigned i=0;i<8;++i)f.overlays.emplace_back(a+i,static_cast<unsigned char>(value>>(8*i)));
        }
        accepted(f);
    }
    {Fixture f(disk,kind,layout);f.base+=0x1000000;rejected(f.run(),"contract-bytes-incompatible");}
    {Fixture f(disk,kind,layout);f.size+=0x1000;accepted(f);}
    {Fixture f(disk,kind,layout);f.layout=static_cast<sc::Layout>(-1);rejected(f.run(),"contract-layout-invalid");need(f.readCalls==0,"invalid enum zero reads");}
    {Fixture f(disk,kind,layout);f.size=0x1000;rejected(f.run(),"contract-pointer-origin-invalid");need(f.readCalls==0,"small image zero reads");}
    {Fixture f(disk,kind,layout);f.base=(std::numeric_limits<U64>::max)();rejected(f.run(),"contract-pointer-origin-invalid");need(f.readCalls==0,"overflow base zero reads");}
    {Fixture f(disk,kind,layout);f.base=0;rejected(f.run(),"contract-image-or-range-invalid");need(f.readCalls==0,"missing preferred base zero reads");}
    {Fixture f(disk,kind,layout);f.fail=true;rejected(f.run(),"contract-read-unavailable");}
    {Fixture f(disk,kind,layout);f.throws=true;rejected(f.run(),"contract-read-exception");}
    // Independent field/path offsets taken from the qualified function bodies.
    std::vector<U64> semantic;
    if(kind==Kind::Record){
        const bool old=layout==sc::Layout::Legacy;
        semantic={0xbedfc+3,0xcfdc0+3,(old?0x9859d0ULL:0x985ab0ULL)+0x72+3,
            (old?0x9d7a30ULL:0x9d7b10ULL)+0x60+2,(old?0x9d7a30ULL:0x9d7b10ULL)+0xa7+2,
            (old?0x9d4b40ULL:0x9d4c20ULL)+0xa+3,(old?0x9d4b40ULL:0x9d4c20ULL)+0x30+3};
    }else{
        const bool old=layout==sc::Layout::Legacy;
        semantic={0x1813ad+3,(old?0x61b110ULL:0x61b230ULL)+0x5d+3,0x17a16d+4,0xbde90+0x4f+2,0xec3c0+2,
            (old?0x627480ULL:0x6275a0ULL)+0x3a+3};
    }
    for(const auto a:semantic){Fixture f(disk,kind,layout);f.flip(a);rejected(f.run(),"contract-bytes-incompatible");}
    return {{"kind",kind==Kind::Record?"record":"ui"},{"layout",layout==sc::Layout::Legacy?"legacy":"public"},{"passed",true},
        {"cases",cases-began},{"descriptorRanges",rs.size()},{"rangeMutationCases",mutationCases},
        {"unknownVersionMetadataAccepted",true},{"unrelatedCodeMutationAccepted",true},{"relocatedPreferredBaseAccepted",true},
        {"keyFieldAndPathMutationRejected",true},{"versionMetadataRva",versionRva},{"unrelatedCodeRva",unrelatedRva}};
}
int main(int argc,char** argv){
    try{
        need(argc==5,"usage: selftest legacy-client public-client legacy-ui public-ui");
        Disk client0(argv[1]),client1(argv[2]),ui0(argv[3]),ui1(argv[4]);
        auto out=J{{"passed",true},{"targetCalls",0},{"targetWrites",0},{"realProcessAccess",false},{"imageHashUsedForAdmission",false},{"versionUsedForAdmission",false},
            {"profiles",J::array({test(client0,Kind::Record,sc::Layout::Legacy,0x1966124,0x198,0x1000),
                test(client1,Kind::Record,sc::Layout::Public,0x1967124,0x198,0x1000),
                test(ui0,Kind::Ui,sc::Layout::Legacy,0x1291618,0x1b0,0x1000),
                test(ui1,Kind::Ui,sc::Layout::Public,0x1291618,0x1b0,0x1000)})}};
        out["assertions"]=assertions;out["cases"]=cases;std::cout<<out.dump(2)<<'\n';return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}

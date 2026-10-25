// Real file/adapter regression. Context/module fields are disk-cache fixture keys;
// this program never observes or opens Steam/game process state.
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <array>
#ifdef YMCC_TEST_VENDOR_ADAPTER
#include "../native/steam_original_recovery_engine/vendor/steam_native_app_state_win32.h"
#else
#include "../native/steam_native_app_state_win32.h"
#endif
#include "../native/steam_original_recovery_file_hash.h"
namespace c=ymcc::steamcompat;
namespace r=ymcc::steamrecordwin;
namespace u=ymcc::steamappstatewin;
namespace fs=std::filesystem;
using J=nlohmann::json;
static J cases=J::array();
static void check(const std::string& name,bool ok){cases.push_back({{"name",name},{"passed",ok}});if(!ok)throw std::runtime_error(name);}
static std::vector<unsigned char> bytes(const fs::path& file){std::ifstream f(file,std::ios::binary);if(!f)throw std::runtime_error("fixture-input");return {std::istreambuf_iterator<char>(f),{}};}
static void write(const fs::path& file,const std::vector<unsigned char>& data){
    {std::ofstream f(file,std::ios::binary|std::ios::trunc);f.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));if(!f)throw std::runtime_error("fixture-write");}
    r::Handle h(CreateFileW(file.c_str(),FILE_WRITE_ATTRIBUTES,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr));
    FILETIME stamp{};GetSystemTimeAsFileTime(&stamp);if(!h.valid()||!SetFileTime(h.h,nullptr,nullptr,&stamp))throw std::runtime_error("fixture-stamp");
}
static std::size_t rawAt(const std::vector<unsigned char>& b,std::uint64_t rva){
    IMAGE_DOS_HEADER dos{};std::memcpy(&dos,b.data(),sizeof dos);const auto nt=static_cast<std::size_t>(dos.e_lfanew);
    IMAGE_NT_HEADERS64 header{};if(nt>b.size()||sizeof header>b.size()-nt)throw std::runtime_error("fixture-nt");std::memcpy(&header,b.data()+nt,sizeof header);
    const auto start=nt+sizeof(DWORD)+sizeof(IMAGE_FILE_HEADER)+header.FileHeader.SizeOfOptionalHeader;
    for(unsigned i=0;i<header.FileHeader.NumberOfSections;++i){IMAGE_SECTION_HEADER section{};const auto at=start+i*sizeof section;
        if(at>b.size()||sizeof section>b.size()-at)throw std::runtime_error("fixture-section");std::memcpy(&section,b.data()+at,sizeof section);
        if(rva>=section.VirtualAddress&&rva-section.VirtualAddress<section.SizeOfRawData){const auto raw=section.PointerToRawData+(rva-section.VirtualAddress);if(raw>=b.size())throw std::runtime_error("fixture-raw");return static_cast<std::size_t>(raw);}}
    throw std::runtime_error("fixture-rva");
}
static void sample(const fs::path& input,const fs::path& output,c::ModuleKind kind,unsigned index){
    auto data=bytes(input);const auto original=c::detail::qualifyBytes(data,kind);if(!original.layout||!original.metadata)throw std::runtime_error("original-contract");
    const auto fixture=output/("sample-"+std::to_string(index)+".pe.bin");write(fixture,data);
    const auto label=std::to_string(index);unsigned callbacks=0;
    const r::HashFile forbidden=[&](const std::wstring&)->std::string{++callbacks;throw std::runtime_error("legacy-external-hash-must-not-run");};
    r::Context context;context.steamPid=171;context.steamBirth=177; // synthetic disk-cache key only
    r::Module module{original.metadata->preferredBase,original.metadata->imageSize,fixture.wstring()};
    auto verify=[&](bool expected,const std::string& exactDigest){
        r::FileStamp stamp{};if(!r::fileStamp(fixture.wstring(),stamp))throw std::runtime_error("fixture-stamp-read");
        std::string digest;bool accepted=false;
        if(kind==c::ModuleKind::Client){r::DiskProfile profile;accepted=r::verifiedDiskProfile(context,module,stamp,forbidden,&profile);digest=profile.sha256;}
        else{auto profile=ymcc::steamappstate::ProfileId::Legacy1791918634;c::Qualification proof;accepted=u::verifiedDiskProfile(context,module,stamp,forbidden,&profile,&digest,&proof);}
        check(label+"/acceptance-"+std::to_string(cases.size()),accepted==expected);
        check(label+"/actual-digest-"+std::to_string(cases.size()),digest==exactDigest);
        check(label+"/no-independent-hash-"+std::to_string(cases.size()),callbacks==0);
    };
    const auto firstDigest=ymcc::steamOriginalHash::hashFileFully(fixture.wstring()).sha256;
    verify(true,firstDigest);verify(true,firstDigest);
    IMAGE_DOS_HEADER dos{};std::memcpy(&dos,data.data(),sizeof dos);data[static_cast<std::size_t>(dos.e_lfanew)+8]^=0x40;
    write(fixture,data);const auto newDigest=ymcc::steamOriginalHash::hashFileFully(fixture.wstring()).sha256;
    check(label+"/unrelated-update-actually-changes-digest",newDigest!=firstDigest);verify(true,newDigest);
    const auto graph=kind==c::ModuleKind::Client?c::recordGraph(*original.layout):c::uiGraph(*original.layout);
    const c::GraphRange* code=nullptr;for(const auto& range:graph.ranges)if(range.kind==c::RangeKind::Code&&(!code||range.size>code->size))code=&range;
    if(!code)throw std::runtime_error("fixture-required-code");data[rawAt(data,code->refRva)]^=1;
    write(fixture,data);const auto badDigest=ymcc::steamOriginalHash::hashFileFully(fixture.wstring()).sha256;verify(false,badDigest);
    {r::Handle exclusive(CreateFileW(fixture.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr));check(label+"/adapter-cache-retains-no-file-handle",exclusive.valid());}
    check(label+"/fixture-deletes-while-cache-alive",fs::remove(fixture));
}
int wmain(int argc,wchar_t** argv){if(argc!=6)return 64;
    bool passed=true;std::string reason;
    try{const fs::path out(argv[1]);fs::create_directories(out);for(unsigned i=0;i<4;++i)sample(argv[i+2],out,i<2?c::ModuleKind::Client:c::ModuleKind::Ui,i);}
    catch(const std::exception& e){passed=false;reason=e.what();}
    const J result={{"schema","ymcc.steam-qualified-digest-adapter.v1"},{"passed",passed},{"checks",cases.size()},{"cases",cases},{"reason",reason},{"targetProcessAccess",0},{"memoryWrites",0},{"diskFixturesOnly",true},
#ifdef YMCC_TEST_VENDOR_ADAPTER
    {"adapter","vendor"}
#else
    {"adapter","root"}
#endif
    };std::cout<<result.dump(2)<<'\n';return passed?0:2;
}

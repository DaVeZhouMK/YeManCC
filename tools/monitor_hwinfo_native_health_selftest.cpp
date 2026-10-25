#include <fstream>
#include <iostream>
#include "../native/monitor_hwinfo_manager.h"
using namespace ymcc::hwinfo;
static void put(std::vector<unsigned char>&b,std::size_t offset,std::uint64_t v,std::size_t count=4){if(offset+count<=b.size())std::memcpy(b.data()+offset,&v,count);}
int main(int argc,char**argv){if(argc!=4)return 2;Json cases;std::ifstream(argv[1])>>cases;Json results=Json::array(),checks=Json::array();bool ok=true;const auto check=[&](const char*n,bool passed){checks.push_back({{"name",n},{"ok",passed}});ok=ok&&passed;if(!passed)std::cerr<<n<<'\n';};
 for(const auto&c:cases){std::vector<unsigned char> b(c["capacity"].get<std::size_t>(),0);put(b,0,0x53695748);put(b,4,c["version"]);put(b,8,1);put(b,12,1700000000ULL,8);for(const auto&pair:std::vector<std::pair<const char*,std::size_t>>{{"sensorOffset",20},{"sensorSize",24},{"sensorCount",28},{"readingOffset",32},{"readingSize",36},{"readingCount",40}})put(b,pair.second,c[pair.first]);if(c["sensorOffset"].get<unsigned>()>=48)put(b,44,3000);
 const auto proof=proofFromView(b.data(),b.size(),0,0,1700000000000ULL);const bool passed=bool(proof)==c["expected"].get<bool>();ok=ok&&passed;results.push_back({{"name",c["name"]},{"accepted",bool(proof)},{"expected",c["expected"]},{"ok",passed}});
 }
 std::vector<unsigned char>b(4096,0);put(b,0,0x53695748);put(b,4,2);put(b,8,1);put(b,12,1700000000ULL,8);put(b,20,48);put(b,24,392);put(b,28,1);put(b,32,440);put(b,36,460);put(b,40,1);put(b,44,3000);
 const auto proof=[&](std::uint64_t now,unsigned period=0){return proofFromView(b.data(),b.size(),0,period,now);};
 check("healthy hardware is valid immediately before original age boundary",bool(proof(1700000005999ULL)));
 check("hardware is invalid exactly at existing health age boundary",!proof(1700000006000ULL));
 check("producer time more than one second in the future is rejected",!proof(1699999998999ULL));
 check("small bounded clock skew retains existing recovery compatibility",bool(proof(1699999999000ULL)));
 check("explicit matching period is proved",bool(proof(1700000000000ULL,3000)));
 check("explicit different period is rejected",!proof(1700000000000ULL,1500));
 put(b,4,3);check("unknown layout can retain basic health",bool(proof(1700000000000ULL)));
 check("unknown layout cannot prove an explicit period",!proof(1700000000000ULL,3000));put(b,4,2);
 put(b,12,0,8);check("zero producer time is never healthy",!proof(1700000000000ULL));put(b,12,UINT64_MAX,8);check("overflowing producer time is never healthy",!proof(1700000000000ULL));put(b,12,1700000000ULL,8);
 put(b,0,0);check("invalid signature is rejected",!proof(1700000000000ULL));put(b,0,0x53695748);
 check("short and absent mappings are rejected before reads",!proofFromView(b.data(),43,0,0,1700000000000ULL)&&!proofFromView(nullptr,4096,0,0,1700000000000ULL));
 const fs::path scratch=argv[3];fs::create_directories(scratch);const auto target=(scratch/L"atomic-config.ini").wstring(),original=(scratch/L"original.ini").wstring();
 DeleteFileW(target.c_str());DeleteFileW(original.c_str());writeAtomic(target,"before");check("atomic new config is verified byte-for-byte",*readBytes(target,32)=="before");writeAtomic(original,"first",true);writeAtomic(original,"second",true);check("original backup is never overwritten",*readBytes(original,32)=="first");writeAtomic(target,"after");check("atomic replacement commits verified bytes",*readBytes(target,32)=="after");
 {Handle hold(CreateFileW(target.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));bool refused=false;try{writeAtomic(target,"should-not-commit");}catch(const Error&){refused=true;}check("locked destination refuses commit and preserves original bytes",refused&&*readBytes(target,32)=="after");}
 bool tempFound=false;for(const auto&e:fs::directory_iterator(scratch))if(e.path().filename().wstring().find(L".ymcc-tmp-")!=std::wstring::npos)tempFound=true;check("successful and failed atomic writes leave no staged files",!tempFound);
 check("required Presented group cannot enter optional sensor policy",!sensorGroupAllowed(L"F0000FF0_0"));
 std::ofstream(argv[2])<<Json({{"ok",ok},{"headerCases",results},{"extraChecks",checks},{"scope","Actual native health view parser on bounded synthetic bytes plus real atomic file operations in isolated workspace. No live HWiNFO, process or registry changes."}}).dump(2);return ok?0:1;
}

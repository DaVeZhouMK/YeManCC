extern "C" void steamColdFilePeer(const wchar_t*,bool,compat::Qualification*);
static int coldSingleFlightOnly(const wchar_t* destination,const wchar_t* clientPath,bool replacementSuffix=false){
 output=fs::absolute(destination);fs::create_directories(output);
 const auto original=readFile(clientPath);const auto expectedDigest=digest(original);
 const auto peer=[&](const fs::path& path,bool hash=true){compat::Qualification value;steamColdFilePeer(path.c_str(),hash,&value);return value;};
 const auto finish=[&]{
  const auto failed=std::count_if(cases.begin(),cases.end(),[](const auto& item){return !item.pass;});
  std::cout<<"{\"mode\":\"cold-single-flight-only\",\"checks\":"<<cases.size()<<",\"failed\":"<<failed<<",\"cases\":[";
  bool comma=false;for(const auto& item:cases){if(comma)std::cout<<',';comma=true;std::cout<<"{\"name\":"<<quoted(item.name)<<",\"passed\":"<<(item.pass?"true":"false")<<'}';}
  std::cout<<"],\"realSteamAccess\":false,\"healthReferenceProduced\":false,\"fullDefaultSelftestExecuted\":false}\n";return failed?1:0;
 };
 if(!replacementSuffix){
 // Two real TUs enter the same original qualifier while leader ReadFile is held.
 std::cerr<<"COLD_STAGE same-key"<<std::endl;
 const auto same=output/L"same.bin";writeFile(same,original);cold_file_ports::select(same.c_str(),true);
 auto leader=std::async(std::launch::async,[&]{return compat::qualifyFile(same.wstring(),compat::ModuleKind::Client);});
 const bool entered=cold_file_ports::waitFor(1);
 auto follower=std::async(std::launch::async,[&]{return peer(same);});
 (void)cold_file_ports::waitFor(2,250);const auto atGate=cold_file_ports::count();cold_file_ports::release();
 const auto first=leader.get(),second=follower.get();cold_file_ports::stop();
 const auto readBytes=first.fileBytesRead+second.fileBytesRead,hashed=first.digestBytesHashed+second.digestBytesHashed;
 const bool once=entered&&atGate==1&&first.layout&&second.layout&&first.identity==second.identity&&first.sha256==expectedDigest&&second.sha256==expectedDigest&&readBytes==original.size()&&hashed==original.size()&&(first.cacheHit!=second.cacheHit);
 std::cout<<"COLD_FLIGHT_SOURCE_OBSERVATION "<<quoted("same-key")<<" gate="<<atGate<<" read="<<readBytes<<" hash="<<hashed<<std::endl;
 check("cold-flight/same-key-one-buffer",once,"same-key two actual TUs must read and hash one original buffer");
 if(!once)return finish();

 // An independent path has a separate flight and completes before leader release.
 std::cerr<<"COLD_STAGE independent"<<std::endl;
 const auto a=output/L"independent-a.bin",b=output/L"independent-b.bin";writeFile(a,original);writeFile(b,original);cold_file_ports::select(a.c_str(),true);
 auto blocked=std::async(std::launch::async,[&]{return compat::qualifyFile(a.wstring(),compat::ModuleKind::Client);});
 const bool independentEntered=cold_file_ports::waitFor(1);auto independent=std::async(std::launch::async,[&]{return peer(b);});
 const bool noGlobalWait=independent.wait_for(std::chrono::seconds(3))==std::future_status::ready;
 cold_file_ports::release();const auto separate=independent.get(),released=blocked.get();cold_file_ports::stop();
 check("cold-flight/independent-file-not-serialized",independentEntered&&noGlobalWait&&separate.layout&&released.layout&&separate.fileBytesRead==original.size(),"different file completes while first file ReadFile remains blocked");

 // Existing digest-free then digest-required upgrade still reads its own one buffer.
 std::cerr<<"COLD_STAGE upgrade"<<std::endl;
 const auto upgrade=output/L"upgrade.bin";writeFile(upgrade,original);
 const auto noDigest=compat::qualifyFile(upgrade.wstring(),compat::ModuleKind::Client,false);
 const auto withDigest=peer(upgrade);const auto warmed=compat::qualifyFile(upgrade.wstring(),compat::ModuleKind::Client,false);
 check("cold-flight/digest-upgrade-preserved",noDigest.layout&&noDigest.sha256.empty()&&noDigest.digestBytesHashed==0&&withDigest.layout&&withDigest.sha256==expectedDigest&&withDigest.fileBytesRead==original.size()&&withDigest.digestBytesHashed==original.size()&&warmed.cacheHit&&warmed.fileBytesRead==0,"false then true remains one original upgrade, later false is cache hit");

 }
 // New identity at the same path must not wait on or inherit the held old file.
 std::cerr<<"COLD_STAGE replacement"<<std::endl;
 const auto replace=output/L"replace.bin",next=output/L"replace-next.bin";writeFile(replace,original);
 auto bytes=original;Header header(bytes);const auto stamp=header.file+offsetof(IMAGE_FILE_HEADER,TimeDateStamp);put<DWORD>(bytes,stamp,at<DWORD>(bytes,stamp)^0x01010101);writeFile(next,bytes);
 cold_file_ports::select(replace.c_str(),true);auto old=std::async(std::launch::async,[&]{return compat::qualifyFile(replace.wstring(),compat::ModuleKind::Client);});
 const bool oldEntered=cold_file_ports::waitFor(1);
 const auto retired=output/L"replace-retired.bin";
 const bool renamed=::MoveFileExW(replace.c_str(),retired.c_str(),MOVEFILE_WRITE_THROUGH)!=FALSE;
 const DWORD renameError=renamed?ERROR_SUCCESS:GetLastError();
 const bool installed=renamed&&::MoveFileExW(next.c_str(),replace.c_str(),MOVEFILE_WRITE_THROUGH)!=FALSE;
 const DWORD installError=installed?ERROR_SUCCESS:GetLastError();
 if(!installed){cold_file_ports::release();(void)old.get();cold_file_ports::stop();std::cerr<<"REPLACEMENT_SETUP rename="<<renamed<<" renameError="<<renameError<<" installError="<<installError<<std::endl;throw std::runtime_error("owned held-file path replacement setup");}
 auto newer=std::async(std::launch::async,[&]{return peer(replace);});const bool replacementIndependent=newer.wait_for(std::chrono::seconds(3))==std::future_status::ready;
 cold_file_ports::release();const auto newValue=newer.get(),oldValue=old.get();cold_file_ports::stop();
 const auto current=peer(replace);
 check("cold-flight/replacement-identity-independent",oldEntered&&replacementIndependent&&oldValue.layout&&newValue.layout&&!(oldValue.identity==newValue.identity)&&oldValue.sha256==expectedDigest&&newValue.sha256==digest(bytes)&&current.sha256==newValue.sha256&&current.identity==newValue.identity,"held old facts never qualify replacement; current path gets new identity");

 // Both ordinary read failure and a throwing transmission unwind the RAII flight.
 for(unsigned failure=1;failure<=2;++failure){
  std::cerr<<"COLD_STAGE failure "<<failure<<std::endl;
  const auto path=output/(failure==1?L"read-failure.bin":L"throw-failure.bin");writeFile(path,original);cold_file_ports::select(path.c_str(),false,failure);
  compat::Qualification failed;bool threw=false;try{failed=compat::qualifyFile(path.wstring(),compat::ModuleKind::Client);}catch(const std::exception&){threw=true;}
  cold_file_ports::stop();auto retry=std::async(std::launch::async,[&]{return peer(path);});const bool completed=retry.wait_for(std::chrono::seconds(3))==std::future_status::ready;const auto recovered=retry.get();
  check(failure==1?"cold-flight/read-failure-releases-flight":"cold-flight/exception-releases-flight",completed&&recovered.layout&&recovered.sha256==expectedDigest&&recovered.fileBytesRead==original.size()&&(failure==1?!threw&&!failed.layout&&failed.win32Error==ERROR_READ_FAULT:threw),"next original caller can qualify after failed transmission without stale flight");
 }
 return finish();
}

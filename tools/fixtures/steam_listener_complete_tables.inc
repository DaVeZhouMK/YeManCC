// Extend the original consumer with controlled OS leaves; execute real listeners/stillOwned.
#include <fstream>
namespace listenerTopologyFixture {
static int endpointTableCompleteness(const char* output,bool control) {
 const auto started=std::chrono::steady_clock::now();
 const HANDLE resultHandle=::CreateFileA(output,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(resultHandle==INVALID_HANDLE_VALUE){const auto error=GetLastError();if(error==ERROR_FILE_EXISTS||error==ERROR_ALREADY_EXISTS)return 2;throw std::runtime_error("fixture output create failed");}
 struct ResultOwner {HANDLE h;~ResultOwner(){if(h!=INVALID_HANDLE_VALUE)::CloseHandle(h);}} resultOwner{resultHandle};
 enum class Fault { none,probeDenied,probeZero,probeSmall,probeOversize,probeSuccess,dataDenied,growth,shortZero,shortCount,overflow,overCapacity,partialNext,padding,zeroRows };
 struct Family {Fault fault=Fault::none;unsigned calls=0;};
 Family f4,f6;std::vector<MIB_TCPROW_OWNER_PID> v4;std::vector<MIB_TCP6ROW_OWNER_PID> v6;
 std::map<HANDLE,DWORD> held;std::vector<DWORD> imageOrder;std::vector<std::string> events;
 unsigned opens=0,closes=0,unexpected=0,checks=0;bool ok=true;Json cases=Json::array();
 const std::wstring root=L"P:\\YMCC-identity-fixture";
 auto need=[&](bool value,const char* label){++checks;if(!value){ok=false;std::cerr<<"FAIL "<<label<<'\n';}};
 const auto port=[](unsigned short p){return static_cast<DWORD>(((p&255)<<8)|(p>>8));};
 auto ipv4=[&](DWORD pid,unsigned short p,bool exposed=false){MIB_TCPROW_OWNER_PID r{};r.dwOwningPid=pid;r.dwLocalAddr=exposed?0:0x0100007f;r.dwLocalPort=port(p);v4.push_back(r);};
 auto ipv6=[&](DWORD pid,unsigned short p,bool exposed=false){MIB_TCP6ROW_OWNER_PID r{};r.dwOwningPid=pid;r.ucLocalAddr[15]=exposed?0:1;r.dwLocalPort=port(p);v6.push_back(r);};
 auto healthy=[&]{f4={};f6={};v4.clear();v6.clear();ipv4(8001,8080);ipv6(8002,8081);};
 listenerEndpointOpen=[&](DWORD access,BOOL inherit,DWORD pid)->HANDLE{
  ++opens;events.push_back("open:"+std::to_string(pid));
  if(access!=PROCESS_QUERY_LIMITED_INFORMATION||inherit||(pid!=8001&&pid!=8002&&pid!=4000)){++unexpected;return nullptr;}
  auto h=reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(0xE10000+opens));held.emplace(h,pid);return h;
 };
 listenerEndpointImage=[&](HANDLE h,DWORD flags,LPWSTR out,PDWORD length)->BOOL{
  if(!held.count(h)||flags||!length){++unexpected;return FALSE;}
  const auto pid=held.at(h);imageOrder.push_back(pid);events.push_back("image:"+std::to_string(pid));
  const auto image=root+(pid==4000?L"\\steam.exe":L"\\bin\\steamwebhelper.exe");
  if(*length<image.size()){++unexpected;return FALSE;}std::copy(image.begin(),image.end(),out);*length=static_cast<DWORD>(image.size());return TRUE;
 };
 listenerEndpointClose=[&](HANDLE h)->BOOL{if(held.erase(h)!=1){++unexpected;return FALSE;}++closes;return TRUE;};
 listenerEndpointQuery=[](SYSTEM_INFORMATION_CLASS kind,PVOID data,ULONG bytes,PULONG returned){return query(kind,data,bytes,returned);};
 listenerEndpointTcp=[&](PVOID out,PDWORD size,BOOL order,ULONG family,TCP_TABLE_CLASS kind,ULONG reserved)->DWORD{
  if(!size||order||kind!=TCP_TABLE_OWNER_PID_LISTENER||reserved||(family!=2&&family!=23)){++unexpected;return ERROR_INVALID_PARAMETER;}
  auto& f=family==2?f4:f6;++f.calls;events.push_back(std::string(family==2?"v4:":"v6:")+(out?"data":"probe"));
  const auto copy=[&](const auto& rows,auto* typed)->DWORD{
   using Table=std::remove_pointer_t<decltype(typed)>;using TableRow=typename std::decay_t<decltype(rows)>::value_type;
   const DWORD header=static_cast<DWORD>(offsetof(Table,table));const DWORD rowSize=sizeof(TableRow);
   const DWORD base=header+static_cast<DWORD>(rows.size())*rowSize;
   const DWORD required=base+((f.fault==Fault::padding||f.fault==Fault::zeroRows)?rowSize:0);
   if(!out){
    *size=required;
    switch(f.fault){case Fault::probeDenied:return ERROR_ACCESS_DENIED;case Fault::probeZero:*size=0;break;case Fault::probeSmall:*size=header-1;break;case Fault::probeOversize:*size=16*1024*1024+1;break;case Fault::probeSuccess:return NO_ERROR;default:break;}
    return ERROR_INSUFFICIENT_BUFFER;
   }
   const DWORD capacity=*size;
   if(f.fault==Fault::dataDenied)return ERROR_ACCESS_DENIED;
   if(f.fault==Fault::growth){*size=capacity+rowSize;return ERROR_INSUFFICIENT_BUFFER;}
   if(capacity<required){++unexpected;return ERROR_INVALID_PARAMETER;}
   std::memset(out,0,capacity);DWORD count=static_cast<DWORD>(rows.size());
   if(!rows.empty())std::memcpy(static_cast<unsigned char*>(out)+header,rows.data(),rows.size()*rowSize);
   *size=required;
   if(f.fault==Fault::shortZero){count=0;*size=header-1;}
   if(f.fault==Fault::shortCount)*size=header-1;
   if(f.fault==Fault::overflow)count=(std::numeric_limits<DWORD>::max)();
   if(f.fault==Fault::overCapacity)*size=capacity+1;
   if(f.fault==Fault::partialNext)*size=base-1;
   if(f.fault==Fault::zeroRows)count=0;
   std::memcpy(out,&count,sizeof(count));return NO_ERROR;
  };
  if(family==2)return copy(v4,static_cast<MIB_TCPTABLE_OWNER_PID*>(nullptr));
  return copy(v6,static_cast<MIB_TCP6TABLE_OWNER_PID*>(nullptr));
 };
 struct PortsGuard {~PortsGuard(){listenerEndpointProbe=false;listenerEndpointOpen={};listenerEndpointImage={};listenerEndpointClose={};listenerEndpointQuery={};listenerEndpointTcp={};ymcc::processInventory::invalidate();}} guard;
 listenerEndpointProbe=true;
 const std::vector<Endpoint> both{{8001,8080,L"127.0.0.1"},{8002,8081,L"::1"}};
 auto test=[&](const std::string& name,const std::vector<Endpoint>& want,const char* reason,bool incomplete=false,bool ownership=false){
  need(held.empty(),"previous image handles returned");opens=closes=unexpected=0;imageOrder.clear();events.clear();f4.calls=f6.calls=0;reset();
  inputs={{4000,1,1,L"steam.exe"},{8001,4000,2,L"steamwebhelper.exe"},{8002,4000,3,L"steamwebhelper.exe"}};
  const auto firstCheck=checks;const bool prior=ok;std::vector<Endpoint> got;std::string error;bool owned=false;
  try{if(ownership)owned=stillOwned(root,both[0],4000);else got=listeners(root,4000);}catch(const std::runtime_error& e){error=e.what();}
  need(error==(reason?reason:"")&&(ownership?!owned:got==want),"real consumer result and reason");
  need(queryCalls==1&&queryClassValid&&listenerToolhelpCalls==0,"one original Basic topology query, no fallback");
  need(f4.calls<=2&&f6.calls<=2&&f4.calls+f6.calls<=4,"max four TCP calls, no retry");
  if(!incomplete)need(f4.calls==2&&f6.calls==2,"complete dual family uses exactly four TCP calls");
  if(incomplete)need(opens==0&&imageOrder.empty(),"incomplete tables reject before image authorization");
  need(opens==closes&&held.empty()&&unexpected==0,"all fake image handles returned, no unrecognized OS access");
  if(!imageOrder.empty()){
   const auto firstImage=std::find_if(events.begin(),events.end(),[](const auto& e){return e.rfind("open:",0)==0;});
   need(std::distance(events.begin(),firstImage)==4,"both family reads complete before first image open");
   for(std::size_t i=0;i+1<imageOrder.size();i+=2)need((imageOrder[i]==8001||imageOrder[i]==8002)&&imageOrder[i+1]==4000,"original child then parent image order");
  }
  cases.push_back(Json{{"case",name},{"passed",ok==prior&&ok},{"checks",checks-firstCheck},{"reason",error},{"owned",owned},{"result",Json::array()},{"basicQueries",queryCalls},{"tcpCalls",f4.calls+f6.calls},{"v4Calls",f4.calls},{"v6Calls",f6.calls},{"opens",opens},{"closes",closes},{"imageOrder",imageOrder},{"events",events}});
  for(const auto& e:got){std::string host;for(const auto ch:e.host)host.push_back(static_cast<char>(ch));cases.back()["result"].push_back({{"pid",e.pid},{"port",e.port},{"host",std::move(host)}});}
 };
 // This is the only baseline control: no malformed counts/sizes reach old row walkers.
 healthy();f6.fault=Fault::dataDenied;test("newfailure1-second-family-denied-valid-sibling",{},"live-debug-unavailable",true);
 if(!control){
  healthy();test("normal-v4-v6",both,nullptr);
  healthy();v4.clear();test("empty-v4",{both[1]},nullptr);
  healthy();v6.clear();test("empty-v6",{both[0]},nullptr);
  healthy();v4.clear();v6.clear();test("empty-both",{},"live-debug-unavailable");
  healthy();v6.clear();ipv6(8001,8080,true);test("v6-exposure-revokes-v4",{},"live-debug-not-loopback");
  healthy();v4.clear();ipv4(8002,8081,true);test("v4-exposure-revokes-v6",{},"live-debug-not-loopback");
  healthy();ipv4(8001,8080);ipv6(8002,8081);test("normal-duplicates-preserve-order",both,nullptr);
  for(int family:{2,23})for(const auto& item:std::vector<std::pair<Fault,const char*>>{
   {Fault::probeDenied,"probe-denied"},{Fault::probeZero,"probe-zero"},{Fault::probeSmall,"probe-small"},{Fault::probeOversize,"probe-oversize"},{Fault::probeSuccess,"probe-unexpected-success"},
   {Fault::dataDenied,"data-denied"},{Fault::growth,"data-growth"},{Fault::shortZero,"short-header-zero-count"},{Fault::shortCount,"short-header-nonzero-count"},{Fault::overflow,"count-overflow"},{Fault::overCapacity,"size-over-capacity"},{Fault::partialNext,"valid-prefix-truncated-next-row"}}){
    healthy();auto& f=family==2?f4:f6;f.fault=item.first;
    // Allocate a physically safe second-row tail while returning one byte short.
    if(item.first==Fault::partialNext){if(family==2)ipv4(8001,8082);else ipv6(8002,8082);}
    const auto name=std::string(family==2?"v4-":"v6-")+item.second;
    test(name,{},"live-debug-unavailable",true);
    test(name+"-still-owned",{},"live-debug-unavailable",true,true);
    healthy();test(name+"-fresh-recovery",both,nullptr);
   }
  for(int family:{2,23}){
   healthy();(family==2?f4:f6).fault=Fault::padding;test(std::string(family==2?"v4":"v6")+"-legal-padding",both,nullptr);
   healthy();(family==2?f4:f6).fault=Fault::zeroRows;test(std::string(family==2?"v4":"v6")+"-zero-count-with-capacity",{both[family==2?1:0]},nullptr);
  }
 }
 Json raw{{"ok",ok},{"selection",control?"endpoint-table-completeness-control":"endpoint-table-completeness"},{"cases",cases},{"caseCount",cases.size()},{"checks",checks},
  {"defaultListenerGroupExecuted",false},{"defaultDocumentGroupExecuted",false},{"realSteamOperations",0},{"realNetworkOperations",0},{"realProcessImageOperations",0},{"fakeImageHandlesRemaining",held.size()},
  {"maximumConcurrentTableBytes",32*1024*1024},{"elapsedMs",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()}};
 const auto bytes=raw.dump(2)+'\n';DWORD written=0;
 if(bytes.size()>(std::numeric_limits<DWORD>::max)()||!::WriteFile(resultOwner.h,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)||written!=bytes.size()||!::FlushFileBuffers(resultOwner.h))throw std::runtime_error("fixture output write failed");
 if(!::CloseHandle(resultOwner.h))throw std::runtime_error("fixture output close failed");resultOwner.h=INVALID_HANDLE_VALUE;
 std::cout<<"ENDPOINT_TABLE_COMPLETENESS "<<Json{{"ok",ok},{"caseCount",cases.size()},{"checks",checks}}.dump()<<'\n';return ok?0:1;
}
}
static std::optional<int> listenerCompleteSelection(int argc,char** argv){
 bool requested=false;for(int i=1;i<argc;++i){const std::string_view a=argv[i];if(a=="--listener-inventory-only"||a=="endpoint-table-completeness"||a=="endpoint-table-completeness-control")requested=true;}
 if(!requested)return {};
 // Preserve both original cases/bodies and the original non-doc whole selector.
 if(argc==4&&std::string_view(argv[1])=="--listener-inventory-only"&&std::string_view(argv[2])=="--case"&&
    (std::string_view(argv[3])=="endpoint-parent-prefilter"||std::string_view(argv[3])=="endpoint-parent-prefilter-cost")){
#ifdef YMCC_STEAM_DOCUMENT_READ_ONLY
  if(std::string_view(argv[3])=="endpoint-parent-prefilter"){listenerTopologyFixture::endpointParentPrefilter(false);return 0;}
  return 2;
#else
  return {};
#endif
 }
#ifndef YMCC_STEAM_DOCUMENT_READ_ONLY
 if(argc==2&&std::string_view(argv[1])=="--listener-inventory-only")return {};
#endif
 if(argc!=5||std::string_view(argv[1])!="--listener-inventory-only"||std::string_view(argv[2])!="--case")return 2;
 const std::string_view selected=argv[3],output=argv[4];
 if((selected!="endpoint-table-completeness"&&selected!="endpoint-table-completeness-control")||output.empty()||output.starts_with("--"))return 2;
 return listenerTopologyFixture::endpointTableCompleteness(argv[4],selected=="endpoint-table-completeness-control");
}

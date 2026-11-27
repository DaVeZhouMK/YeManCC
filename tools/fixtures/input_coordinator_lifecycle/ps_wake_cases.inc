// Opt-in OS leaf graph for the explicit source-reconfirm selector only.
// The production discovery, identity, journal, power and admission bodies decide.
static bool fixtureSourceReconfirmOsScope=false,fixtureSourceReconfirmConnected=false;
static bool fixtureSourceReconfirmUnknown=false,fixtureSourceReconfirmVirtual=false;
static bool fixtureSourceReconfirmSlotReplaced=false,fixtureSourceReconfirmSetOpen=false;
static unsigned fixtureSourceReconfirmCreates=0,fixtureSourceReconfirmCloses=0;
static unsigned fixtureSourceReconfirmPathReads=0,fixtureSourceReconfirmIndexReads=0;
static unsigned fixtureSourceReconfirmParents=0,fixtureSourceReconfirmIds=0;
static std::wstring fixtureSourceReconfirmHid,fixtureSourceReconfirmRouteRoot;
static const std::wstring fixtureSourceReconfirmRoot=L"USB\\VID_045E&PID_028E\\SOURCE_RECONFIRM_CONTROLLER";
static const std::wstring fixtureSourceReconfirmRoute=L"\\\\?\\USB#VID_045E&PID_028E#SOURCE_RECONFIRM_CONTROLLER#{EC87F1E3-C13B-4100-B5F7-8B84D54260CB}";
static constexpr DEVINST fixtureSourceReconfirmXusbNode=0x53430001;
static constexpr DEVINST fixtureSourceReconfirmHidNode=0x53430002;
static constexpr DEVINST fixtureSourceReconfirmRootNode=0x53430003;
static HDEVINFO fixtureSourceReconfirmSet(){return reinterpret_cast<HDEVINFO>(static_cast<uintptr_t>(0x53435243));}
static std::function<void()> fixtureSourceReconfirmAfterRoute;
static void fixtureSourceReconfirmRequireSet(HDEVINFO set){
 if(!fixtureSourceReconfirmSetOpen||set!=fixtureSourceReconfirmSet()){
  fixtureCountForbiddenIo("foreign-source-reconfirm-SetupAPI-handle");throw std::logic_error("foreign-source-reconfirm-SetupAPI-handle");
 }
}

// Only SetupAPI leaf calls from the exact production PS endpoint reader are
// controlled. No HID is opened and no driver/device/installed app is touched.
static bool fixturePsProbeActive=false,fixturePsOpenFails=false,fixturePsEndSeen=false;
static DWORD fixturePsEndError=ERROR_NO_MORE_ITEMS;
static int fixturePsSizeFailure=-1,fixturePsDetailFailure=-1;
static unsigned fixturePsEnumCalls=0,fixturePsDestroyCalls=0,fixturePsOpenCalls=0;
static std::vector<std::wstring> fixturePsPaths;
static std::wstring fixturePsDetachedRoot;
static CONFIGRET fixturePsDetachedRootResult=CR_NO_SUCH_DEVNODE;
static unsigned fixturePsRootLocateCalls=0;
static ymcc::psEndpointOwnership::Snapshot fixturePsLastAuthority;
static unsigned fixturePsLastSdkSerial=1;
static bool fixturePsSameInstanceNames=false;
static int fixturePsParentFailure=-1,fixturePsIdFailure=-1;
static bool fixturePsParentCycle=false,fixturePsDepthLimit=false;
static unsigned fixturePsParentCalls=0,fixturePsIdCalls=0;
// One-shot controlled interleaving at the actual read-only I/O boundary.
static std::function<void()> fixturePsBeforeRead;
static HDEVINFO fixturePsGetClassDevs(const GUID*,PCWSTR,HWND,DWORD){
 const bool returnRead=fixtureFullPipe&&g_psEndpointOwnership.retirementRequired();
 if(!fixturePsProbeActive&&!returnRead){fixtureCountForbiddenIo("unadmitted-PS-SetupAPI-boundary");throw std::logic_error("unadmitted-PS-SetupAPI-boundary");}
 if(returnRead&&!fixturePsReturnManualTopology&&!fixturePsAutoReturnTopology()){SetLastError(ERROR_NOT_READY);return INVALID_HANDLE_VALUE;}
 ++fixturePsOpenCalls;
 if(fixturePsBeforeRead){auto invoke=std::move(fixturePsBeforeRead);fixturePsBeforeRead={};invoke();}
 if(fixturePsOpenFails){SetLastError(ERROR_ACCESS_DENIED);return INVALID_HANDLE_VALUE;}
 return reinterpret_cast<HDEVINFO>(static_cast<uintptr_t>(0x5053));
}
static BOOL fixturePsEnum(HDEVINFO set,PSP_DEVINFO_DATA,const GUID*,DWORD index,PSP_DEVICE_INTERFACE_DATA data){
 if((!fixturePsProbeActive&&!(fixtureFullPipe&&g_psEndpointOwnership.retirementRequired()))||set!=reinterpret_cast<HDEVINFO>(static_cast<uintptr_t>(0x5053))){fixtureCountForbiddenIo("unknown-PS-enumeration-handle");throw std::logic_error("unknown-PS-enumeration-handle");}
 ++fixturePsEnumCalls;if(index>=fixturePsPaths.size()){fixturePsEndSeen=true;SetLastError(fixturePsEndError);return FALSE;}
 data->Reserved=index;return TRUE;
}
static BOOL fixturePsDetail(HDEVINFO,PSP_DEVICE_INTERFACE_DATA data,PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail,DWORD bytes,PDWORD required,PSP_DEVINFO_DATA info){
 const auto index=static_cast<size_t>(data->Reserved);if(index>=fixturePsPaths.size()){fixtureCountForbiddenIo("PS-detail-index-out-of-range");throw std::logic_error("PS-detail-index-out-of-range");}
 const auto& value=fixturePsPaths[index];const DWORD size=static_cast<DWORD>(offsetof(SP_DEVICE_INTERFACE_DETAIL_DATA_W,DevicePath)+(value.size()+1)*sizeof(wchar_t));
 if(!detail){if(required)*required=static_cast<int>(index)==fixturePsSizeFailure?0:size;SetLastError(static_cast<int>(index)==fixturePsSizeFailure?ERROR_ACCESS_DENIED:ERROR_INSUFFICIENT_BUFFER);return FALSE;}
 if(static_cast<int>(index)==fixturePsDetailFailure){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
 if(bytes<size){if(required)*required=size;SetLastError(ERROR_INSUFFICIENT_BUFFER);return FALSE;}
 memcpy(detail->DevicePath,value.c_str(),(value.size()+1)*sizeof(wchar_t));if(info)info->DevInst=0x50530000+static_cast<DEVINST>(index);return TRUE;
}
static BOOL fixturePsDestroy(HDEVINFO){++fixturePsDestroyCalls;return TRUE;}

static std::wstring fixturePsRootForPath(size_t index){
 const auto& p=fixturePsPaths.at(index);const auto serial=fixtureFullPipe?(fixturePsSameInstanceNames?1:fullPipeRecreateCommands()+1):fixturePsLastSdkSerial;
 if(p.find(L"HIDMAESTRO-OWN")!=std::wstring::npos||p.find(L"NEW-HID-SAME-ROOT")!=std::wstring::npos)
  return L"SWD\\HIDMAESTRO_VID_054C_PID_0DF2&IG_00\\OFFLINE_PS_"+std::to_wstring(serial);
 if(p.find(L"OLD-TARGET-GENERATION")!=std::wstring::npos)return L"SWD\\HIDMAESTRO_VID_054C_PID_0DF2&IG_00\\RETIRED_PS";
 if(p.find(L"OTHER-PROCESS")!=std::wstring::npos)return L"SWD\\HIDMAESTRO_VID_054C_PID_0DF2&IG_00\\PEER_PROCESS";
 if(p.find(L"PHYSICAL")!=std::wstring::npos)return L"USB\\VID_1234&PID_0001\\OFFLINE_PHYSICAL";
 return L"SWD\\HIDMAESTRO_VID_28DE_PID_1205\\FOREIGN";
}
// The root can remain present independently of all HID interfaces. This is a
// controlled read-only OS fact, never an injected ownership/retirement result.
static CONFIGRET fixturePsLocate(PDEVINST node,PWSTR id,ULONG flags){
 if((!fixturePsProbeActive && !(fixtureFullPipe&&g_psEndpointOwnership.retirementRequired())) ||
    !node || !id || !*id || flags!=CM_LOCATE_DEVNODE_NORMAL){fixtureCountForbiddenIo("unadmitted-PS-root-lookup");throw std::logic_error("unadmitted-PS-root-lookup");}
 ++fixturePsRootLocateCalls;
 if(!fixturePsDetachedRoot.empty() && _wcsicmp(id,fixturePsDetachedRoot.c_str())==0){
  if(fixturePsDetachedRootResult==CR_SUCCESS)*node=0x50550000;
  return fixturePsDetachedRootResult;
 }
 for(size_t i=0;i<fixturePsPaths.size();++i)if(_wcsicmp(id,fixturePsRootForPath(i).c_str())==0){*node=0x50540000+static_cast<DEVINST>(i);return CR_SUCCESS;}
 return CR_NO_SUCH_DEVNODE;
}
static CONFIGRET fixturePsDeviceId(DEVINST node,PWSTR value,ULONG chars,ULONG){
 ++fixturePsIdCalls;std::wstring id;size_t index=0;
 if(node>=0x50530000&&node<0x50530000+fixturePsPaths.size()){
  index=node-0x50530000;if(static_cast<int>(index)==fixturePsIdFailure)return CR_FAILURE;
  const auto root=fixturePsRootForPath(index);id=L"HID"+root.substr(root.find(L'\\'));
  if(fixturePsPaths[index].find(L"NEW-HID-SAME-ROOT")!=std::wstring::npos)id+=L"_REENUM";
 }else if(node>=0x50540000&&node<0x50540000+fixturePsPaths.size())id=fixturePsRootForPath(node-0x50540000);
 else if(fixturePsDepthLimit&&node>=0x50600000&&node<0x50610000)id=L"ROOT\\OFFLINE_DEPTH_"+std::to_wstring(node);
 else{fixtureCountForbiddenIo("unadmitted-PS-CM-instance");throw std::logic_error("unadmitted-PS-CM-instance");}
 if(chars<=id.size())return CR_BUFFER_SMALL;memcpy(value,id.c_str(),(id.size()+1)*sizeof(wchar_t));return CR_SUCCESS;
}
static CONFIGRET fixturePsParent(PDEVINST parent,DEVINST node,ULONG){
 if(fixtureSourceReconfirmOsScope){
  ++fixtureSourceReconfirmParents;if(!parent)return CR_NO_SUCH_DEVNODE;
  if(node==fixtureSourceReconfirmXusbNode){*parent=fixtureSourceReconfirmHidNode;return CR_SUCCESS;}
  if(node==fixtureSourceReconfirmHidNode){*parent=fixtureSourceReconfirmRootNode;return CR_SUCCESS;}
  return CR_NO_SUCH_DEVNODE;
 }
 ++fixturePsParentCalls;
 if(node>=0x50530000&&node<0x50530000+fixturePsPaths.size()){
  const auto index=node-0x50530000;if(static_cast<int>(index)==fixturePsParentFailure)return CR_ACCESS_DENIED;
  *parent=fixturePsParentCycle?node:fixturePsDepthLimit?0x50600000+index*32:0x50540000+index;return CR_SUCCESS;
 }
 if(fixturePsDepthLimit&&node>=0x50600000&&node<0x50610000){*parent=node+1;return CR_SUCCESS;}
 if(node>=0x50540000&&node<0x50540000+fixturePsPaths.size())return CR_NO_SUCH_DEVNODE;
 fixtureCountForbiddenIo("unadmitted-PS-CM-parent");throw std::logic_error("unadmitted-PS-CM-parent");
}

struct FixturePsReadScope {bool prior=fixturePsProbeActive;FixturePsReadScope(){fixturePsProbeActive=true;}~FixturePsReadScope(){fixturePsProbeActive=prior;}};

// Controlled complete present-device leaf for the no-pin PS profile consumer.
// The shared production parser, not the fixture, decides Present/Unknown/Absent.
static std::string fixturePsProfileScenario="absent";
static json fixturePsProfileObservations=json::array();
static ymcc::targetReturn::pnpAbsence::Result fixturePsProfileAbsence(){
 using namespace ymcc::targetReturn::pnpAbsence;
 if(!fixturePsProbeActive && !(fixtureFullPipe&&g_psEndpointOwnership.retirementRequired())){
  fixtureCountForbiddenIo("unadmitted-PS-profile-root-reader");throw std::logic_error("unadmitted-PS-profile-root-reader");}
 struct Reader {
  struct Node {std::wstring id;std::vector<std::wstring> hardware;};
  std::vector<Node> nodes;DWORD last=ERROR_SUCCESS;unsigned closes=0,enums=0,properties=0;
  Reader(){
   if(fixturePsProfileScenario=="root-present")nodes.push_back({L"ROOT\\HIDCLASS\\HM_ABCD",{L"root\\HidMaestro",L"root\\VID_054C&PID_0DF2"}});
   else if(fixturePsProfileScenario=="generic")nodes.push_back({L"ROOT\\HIDCLASS\\HM_FUTURE",{L"ROOT\\HIDMAESTRO"}});
   else if(fixturePsProfileScenario=="malformed")nodes.push_back({L"ROOT\\HIDCLASS\\HM_ABCD",{L"ROOT\\VID_054C&PID_0DF2"}});
   else if(fixturePsProfileScenario=="other-families"){
    nodes.push_back({L"USB\\VID_28DE&PID_1205\\LOCAL_VALVE",{L"USB\\VID_28DE&PID_1205"}});
    nodes.push_back({L"ROOT\\HIDMAESTROXUSB\\PEER",{L"ROOT\\HIDMAESTROXUSB"}});
   }else nodes.push_back({L"PCI\\UNRELATED\\LOCAL",{L"PCI\\UNRELATED"}});
  }
  HDEVINFO getClassDevs(const GUID*,PCWSTR,HWND,DWORD flags){if(flags!=(DIGCF_PRESENT|DIGCF_ALLCLASSES))throw std::logic_error("PS-profile-present-class-scope");return reinterpret_cast<HDEVINFO>(std::uintptr_t{0x50570000});}
  BOOL enumerate(HDEVINFO,DWORD index,PSP_DEVINFO_DATA info){++enums;if(fixturePsProfileScenario=="enum-error"&&index==1){last=ERROR_ACCESS_DENIED;return FALSE;}if(index>=nodes.size()){last=ERROR_NO_MORE_ITEMS;return FALSE;}info->DevInst=index+1;return TRUE;}
  BOOL destroy(HDEVINFO){++closes;return TRUE;}
  CONFIGRET deviceId(DEVINST node,PWCHAR out,ULONG chars,ULONG){const auto& id=nodes.at(node-1).id;if(chars<=id.size())return CR_BUFFER_SMALL;std::copy(id.begin(),id.end(),out);out[id.size()]=0;return CR_SUCCESS;}
  CONFIGRET hardwareIds(DEVINST node,const DEVPROPKEY*,DEVPROPTYPE* type,PBYTE out,PULONG bytes,ULONG){
   ++properties;*type=DEVPROP_TYPE_STRING_LIST;std::vector<wchar_t> data;
   for(const auto& id:nodes.at(node-1).hardware){data.insert(data.end(),id.begin(),id.end());data.push_back(0);}data.push_back(0);
   if(fixturePsProfileScenario=="malformed"){data.back()=L'X';}
   const auto size=ULONG(data.size()*sizeof(wchar_t));if(*bytes<size){*bytes=size;return CR_BUFFER_SMALL;}
   std::memcpy(out,data.data(),size);*bytes=size;return CR_SUCCESS;
  }
  DWORD error(){return last;}
 } reader;
 const auto result=verifyPsProfile(reader);
 fixturePsProfileObservations.push_back({{"scenario",fixturePsProfileScenario},{"state",stateName(result.state)},
  {"reason",reasonName(result.reason)},{"nodes",result.scannedNodes},{"closes",reader.closes},{"enumerations",reader.enums},
  {"properties",reader.properties},{"ownershipInjected",false},{"deviceWrites",false}});
 if(reader.closes!=1)throw std::logic_error("PS-profile-owned-set-not-closed");
 return result;
}

static void fixturePsReadConfig(std::vector<std::wstring> paths,DWORD end=ERROR_NO_MORE_ITEMS){fixturePsProfileScenario="absent";fixturePsPaths=std::move(paths);fixturePsDetachedRoot.clear();fixturePsDetachedRootResult=CR_NO_SUCH_DEVNODE;fixturePsOpenFails=false;fixturePsEndError=end;fixturePsSizeFailure=fixturePsDetailFailure=fixturePsParentFailure=fixturePsIdFailure=-1;fixturePsParentCycle=fixturePsDepthLimit=false;}
// Per-case real private Host precondition; never borrow a previous case's pin.
static void fullPipeBeginReadonlyPsOwner(const char* fault,bool expectOwnership);
static void fullPipeEndReadonlyPsOwner();
struct FullPipeReadonlyPsOwner {
 bool active=true;
 FullPipeReadonlyPsOwner(){try{fullPipeBeginReadonlyPsOwner("none",true);}catch(...){active=false;try{fullPipeEndReadonlyPsOwner();}catch(...){}throw;}}
 void close(){if(active){active=false;fullPipeEndReadonlyPsOwner();}}
 ~FullPipeReadonlyPsOwner(){if(active){try{close();}catch(...){}}}
};
static void psEndpointProbeCases(){
 cases.push_back({{"name","PS-WAKE/read-only-endpoint-enumeration"}});json rows=json::array();
 FullPipeReadonlyPsOwner privateOwner;
 const auto authority=fixturePsLastAuthority;check(authority.valid,"read-only query fixture authority came from an accepted real Host reply");
 const std::wstring own=L"HID#VID_054C&PID_0DF2#HIDMAESTRO-OWN",foreign=L"HID#VID_28DE&PID_1205#HM-CTL-FOREIGN",physical=L"HID#VID_1234&PID_0001#PHYSICAL";
 struct Scenario{const char* name;std::vector<std::wstring> paths;bool openFails;DWORD endError;int sizeFailure,detailFailure;PsWakeProbe expected;int parentFailure=-1,idFailure=-1;bool cycle=false,depth=false,noOwner=false;};
 const std::vector<Scenario> scenarios={
 {"open-unavailable",{},true,ERROR_NO_MORE_ITEMS,-1,-1,PsWakeProbe::Unknown},
 {"complete-empty",{},false,ERROR_NO_MORE_ITEMS,-1,-1,PsWakeProbe::Absent},
 {"complete-own-present",{own},false,ERROR_NO_MORE_ITEMS,-1,-1,PsWakeProbe::Present},
 {"complete-foreign-present",{foreign},false,ERROR_NO_MORE_ITEMS,-1,-1,PsWakeProbe::Foreign},
 {"complete-physical-only",{physical},false,ERROR_NO_MORE_ITEMS,-1,-1,PsWakeProbe::Absent},
 {"enumeration-denied-before-first",{},false,ERROR_ACCESS_DENIED,-1,-1,PsWakeProbe::Unknown},
 {"enumeration-failed-after-own",{own},false,ERROR_GEN_FAILURE,-1,-1,PsWakeProbe::Unknown},
 {"detail-size-unavailable",{own},false,ERROR_NO_MORE_ITEMS,0,-1,PsWakeProbe::Unknown},
 {"detail-read-failed",{own},false,ERROR_NO_MORE_ITEMS,-1,0,PsWakeProbe::Unknown},
 {"interface-bound-without-complete-end",std::vector<std::wstring>(257,physical),false,ERROR_NO_MORE_ITEMS,-1,-1,PsWakeProbe::Unknown},
 {"partial-detail-after-foreign",{foreign,own},false,ERROR_NO_MORE_ITEMS,-1,1,PsWakeProbe::Unknown},
 {"complete-same-profile-peer",{L"HID#VID_054C&PID_0DF2#HIDMAESTRO-OTHER-PROCESS-B"},false,ERROR_NO_MORE_ITEMS,-1,-1,PsWakeProbe::Foreign},
 {"complete-same-profile-retired",{L"HID#VID_054C&PID_0DF2#HIDMAESTRO-OLD-TARGET-GENERATION"},false,ERROR_NO_MORE_ITEMS,-1,-1,PsWakeProbe::Foreign},
 {"new-HID-same-SDK-root",{L"HID#VID_054C&PID_0DF2#HIDMAESTRO-NEW-HID-SAME-ROOT"},false,ERROR_NO_MORE_ITEMS,-1,-1,PsWakeProbe::Present},
 {"parent-query-denied",{own},false,ERROR_NO_MORE_ITEMS,-1,-1,PsWakeProbe::Unknown,0},
 {"instance-query-unavailable",{own},false,ERROR_NO_MORE_ITEMS,-1,-1,PsWakeProbe::Unknown,-1,0},
 {"parent-cycle",{own},false,ERROR_NO_MORE_ITEMS,-1,-1,PsWakeProbe::Unknown,-1,-1,true},
 {"parent-depth-truncated",{own},false,ERROR_NO_MORE_ITEMS,-1,-1,PsWakeProbe::Unknown,-1,-1,false,true},
 {"profile-present-without-owned-pin",{own},false,ERROR_NO_MORE_ITEMS,-1,-1,PsWakeProbe::Unknown,-1,-1,false,false,true}};
 for(const auto& test:scenarios){fixturePsProbeActive=true;fixturePsOpenFails=test.openFails;fixturePsEndError=test.endError;fixturePsSizeFailure=test.sizeFailure;fixturePsDetailFailure=test.detailFailure;fixturePsPaths=test.paths;fixturePsEndSeen=false;fixturePsEnumCalls=fixturePsDestroyCalls=0;
  fixturePsParentFailure=test.parentFailure;fixturePsIdFailure=test.idFailure;fixturePsParentCycle=test.cycle;fixturePsDepthLimit=test.depth;fixturePsParentCalls=fixturePsIdCalls=0;
  auto expectedOwner=authority;if(test.noOwner)expectedOwner.valid=false;
  const auto actual=psWakeProbeOwnEndpoint(expectedOwner);fixturePsProbeActive=false;const auto decision=psWakeDecide(actual.kind,false,1);const auto name=[](PsWakeProbe value){return value==PsWakeProbe::Present?"present":value==PsWakeProbe::Absent?"absent":value==PsWakeProbe::Foreign?"foreign":"unknown";};
  rows.push_back({{"scenario",test.name},{"expected",name(test.expected)},{"actual",name(actual.kind)},{"action",decision.action},{"rebuild",decision.rebuild},{"enumerationCalls",fixturePsEnumCalls},{"destroyCalls",fixturePsDestroyCalls},{"terminalEnumerationObserved",fixturePsEndSeen},{"ownMatches",actual.ownMatches},{"foreignVirtual",actual.foreignVirtual}});
  check(actual.kind==test.expected,"actual PS endpoint reader does not turn incomplete observation into present/absent authority");
  check(decision.rebuild==(test.expected==PsWakeProbe::Absent),"actual PS wake decision permits rebuild only for complete endpoint absence");
  check(fixturePsDestroyCalls==((test.openFails||test.noOwner)?0u:1u)&&fixturePsEnumCalls<=256&&fixturePsParentCalls<=256*16&&fixturePsIdCalls<=256*16,"read-only PS probe remains bounded and closes its own SetupAPI set once");
 }
 privateOwner.close();
 cases.back()["ownershipSnapshotSource"]="accepted actual prior Host PREPARE/RECREATE reply, read-only fixture input only";cases.back()["endpointReads"]=rows;cases.back()["actualProductionReaderAndDecision"]=true;cases.back()["controlledSetupApiLeaves"]=true;
}

// Controlled physical slot and SDK-created Elite slot for the complete native
// Xbox startup verifier. Exact target paths/PnP IDs match the managed fixture.
static const std::wstring fixtureEliteRoot=L"SWD\\HIDMAESTRO_VID_045E_PID_0B00&IG_00\\OFFLINE_ELITE";
static const std::wstring fixtureEliteHid=L"HID\\HIDMAESTRO_VID_045E_PID_0B00&IG_00\\OFFLINE_ELITE";
static const std::wstring fixtureElitePath=L"\\\\?\\HID#HIDMAESTRO_VID_045E_PID_0B00&IG_00#OFFLINE_ELITE";
// Controlled OS identity input. Generic cases keep this value; the MSI runtime
// case scopes its exact returned local identity and restores this value on exit.
static std::wstring fixturePhysicalPath=L"\\\\?\\HID#VID_1234&PID_5678#OFFLINE_PHYSICAL";
static bool fixtureElitePrepared(){
 const bool exactObservedOldRelease=fixtureClassifiedReturnReleasedEpoch&&fixtureClassifiedReturnReleasedEpoch==g_inputHostEpoch&&!g_inputHostProcess;
 return fixtureXinputSourceLive&&g_inputHostPrepared&&inputHostConfiguredPersona()=="elite"&&!exactObservedOldRelease;
}
static DWORD fixtureControlledXinputState(DWORD slot,XINPUT_STATE* state){
 if(fixtureSourceReconfirmOsScope){
  ++inertXinputReads;if(!fixtureXinputSourceLive||!fixtureSourceReconfirmConnected||slot!=1||!state)return ERROR_DEVICE_NOT_CONNECTED;
  ++fixtureXinputLivePolls[slot];*state={};state->dwPacketNumber=1;return ERROR_SUCCESS;
 }
 ++inertXinputReads;if(!fixtureXinputSourceLive||slot>=4)return ERROR_DEVICE_NOT_CONNECTED;++fixtureXinputLivePolls[slot];
 if(!state||(slot!=0&&!(slot==1&&fixtureElitePrepared())))return ERROR_DEVICE_NOT_CONNECTED;*state={};state->dwPacketNumber=1;return ERROR_SUCCESS;
}
static std::wstring fixtureControlledXinputPath(DWORD slot){
 if(fixtureSourceReconfirmOsScope){
  ++inertXinputReads;++fixtureSourceReconfirmPathReads;
  if(!fixtureXinputSourceLive||!fixtureSourceReconfirmConnected||slot!=1||fixtureSourceReconfirmUnknown)return {};
  return fixtureSourceReconfirmVirtual?fixtureElitePath:fixtureSourceReconfirmRoute;
 }++inertXinputReads;if(!fixtureXinputSourceLive)return {};if(slot==0)return fixturePhysicalPath;if(slot==1&&fixtureElitePrepared())return fixtureElitePath;return {};}
static BYTE fixtureControlledXinputIndex(const std::wstring& path){
 if(fixtureSourceReconfirmOsScope){
  ++fixtureSourceReconfirmIndexReads;
  return fixtureXinputSourceLive&&fixtureSourceReconfirmConnected&&path==fixtureSourceReconfirmRoute?
   (fixtureSourceReconfirmSlotReplaced?0:1):0xFF;
 }if(!fixtureXinputSourceLive)return 0xFF;return path==fixturePhysicalPath?0:path==fixtureElitePath&&fixtureElitePrepared()?1:0xFF;}
static HDEVINFO fixtureXboxCreate(const GUID*,HWND){
 if(fixtureSourceReconfirmOsScope){
  if(fixtureSourceReconfirmSetOpen)throw std::logic_error("source-reconfirm-set-already-open");
  fixtureSourceReconfirmSetOpen=true;++fixtureSourceReconfirmCreates;return fixtureSourceReconfirmSet();
 }if(!fixtureElitePrepared()){fixtureCountForbiddenIo("unadmitted-Elite-identity-query");throw std::logic_error("unadmitted-Elite-identity-query");}return reinterpret_cast<HDEVINFO>(static_cast<uintptr_t>(0x454c4954));}
static BOOL fixtureXboxOpen(HDEVINFO set,PCWSTR path,DWORD,PSP_DEVICE_INTERFACE_DATA iface){
 if(fixtureSourceReconfirmOsScope){
  fixtureSourceReconfirmRequireSet(set);if(!path||fixtureSourceReconfirmRoute!=path||!iface)return FALSE;
  iface->Flags=SPINT_ACTIVE;iface->Reserved=fixtureSourceReconfirmXusbNode;return TRUE;
 }if(set!=reinterpret_cast<HDEVINFO>(static_cast<uintptr_t>(0x454c4954)))throw std::logic_error("foreign-Elite-identity-handle");if(!path||fixtureElitePath!=path)return FALSE;iface->Flags=SPINT_ACTIVE;return TRUE;}
static BOOL fixtureXboxDetail(HDEVINFO set,PSP_DEVICE_INTERFACE_DATA iface,PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail,DWORD bytes,PDWORD needed,PSP_DEVINFO_DATA info){
 if(fixtureSourceReconfirmOsScope){
  fixtureSourceReconfirmRequireSet(set);if(!iface||iface->Reserved!=fixtureSourceReconfirmXusbNode)return FALSE;
  const DWORD size=static_cast<DWORD>(offsetof(SP_DEVICE_INTERFACE_DETAIL_DATA_W,DevicePath)+(fixtureSourceReconfirmRoute.size()+1)*sizeof(wchar_t));
  if(needed)*needed=size;if(!detail||bytes<size){SetLastError(ERROR_INSUFFICIENT_BUFFER);return FALSE;}
  memcpy(detail->DevicePath,fixtureSourceReconfirmRoute.c_str(),(fixtureSourceReconfirmRoute.size()+1)*sizeof(wchar_t));
  if(info)info->DevInst=fixtureSourceReconfirmXusbNode;return TRUE;
 }
 const DWORD size=static_cast<DWORD>(offsetof(SP_DEVICE_INTERFACE_DETAIL_DATA_W,DevicePath)+(fixtureElitePath.size()+1)*sizeof(wchar_t));if(!detail){if(needed)*needed=size;SetLastError(ERROR_INSUFFICIENT_BUFFER);return FALSE;}if(bytes<size)return FALSE;memcpy(detail->DevicePath,fixtureElitePath.c_str(),(fixtureElitePath.size()+1)*sizeof(wchar_t));if(info)info->DevInst=0x454c4954;return TRUE;
}
static BOOL fixtureXboxDestroy(HDEVINFO set){
 if(fixtureSourceReconfirmOsScope){
  fixtureSourceReconfirmRequireSet(set);fixtureSourceReconfirmSetOpen=false;++fixtureSourceReconfirmCloses;
  if(fixtureSourceReconfirmAfterRoute){auto callback=std::move(fixtureSourceReconfirmAfterRoute);fixtureSourceReconfirmAfterRoute={};callback();}
 }
 return TRUE;
}
static CONFIGRET fixtureXboxDeviceId(DEVINST node,PWSTR value,ULONG chars,ULONG){
 if(fixtureSourceReconfirmOsScope){
  ++fixtureSourceReconfirmIds;std::wstring id;
  if(node==fixtureSourceReconfirmXusbNode)id=L"USB\\VID_045E&PID_028E&IG_XUSB\\SOURCE_RECONFIRM_ROUTE";
  else if(node==fixtureSourceReconfirmHidNode)id=fixtureSourceReconfirmHid;
  else if(node==fixtureSourceReconfirmRootNode)id=fixtureSourceReconfirmRouteRoot;
  else return CR_NO_SUCH_DEVNODE;
  if(!value||chars<=id.size())return CR_BUFFER_SMALL;memcpy(value,id.c_str(),(id.size()+1)*sizeof(wchar_t));return CR_SUCCESS;
 }if(node!=0x454c4954||chars<=fixtureEliteHid.size())return CR_NO_SUCH_DEVNODE;memcpy(value,fixtureEliteHid.c_str(),(fixtureEliteHid.size()+1)*sizeof(wchar_t));return CR_SUCCESS;}
static CONFIGRET fixtureXboxLocate(PDEVINST,DEVINSTID_W,ULONG){return CR_NO_SUCH_DEVNODE;}
static CONFIGRET fixtureXboxStatus(PULONG,PULONG,DEVINST,ULONG){return CR_NO_SUCH_DEVNODE;}


// Explicit selector only; never append this function to the default case list.
static const char* sourceReconfirmCollectionVariants[]={
 "same-root","other-root","unknown","virtual","slot-replaced","stale-power",
 "pending-notification","publication-only","journal-run-changed","journal-root-changed",
 "journal-inactive","host-busy","exact-identity","msi-family-peer"
};

static void fixtureSourceReconfirmScan(){
 // Same sampling owner and local variables as gamepadReadStateUnlocked.
 std::lock_guard<std::mutex> sampleOwner(g_gamepadReadMx);
 const ULONGLONG probeNow=GetTickCount64();const int probeSrc=kGpReadBus;const bool probeCostOn=false;
 const auto diagnosticReadPower=currentPowerGeneration();
 const auto diagnosticReadTopology=g_xinputTopologyEpoch.load(std::memory_order_acquire);
 #include "xinput_discovery.inc"
}
static void fixtureSourceReconfirmBoundary(){
 xinputSlotsMarkDiscoveryDirty();xinputSlotsRequestIdentityReprobe(2,"fixture-source-collection-boundary");
 g_xinputSlotFullScanEpochStart.store(g_xinputTopologyEpoch.load(std::memory_order_acquire),std::memory_order_release);
 g_xinputSlotFullScanOnce.store(true,std::memory_order_relaxed);
}

// The holder never blocks acquiring Host. A bounded read-only native thread is
// enough to exercise the real try_lock branch; no child/device process is used.
struct FixtureSourceReconfirmBusyHost {
 std::atomic<bool> held{false},release{false},done{false};std::thread worker;
 void start(){worker=std::thread([this]{
  std::unique_lock<InputHostMutex> lock(g_inputHostMx,std::try_to_lock);
  held.store(lock.owns_lock(),std::memory_order_release);
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(1500);
  while(lock.owns_lock()&&!release.load(std::memory_order_acquire)&&std::chrono::steady_clock::now()<deadline)
   std::this_thread::sleep_for(std::chrono::milliseconds(1));
  done.store(true,std::memory_order_release);
 });}
 bool acquired(){const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(250);
  while(!held.load(std::memory_order_acquire)&&!done.load(std::memory_order_acquire)&&std::chrono::steady_clock::now()<deadline)
   std::this_thread::sleep_for(std::chrono::milliseconds(1));
  return held.load(std::memory_order_acquire)&&!done.load(std::memory_order_acquire);
 }
 ~FixtureSourceReconfirmBusyHost(){release.store(true,std::memory_order_release);if(worker.joinable())worker.join();}
};

static const char* sourceReconfirmAdoptionVariants[]={"post-proof-clear","route-failed-clear"};
// A true competing native thread only tries Host. It never acquires Sample or
// waits for the sampling owner. Journal inactive is the controlled completed
// visibility-return input; all source retirement below is actual Clear.
struct FixtureSourceReconfirmClear {
 std::atomic<bool> done{false};std::thread worker;
 unsigned attempts=0,clearCalls=0;bool lastAcquired=false,cleared=false;
 unsigned maskBeforeClear=0,pendingBeforeClear=0,primaryBeforeClear=0;
 void start(){
  done.store(false,std::memory_order_release);lastAcquired=false;++attempts;
  worker=std::thread([this]{
   std::unique_lock<InputHostMutex> owner(g_inputHostMx,std::try_to_lock);
   lastAcquired=owner.owns_lock();
   if(lastAcquired){
    maskBeforeClear=g_xinputPhysicalSourceMask;pendingBeforeClear=g_xinputSourceReconfirmMask.load();primaryBeforeClear=g_xinputPrimarySlot;
    g_hidHideVisibilityJournal.active=false;
    inputHostClearLocked();++clearCalls;cleared=true;
   }
   done.store(true,std::memory_order_release);
  });
 }
 bool finish(){
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(250);
  while(!done.load(std::memory_order_acquire)&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
  const bool timely=done.load(std::memory_order_acquire);if(worker.joinable())worker.join();return timely;
 }
 ~FixtureSourceReconfirmClear(){if(worker.joinable())worker.join();}
};

static void sourceReconfirmCollectionCases(const std::string& only={}){
 std::vector<std::string> selected;
 if(only.empty()){for(const char* token:sourceReconfirmCollectionVariants)selected.emplace_back(token);}
 else if(only=="adoption-owner"){for(const char* token:sourceReconfirmAdoptionVariants)selected.emplace_back(token);for(const char* token:sourceReconfirmCollectionVariants)if(std::string(token)=="exact-identity"||std::string(token)=="msi-family-peer")selected.emplace_back(token);}
 else{
  bool known=false;for(const char* token:sourceReconfirmCollectionVariants)known|=only==token;
  for(const char* token:sourceReconfirmAdoptionVariants)known|=only==token;
  if(!known)throw std::logic_error("unknown-source-reconfirm-variant");selected.push_back(only);
 }
 for(const auto& variant:selected){
  seed();const auto failureBefore=failures.size();
  cases.push_back({{"name","source-reconfirm-collection/"+variant}});
  const auto oldPower=currentPowerGeneration();const auto oldPhase=g_powerLifecycle.load();
  const auto oldGate=g_hardwareWriteGate.load();const auto oldPublication=g_sharedPowerPublicationRevision.load();
  struct Restore {
   unsigned long long power;PowerLifecycle phase;bool gate;unsigned long long publication;
   ymcc::sleeppower::Dispatch::Receipt notification;
   ~Restore(){
    fixtureSourceReconfirmAfterRoute={};fixtureSourceReconfirmAfterProof={};fixtureSourceReconfirmAfterReconfirmedLog={};fixtureSourceReconfirmOsScope=false;fixtureXinputSourceLive=false;
    if(notification)g_sleepPowerDispatch.complete(notification,GetTickCount64());
    g_powerGeneration=power;g_powerLifecycle=phase;g_hardwareWriteGate=gate;g_sharedPowerPublicationRevision=publication;
    g_hidHideVisibilityJournal={};g_inputHostEpoch=0;g_inputHostRunId.clear();
   }
  } restore{oldPower,oldPhase,oldGate,oldPublication,{}};
  const bool exact=variant=="exact-identity",msi=variant=="msi-family-peer";
  const bool adoption=variant=="post-proof-clear"||variant=="route-failed-clear";
  if(!msi){g_machineIdentity.family=YmccFamily::AsusRogAllyX;g_machineIdentity.manufacturer="ASUSTEK COMPUTER INC.";g_machineIdentity.product="ROG Ally X";}
  g_msiDinput.enabled=false;g_msiDinput.mainInterfaceMatched=false;g_msiDinputSourceValid=false;
  {std::lock_guard<std::mutex> lock(g_msiMainSourceMx);g_msiMainSource={};
   if(msi){g_msiMainSource.valid=true;g_msiMainSource.generation=77;g_msiMainSource.vendorId=0x0db0;g_msiMainSource.productId=0x1902;}}
  g_exitRequested=false;g_inputReady=true;g_inputCaptureStop=false;g_powerLifecycle=PowerLifecycle::Ready;
  g_hardwareWriteGate=true;if(!currentPowerGeneration())g_powerGeneration=1;
  fixtureSourceReconfirmOsScope=true;fixtureXinputSourceLive=true;fixtureSourceReconfirmConnected=true;
  fixtureSourceReconfirmUnknown=fixtureSourceReconfirmVirtual=fixtureSourceReconfirmSlotReplaced=false;
  fixtureSourceReconfirmSetOpen=false;fixtureSourceReconfirmCreates=fixtureSourceReconfirmCloses=0;
  fixtureSourceReconfirmPathReads=fixtureSourceReconfirmIndexReads=fixtureSourceReconfirmParents=fixtureSourceReconfirmIds=0;
  fixtureSourceReconfirmAfterRoute={};fixtureSourceReconfirmRouteRoot=fixtureSourceReconfirmRoot;
  HidHidePhysicalIdentity selected;selected.resolved=true;selected.vendorId=msi?0x0db0:0x045e;selected.productId=msi?0x1901:0x028e;
  selected.usagePage=1;selected.usage=5;
  selected.deviceInstanceId=msi?"HID\\VID_0DB0&PID_1901&IG_01\\SOURCE_RECONFIRM_CONTROLLER":"HID\\VID_045E&PID_028E&IG_01\\SOURCE_RECONFIRM_CONTROLLER";
  selected.sourcePath=selected.deviceInstanceId;selected.baseContainerDeviceInstanceId=W2U(fixtureSourceReconfirmRoot);
  selected.physicalIdentity=selected.deviceInstanceId+"|"+selected.baseContainerDeviceInstanceId+"|VID_"+std::to_string(selected.vendorId)+"|PID_"+std::to_string(selected.productId);
  fixtureSourceReconfirmHid=U2W(selected.deviceInstanceId);
  // Input setup models an already selected pre-target owner. Every mask after
  // the first scan, including release/recovery/admittedMask, is production output.
  {std::scoped_lock owner(g_gamepadReadMx,g_hidHidePhysicalIdentityMx);
   g_hidHidePhysicalIdentity=selected;g_xinputPhysicalSourceLocked=true;g_xinputPhysicalSourceMask=2;
   g_xinputPreTargetSlots=2;g_xinputPrimarySlot=g_xinputSemanticSlot=1;
   g_xinputConnectedMask=g_xinputAdmittedMask=g_xinputPrevSampleMask=g_xinputKnownSlotMask=0;
   g_xinputVirtualEndpointMask=g_xinputVirtualEndpointProbed=g_xinputSlotUnknownMask=0;
   g_xinputSourceReconfirmMask=0;g_xinputSlotReprobeMask=0;g_xinputPreTargetBoundaryScanned=false;
   g_xinputReconfirmIdentity.clear();g_xinputReconfirmMsiGeneration=0;g_xinputReconfirmRejectLogged=false;
   for(unsigned slot=0;slot<4;++slot){g_xinputSlotIdentityResolvedEpoch[slot]=0;g_xinputSlotReprobeAt[slot]=0;g_xinputFeedbackIdentity[slot]=ymcc::input::identityRefresh::Slot{};}
   g_xinputDeferredFeedbackStop.clear();
  }
  {std::lock_guard<InputHostMutex> owner(g_inputHostMx);
   g_inputHostEpoch=41;g_inputHostRunId="source-reconfirm-original-run";
   g_hidHideVisibilityJournal={};g_hidHideVisibilityJournal.active=true;
   g_hidHideVisibilityJournal.transactionId="source-reconfirm-original-transaction";
   g_hidHideVisibilityJournal.physical=selected;
  }
  fixtureSourceReconfirmBoundary();fixtureSourceReconfirmScan();
  check(g_xinputConnectedMask==2&&g_xinputAdmittedMask==2&&g_xinputPrevSampleMask==2,
   "actual initial discovery samples physical slot1 and publishes the existing selected source");
  const auto initialProof=g_xinputFeedbackIdentity[1].capture(g_xinputSlotIdentityEpoch.load(),currentPowerGeneration());
  check(initialProof.valid&&!initialProof.legacyFallback,"initial source proof is produced by actual current discovery, never legacy fallback");
  fixtureSourceReconfirmConnected=false;fixtureSourceReconfirmScan();
  const auto releasedMask=g_xinputPhysicalSourceMask,releasedAdmitted=g_xinputAdmittedMask;
  const auto releasedPending=g_xinputSourceReconfirmMask.load();const auto droppedIdentity=g_xinputReconfirmIdentity;
  check(releasedMask==0&&releasedAdmitted==0&&releasedPending==2&&droppedIdentity==ascii_lower(selected.physicalIdentity),
   "actual connected-to-lost edge releases admission and captures its original pending identity");
  if(!exact){
   selected.deviceInstanceId=msi?"HID\\VID_0DB0&PID_1902&MI_00&COL01\\SOURCE_RECONFIRM_CONTROLLER":"HID\\VID_045E&PID_028E&IG_02\\SOURCE_RECONFIRM_CONTROLLER";
   selected.sourcePath=selected.deviceInstanceId;selected.productId=msi?0x1902:0x028e;
   selected.physicalIdentity=selected.deviceInstanceId+"|"+selected.baseContainerDeviceInstanceId+"|VID_"+std::to_string(selected.vendorId)+"|PID_"+std::to_string(selected.productId);
  }
  {std::lock_guard<std::mutex> owner(g_hidHidePhysicalIdentityMx);g_hidHidePhysicalIdentity=selected;}
  fixtureSourceReconfirmHid=U2W(selected.deviceInstanceId);fixtureSourceReconfirmConnected=true;
  fixtureSourceReconfirmUnknown=variant=="unknown";fixtureSourceReconfirmVirtual=variant=="virtual";
  if(variant=="other-root"||variant=="route-failed-clear")fixtureSourceReconfirmRouteRoot=L"USB\\VID_045E&PID_028E\\OTHER_CONTROLLER";
  unsigned interleavings=0;
  if(variant=="slot-replaced"||variant=="stale-power"||variant=="pending-notification"||variant=="publication-only"||
     variant=="journal-run-changed"||variant=="journal-root-changed"||variant=="journal-inactive"){
   fixtureSourceReconfirmAfterRoute=[&]{++interleavings;
    if(variant=="slot-replaced")fixtureSourceReconfirmSlotReplaced=true;
    else if(variant=="stale-power")g_powerGeneration.fetch_add(1,std::memory_order_acq_rel);
    else if(variant=="pending-notification")restore.notification=g_sleepPowerDispatch.window(PBT_APMSUSPEND,GetTickCount64());
    else if(variant=="publication-only")g_sharedPowerPublicationRevision.fetch_add(1,std::memory_order_acq_rel);
    else {std::lock_guard<InputHostMutex> owner(g_inputHostMx);
     if(variant=="journal-run-changed")g_inputHostRunId="source-reconfirm-successor-run";
     if(variant=="journal-root-changed")g_hidHideVisibilityJournal.physical.baseContainerDeviceInstanceId="USB\\VID_045E&PID_028E\\OTHER_JOURNAL_ROOT";
     if(variant=="journal-inactive")g_hidHideVisibilityJournal.active=false;
    }
   };
  }
  fixtureSourceReconfirmCreates=fixtureSourceReconfirmCloses=0;
  fixtureSourceReconfirmPathReads=fixtureSourceReconfirmIndexReads=fixtureSourceReconfirmParents=fixtureSourceReconfirmIds=0;
  unsigned proofCallbacks=0,commitCallbacks=0;bool proofResult=false,clearAcquiredAtProof=false;
  FixtureSourceReconfirmClear clear;
  if(adoption)fixtureSourceReconfirmAfterProof=[&](bool value){
   ++proofCallbacks;proofResult=value;clear.start();
   check(clear.finish(),"actual competing Clear thread returns within the bounded nonblocking observation");
   clearAcquiredAtProof=clear.lastAcquired;
   if(value&&!clearAcquiredAtProof)fixtureSourceReconfirmAfterReconfirmedLog=[&]{
    ++commitCallbacks;
    check(g_xinputPhysicalSourceMask==2&&g_xinputSourceReconfirmMask.load()==0,"valid fallback commits source and pending before the actual post-commit Clear");
    clear.start();check(clear.finish(),"actual post-commit Clear completes before the original outer pending publication");
   };
  };
  fixtureSourceReconfirmBoundary();
  {
   FixtureSourceReconfirmBusyHost busy;
   if(variant=="host-busy"){busy.start();check(busy.acquired(),"another bounded native thread actually owns Host before sampling");}
   fixtureSourceReconfirmScan();
   if(variant=="host-busy")check(busy.held.load()&&!busy.done.load(),"Host remains owned by the other thread throughout the real nonblocking rejection");
  }
  const bool expected=variant=="same-root"||variant=="publication-only"||exact||msi;
  if(adoption){
   check(clear.cleared,"actual proof or post-commit callback has executed Clear before sampling returns");
   if(proofResult&&!clearAcquiredAtProof)check(commitCallbacks==1&&clear.maskBeforeClear==2&&clear.pendingBeforeClear==0,"retained guards defer Clear until the real source and pending commit, before the old tail");
   check(proofCallbacks==1&&proofResult==(variant=="post-proof-clear"),"actual helper result is observed once without replacing true or false");
   check(clear.cleared&&clear.clearCalls==1,"one actual Host Clear retires the controlled already-returned owner");
   std::string retiredRoot;{std::lock_guard<std::mutex> owner(g_hidHidePhysicalIdentityMx);retiredRoot=g_xinputReconfirmPhysicalRoot;}
   check(!g_xinputPhysicalSourceLocked&&g_xinputPhysicalSourceMask==0&&
    g_xinputPreTargetSlots==0&&g_xinputSourceReconfirmMask.load()==0&&retiredRoot.empty(),
    "completed Clear wins over old source adoption and old pending publication");
  }else check(g_xinputPhysicalSourceMask==(expected?2u:0u)&&g_xinputAdmittedMask==(expected?2u:0u)&&
    g_xinputSourceReconfirmMask.load()==(expected?0u:2u),
   "actual drop/reconfirm and mask publication restore only a current proven source");
  check(!fixtureSourceReconfirmSetOpen&&fixtureSourceReconfirmCreates==fixtureSourceReconfirmCloses&&fixtureSourceReconfirmCreates<=1,
   "each actual route query closes exactly its own SetupAPI set once");
#ifdef YMCC_FIXTURE_SOURCE_RECONFIRM_COLLECTION
  if(!adoption&&!exact&&!msi&&variant!="unknown"&&variant!="virtual"&&variant!="host-busy"){
   check(fixtureSourceReconfirmCreates==1&&fixtureSourceReconfirmCloses==1&&fixtureSourceReconfirmParents>=2&&
     fixtureSourceReconfirmIds>=3&&fixtureSourceReconfirmPathReads>=2&&fixtureSourceReconfirmIndexReads>=(variant=="other-root"?1u:2u),
    "real helper traverses XUSB to current HID to USB root and rechecks path index");
   if(variant!="same-root"&&variant!="other-root")check(interleavings==1,"actual route boundary consumed the selected one-shot fact change");
  }
#endif
  if(exact||msi)check(fixtureSourceReconfirmCreates==0,"original exact identity and managed MSI peer retain their original admission basis");
  if(adoption)cases.back()["sourceAdoption"]={{"variant",variant},{"actualHelperProofCallbacks",proofCallbacks},
   {"actualHelperResult",proofResult},{"clearAcquiredAtProof",clearAcquiredAtProof},{"commitCallbacks",commitCallbacks},{"actualClearCalls",clear.clearCalls},
   {"clearThreadAttempts",clear.attempts},{"maskBeforeActualClear",clear.maskBeforeClear},{"pendingBeforeActualClear",clear.pendingBeforeClear},
   {"primaryBeforeActualClear",clear.primaryBeforeClear},{"finalPrimary",g_xinputPrimarySlot},{"finalSourceMask",g_xinputPhysicalSourceMask},
   {"finalPending",g_xinputSourceReconfirmMask.load()},{"actualThreadAndClear",true},{"fullOffChainExecuted",false}};
  cases.back()["sourceReconfirm"]={{"variant",variant},{"slot",1},{"releasedMask",releasedMask},{"releasedAdmittedMask",releasedAdmitted},
   {"releasedPending",releasedPending},{"droppedIdentity",droppedIdentity},{"returnedIdentity",selected.physicalIdentity},
   {"sourceMask",g_xinputPhysicalSourceMask},{"admittedMask",g_xinputAdmittedMask},{"pendingMask",g_xinputSourceReconfirmMask.load()},
   {"expectedMask",expected?2:0},{"setupCreates",fixtureSourceReconfirmCreates},{"setupCloses",fixtureSourceReconfirmCloses},
   {"pathReads",fixtureSourceReconfirmPathReads},{"indexReads",fixtureSourceReconfirmIndexReads},{"parentReads",fixtureSourceReconfirmParents},
   {"deviceIdReads",fixtureSourceReconfirmIds},{"interleavings",interleavings},{"productionDiscoveryDropReconfirmAndMask",true},
   {"helperResultInjected",false},{"deviceOrChildProcessStarted",false}};
  cases.back()["events"]=fixtureEvents;cases.back()["passed"]=failures.size()==failureBefore;
 }
}

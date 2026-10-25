// Only SetupAPI leaf calls from the exact production PS endpoint reader are
// controlled. No HID is opened and no driver/device/installed app is touched.
static bool fixturePsProbeActive=false,fixturePsOpenFails=false,fixturePsEndSeen=false;
static DWORD fixturePsEndError=ERROR_NO_MORE_ITEMS;
static int fixturePsSizeFailure=-1,fixturePsDetailFailure=-1;
static unsigned fixturePsEnumCalls=0,fixturePsDestroyCalls=0,fixturePsOpenCalls=0;
static std::vector<std::wstring> fixturePsPaths;
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
 if(!fixturePsProbeActive&&!returnRead){++forbiddenIo;throw std::logic_error("unadmitted-PS-SetupAPI-boundary");}
 if(returnRead&&!fixturePsReturnManualTopology&&!fixturePsAutoReturnTopology()){SetLastError(ERROR_NOT_READY);return INVALID_HANDLE_VALUE;}
 ++fixturePsOpenCalls;
 if(fixturePsBeforeRead){auto invoke=std::move(fixturePsBeforeRead);fixturePsBeforeRead={};invoke();}
 if(fixturePsOpenFails){SetLastError(ERROR_ACCESS_DENIED);return INVALID_HANDLE_VALUE;}
 return reinterpret_cast<HDEVINFO>(static_cast<uintptr_t>(0x5053));
}
static BOOL fixturePsEnum(HDEVINFO set,PSP_DEVINFO_DATA,const GUID*,DWORD index,PSP_DEVICE_INTERFACE_DATA data){
 if((!fixturePsProbeActive&&!(fixtureFullPipe&&g_psEndpointOwnership.retirementRequired()))||set!=reinterpret_cast<HDEVINFO>(static_cast<uintptr_t>(0x5053))){++forbiddenIo;throw std::logic_error("unknown-PS-enumeration-handle");}
 ++fixturePsEnumCalls;if(index>=fixturePsPaths.size()){fixturePsEndSeen=true;SetLastError(fixturePsEndError);return FALSE;}
 data->Reserved=index;return TRUE;
}
static BOOL fixturePsDetail(HDEVINFO,PSP_DEVICE_INTERFACE_DATA data,PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail,DWORD bytes,PDWORD required,PSP_DEVINFO_DATA info){
 const auto index=static_cast<size_t>(data->Reserved);if(index>=fixturePsPaths.size()){++forbiddenIo;throw std::logic_error("PS-detail-index-out-of-range");}
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
static CONFIGRET fixturePsDeviceId(DEVINST node,PWSTR value,ULONG chars,ULONG){
 ++fixturePsIdCalls;std::wstring id;size_t index=0;
 if(node>=0x50530000&&node<0x50530000+fixturePsPaths.size()){
  index=node-0x50530000;if(static_cast<int>(index)==fixturePsIdFailure)return CR_FAILURE;
  const auto root=fixturePsRootForPath(index);id=L"HID"+root.substr(root.find(L'\\'));
  if(fixturePsPaths[index].find(L"NEW-HID-SAME-ROOT")!=std::wstring::npos)id+=L"_REENUM";
 }else if(node>=0x50540000&&node<0x50540000+fixturePsPaths.size())id=fixturePsRootForPath(node-0x50540000);
 else if(fixturePsDepthLimit&&node>=0x50600000&&node<0x50610000)id=L"ROOT\\OFFLINE_DEPTH_"+std::to_wstring(node);
 else{++forbiddenIo;throw std::logic_error("unadmitted-PS-CM-instance");}
 if(chars<=id.size())return CR_BUFFER_SMALL;memcpy(value,id.c_str(),(id.size()+1)*sizeof(wchar_t));return CR_SUCCESS;
}
static CONFIGRET fixturePsParent(PDEVINST parent,DEVINST node,ULONG){
 ++fixturePsParentCalls;
 if(node>=0x50530000&&node<0x50530000+fixturePsPaths.size()){
  const auto index=node-0x50530000;if(static_cast<int>(index)==fixturePsParentFailure)return CR_ACCESS_DENIED;
  *parent=fixturePsParentCycle?node:fixturePsDepthLimit?0x50600000+index*32:0x50540000+index;return CR_SUCCESS;
 }
 if(fixturePsDepthLimit&&node>=0x50600000&&node<0x50610000){*parent=node+1;return CR_SUCCESS;}
 if(node>=0x50540000&&node<0x50540000+fixturePsPaths.size())return CR_NO_SUCH_DEVNODE;
 ++forbiddenIo;throw std::logic_error("unadmitted-PS-CM-parent");
}

struct FixturePsReadScope {bool prior=fixturePsProbeActive;FixturePsReadScope(){fixturePsProbeActive=true;}~FixturePsReadScope(){fixturePsProbeActive=prior;}};
static void fixturePsReadConfig(std::vector<std::wstring> paths,DWORD end=ERROR_NO_MORE_ITEMS){fixturePsPaths=std::move(paths);fixturePsOpenFails=false;fixturePsEndError=end;fixturePsSizeFailure=fixturePsDetailFailure=fixturePsParentFailure=fixturePsIdFailure=-1;fixturePsParentCycle=fixturePsDepthLimit=false;}
static void psEndpointProbeCases(){
 cases.push_back({{"name","PS-WAKE/read-only-endpoint-enumeration"}});json rows=json::array();
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
 cases.back()["ownershipSnapshotSource"]="accepted actual prior Host PREPARE/RECREATE reply, read-only fixture input only";cases.back()["endpointReads"]=rows;cases.back()["actualProductionReaderAndDecision"]=true;cases.back()["controlledSetupApiLeaves"]=true;
}

// Controlled physical slot and SDK-created Elite slot for the complete native
// Xbox startup verifier. Exact target paths/PnP IDs match the managed fixture.
static const std::wstring fixtureEliteRoot=L"SWD\\HIDMAESTRO_VID_045E_PID_0B00&IG_00\\OFFLINE_ELITE";
static const std::wstring fixtureEliteHid=L"HID\\HIDMAESTRO_VID_045E_PID_0B00&IG_00\\OFFLINE_ELITE";
static const std::wstring fixtureElitePath=L"\\\\?\\HID#HIDMAESTRO_VID_045E_PID_0B00&IG_00#OFFLINE_ELITE";
static const std::wstring fixturePhysicalPath=L"\\\\?\\HID#VID_1234&PID_5678#OFFLINE_PHYSICAL";
static bool fixtureElitePrepared(){return fixtureXinputSourceLive&&g_inputHostPrepared&&inputHostConfiguredPersona()=="elite";}
static DWORD fixtureControlledXinputState(DWORD slot,XINPUT_STATE* state){
 ++inertXinputReads;if(!fixtureXinputSourceLive||slot>=4)return ERROR_DEVICE_NOT_CONNECTED;++fixtureXinputLivePolls[slot];
 if(!state||(slot!=0&&!(slot==1&&fixtureElitePrepared())))return ERROR_DEVICE_NOT_CONNECTED;*state={};state->dwPacketNumber=1;return ERROR_SUCCESS;
}
static std::wstring fixtureControlledXinputPath(DWORD slot){++inertXinputReads;if(!fixtureXinputSourceLive)return {};if(slot==0)return fixturePhysicalPath;if(slot==1&&fixtureElitePrepared())return fixtureElitePath;return {};}
static BYTE fixtureControlledXinputIndex(const std::wstring& path){if(!fixtureXinputSourceLive)return 0xFF;return path==fixturePhysicalPath?0:path==fixtureElitePath&&fixtureElitePrepared()?1:0xFF;}
static HDEVINFO fixtureXboxCreate(const GUID*,HWND){if(!fixtureElitePrepared()){++forbiddenIo;throw std::logic_error("unadmitted-Elite-identity-query");}return reinterpret_cast<HDEVINFO>(static_cast<uintptr_t>(0x454c4954));}
static BOOL fixtureXboxOpen(HDEVINFO set,PCWSTR path,DWORD,PSP_DEVICE_INTERFACE_DATA iface){if(set!=reinterpret_cast<HDEVINFO>(static_cast<uintptr_t>(0x454c4954)))throw std::logic_error("foreign-Elite-identity-handle");if(!path||fixtureElitePath!=path)return FALSE;iface->Flags=SPINT_ACTIVE;return TRUE;}
static BOOL fixtureXboxDetail(HDEVINFO,PSP_DEVICE_INTERFACE_DATA,PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail,DWORD bytes,PDWORD needed,PSP_DEVINFO_DATA info){
 const DWORD size=static_cast<DWORD>(offsetof(SP_DEVICE_INTERFACE_DETAIL_DATA_W,DevicePath)+(fixtureElitePath.size()+1)*sizeof(wchar_t));if(!detail){if(needed)*needed=size;SetLastError(ERROR_INSUFFICIENT_BUFFER);return FALSE;}if(bytes<size)return FALSE;memcpy(detail->DevicePath,fixtureElitePath.c_str(),(fixtureElitePath.size()+1)*sizeof(wchar_t));if(info)info->DevInst=0x454c4954;return TRUE;
}
static BOOL fixtureXboxDestroy(HDEVINFO){return TRUE;}
static CONFIGRET fixtureXboxDeviceId(DEVINST node,PWSTR value,ULONG chars,ULONG){if(node!=0x454c4954||chars<=fixtureEliteHid.size())return CR_NO_SUCH_DEVNODE;memcpy(value,fixtureEliteHid.c_str(),(fixtureEliteHid.size()+1)*sizeof(wchar_t));return CR_SUCCESS;}
static CONFIGRET fixtureXboxLocate(PDEVINST,DEVINSTID_W,ULONG){return CR_NO_SUCH_DEVNODE;}
static CONFIGRET fixtureXboxStatus(PULONG,PULONG,DEVINST,ULONG){return CR_NO_SUCH_DEVNODE;}

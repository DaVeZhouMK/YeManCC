#include "memory-boundaries.h"
#include "identity-source.generated.h"

namespace {
unsigned checks=0;
void require(bool condition,const std::string& message){++checks;if(!condition)throw std::runtime_error(message);}
HANDLE rawHandle(std::uintptr_t n=0x55){return reinterpret_cast<HANDLE>(n);}
std::uintptr_t cacheKey(){return reinterpret_cast<std::uintptr_t>(rawHandle());}
void reset(){
    hidHideInvalidateRawInputIdentityCache();memory::nodes.clear();memory::devices.clear();memory::rawPaths.clear();
    memory::opens=memory::closes=memory::preps=memory::frees=memory::xusb=memory::hit=memory::discards=memory::routeRefreshes=memory::enumerations=memory::selectedTransitions=memory::msiNotifications=0;
    memory::now=100;memory::error=0;memory::route=L"xusb-original";memory::routeHook={};g_machineIdentity={};g_hidHidePhysicalIdentity={};
    memory::nodes[1]={"HID\\VID_17EF&PID_6182&COL01\\PHYSICAL",2};
    memory::nodes[2]={"USB\\VID_17EF&PID_6182\\OWNER",0};
    memory::devices.push_back({L"\\?\\hid#vendor-motion",1});memory::rawPaths[rawHandle()]=memory::devices[0].path;
}
SP_DEVINFO_DATA info(){SP_DEVINFO_DATA data;data.cbSize=sizeof(data);data.DevInst=1;return data;}
bool interfaceCall(bool capture,HidHidePhysicalIdentity& out,HidResolveReject& reject,CONFIGRET*error=nullptr,bool*depth=nullptr,std::string*top=nullptr,bool*live=nullptr){
    auto data=info();return hidHideResolveInterfaceIdentity(memory::devices[0].path,&data,memory::set(),out,&reject,nullptr,error,depth,top,live,capture);}
HidHidePhysicalIdentity physical(bool capture=true){HidHidePhysicalIdentity out;HidResolveReject reject;require(interfaceCall(capture,out,reject),"fixture physical source was rejected");return out;}
void setModel(const std::string& name,bool gos){g_machineIdentity.deviceClass="HandheldCompanion.Devices."+name;auto&d=memory::devices[0];d.vid=gos?0x1A86:0x17EF;d.pid=gos?0xE310:0x6182;}
void testDefaultVendorRejects(){reset();auto data=info();HidHidePhysicalIdentity out;out.resolved=true;HidResolveReject reject=HidResolveReject::None;
    require(!hidHideResolveInterfaceIdentity(memory::devices[0].path,&data,memory::set(),out,&reject),"default scope must reject FFA0 vendor collection");
    require(reject==HidResolveReject::NotGamepad&&!out.resolved&&out.physicalIdentity.empty(),"default vendor must be actual NotGamepad without identity");
    require(memory::opens==1&&memory::closes==1&&memory::preps==1&&memory::frees==1&&memory::xusb==0,"default rejection did not run/clean real interface predicate");
    std::vector<HidHidePhysicalIdentity> candidates;bool complete=false;
    require(!hidHideResolvePhysicalIdentities(candidates,&complete)&&candidates.empty()&&complete,"default enumeration must not offer vendor hide candidates");
    auto accepted=physical();const auto gen=g_hidResolveCacheGeneration.load();
    require(executeProductionCacheFill(true,cacheKey(),HidResolveKind::Physical,accepted,ascii_lower(W2U(memory::devices[0].path)),memory::now,gen),"capture fill failed");
    require(executeProductionCacheFill(false,cacheKey(),HidResolveKind::Unsupported,{},ascii_lower(W2U(memory::devices[0].path)),memory::now,gen),"default fill failed");
    g_hidHidePhysicalIdentity.physicalIdentity="unchanged-selected";hidHideObserveRawInputDevice(rawHandle());
    require(g_hidHidePhysicalIdentity.physicalIdentity=="unchanged-selected"&&memory::selectedTransitions==0&&memory::msiNotifications==0,"default Unsupported must not enter selected publication");
}
void testExactFourModels(){
    const std::array<std::pair<const char*,bool>,4> models{{{"LegionGoTablet",false},{"LegionGoTablet2",false},{"LegionGoSZ1",true},{"LegionGoSZ2",true}}};
    const std::array<USHORT,8> tablet{0x6182,0x6183,0x6184,0x6185,0x61EB,0x61EC,0x61ED,0x61EE};const std::array<USHORT,2> gos{0xE310,0xE311};
    for(const auto& model:models){const std::vector<USHORT> pids=model.second?std::vector<USHORT>(gos.begin(),gos.end()):std::vector<USHORT>(tablet.begin(),tablet.end());
        for(const auto pid:pids){reset();setModel(model.first,model.second);memory::devices[0].pid=pid;HidHidePhysicalIdentity out;HidResolveReject reject;
            require(interfaceCall(true,out,reject)&&out.resolved&&reject==HidResolveReject::None,"exact Legion model VIDPID FFA0/1 rejected");
            require(out.vendorId==memory::devices[0].vid&&out.productId==pid&&out.usagePage==0xFFA0&&out.usage==1&&!out.baseContainerDeviceInstanceId.empty(),"accepted capture identity fields missing");
            require(memory::xusb==0&&out.baseContainerDevicePath.empty(),"capture-only identity must not resolve XUSB");
            require(!interfaceCall(false,out,reject)&&reject==HidResolveReject::NotGamepad,"explicit false leaked vendor capture permission");}
        for(unsigned wrong=0;wrong<5;++wrong){reset();setModel(model.first,model.second);auto&d=memory::devices[0];if(wrong==0)d.vid=0x045E;if(wrong==1)d.pid=0xffff;if(wrong==2)d.page=0xFFA1;if(wrong==3)d.usage=2;if(wrong==4){d.vid=model.second?0x17EF:0x1A86;d.pid=model.second?0x6182:0xE310;}
            HidHidePhysicalIdentity out;HidResolveReject reject;require(!interfaceCall(true,out,reject)&&reject==HidResolveReject::NotGamepad,"wrong VID/PID/page/usage/family crossed capture predicate");}}
    for(const auto name:{"LegionGo","LegionGoTabletExtra","legiongotablet","LegionGoSZ3","ROGAlly",""}){reset();g_machineIdentity.deviceClass=std::string("HandheldCompanion.Devices.")+name;HidHidePhysicalIdentity out;HidResolveReject reject;require(!interfaceCall(true,out,reject)&&reject==HidResolveReject::NotGamepad,"non-exact model admitted vendor scope");}
    reset();g_machineIdentity.family=YmccFamily::Other;HidHidePhysicalIdentity out;HidResolveReject reject;require(!interfaceCall(true,out,reject)&&reject==HidResolveReject::NotGamepad,"wrong machine family admitted vendor scope");
}
void testVirtualMarkers(){for(const auto marker:{"HIDMAESTRO","hm-ctl","ViGeM"})for(bool base:{false,true}){reset();memory::nodes[base?2:1].id+='\\'+std::string(marker);HidHidePhysicalIdentity out;HidResolveReject reject;
    require(!interfaceCall(true,out,reject)&&reject==HidResolveReject::VirtualMarker&&!out.resolved,"capture vendor must still reject actual virtual marker predicate");}}
void testAncestorEvidence(){
    for(unsigned kind=0;kind<4;++kind){reset();memory::nodes[2].id=kind==3?"ROOT\\UNKNOWN\\0":"HTREE\\ROOT\\0";if(kind==1)memory::nodes[1].parentResult=CR_INVALID_DEVINST;if(kind==2)memory::nodes[1].parent=1;if(kind==3)memory::nodes[2].statusResult=CR_INVALID_DEVINST;
        HidHidePhysicalIdentity out;HidResolveReject reject;CONFIGRET error=0;bool depth=false,live=false;std::string top;
        require(!interfaceCall(true,out,reject,&error,&depth,&top,&live)&&!out.resolved,"capture must reject missing/unproven base ancestor");
        require(reject==(kind==0?HidResolveReject::NoBaseContainer:HidResolveReject::ProbeFailed),"ancestor complete/incomplete evidence classification changed");
        if(kind==0)require(top=="HTREE\\ROOT\\0"&&live&&error==CR_NO_SUCH_DEVNODE,"live root witness not produced by real ancestor walker");
        if(kind==2)require(depth,"ancestor cycle must report real depth limit");}
    reset();memory::nodes[2]={"USB\\VID_17EF&PID_6182&MI_00\\CHILD",3};memory::nodes[3]={"USB\\VID_17EF&PID_6182\\REAL-OWNER",0};
    auto out=physical();require(out.baseContainerDeviceInstanceId=="USB\\VID_17EF&PID_6182\\REAL-OWNER","real ancestor walker selected qualified child instead of owning base");
}
void testProbeFailures(){for(unsigned kind=0;kind<4;++kind){reset();auto&d=memory::devices[0];if(kind==0)d.open=false;if(kind==1)d.attributes=false;if(kind==2)d.preparsed=false;if(kind==3)d.caps=false;HidHidePhysicalIdentity out;HidResolveReject reject;
    require(!interfaceCall(true,out,reject)&&!out.resolved,"failed boundary probe admitted identity");require(reject==(kind==0?HidResolveReject::OpenFailed:HidResolveReject::ProbeFailed),"probe rejection kind changed");require(memory::preps==memory::frees,"preparsed data was not freed");}}
void fillBoth(){auto identity=physical();const auto generation=g_hidResolveCacheGeneration.load();const auto path=ascii_lower(W2U(memory::devices[0].path));
    require(executeProductionCacheFill(false,cacheKey(),HidResolveKind::Unsupported,{},path,memory::now,generation),"default Unsupported fill failed");
    require(executeProductionCacheFill(true,cacheKey(),HidResolveKind::Physical,identity,path,memory::now,generation),"capture Physical fill failed");}
void testCacheNamespaces(){reset();fillBoth();const auto probes=memory::opens;HidHidePhysicalIdentity out;std::uint64_t gen=0;
    require(!hidHideResolveRawInputIdentity(rawHandle(),out,&gen)&&!out.resolved&&gen!=0,"default must hit its Unsupported cache independently");
    require(hidHideResolveRawInputIdentity(rawHandle(),out,&gen,true)&&out.resolved,"capture must hit its Physical cache independently");
    require(!hidHideResolveRawInputIdentity(rawHandle(),out,&gen)&&!out.resolved,"capture hit contaminated later default cache");
    require(memory::hit==3&&memory::opens==probes&&g_hidResolveCache.at(cacheKey()).kind==HidResolveKind::Unsupported&&g_legionMotionResolveCache.at(cacheKey()).kind==HidResolveKind::Physical,"same HANDLE cache namespace isolation failed");
    memory::rawPaths[rawHandle()]=L"different-path";require(!hidHideResolveRawInputIdentity(rawHandle(),out,&gen,true)&&gen==0,"reused HANDLE with different path hit old identity");
    g_machineIdentity.deviceClass="unknown";memory::rawPaths[rawHandle()]=memory::devices[0].path;require(!hidHideResolveRawInputIdentity(rawHandle(),out,&gen,true)&&gen==0,"capture route off used previously physical cache");}
void testInvalidateAndFillFence(){reset();fillBoth();g_hidResolveDiagKeys.insert("old");const auto old=g_hidResolveCacheGeneration.load();hidHideInvalidateRawInputIdentityCache();
    require(g_hidResolveCache.empty()&&g_legionMotionResolveCache.empty()&&g_hidResolveDiagKeys.empty(),"invalidate must clear both scope namespaces and diagnostics");
    require(g_hidResolveCacheGeneration==old+1&&!hidHideResolvedGenerationStillCurrent(old)&&!hidHideResolvedGenerationStillCurrent(0)&&hidHideResolvedGenerationStillCurrent(old+1),"invalidate generation fence failed");
    auto identity=physical();const auto path=ascii_lower(W2U(memory::devices[0].path));
    for(bool capture:{false,true})require(!executeProductionCacheFill(capture,cacheKey(),HidResolveKind::Physical,identity,path,memory::now,old),"stale resolver generation was filled into new scope cache");
    require(g_hidResolveCache.empty()&&g_legionMotionResolveCache.empty(),"stale fill repopulated a namespace");
    require(executeProductionCacheFill(true,cacheKey(),HidResolveKind::Physical,identity,path,memory::now,old+1),"fresh generation fill failed");
    ++g_hidResolveCacheGeneration;HidHidePhysicalIdentity out;std::uint64_t delivered=99;require(!hidHideResolveRawInputIdentity(rawHandle(),out,&delivered,true)&&!out.resolved&&delivered==0,"cache entry with stale generation delivered");}
void testEvictionNamespaces(){reset();const auto gen=g_hidResolveCacheGeneration.load();auto identity=physical();
    for(std::uintptr_t i=0;i<kHidResolveCacheMaxEntries;++i)for(bool capture:{false,true})require(executeProductionCacheFill(capture,i,HidResolveKind::Physical,identity,"path",100+i,gen),"cache population failed");
    const auto full=kHidResolveCacheMaxEntries;require(executeProductionCacheFill(true,full,HidResolveKind::Physical,identity,"new",500,gen),"capture capacity fill failed");
    require(g_legionMotionResolveCache.size()==full&&!g_legionMotionResolveCache.count(0)&&g_legionMotionResolveCache.count(full)&&g_hidResolveCache.size()==full&&g_hidResolveCache.count(0),"capture capacity must evict only capture namespace");
    require(executeProductionCacheFill(false,full,HidResolveKind::Physical,identity,"new",600,gen),"default capacity fill failed");
    require(g_hidResolveCache.size()==full&&!g_hidResolveCache.count(0)&&g_hidResolveCache.count(full)&&g_legionMotionResolveCache.size()==full&&g_legionMotionResolveCache.count(1),"default capacity must evict only default namespace");
    const auto before=g_hidResolveCache.size();require(executeProductionCacheFill(false,full,HidResolveKind::Physical,identity,"updated",700,gen)&&g_hidResolveCache.size()==before,"updating existing cache key evicted unrelated entry");
}
void testCaptureNoXusbDefaultRefresh(){reset();fillBoth();memory::now=2000;HidHidePhysicalIdentity out;std::uint64_t gen=0;const auto routes=memory::xusb;
    require(hidHideResolveRawInputIdentity(rawHandle(),out,&gen,true),"capture cache hit failed");require(memory::xusb==routes&&memory::routeRefreshes==0,"capture must not refresh XUSB/default route");
    memory::devices[0].page=1;memory::devices[0].usage=5;const auto normal=physical(false);require(memory::xusb==routes+1&&!normal.baseContainerDevicePath.empty(),"normal gamepad default interface XUSB route stopped working");
    require(executeProductionCacheFill(false,cacheKey(),HidResolveKind::Physical,normal,ascii_lower(W2U(memory::devices[0].path)),memory::now,gen),"normal default physical fill failed");
    memory::now+=1000;memory::route=L"xusb-refreshed";require(hidHideResolveRawInputIdentity(rawHandle(),out,&gen)&&out.baseContainerDevicePath=="xusb-refreshed"&&memory::xusb==routes+2&&memory::routeRefreshes==1,"default route refresh no longer follows original cache path");
    reset();memory::nodes[1].id="HID\\VID_17EF&PID_6182&IG_00\\ORIGINAL-XINPUT";auto legacy=physical(false);require(legacy.resolved&&memory::xusb==1,"legacy IG_ default admission changed");}
void testHitGenerationInterleave(){reset();memory::devices[0].page=1;memory::devices[0].usage=5;auto identity=physical(false);const auto old=g_hidResolveCacheGeneration.load();
    require(executeProductionCacheFill(false,cacheKey(),HidResolveKind::Physical,identity,ascii_lower(W2U(memory::devices[0].path)),memory::now,old),"fill failed");memory::now+=1000;
    memory::routeHook=[] {hidHideInvalidateRawInputIdentityCache();};HidHidePhysicalIdentity out;std::uint64_t gen=99;
    require(!hidHideResolveRawInputIdentity(rawHandle(),out,&gen)&&!out.resolved&&gen==0&&memory::discards==1,"invalidated cache snapshot was delivered during XUSB refresh");
    require(g_hidResolveCache.empty()&&g_legionMotionResolveCache.empty()&&g_hidResolveCacheGeneration==old+1,"interleaved invalidation did not fence both scopes");}
} // namespace
int main(int argc,char**argv){const std::vector<std::pair<std::string,std::function<void()>>> cases{
    {"default-vendor-rejection-and-publication",testDefaultVendorRejects},{"four-exact-model-predicate",testExactFourModels},{"virtual-marker-refusal",testVirtualMarkers},
    {"base-ancestor-evidence",testAncestorEvidence},{"boundary-probe-failures",testProbeFailures},{"cache-namespaces",testCacheNamespaces},
    {"invalidate-and-fill-generation-fence",testInvalidateAndFillFence},{"namespace-capacity-eviction",testEvictionNamespaces},{"capture-no-xusb-default-refresh",testCaptureNoXusbDefaultRefresh},
    {"cache-hit-generation-interleave",testHitGenerationInterleave}};
    const std::string filter=argc==3&&std::string(argv[1])=="--case"?argv[2]:"";if(argc!=1&&filter.empty()){std::cerr<<"invalid arguments\n";return 2;}unsigned ran=0;
    for(const auto& item:cases){if(!filter.empty()&&filter!=item.first)continue;try{auto before=checks;item.second();++ran;std::cout<<"PASS "<<item.first<<" checks="<<checks-before<<'\n';}
        catch(const std::exception&e){std::cerr<<"FAIL "<<item.first<<": "<<e.what()<<'\n';return 1;}}
    if(!ran){std::cerr<<"unknown case\n";return 2;}std::cout<<"SUMMARY cases="<<ran<<" checks="<<checks<<'\n';return 0;}

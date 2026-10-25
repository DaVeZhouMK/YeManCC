#define NOMINMAX
#define YMCC_RADIAL_RAW_TOUCH_TEST 1
#include "../native/radial_raw_touch.h"
#include "../deps/json/json.hpp"
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace ymcc::radial::rawtouch;
namespace ymcc::radial::rawtouch {struct ObserverTestAccess {static HWND window(const Observer& o){return o.window_;}static void timer(Observer& o){SendMessageW(o.window_,WM_TIMER,Observer::timerId,0);}};}
using Json=nlohmann::json;
static void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
static Descriptor descriptor(){
 Descriptor d;d.bytes=9;Layout l;l.id=1;Finger f;f.tip=Field{0x0d,0x42,1,1,0,1,true};f.id=Field{0x0d,0x51,1,8,0,255,false};f.x=Field{1,0x30,1,16,0,10000,false};f.y=Field{1,0x31,1,16,0,10000,false};l.fingers={f};l.count=Field{0x0d,0x54,0,8,0,64,false};l.scan=Field{0x0d,0x56,0,8,0,255,false};d.layouts[1]=l;d.ignoredReports.insert(7);
 // The Windows HID parser boundary is explicit. The actual production decoder
 // and packet/session consumer below receive these usage values unchanged.
 d.read=[](const Field& f,std::span<const BYTE> b)->std::optional<ULONG>{if(b.size()!=9)return {};if(f.page==0x0d){if(f.usage==0x42)return b[1];if(f.usage==0x51)return b[2];if(f.usage==0x54)return b[7];if(f.usage==0x56)return b[8];}if(f.page==1&&f.usage==0x30)return ULONG(b[3])|(ULONG(b[4])<<8);if(f.page==1&&f.usage==0x31)return ULONG(b[5])|(ULONG(b[6])<<8);return {};};return d;
}
static std::vector<BYTE> report(bool tip=true,BYTE id=3,USHORT x=3000,USHORT y=4000,BYTE count=1,BYTE scan=2){return {1,BYTE(tip),id,BYTE(x),BYTE(x>>8),BYTE(y),BYTE(y>>8),count,scan};}
static std::vector<BYTE> packet(HANDLE device,const std::vector<std::vector<BYTE>>& reports){
 constexpr size_t base=offsetof(RAWINPUT,data)+offsetof(RAWHID,bRawData);const auto n=reports.empty()?9:reports.front().size();std::vector<BYTE> data(base+n*reports.size());auto* raw=reinterpret_cast<RAWINPUT*>(data.data());raw->header.dwType=RIM_TYPEHID;raw->header.dwSize=static_cast<DWORD>(data.size());raw->header.hDevice=device;raw->data.hid.dwSizeHid=static_cast<DWORD>(n);raw->data.hid.dwCount=static_cast<DWORD>(reports.size());for(size_t i=0;i<reports.size();i++){require(reports[i].size()==n,"fixture report length");std::memcpy(raw->data.hid.bRawData+i*n,reports[i].data(),n);}return data;
}
struct Harness {
 uint64_t now=1000;std::vector<RAWINPUTDEVICE> registered;unsigned registerCalls=0,removeCalls=0,reads=0,describes=0,cleanups=0,fires=0,contacts=0;bool queryOk=true,registerOk=true,removeOk=true,supported=true,allowed=true;
 Context cookie{0x1234,11,12,1};std::vector<BYTE> raw;std::vector<RAWINPUTDEVICE> writes;Observer observer;
 Harness():observer(Api{
  [this](HRAWINPUT h)->std::optional<std::vector<BYTE>>{++reads;if(h!=reinterpret_cast<HRAWINPUT>(this))return {};return raw;},
  [this](HANDLE)->std::optional<Descriptor>{++describes;if(!supported)return {};return descriptor();},
  [this]()->std::optional<std::vector<RAWINPUTDEVICE>>{if(!queryOk)return {};return registered;},
  [this](const RAWINPUTDEVICE& d){writes.push_back(d);if(d.dwFlags&RIDEV_REMOVE){++removeCalls;if(!removeOk)return false;registered.erase(std::remove_if(registered.begin(),registered.end(),[&](const auto& old){return old.usUsagePage==d.usUsagePage&&old.usUsage==d.usUsage;}),registered.end());return true;}++registerCalls;if(!registerOk)return false;registered.push_back(d);return true;},
  [this]{return now;},[this](HWND,UINT,WPARAM,LPARAM){++cleanups;return LRESULT(0);}
 }){}
 bool start(unsigned ms=650){return observer.configure(true,ms,[this]()->std::optional<Context>{if(!allowed)return {};return cookie;},[this]{++fires;},[this]{++contacts;});}
 void send(std::vector<BYTE> bytes,WPARAM mode=RIM_INPUTSINK){raw=std::move(bytes);SendMessageW(ObserverTestAccess::window(observer),WM_INPUT,mode,reinterpret_cast<LPARAM>(this));}
 void frame(const std::vector<BYTE>& r,HANDLE device=reinterpret_cast<HANDLE>(100)){send(packet(device,{r}));}
 void tick(uint64_t at){now=at;ObserverTestAccess::timer(observer);}
};
int main(int argc,char** argv){
 Json result={{"pass",false},{"cases",Json::array()},{"hardwareAccessed",false},{"systemRawInputRegistered",false},{"systemInputInjected",false},{"normalYmccStarted",false},{"scope","Actual production raw-HID decoder, assembly, hold and owned Win32 message consumer; Windows HID/registration boundaries use explicit memory fixtures"}};
 const auto foreground=GetForegroundWindow();POINT cursorBefore{};GetCursorPos(&cursorBefore);
 auto test=[&](const char* name,auto body){try{body();result["cases"].push_back({{"name",name},{"pass",true}});}catch(const std::exception& e){result["cases"].push_back({{"name",name},{"pass",false},{"error",e.what()}});throw;}};
 try{
  test("single standard contact report decodes logical coordinates",[]{auto d=descriptor();require(descriptorValid(d),"descriptor");auto p=decode(d,report());require(p&&p->slots.size()==1&&p->slots[0]&&p->slots[0]->id==3&&std::abs(p->slots[0]->x-.3)<1e-9,"decode");});
  test("truncated report and unknown ID cannot invent a contact",[]{auto d=descriptor();auto b=report();b.pop_back();require(!decode(d,b),"truncation");b=report();b[0]=2;require(!decode(d,b),"unknown");});
  test("invalid tip and out-of-range coordinates are rejected",[]{auto d=descriptor();auto b=report();b[1]=2;require(!decode(d,b),"tip");require(!decode(d,report(true,1,10001)),"range");});
  test("contact count bounds are enforced",[]{require(!decode(descriptor(),report(true,1,2,3,65)),"count");});
  test("signed coordinate and 32-bit unsigned range normalize without overflow",[]{Field signed8{1,0x30,0,8,-100,100,false};require(std::abs(*coordinate(signed8,156)-0.)<1e-9,"signed");Field unsigned32{1,0x30,0,32,0,4294967295LL,false};require(*coordinate(unsigned32,0xffffffff)==1.,"unsigned");require(!coordinate(Field{1,0x30,0,16,4,4,false},4),"zero range");});
  test("tip-up does not require stale X Y data",[]{auto d=descriptor();auto b=report(false,0,65535,65535,0);auto p=decode(d,b);require(p&&!p->slots[0],"tip up");});
  test("value boundary failure cannot become a zero coordinate",[]{auto d=descriptor();d.read=[](const Field&,std::span<const BYTE>)->std::optional<ULONG>{return {};};require(!decode(d,report()),"read failure");});
  test("three serial reports assemble one contact frame",[]{Assembler a;Part p{1,{Contact{1,.1,.2}},3,5,false};require(a.feed(p,0).kind==Assembled::Kind::Pending,"first");p.count=0;p.slots={Contact{2,.2,.3}};require(a.feed(p,10).kind==Assembled::Kind::Pending,"second");p.slots={Contact{3,.3,.4}};auto f=a.feed(p,20);require(f.kind==Assembled::Kind::Complete&&f.contacts.size()==3,"third");});
  test("serial scan mismatch and incomplete-frame timeout are rejected",[]{Assembler a;Part p{1,{Contact{1,.1,.2}},2,5,false};a.feed(p,0);p.count=0;p.scan=6;require(a.feed(p,1).kind==Assembled::Kind::Invalid,"scan");p.count=2;p.scan=5;a.feed(p,0);p.count=0;require(a.feed(p,101).kind==Assembled::Kind::Invalid,"timeout");});
  test("duplicate live contact IDs and unframed continuation are rejected",[]{Assembler a;Part p{1,{Contact{1,.1,.2},Contact{1,.2,.3}},2,{},false};require(a.feed(p,0).kind==Assembled::Kind::Invalid,"duplicates");p.count=0;p.slots={Contact{1,.1,.2}};require(a.feed(p,1).kind==Assembled::Kind::Invalid,"continuation");});
  test("implicit single contact cannot masquerade as a serial multi-touch frame",[]{Assembler a;Part p{0,{Contact{0,.1,.2}},2,{},true};require(a.feed(p,0).kind==Assembled::Kind::Invalid,"implicit multiple");});
  test("hold fires at duration once and rearms after complete release",[]{Hold h;Context c{1,2,3,4};h.frame(1,{{7,.2,.3}},c,100,650);require(!h.take(749,c)&&h.take(750,c)&&!h.take(900,c),"once");h.frame(1,{},c,901,650);h.frame(1,{{8,.2,.3}},c,902,650);require(h.take(1552,c),"rearm");});
  test("movement, changed finger and multi-device contact cancel until all up",[]{Context c{1,2,3,4};Hold h;h.frame(1,{{1,.2,.3}},c,0,650);h.frame(1,{{1,.21,.3}},c,100,650);require(!h.take(650,c),"move");h.frame(1,{},c,700,650);h.frame(1,{{1,.2,.3}},c,800,650);h.frame(2,{{1,.2,.3}},c,900,650);h.frame(2,{},c,1000,650);require(!h.take(1450,c),"second device");h.frame(1,{},c,1500,650);h.frame(1,{{1,.2,.3}},c,1600,650);h.frame(1,{{2,.2,.3}},c,1700,650);require(!h.take(2250,c),"finger swap");});
  test("foreground power unavailable context and late timers never fire",[]{Context c{1,2,3,4};Hold h;h.frame(1,{{1,.2,.3}},c,0,650);auto newer=c;++newer.power;require(!h.take(650,newer),"power");h.reset();h.frame(1,{{1,.2,.3}},c,0,650);require(!h.take(650,{}),"not ready");h.reset();h.frame(1,{{1,.2,.3}},c,0,650);require(!h.take(1651,c),"late");});
  test("existing other HID classes remain registered",[]{Harness h;h.registered={{1,5,RIDEV_INPUTSINK,reinterpret_cast<HWND>(99)}};require(h.start(),"start");require(h.registered.size()==2&&h.writes[0].usUsagePage==0x0d&&h.writes[0].usUsage==4,"only touchscreen");require(h.writes[0].dwFlags==(RIDEV_INPUTSINK|RIDEV_DEVNOTIFY),"passive flags");h.observer.shutdown();require(h.registered.size()==1&&h.registered[0].usUsagePage==1,"preserve");});
  test("another window's touchscreen registration is not replaced",[]{Harness h;h.registered={{0x0d,4,RIDEV_INPUTSINK,reinterpret_cast<HWND>(99)}};require(!h.start()&&h.registerCalls==0&&h.removeCalls==0,"conflict");});
  test("registration query or registration failure creates no accepted observer",[]{Harness h;h.queryOk=false;require(!h.start()&&!h.observer.installed(),"query");h.queryOk=true;h.registerOk=false;require(!h.start()&&!h.observer.installed()&&!ObserverTestAccess::window(h.observer),"register");});
  test("owned WM_INPUT and WM_TIMER consumers summon without promoted mouse packets",[]{Harness h;require(h.start(),"start");h.frame(report());require(h.contacts==1&&h.observer.contactActive(),"raw contact");h.tick(1649);require(!h.fires,"early");h.tick(1650);h.tick(1800);require(h.fires==1&&h.reads==1&&h.cleanups==1,"once");h.frame(report(false,3,0,0,0));require(!h.observer.contactActive(),"up");});
  test("foreground WM_INPUT cleanup is retained",[]{Harness h;require(h.start(),"start");h.send(packet(reinterpret_cast<HANDLE>(100),{report()}),RIM_INPUT);require(h.cleanups==1,"cleanup");});
  test("batched down and up never leave a pending hold",[]{Harness h;require(h.start(),"start");h.send(packet(reinterpret_cast<HANDLE>(100),{report(),report(false,3,0,0,0)}));h.tick(1650);require(!h.fires&&!h.observer.contactActive(),"batch");});
  test("raw packet truncation zero count and overflow count are rejected",[]{for(int mode=0;mode<4;mode++){Harness h;require(h.start(),"start");auto b=packet(reinterpret_cast<HANDLE>(100),{report()});auto* raw=reinterpret_cast<RAWINPUT*>(b.data());if(mode==0){b.pop_back();raw=reinterpret_cast<RAWINPUT*>(b.data());raw->header.dwSize=static_cast<DWORD>(b.size());}else if(mode==1)raw->data.hid.dwCount=0;else if(mode==2)raw->data.hid.dwCount=0xffffffff;else raw->header.hDevice=nullptr;h.send(b);h.tick(1650);require(!h.fires&&h.observer.rejectedReports()>0,"bad packet");}});
  test("ABI padding does not alter decoded report",[]{Harness h;require(h.start(),"start");auto b=packet(reinterpret_cast<HANDLE>(100),{report()});b.resize(b.size()+7);reinterpret_cast<RAWINPUT*>(b.data())->header.dwSize=static_cast<DWORD>(b.size());h.send(b);h.tick(1650);require(h.fires==1,"padding");});
  test("known non-contact report ID leaves the live contact unchanged",[]{Harness h;require(h.start(),"start");h.frame(report());auto other=report();other[0]=7;h.frame(other);h.tick(1650);require(h.fires==1,"auxiliary");});
  test("unsupported descriptor never enables raw summoning",[]{Harness h;h.supported=false;require(h.start(),"start");h.frame(report());h.tick(1650);require(!h.fires&&h.describes==1&&h.observer.reason()=="unsupported-touch-descriptor","unsupported");});
  test("movement and foreground change cancel actual consumer's timer",[]{for(int mode=0;mode<2;mode++){Harness h;require(h.start(),"start");h.frame(report());if(!mode)h.frame(report(true,3,3300,4000));else ++h.cookie.foreground;h.tick(1650);require(!h.fires,"cancel");}});
  test("owned monitor or unavailable power context quarantines a whole contact",[]{Harness h;h.allowed=false;require(h.start(),"start");h.frame(report());h.allowed=true;h.tick(1650);require(!h.fires,"blocked cohort");h.frame(report(false,3,0,0,0));h.now=1700;h.frame(report());h.tick(2350);require(h.fires==1,"fresh contact");});
  test("incomplete raw multi-touch immediately suppresses compatibility source",[]{Harness h;require(h.start(),"start");h.frame(report(true,1,100,200,2));require(h.observer.contactActive()&&h.contacts==1,"suppress");h.tick(1650);require(!h.fires,"incomplete");});
  test("device removal cancels and new handle generation is described afresh",[]{Harness h;require(h.start(),"start");h.frame(report());SendMessageW(ObserverTestAccess::window(h.observer),WM_INPUT_DEVICE_CHANGE,GIDC_REMOVAL,100);h.tick(1650);require(!h.fires,"removed");h.now=1700;h.frame(report());require(h.describes==2,"new description");h.tick(2350);require(h.fires==1,"new contact");});
  test("reconfiguration never rearms an already held finger",[]{Harness h;require(h.start(),"start");h.frame(report());require(h.start(400)&&h.registerCalls==1,"same owner");h.tick(1650);require(!h.fires,"not rearmed");h.frame(report(false,3,0,0,0));h.now=1800;h.frame(report());h.tick(2200);require(h.fires==1,"new duration");});
  test("shutdown cannot remove a later foreign owner",[]{Harness h;require(h.start(),"start");h.registered[0].hwndTarget=reinterpret_cast<HWND>(77);require(h.observer.shutdown()&&h.removeCalls==0&&h.registered.size()==1,"foreign owner");});
  test("failed removal disables actions and remains explicitly retryable",[]{Harness h;require(h.start(),"start");h.frame(report());h.removeOk=false;require(!h.observer.shutdown()&&!h.observer.installed(),"failure receipt");h.tick(1650);require(!h.fires,"disabled");h.removeOk=true;require(h.observer.shutdown()&&!ObserverTestAccess::window(h.observer)&&h.registered.empty(),"retry");});
  test("idle reconfiguration leaves the next fresh contact usable",[]{Harness h;require(h.start(),"start");require(h.start(400),"idle update");h.frame(report());h.tick(1400);require(h.fires==1,"fresh contact after idle configure");});
  test("ownership query failure cannot bless cleanup or destroy its pending owner",[]{Harness h;require(h.start(),"start");h.queryOk=false;require(!h.observer.shutdown()&&!h.observer.installed()&&ObserverTestAccess::window(h.observer),"unknown ownership");h.queryOk=true;require(h.observer.shutdown()&&h.registered.empty(),"query retry");});
  test("native unsupported invalid handle is rejected without device enumeration",[]{require(!systemDescriptor(reinterpret_cast<HANDLE>(-3)),"invalid system handle");});
  // Samples of the user's active desktop are observations, not test-owned
  // state: the user can move or focus an application while this memory-only
  // message consumer runs. This harness calls no cursor/input injection API.
  POINT after{};GetCursorPos(&after);result["userForegroundUnchanged"]=GetForegroundWindow()==foreground;result["userCursorUnchanged"]=after.x==cursorBefore.x&&after.y==cursorBefore.y;result["externalDesktopSamplesAreAcceptanceAssertions"]=false;result["pass"]=true;
 }catch(const std::exception& e){result["error"]=e.what();}
 if(argc>1){std::ofstream file(argv[1]);file<<result.dump(2);}
 std::cout<<Json{{"pass",result["pass"]},{"cases",result["cases"].size()},{"error",result.value("error",std::string{})}}.dump()<<std::endl;return result["pass"].get<bool>()?0:2;
}

#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace ymcc::radial::rawtouch {
constexpr USHORT digitizerPage=0x0d,touchScreenUsage=4;
constexpr size_t maxContacts=64,maxPacketBytes=1024*1024,maxReports=128;
struct Field {
 USHORT page=0,usage=0,link=0,bits=0;int64_t minimum=0,maximum=0;bool button=false;
};
struct Finger {std::optional<Field> x,y,id,tip;};
struct Layout {UCHAR id=0;std::vector<Finger> fingers;std::optional<Field> count,scan;bool valid=true;};
using ReadValue=std::function<std::optional<ULONG>(const Field&,std::span<const BYTE>)>;
struct Descriptor {USHORT bytes=0;std::map<UCHAR,Layout> layouts;std::set<UCHAR> ignoredReports;ReadValue read;};
struct Contact {ULONG id=0;double x=0,y=0;};
struct Part {UCHAR report=0;std::vector<std::optional<Contact>> slots;std::optional<ULONG> count,scan;bool implicitId=false;};
inline std::optional<int64_t> logical(const Field& f,ULONG raw){
 if(!f.bits||f.bits>32||f.minimum>f.maximum)return {};
 int64_t value=raw;
 if(f.bits<32){const uint64_t limit=uint64_t{1}<<f.bits;if(raw>=limit)return {};if(f.minimum<0&&(raw&(ULONG{1}<<(f.bits-1))))value-=limit;}
 else if(f.minimum<0)value=static_cast<int32_t>(raw);
 if(value<f.minimum||value>f.maximum)return {};return value;
}
inline std::optional<double> coordinate(const Field& f,ULONG raw){auto value=logical(f,raw);if(!value||f.minimum==f.maximum)return {};return double(*value-f.minimum)/double(int64_t(f.maximum)-f.minimum);}
inline bool descriptorValid(const Descriptor& d){
 if(!d.bytes||!d.read||d.layouts.empty())return false;
 for(const auto& [id,l]:d.layouts){if(!l.valid||l.fingers.empty()||l.fingers.size()>maxContacts||l.id!=id)return false;
  for(const auto& f:l.fingers)if(!f.x||!f.y||!f.tip||f.x->minimum>=f.x->maximum||f.y->minimum>=f.y->maximum||(!f.id&&l.fingers.size()!=1))return false;
 }return true;
}
inline std::optional<Part> decode(const Descriptor& d,std::span<const BYTE> report){
 if(report.size()!=d.bytes||report.empty())return {};
 const UCHAR id=d.layouts.size()==1&&d.layouts.begin()->first==0?0:report[0];
 const auto found=d.layouts.find(id);if(found==d.layouts.end())return {};
 const auto& layout=found->second;Part p;p.report=id;
 if(layout.count){const auto value=d.read(*layout.count,report);if(!value)return {};const auto n=logical(*layout.count,*value);if(!n||*n<0||*n>maxContacts)return {};p.count=static_cast<ULONG>(*n);}
 if(layout.scan){const auto value=d.read(*layout.scan,report);if(!value)return {};p.scan=value;}
 for(const auto& f:layout.fingers){
  const auto tipRaw=d.read(*f.tip,report);if(!tipRaw)return {};const auto tip=logical(*f.tip,*tipRaw);if(!tip||(*tip!=0&&*tip!=1))return {};
  if(!*tip){p.slots.push_back({});continue;}
  ULONG contactId=0;if(f.id){const auto raw=d.read(*f.id,report);if(!raw)return {};const auto n=logical(*f.id,*raw);if(!n||*n<0||*n>0xffffffffULL)return {};contactId=static_cast<ULONG>(*n);}else p.implicitId=true;
  const auto x=d.read(*f.x,report),y=d.read(*f.y,report);if(!x||!y)return {};
  const auto nx=coordinate(*f.x,*x),ny=coordinate(*f.y,*y);if(!nx||!ny)return {};
  p.slots.push_back(Contact{contactId,*nx,*ny});
 }
 return p;
}
struct Assembled {enum class Kind {Invalid,Pending,Complete};Kind kind=Kind::Invalid;std::vector<Contact> contacts;bool multiple=false;};
class Assembler {
 size_t expected_=0,received_=0;uint64_t started_=0;UCHAR report_=0;std::optional<ULONG> scan_;std::vector<Contact> contacts_;
 Assembled invalid(){clear();return {};}
public:
 void clear(){expected_=received_=0;started_=0;scan_.reset();contacts_.clear();}
 Assembled feed(const Part& p,uint64_t now){
  if(p.slots.empty()||p.slots.size()>maxContacts)return invalid();
  if(!p.count){clear();for(const auto& c:p.slots)if(c)contacts_.push_back(*c);}
  else {
   if(*p.count>maxContacts||(p.implicitId&&*p.count>1))return invalid();
   if(*p.count){clear();expected_=*p.count;started_=now;report_=p.report;scan_=p.scan;}
   else if(!expected_){for(const auto& c:p.slots)if(c)return invalid();return {Assembled::Kind::Complete,{},{}};}
   else if(now<started_||now-started_>100||report_!=p.report||scan_!=p.scan)return invalid();
   const auto remaining=expected_-received_,used=std::min(remaining,p.slots.size());
   for(size_t i=0;i<used;i++)if(p.slots[i])contacts_.push_back(*p.slots[i]);
   for(size_t i=used;i<p.slots.size();i++)if(p.slots[i])return invalid();
   received_+=used;if(received_<expected_)return {Assembled::Kind::Pending,{},expected_>1};
  }
  std::set<ULONG> ids;for(const auto& c:contacts_)if(!ids.insert(c.id).second)return invalid();
  auto result=std::move(contacts_);clear();return {Assembled::Kind::Complete,std::move(result),false};
 }
};
struct Context {
 uintptr_t foreground=0;DWORD process=0,thread=0;uint64_t power=0;
 bool operator==(const Context&)const=default;
};
class Hold {
 std::map<uintptr_t,std::vector<Contact>> frames_;bool blocked_=false,fired_=false,armed_=false;
 uintptr_t device_=0;ULONG id_=0;double x_=0,y_=0;uint64_t deadline_=0;Context context_{};
 size_t count()const{size_t n=0;for(const auto& [_,v]:frames_)n+=v.size();return n;}
 void disarm(){armed_=false;deadline_=0;}
public:
 bool contact()const{return count()!=0;}
 bool blocked()const{return blocked_;}
 std::optional<uint64_t> deadline()const{return armed_&&!blocked_&&!fired_?std::optional<uint64_t>(deadline_):std::nullopt;}
 void reset(){frames_.clear();blocked_=fired_=false;device_=0;disarm();}
 void invalidate(){blocked_=true;disarm();}
 void remove(uintptr_t device){frames_.erase(device);invalidate();if(!count())reset();}
 void frame(uintptr_t device,const std::vector<Contact>& contacts,const std::optional<Context>& context,uint64_t now,unsigned duration){
  frames_[device]=contacts;const auto n=count();if(!n){reset();return;}
  if(n!=1||!context){invalidate();return;}if(blocked_||fired_)return;
  const Contact* selected=nullptr;uintptr_t selectedDevice=0;for(const auto& [key,v]:frames_)if(!v.empty()){selected=&v.front();selectedDevice=key;break;}
  if(!selected||!std::isfinite(selected->x)||!std::isfinite(selected->y)||selected->x<0||selected->x>1||selected->y<0||selected->y>1){invalidate();return;}
  if(!armed_){device_=selectedDevice;id_=selected->id;x_=selected->x;y_=selected->y;context_=*context;deadline_=now+std::clamp(duration,400u,1500u);armed_=true;return;}
  if(selectedDevice!=device_||selected->id!=id_||*context!=context_||std::hypot(selected->x-x_,selected->y-y_)>=.006)invalidate();
 }
 bool take(uint64_t now,const std::optional<Context>& context){
  if(!deadline()||now<deadline_)return false;
  if(!context||*context!=context_||now-deadline_>1000){invalidate();return false;}
  fired_=true;disarm();return true;
 }
};

inline std::optional<Descriptor> systemDescriptor(HANDLE device){
 RID_DEVICE_INFO info{};info.cbSize=sizeof(info);UINT infoSize=sizeof(info);
 if(!device||GetRawInputDeviceInfoW(device,RIDI_DEVICEINFO,&info,&infoSize)==UINT(-1)||info.dwType!=RIM_TYPEHID||info.hid.usUsagePage!=digitizerPage||info.hid.usUsage!=touchScreenUsage)return {};
 UINT bytes=0;if(GetRawInputDeviceInfoW(device,RIDI_PREPARSEDDATA,nullptr,&bytes)==UINT(-1)||!bytes||bytes>maxPacketBytes)return {};
 auto memory=std::make_shared<std::vector<BYTE>>(bytes);UINT size=bytes;
 if(GetRawInputDeviceInfoW(device,RIDI_PREPARSEDDATA,memory->data(),&size)==UINT(-1)||size!=bytes)return {};
 const auto ppd=reinterpret_cast<PHIDP_PREPARSED_DATA>(memory->data());HIDP_CAPS caps{};
 if(HidP_GetCaps(ppd,&caps)!=HIDP_STATUS_SUCCESS||caps.UsagePage!=digitizerPage||caps.Usage!=touchScreenUsage||!caps.InputReportByteLength||caps.NumberInputValueCaps>4096||caps.NumberInputButtonCaps>4096||caps.NumberLinkCollectionNodes>4096)return {};
 std::vector<HIDP_VALUE_CAPS> values(caps.NumberInputValueCaps);USHORT valueCount=caps.NumberInputValueCaps;
 if(valueCount&&HidP_GetValueCaps(HidP_Input,values.data(),&valueCount,ppd)!=HIDP_STATUS_SUCCESS)return {};values.resize(valueCount);
 std::vector<HIDP_BUTTON_CAPS> buttons(caps.NumberInputButtonCaps);USHORT buttonCount=caps.NumberInputButtonCaps;
 if(buttonCount&&HidP_GetButtonCaps(HidP_Input,buttons.data(),&buttonCount,ppd)!=HIDP_STATUS_SUCCESS)return {};buttons.resize(buttonCount);
 std::vector<HIDP_LINK_COLLECTION_NODE> nodes(caps.NumberLinkCollectionNodes);ULONG nodeCount=caps.NumberLinkCollectionNodes;
 if(nodeCount&&HidP_GetLinkCollectionNodes(nodes.data(),&nodeCount,ppd)!=HIDP_STATUS_SUCCESS)return {};nodes.resize(nodeCount);
 auto fingerLink=[&](USHORT link){for(size_t depth=0;link<nodes.size()&&depth<=nodes.size();depth++){const auto& node=nodes[link];if(node.LinkUsagePage==digitizerPage&&node.LinkUsage==0x22)return link;if(!link||node.Parent==link)break;link=node.Parent;}return USHORT(0);};
 Descriptor d;d.bytes=caps.InputReportByteLength;std::map<std::pair<UCHAR,USHORT>,Finger> fingers;
 auto attach=[&](UCHAR id,const Field& f){auto& layout=d.layouts[id];layout.id=id;const auto group=fingerLink(f.link);auto& finger=fingers[{id,group}];
  auto set=[&](std::optional<Field>& slot){if(slot)layout.valid=false;else slot=f;};
  if(f.page==1&&f.usage==0x30)set(finger.x);else if(f.page==1&&f.usage==0x31)set(finger.y);else if(f.page==digitizerPage&&f.usage==0x42)set(finger.tip);else if(f.page==digitizerPage&&f.usage==0x51)set(finger.id);
  else if(f.page==digitizerPage&&f.usage==0x54)set(layout.count);else if(f.page==digitizerPage&&f.usage==0x56)set(layout.scan);
 };
 const std::pair<USHORT,USHORT> usages[]={{1,0x30},{1,0x31},{digitizerPage,0x42},{digitizerPage,0x51},{digitizerPage,0x54},{digitizerPage,0x56}};
 for(const auto& c:values)for(const auto [page,usage]:usages){
  const USHORT lo=c.IsRange?c.Range.UsageMin:c.NotRange.Usage,hi=c.IsRange?c.Range.UsageMax:c.NotRange.Usage;
  if(c.UsagePage!=page||usage<lo||usage>hi)continue;
  if(!c.BitSize||c.BitSize>32||!c.ReportCount||c.ReportCount>unsigned(hi-lo+1))return {};
  attach(c.ReportID,Field{page,usage,c.LinkCollection,c.BitSize,c.LogicalMin,c.LogicalMin>=0?int64_t(static_cast<ULONG>(c.LogicalMax)):int64_t(c.LogicalMax),false});
 }
 for(const auto& c:buttons){const USHORT lo=c.IsRange?c.Range.UsageMin:c.NotRange.Usage,hi=c.IsRange?c.Range.UsageMax:c.NotRange.Usage;if(c.UsagePage==digitizerPage&&lo<=0x42&&hi>=0x42)attach(c.ReportID,Field{digitizerPage,0x42,c.LinkCollection,1,0,1,true});}
 for(auto& [key,f]:fingers)if(f.x||f.y||f.tip||f.id){if(!f.x||!f.y||!f.tip){d.layouts[key.first].valid=false;continue;}d.layouts[key.first].fingers.push_back(f);}
 // Non-contact report IDs are irrelevant to a touch hold; never guess their layout.
 for(auto i=d.layouts.begin();i!=d.layouts.end();)if(i->second.fingers.empty()){d.ignoredReports.insert(i->first);i=d.layouts.erase(i);}else ++i;
 d.read=[memory](const Field& f,std::span<const BYTE> report)->std::optional<ULONG>{
  auto preparsed=reinterpret_cast<PHIDP_PREPARSED_DATA>(memory->data());auto data=reinterpret_cast<PCHAR>(const_cast<BYTE*>(report.data()));
  if(f.button){USAGE usages[64]{};ULONG count=64;if(HidP_GetUsages(HidP_Input,f.page,f.link,usages,&count,preparsed,data,static_cast<ULONG>(report.size()))!=HIDP_STATUS_SUCCESS)return {};return std::find(usages,usages+count,f.usage)!=usages+count?1u:0u;}
  ULONG value=0;if(HidP_GetUsageValue(HidP_Input,f.page,f.link,f.usage,&value,preparsed,data,static_cast<ULONG>(report.size()))!=HIDP_STATUS_SUCCESS)return {};return value;
 };
 if(!descriptorValid(d))return {};return d;
}
struct Api {
 std::function<std::optional<std::vector<BYTE>>(HRAWINPUT)> read;
 std::function<std::optional<Descriptor>(HANDLE)> describe;
 std::function<std::optional<std::vector<RAWINPUTDEVICE>>()> registrations;
 std::function<bool(const RAWINPUTDEVICE&)> registration;
 std::function<uint64_t()> now;
 std::function<LRESULT(HWND,UINT,WPARAM,LPARAM)> cleanup;
};
inline Api systemApi(){return {
 [](HRAWINPUT input)->std::optional<std::vector<BYTE>>{UINT size=0;if(GetRawInputData(input,RID_INPUT,nullptr,&size,sizeof(RAWINPUTHEADER))==UINT(-1)||size<sizeof(RAWINPUTHEADER)||size>maxPacketBytes)return {};std::vector<BYTE> data(size);UINT actual=size;if(GetRawInputData(input,RID_INPUT,data.data(),&actual,sizeof(RAWINPUTHEADER))!=size||actual!=size)return {};return data;},
 systemDescriptor,
 []()->std::optional<std::vector<RAWINPUTDEVICE>>{UINT count=0;if(GetRegisteredRawInputDevices(nullptr,&count,sizeof(RAWINPUTDEVICE))==UINT(-1)||count>1024)return {};if(!count)return std::vector<RAWINPUTDEVICE>{};std::vector<RAWINPUTDEVICE> devices(count);const auto read=GetRegisteredRawInputDevices(devices.data(),&count,sizeof(RAWINPUTDEVICE));if(read==UINT(-1)||read!=count)return {};devices.resize(count);return devices;},
 [](const RAWINPUTDEVICE& device){return RegisterRawInputDevices(&device,1,sizeof(device))!=FALSE;},
 []{return GetTickCount64();},[](HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);}
 };}
class Observer {
#ifdef YMCC_RADIAL_RAW_TOUCH_TEST
 friend struct ObserverTestAccess;
#endif
 static constexpr UINT_PTR timerId=0x5954;
 struct Device {std::optional<Descriptor> descriptor;Assembler frames;bool rawContact=false;};
 Api api_;HWND window_=nullptr;bool registered_=false,enabled_=false;unsigned duration_=650;uint64_t armedDeadline_=0;Hold hold_;
 std::map<HANDLE,Device> devices_;size_t observed_=0,rejected_=0;std::string reason_="disabled";
 std::function<std::optional<Context>()> context_;std::function<void()> summon_,contact_;
 std::optional<Context> context()const{return context_?context_():std::nullopt;}
 std::optional<bool> ownsRegistration()const{const auto devices=api_.registrations();if(!devices)return {};for(const auto& d:*devices)if(d.usUsagePage==digitizerPage&&d.usUsage==touchScreenUsage)return d.hwndTarget==window_;return false;}
 void timer(){const auto deadline=hold_.deadline();if(!deadline){if(window_)KillTimer(window_,timerId);armedDeadline_=0;return;}if(armedDeadline_==*deadline)return;armedDeadline_=*deadline;const auto now=api_.now();SetTimer(window_,timerId,static_cast<UINT>(std::clamp<uint64_t>(*deadline>now?*deadline-now:1,1,1500)),nullptr);}
 void message(HRAWINPUT input){
  if(!enabled_)return;
  const auto bytes=api_.read(input);constexpr size_t header=offsetof(RAWINPUT,data)+offsetof(RAWHID,bRawData);
  if(!bytes||bytes->size()<header){++rejected_;hold_.invalidate();timer();return;}
  const auto* raw=reinterpret_cast<const RAWINPUT*>(bytes->data());const auto& hid=raw->data.hid;
  if(raw->header.dwType!=RIM_TYPEHID||!raw->header.hDevice||raw->header.dwSize!=bytes->size()||!hid.dwSizeHid||!hid.dwCount||hid.dwCount>maxReports||uint64_t(hid.dwSizeHid)*hid.dwCount>bytes->size()-header){++rejected_;hold_.invalidate();timer();return;}
  auto found=devices_.find(raw->header.hDevice);if(found==devices_.end()){if(devices_.size()>=16){++rejected_;hold_.invalidate();timer();return;}found=devices_.emplace(raw->header.hDevice,Device{api_.describe(raw->header.hDevice),{}}).first;}
  auto& device=found->second;if(!device.descriptor){++rejected_;reason_="unsupported-touch-descriptor";return;}
  ++observed_;reason_="raw-touch-observed";
  for(DWORD i=0;i<hid.dwCount;i++){
   if(device.descriptor->ignoredReports.contains(hid.bRawData[size_t(i)*hid.dwSizeHid]))continue;
   const auto part=decode(*device.descriptor,{hid.bRawData+size_t(i)*hid.dwSizeHid,hid.dwSizeHid});if(!part){++rejected_;device.frames.clear();hold_.invalidate();continue;}
   const bool wasContact=contactActive();if(part->count.value_or(0)||std::any_of(part->slots.begin(),part->slots.end(),[](const auto& c){return c.has_value();}))device.rawContact=true;if(!wasContact&&contactActive()&&contact_)contact_();
   const auto frame=device.frames.feed(*part,api_.now());if(frame.kind==Assembled::Kind::Invalid){++rejected_;hold_.invalidate();continue;}
   if(frame.kind==Assembled::Kind::Pending){if(frame.multiple)hold_.invalidate();continue;}
   device.rawContact=!frame.contacts.empty();hold_.frame(reinterpret_cast<uintptr_t>(raw->header.hDevice),frame.contacts,frame.contacts.empty()?std::nullopt:context(),api_.now(),duration_);
  }
  timer();
 }
 static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){
  auto* self=reinterpret_cast<Observer*>(GetWindowLongPtrW(h,GWLP_USERDATA));if(m==WM_NCCREATE){self=static_cast<Observer*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
  if(!self)return DefWindowProcW(h,m,w,l);
  if(m==WM_INPUT){self->message(reinterpret_cast<HRAWINPUT>(l));return self->api_.cleanup(h,m,w,l);}
  if(m==WM_INPUT_DEVICE_CHANGE){self->devices_.erase(reinterpret_cast<HANDLE>(l));self->hold_.remove(static_cast<uintptr_t>(l));self->timer();return 0;}
  if(m==WM_TIMER&&w==timerId){KillTimer(h,timerId);self->armedDeadline_=0;const bool fire=self->hold_.take(self->api_.now(),self->context());if(fire&&self->summon_)self->summon_();self->timer();return 0;}
  return DefWindowProcW(h,m,w,l);
 }
public:
 explicit Observer(Api api=systemApi()):api_(std::move(api)){}
 Observer(const Observer&)=delete;Observer& operator=(const Observer&)=delete;
 bool configure(bool enabled,unsigned duration,std::function<std::optional<Context>()> context,std::function<void()> summon,std::function<void()> contact){
  if(!enabled)return shutdown();duration_=std::clamp(duration,400u,1500u);context_=std::move(context);summon_=std::move(summon);contact_=std::move(contact);
  if(registered_){const auto owner=ownsRegistration();if(!owner){enabled_=false;hold_.invalidate();timer();reason_="touch-registration-query-failed";return false;}if(*owner){enabled_=true;if(contactActive()||hold_.contact())hold_.invalidate();else hold_.reset();timer();return true;}shutdown();reason_="touch-registration-ownership-lost";return false;}
  WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=proc;wc.lpszClassName=L"YMCC_RadialRawTouch";
  if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS){reason_="touch-window-class-unavailable";return false;}
  window_=CreateWindowExW(0,wc.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,this);if(!window_){reason_="touch-window-unavailable";return false;}
  const auto before=api_.registrations();if(!before){shutdown();reason_="touch-registration-query-failed";return false;}
  for(const auto& d:*before)if(d.usUsagePage==digitizerPage&&d.usUsage==touchScreenUsage){shutdown();reason_="touchscreen-class-already-owned";return false;}
  const RAWINPUTDEVICE device{digitizerPage,touchScreenUsage,RIDEV_INPUTSINK|RIDEV_DEVNOTIFY,window_};
  if(!api_.registration(device)){shutdown();reason_="touch-registration-failed";return false;}
  registered_=enabled_=true;reason_="registered-awaiting-touch-report";return true;
 }
 bool installed()const{return registered_&&enabled_;}bool contactActive()const{if(!enabled_)return false;for(const auto& [_,device]:devices_)if(device.rawContact)return true;return false;}
 size_t observedDevices()const{size_t count=0;for(const auto& [_,d]:devices_)if(d.descriptor)++count;return count;}size_t observedReports()const{return observed_;}size_t rejectedReports()const{return rejected_;}const std::string& reason()const{return reason_;}
 bool shutdown(){enabled_=false;if(window_)KillTimer(window_,timerId);hold_.reset();devices_.clear();armedDeadline_=0;context_={};summon_={};contact_={};if(registered_){const auto owner=ownsRegistration();if(!owner){reason_="touch-registration-query-failed";return false;}if(*owner){const RAWINPUTDEVICE remove{digitizerPage,touchScreenUsage,RIDEV_REMOVE,nullptr};if(!api_.registration(remove)){reason_="touch-registration-remove-failed";return false;}}}registered_=false;if(window_)DestroyWindow(window_);window_=nullptr;reason_="disabled";return true;}
 ~Observer(){if(!shutdown()&&window_){SetWindowLongPtrW(window_,GWLP_USERDATA,0);DestroyWindow(window_);window_=nullptr;}}
};
}

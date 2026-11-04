#define _WIN32_WINNT 0x0A00
#define NOMINMAX
#include <initguid.h>
#include "../native/msi_legacy_sensor.h"
#include <iostream>
#include <mutex>
#include <chrono>
#include <thread>
using namespace ymcc::msi_sensor;
struct Shared {
    std::atomic<unsigned> managers{0},sensors{0},reports{0},samples{0},writes{0},permissions{0};
    std::atomic<unsigned> gyroReads{0},accelReads{0};
    std::atomic<DWORD> createdThread{0},destroyedThread{0};
    HANDLE readEntered=CreateEventW(nullptr,TRUE,FALSE,nullptr),release=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    bool block=false,freeze=false,noTimestamp=false,missingAxis=false,denied=false,ambiguous=false,noSensors=false;
    bool sdk=false,standardType=false,typed=false,categoryNull=false,categoryEmpty=false,typedNull=false,duplicateAccel=false,blockType=false;
    HRESULT categoryResult=S_OK,gyroTypeResult=S_OK,accelTypeResult=S_OK;
    std::atomic<unsigned> categoryCalls{0},gyroTypeCalls{0},accelTypeCalls{0};
    std::vector<std::pair<std::string,HRESULT>> diagnostics;
    std::vector<Descriptor> inventoried;
    std::array<int,2> selected{-1,-1};
    std::mutex mx;
    std::vector<Sample> received;
    ~Shared(){CloseHandle(readEntered);CloseHandle(release);}
};
template<class I> struct RefObject : I {
    std::atomic<ULONG> refs{1};
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        if(IsEqualIID(id,__uuidof(I))||IsEqualIID(id,__uuidof(IUnknown))){*out=static_cast<I*>(this);AddRef();return S_OK;}
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ++refs;}
    ULONG STDMETHODCALLTYPE Release() override {const auto n=--refs;if(!n)delete this;return n;}
    virtual ~RefObject()=default;
};
struct Keys final : RefObject<IPortableDeviceKeyCollection> {
    std::vector<PROPERTYKEY> keys;
    HRESULT STDMETHODCALLTYPE GetCount(DWORD* n) override {if(!n)return E_POINTER;*n=static_cast<DWORD>(keys.size());return S_OK;}
    HRESULT STDMETHODCALLTYPE GetAt(DWORD i,PROPERTYKEY* key) override {if(!key)return E_POINTER;if(i>=keys.size())return E_INVALIDARG;*key=keys[i];return S_OK;}
    HRESULT STDMETHODCALLTYPE Add(REFPROPERTYKEY key) override {keys.push_back(key);return S_OK;}
    HRESULT STDMETHODCALLTYPE Clear() override {keys.clear();return S_OK;}
    HRESULT STDMETHODCALLTYPE RemoveAt(DWORD i) override {if(i>=keys.size())return E_INVALIDARG;keys.erase(keys.begin()+i);return S_OK;}
};
struct Report final : RefObject<ISensorDataReport> {
    std::shared_ptr<Shared> state;Kind kind;unsigned sequence;
    Report(std::shared_ptr<Shared> s,Kind k,unsigned seq):state(std::move(s)),kind(k),sequence(seq){++state->reports;}
    ~Report(){--state->reports;}
    HRESULT STDMETHODCALLTYPE GetTimestamp(SYSTEMTIME* out) override {
        if(!out)return E_POINTER;if(state->noTimestamp)return E_FAIL;
        const UINT64 time=134361240000000000ull+(state->freeze?1:sequence)*100000ull;
        FILETIME file{static_cast<DWORD>(time),static_cast<DWORD>(time>>32)};
        return FileTimeToSystemTime(&file,out)?S_OK:E_FAIL;
    }
    HRESULT STDMETHODCALLTYPE GetSensorValue(REFPROPERTYKEY key,PROPVARIANT* out) override {
        if(!out)return E_POINTER;
        if(state->sdk) {
            const DWORD first=kind==Kind::Gyro?10:2;
            if(!IsEqualGUID(key.fmtid,kSdkMotion)||key.pid<first||key.pid>first+2||(state->missingAxis&&key.pid==first+2))return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
            *out=PROPVARIANT{};out->vt=VT_R8;out->dblVal=(kind==Kind::Gyro?10.0:100.0)+key.pid-first;return S_OK;
        }
        if(!IsEqualGUID(key.fmtid,kClawHidMotion)||key.pid<7||key.pid>9||(state->missingAxis&&key.pid==9))return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        *out=PROPVARIANT{};out->vt=VT_R8;out->dblVal=static_cast<double>((kind==Kind::Gyro?10:100)+key.pid);return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSensorValues(IPortableDeviceKeyCollection*,IPortableDeviceValues** out) override {if(out)*out=nullptr;return E_NOTIMPL;}
};
struct Sensor final : RefObject<ISensor> {
    std::shared_ptr<Shared> state;Kind kind;GUID id{};
    Sensor(std::shared_ptr<Shared> s,Kind k,unsigned ordinal):state(std::move(s)),kind(k){id.Data1=ordinal;++state->sensors;}
    ~Sensor(){--state->sensors;}
    HRESULT STDMETHODCALLTYPE GetID(SENSOR_ID* out) override {if(!out)return E_POINTER;*out=id;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetCategory(SENSOR_CATEGORY_ID* out) override {if(!out)return E_POINTER;*out=kAllSensors;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetType(SENSOR_TYPE_ID* out) override {if(!out)return E_POINTER;if(state->standardType){*out=kind==Kind::Gyro?kGyroType:kAccelType;return S_OK;}*out=GUID{};return S_OK;}// EX-style custom type filtered by WinRT.
    HRESULT STDMETHODCALLTYPE GetFriendlyName(BSTR* out) override {if(!out)return E_POINTER;*out=SysAllocString(kind==Kind::Gyro?L"Physical Gyrometer":L"Physical Accelerometer");return *out?S_OK:E_OUTOFMEMORY;}
    HRESULT STDMETHODCALLTYPE GetProperty(REFPROPERTYKEY key,PROPVARIANT* out) override {
        if(!out)return E_POINTER;*out=PROPVARIANT{};
        if(key.pid==13){out->vt=VT_UI4;out->ulVal=4;return S_OK;}
        const wchar_t* text=key.pid==9?(kind==Kind::Gyro?L"Physical Gyrometer":L"Physical Accelerometer"):key.pid==15?L"\\\\?\\HID#VID_8087&PID_0AC2#fixture#{sensor}":nullptr;
        if(!text)return E_NOTIMPL;
        const size_t bytes=(wcslen(text)+1)*sizeof(wchar_t);out->pwszVal=static_cast<wchar_t*>(CoTaskMemAlloc(bytes));
        if(!out->pwszVal)return E_OUTOFMEMORY;memcpy(out->pwszVal,text,bytes);out->vt=VT_LPWSTR;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetProperties(IPortableDeviceKeyCollection*,IPortableDeviceValues** out) override {if(out)*out=nullptr;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetSupportedDataFields(IPortableDeviceKeyCollection** out) override {
        if(!out)return E_POINTER;auto* keys=new Keys;if(state->sdk){const DWORD first=kind==Kind::Gyro?10:2;for(DWORD p=first;p<=first+2;++p)keys->keys.push_back(PROPERTYKEY{kSdkMotion,p});*out=keys;return S_OK;}for(DWORD p=7;p<=9;++p)keys->keys.push_back(PROPERTYKEY{kClawHidMotion,p});*out=keys;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetProperties(IPortableDeviceValues*,IPortableDeviceValues** out) override {++state->writes;if(out)*out=nullptr;return E_ACCESSDENIED;}
    HRESULT STDMETHODCALLTYPE SupportsDataField(REFPROPERTYKEY key,VARIANT_BOOL* out) override {if(!out)return E_POINTER;if(state->sdk){const DWORD first=kind==Kind::Gyro?10:2;*out=IsEqualGUID(key.fmtid,kSdkMotion)&&key.pid>=first&&key.pid<=first+2?VARIANT_TRUE:VARIANT_FALSE;return S_OK;}*out=IsEqualGUID(key.fmtid,kClawHidMotion)&&key.pid>=7&&key.pid<=9?VARIANT_TRUE:VARIANT_FALSE;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetState(SensorState* out) override {if(!out)return E_POINTER;*out=state->denied?SENSOR_STATE_ACCESS_DENIED:SENSOR_STATE_READY;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetData(ISensorDataReport** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        const unsigned count=kind==Kind::Gyro?++state->gyroReads:++state->accelReads;
        if(state->denied)return E_ACCESSDENIED;
        if(state->block){SetEvent(state->readEntered);WaitForSingleObject(state->release,INFINITE);}
        *out=new Report(state,kind,count);return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SupportsEvent(REFGUID,VARIANT_BOOL* out) override {if(!out)return E_POINTER;*out=VARIANT_FALSE;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetEventInterest(GUID** out,ULONG* n) override {if(out)*out=nullptr;if(n)*n=0;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetEventInterest(GUID*,ULONG) override {++state->writes;return E_ACCESSDENIED;}
    HRESULT STDMETHODCALLTYPE SetEventSink(ISensorEvents*) override {++state->writes;return E_ACCESSDENIED;}
};
struct Collection final : RefObject<ISensorCollection> {
    std::vector<ComPtr<ISensor>> sensors;
    HRESULT STDMETHODCALLTYPE GetAt(ULONG i,ISensor** out) override {if(!out)return E_POINTER;*out=nullptr;if(i>=sensors.size())return E_INVALIDARG;return sensors[i].CopyTo(out);}
    HRESULT STDMETHODCALLTYPE GetCount(ULONG* n) override {if(!n)return E_POINTER;*n=static_cast<ULONG>(sensors.size());return S_OK;}
    HRESULT STDMETHODCALLTYPE Add(ISensor*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Remove(ISensor*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE RemoveByID(REFSENSOR_ID) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Clear() override {sensors.clear();return S_OK;}
};
struct Manager final : RefObject<ISensorManager> {
    std::shared_ptr<Shared> state;ComPtr<Collection> collection;
    Manager(std::shared_ptr<Shared> s):state(std::move(s)) {
        ++state->managers;state->createdThread=GetCurrentThreadId();collection.Attach(new Collection);
        if(!state->noSensors){ComPtr<ISensor> gyro;gyro.Attach(new Sensor(state,Kind::Gyro,1));collection->sensors.push_back(gyro);
            if(state->ambiguous){ComPtr<ISensor> extra;extra.Attach(new Sensor(state,Kind::Gyro,3));collection->sensors.push_back(extra);}
            ComPtr<ISensor> accel;accel.Attach(new Sensor(state,Kind::Accel,2));collection->sensors.push_back(accel);
            if(state->duplicateAccel){ComPtr<ISensor> extra;extra.Attach(new Sensor(state,Kind::Accel,4));collection->sensors.push_back(extra);}}
    }
    ~Manager(){state->destroyedThread=GetCurrentThreadId();--state->managers;}
    HRESULT STDMETHODCALLTYPE GetSensorsByCategory(REFSENSOR_CATEGORY_ID,ISensorCollection** out) override {
        if(!out)return E_POINTER;*out=nullptr;++state->categoryCalls;
        if(FAILED(state->categoryResult))return state->categoryResult;
        if(state->categoryNull)return S_OK;
        if(state->categoryEmpty){*out=new Collection;return S_OK;}
        return collection.CopyTo(out);
    }
    HRESULT STDMETHODCALLTYPE GetSensorsByType(REFSENSOR_TYPE_ID type,ISensorCollection** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        const bool gyro=IsEqualGUID(type,kGyroType),accel=IsEqualGUID(type,kAccelType);
        if(!gyro&&!accel)return E_INVALIDARG;
        if(gyro)++state->gyroTypeCalls;else ++state->accelTypeCalls;
        if(!state->typed)return E_NOTIMPL;
        if(state->blockType){SetEvent(state->readEntered);WaitForSingleObject(state->release,INFINITE);}
        const HRESULT result=gyro?state->gyroTypeResult:state->accelTypeResult;
        if(FAILED(result))return result;
        if(state->typedNull)return S_OK;
        ComPtr<Collection> filtered;filtered.Attach(new Collection);
        for(const auto& sensor:collection->sensors){GUID actual{};if(SUCCEEDED(sensor->GetType(&actual))&&IsEqualGUID(actual,type))filtered->sensors.push_back(sensor);}
        return filtered.CopyTo(out);
    }
    HRESULT STDMETHODCALLTYPE GetSensorByID(REFSENSOR_ID,ISensor** out) override {if(out)*out=nullptr;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetEventSink(ISensorManagerEvents*) override {++state->writes;return E_ACCESSDENIED;}
    HRESULT STDMETHODCALLTYPE RequestPermissions(HWND,ISensorCollection*,BOOL) override {++state->permissions;return E_ACCESSDENIED;}
};
static bool await(const std::function<bool()>& ready,unsigned timeout=1500) {
    const ULONGLONG until=GetTickCount64()+timeout;do{if(ready())return true;Sleep(1);}while(GetTickCount64()<until);return ready();
}
static Options options(const std::shared_ptr<Shared>& state,UINT64 generation=1) {
    Options out;out.generation=generation;out.clawHidCompatible=true;
    out.managerFactory=[state](ISensorManager** manager){if(!manager)return E_POINTER;*manager=new Manager(state);return S_OK;};
    out.sample=[state](const Sample& sample){std::lock_guard<std::mutex> lock(state->mx);state->received.push_back(sample);++state->samples;};
    out.diagnostic=[state](const char* event,HRESULT hr){std::lock_guard<std::mutex> lock(state->mx);state->diagnostics.emplace_back(event,hr);};
    out.inventory=[state](const std::vector<Descriptor>& inventory,int gyro,int accel){std::lock_guard<std::mutex> lock(state->mx);state->inventoried=inventory;state->selected={gyro,accel};};
    return out;
}
static std::shared_ptr<Shared> typedState() {
    auto state=std::make_shared<Shared>();state->sdk=state->standardType=state->typed=true;
    state->categoryResult=HRESULT_FROM_WIN32(ERROR_NOT_FOUND);return state;
}
static Options typedOptions(const std::shared_ptr<Shared>& state,UINT64 generation=61) {
    auto out=options(state,generation);out.clawHidCompatible=false;return out;
}
static bool diagnosed(const Shared& state,const char* event,HRESULT hr) {
    for(const auto& entry:state.diagnostics)if(entry.first==event&&entry.second==hr)return true;
    return false;
}
// Called only after the owning worker has drained; expose the actual callbacks,
// not reconstructed readings, in the original test's raw output.
static void traceTyped(const char* scenario,const Shared& state) {
    std::cout<<"EVIDENCE scenario="<<scenario<<" samples="<<state.samples<<" gyroReads="<<state.gyroReads<<" accelReads="<<state.accelReads
        <<" categoryCalls="<<state.categoryCalls<<" gyroTypeCalls="<<state.gyroTypeCalls<<" accelTypeCalls="<<state.accelTypeCalls
        <<" inventory="<<state.inventoried.size()<<" selected="<<state.selected[0]<<","<<state.selected[1]
        <<" managers="<<state.managers<<" sensors="<<state.sensors<<" reports="<<state.reports
        <<" writes="<<state.writes<<" permissions="<<state.permissions<<" createdThread="<<state.createdThread<<" destroyedThread="<<state.destroyedThread<<"\n";
    for(const auto& entry:state.diagnostics)std::cout<<"DIAGNOSTIC scenario="<<scenario<<" phase="<<entry.first<<" hresult="<<entry.second<<"\n";
    std::array<bool,2> shown{};
    for(const auto& sample:state.received) {
        const unsigned slot=sample.kind==Kind::Gyro?0:1;if(shown[slot])continue;shown[slot]=true;
        std::cout<<"SAMPLE scenario="<<scenario<<" kind="<<(slot==0?"gyro":"accel")<<" id="<<guidString(sample.descriptor.id)
            <<" type="<<guidString(sample.descriptor.type)<<" layout="<<sample.layout<<" axes="<<sample.axes[0]<<","<<sample.axes[1]<<","<<sample.axes[2]
            <<" timestamp100ns="<<sample.timestamp100ns<<" generation="<<sample.generation<<" receiptTick="<<sample.receiptTick<<"\n";
    }
}
int main() {
    unsigned cases=0,failed=0;
    auto check=[&](const char* name,bool ok){++cases;if(!ok)++failed;std::cout<<(ok?"PASS ":"FAIL ")<<name<<"\n";};
    {
        auto state=std::make_shared<Shared>();Provider provider;
        check("real provider starts SDK mock transport",provider.start(options(state,21)));
        check("both configured gyro and accel deliver real report triples",await([&]{return state->samples>=2;}));
        check("normal stop drains and settles worker",provider.stop(1500)&&!provider.running());
        bool gyro=false,accel=false,raw=true,generation=true;
        for(const auto& sample:state->received){gyro|=sample.kind==Kind::Gyro;accel|=sample.kind==Kind::Accel;raw&=sample.axes[0]==(sample.kind==Kind::Gyro?17:107)&&sample.axes[2]==(sample.kind==Kind::Gyro?19:109);generation&=sample.generation==21&&sample.timestamp100ns>0;}
        check("actual poll preserves raw axes and records current generation/timestamp",gyro&&accel&&raw&&generation);
        check("COM manager and all sensor/report references released",state->managers==0&&state->sensors==0&&state->reports==0);
        check("COM references released on creating MTA rather than BUS",state->createdThread!=GetCurrentThreadId()&&state->createdThread==state->destroyedThread);
        check("read-only adapter never sets properties/events or prompts permissions",state->writes==0&&state->permissions==0);
    }
    {
        auto state=std::make_shared<Shared>();state->freeze=true;Provider provider;auto opt=options(state);opt.wantAccel=false;
        provider.start(std::move(opt));check("cached timestamp first report accepted",await([&]{return state->samples>=1;}));
        Sleep(120);provider.stop(1500);
        check("repeated GetData of same cached timestamp cannot fake sample rate",state->gyroReads>1&&state->samples==1);
    }
    for(unsigned scenario=0;scenario<3;++scenario) {
        auto state=std::make_shared<Shared>();state->noTimestamp=scenario==0;state->missingAxis=scenario==1;state->denied=scenario==2;
        Provider provider;provider.start(options(state));await([&]{return state->gyroReads>2;});provider.stop(1500);
        check(scenario==0?"missing device timestamp does not manufacture motion":scenario==1?"missing axis does not reuse or manufacture motion":"access denied does not manufacture motion",state->samples==0);
        check("failure cleanup releases SDK objects",state->managers==0&&state->sensors==0&&state->reports==0);
    }
    {
        auto state=std::make_shared<Shared>();state->ambiguous=true;Provider provider;auto opt=options(state);opt.wantAccel=false;
        provider.start(std::move(opt));check("ambiguous actual inventory ends rather than choosing first gyro",await([&]{return !provider.running();}));provider.stop(1500);
        check("ambiguous inventory never opens gyro report or publishes values",state->samples==0&&state->gyroReads==0);
    }
    {
        auto state=std::make_shared<Shared>();state->noSensors=true;Provider provider;provider.start(options(state));
        check("absent sensors terminate the actual adapter cleanly",await([&]{return !provider.running();})&&provider.stop(1500)&&state->samples==0);
    }
    {
        auto blocked=std::make_shared<Shared>();blocked->block=true;Provider provider;provider.start(options(blocked,41));
        check("actual GetData can block independently of BUS",WaitForSingleObject(blocked->readEntered,1500)==WAIT_OBJECT_0);
        const ULONGLONG before=GetTickCount64();const bool settled=provider.stop(20);
        check("uncancellable mock read has bounded stop and stays quarantined",!settled&&provider.running()&&GetTickCount64()-before<500);
        auto fresh=std::make_shared<Shared>();check("quarantined old reader cannot stack new workers",!provider.start(options(fresh,42))&&fresh->managers==0);
        SetEvent(blocked->release);check("retired reader eventually drains when transport releases",await([&]{return !provider.running();})&&provider.stop(1500));
        check("late GetData after stop cannot publish old-generation callback",blocked->samples==0);
        check("retired COM objects are released on their own thread",blocked->managers==0&&blocked->sensors==0&&blocked->reports==0&&blocked->createdThread==blocked->destroyedThread);
        check("resume can start a fresh generation after old owner settles",provider.start(options(fresh,42))&&await([&]{return fresh->samples>=2;}));provider.stop(1500);
        bool current=!fresh->received.empty();for(const auto& sample:fresh->received)current&=sample.generation==42;
        check("resumed generation never carries old owner token",current&&fresh->managers==0&&fresh->sensors==0);
    }
    {
        auto state=std::make_shared<Shared>();Provider provider;auto opt=options(state,51);
        auto generation=std::make_shared<std::atomic<UINT64>>(51);
        auto entered=CreateEventW(nullptr,TRUE,FALSE,nullptr),release=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        auto downstream=opt.sample;
        opt.sample=[generation,entered,release,downstream](const Sample& sample){
            SetEvent(entered);WaitForSingleObject(release,INFINITE);
            // Production's sink rechecks the generation at the actual acceptance
            // point. An already-entered callback is not cancelled by GetData stop.
            if(generation->load()==sample.generation) downstream(sample);
        };
        provider.start(std::move(opt));
        check("sample callback can already be in-flight before stop",WaitForSingleObject(entered,1500)==WAIT_OBJECT_0);
        generation->store(0);const ULONGLONG before=GetTickCount64();const bool settled=provider.stop(20);
        check("blocked in-flight callback is bounded and owner stays quarantined",!settled&&provider.running()&&GetTickCount64()-before<500);
        auto fresh=std::make_shared<Shared>();check("in-flight callback does not allow a second provider",!provider.start(options(fresh,52)));
        SetEvent(release);check("old callback drains only after its own release",await([&]{return !provider.running();})&&provider.stop(1500));
        check("generation invalidation suppresses downstream late delivery",state->samples==0&&state->gyroReads==1&&state->accelReads==0);
        check("blocked callback lifetime retains COM references until owner drains",state->managers==0&&state->sensors==0&&state->reports==0);
        CloseHandle(entered);CloseHandle(release);
        check("fresh generation starts after in-flight callback owner actually exits",provider.start(options(fresh,52))&&await([&]{return fresh->samples>=2;}));provider.stop(1500);
    }
    {
        Provider provider;Options bad;std::atomic<unsigned> errors{0};bad.managerFactory=[](ISensorManager** out){*out=nullptr;return S_OK;};bad.diagnostic=[&](const char*,HRESULT){++errors;};
        provider.start(std::move(bad));check("invalid null COM result fails without dereferencing",await([&]{return !provider.running();})&&provider.stop(1500)&&errors==1);
    }
    {
        auto state=typedState();Provider provider;
        check("category ERROR_NOT_FOUND starts actual typed fallback",provider.start(typedOptions(state,61)));
        check("typed SDK gyro and accel actually publish from poll",await([&]{return state->gyroReads>0&&state->accelReads>0&&state->samples>=2;}));
        check("typed fallback normal stop settles owner",provider.stop(1500)&&provider.stopped());
        bool gyro=false,accel=false,values=true;
        for(const auto& sample:state->received){const bool g=sample.kind==Kind::Gyro;gyro|=g;accel|=!g;values&=sample.axes==(g?std::array<double,3>{10,11,12}:std::array<double,3>{100,101,102})&&sample.layout==(g?"sdk-motion-gyro-dps":"sdk-motion-accel-g")&&sample.generation==61&&sample.timestamp100ns>=134361240000100000ull;}
        check("typed samples retain SDK layout raw axes timestamp and generation",gyro&&accel&&values);
        check("typed complete inventory precedes unique pair selection",state->inventoried.size()==2&&state->selected==std::array<int,2>{0,1}&&state->categoryCalls==1&&state->gyroTypeCalls==1&&state->accelTypeCalls==1);
        check("fallback diagnostics retain category HRESULT and both typed outcomes",diagnosed(*state,"enumeration-category-failed",HRESULT_FROM_WIN32(ERROR_NOT_FOUND))&&diagnosed(*state,"enumeration-gyro-type-result",S_OK)&&diagnosed(*state,"enumeration-accel-type-result",S_OK)&&diagnosed(*state,"enumeration-source-types",S_OK));
        check("typed references drain on the original MTA without writes",state->managers==0&&state->sensors==0&&state->reports==0&&state->createdThread==state->destroyedThread&&state->writes==0&&state->permissions==0);
        traceTyped("typed-success",*state);
    }
    for(unsigned empty=0;empty<2;++empty) {
        auto state=typedState();state->categoryResult=S_OK;state->categoryNull=empty==0;state->categoryEmpty=empty==1;Provider provider;
        provider.start(typedOptions(state));const bool delivered=await([&]{return state->samples>=2;});provider.stop(1500);
        check(empty==0?"successful null category collection uses typed fallback":"successful empty category collection uses typed fallback",delivered&&state->gyroTypeCalls==1&&state->accelTypeCalls==1&&diagnosed(*state,"enumeration-category-empty",S_OK));
    }
    for(HRESULT failure:{E_ACCESSDENIED,E_FAIL}) {
        auto state=typedState();state->categoryResult=failure;Provider provider;provider.start(typedOptions(state));
        const bool ended=await([&]{return !provider.running();})&&provider.stop(1500);
        check("category permission or other failure cannot be hidden by typed fallback",ended&&state->samples==0&&state->gyroTypeCalls==0&&state->accelTypeCalls==0&&diagnosed(*state,"enumeration-failed",failure));
    }
    for(unsigned failure=0;failure<4;++failure) {
        auto state=typedState();if(failure==0){state->gyroTypeResult=E_ACCESSDENIED;state->accelTypeResult=E_FAIL;}
        if(failure==1)state->gyroTypeResult=E_ACCESSDENIED;
        if(failure==2)state->accelTypeResult=E_FAIL;
        if(failure==3)state->typedNull=true;
        Provider provider;provider.start(typedOptions(state));const bool ended=await([&]{return !provider.running();})&&provider.stop(1500);
        const HRESULT gyro=state->typedNull?E_POINTER:state->gyroTypeResult,accel=state->typedNull?E_POINTER:state->accelTypeResult;
        check("failed or null requested typed collection publishes neither partial nor guessed motion",ended&&state->samples==0&&state->gyroReads==0&&state->accelReads==0&&state->gyroTypeCalls==1&&state->accelTypeCalls==1&&diagnosed(*state,"enumeration-gyro-type-result",gyro)&&diagnosed(*state,"enumeration-accel-type-result",accel));
        check("failed typed enumeration releases all COM references",state->managers==0&&state->sensors==0&&state->reports==0);
        traceTyped(failure==0?"typed-both-failed":failure==1?"gyro-type-failed":failure==2?"accel-type-failed":"typed-null",*state);
    }
    {
        auto state=typedState();state->noSensors=true;Provider provider;provider.start(typedOptions(state));
        check("successful empty typed inventories cannot manufacture readings",await([&]{return !provider.running();})&&provider.stop(1500)&&state->samples==0&&state->gyroReads==0&&state->accelReads==0);
    }
    for(bool categorySuccess:{false,true}) {
        auto state=typedState();state->ambiguous=state->duplicateAccel=true;if(categorySuccess)state->categoryResult=S_OK;
        Provider provider;provider.start(typedOptions(state));const bool ended=await([&]{return !provider.running();})&&provider.stop(1500);
        check(categorySuccess?"successful category keeps all duplicate sources and never uses typed":"typed enumeration retains duplicate source ambiguity instead of picking first",ended&&state->inventoried.size()==4&&state->selected==std::array<int,2>{-1,-1}&&state->samples==0&&state->gyroReads==0&&state->accelReads==0&&state->gyroTypeCalls==(categorySuccess?0u:1u)&&state->accelTypeCalls==(categorySuccess?0u:1u));
        traceTyped(categorySuccess?"category-ambiguous":"typed-ambiguous",*state);
    }
    for(bool complete:{true,false}) {
        auto state=typedState();state->ambiguous=true;Provider provider;auto opt=typedOptions(state);GUID gyro{},accel{};gyro.Data1=1;accel.Data1=complete?2:99;opt.explicitPair=std::array<GUID,2>{gyro,accel};
        provider.start(std::move(opt));const bool done=await([&]{return complete?state->samples>=2:!provider.running();});provider.stop(1500);
        bool exact=true;for(const auto& sample:state->received)exact&=sample.descriptor.id.Data1==(sample.kind==Kind::Gyro?1u:2u);
        check(complete?"typed explicit source pair selects only the two exact IDs":"incomplete typed explicit pair cannot fall back to another ID",done&&exact&&(complete?state->samples>=2:state->samples==0&&state->gyroReads==0&&state->accelReads==0));
    }
    for(unsigned invalid=0;invalid<3;++invalid) {
        auto state=typedState();state->freeze=invalid==0;state->noTimestamp=invalid==1;state->missingAxis=invalid==2;Provider provider;auto opt=typedOptions(state);opt.wantAccel=false;
        provider.start(std::move(opt));const bool read=await([&]{return state->gyroReads>=3;});provider.stop(1500);
        check(invalid==0?"typed cached timestamps cannot fake fresh samples":invalid==1?"typed missing timestamp cannot use receipt time":"typed incomplete axes cannot publish partial data",read&&state->samples==(invalid==0?1u:0u)&&state->accelTypeCalls==0);
    }
    {
        auto state=typedState();state->sdk=false;Provider provider;provider.start(typedOptions(state));const bool read=await([&]{return state->gyroReads>=3;});provider.stop(1500);
        check("OneX standard type never borrows MSI private field layout",read&&state->samples==0&&state->inventoried.size()==2);
    }
    for(bool enumeration:{false,true}) {
        auto state=typedState();state->block=!enumeration;state->blockType=enumeration;Provider provider;provider.start(typedOptions(state,71));
        check(enumeration?"typed enumeration can block inside the owned MTA":"typed GetData can block inside the owned MTA",WaitForSingleObject(state->readEntered,1500)==WAIT_OBJECT_0);
        const ULONGLONG before=GetTickCount64();const bool settled=provider.stop(20);
        check("typed blocked operation has bounded stop and retains its owner",!settled&&provider.running()&&GetTickCount64()-before<500);
        auto fresh=typedState();check("typed retired owner prevents a stacked worker",!provider.start(typedOptions(fresh,72))&&fresh->managers==0);
        SetEvent(state->release);const bool drained=await([&]{return !provider.running();})&&provider.stop(1500);
        check("late typed enumeration or report cannot resurrect stopped generation",drained&&state->samples==0&&state->managers==0&&state->sensors==0&&state->reports==0&&state->createdThread==state->destroyedThread&&(!enumeration||(state->inventoried.empty()&&state->accelTypeCalls==0&&state->diagnostics.size()==2)));
        traceTyped(enumeration?"stop-late-enumeration":"stop-late-report",*state);
        const bool resumed=provider.start(typedOptions(fresh,72))&&await([&]{return fresh->samples>=2;});provider.stop(1500);
        bool current=true;for(const auto& sample:fresh->received)current&=sample.generation==72;
        check("typed restart publishes only the fresh generation after owner drains",resumed&&current&&fresh->managers==0&&fresh->sensors==0&&fresh->reports==0);
    }
    std::cout<<"cases="<<cases<<" failed="<<failed<<"\n";return failed?3:0;
}

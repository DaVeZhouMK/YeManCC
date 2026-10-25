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
    HRESULT STDMETHODCALLTYPE GetType(SENSOR_TYPE_ID* out) override {if(!out)return E_POINTER;*out=GUID{};return S_OK;}// EX-style custom type filtered by WinRT.
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
        if(!out)return E_POINTER;auto* keys=new Keys;for(DWORD p=7;p<=9;++p)keys->keys.push_back(PROPERTYKEY{kClawHidMotion,p});*out=keys;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetProperties(IPortableDeviceValues*,IPortableDeviceValues** out) override {++state->writes;if(out)*out=nullptr;return E_ACCESSDENIED;}
    HRESULT STDMETHODCALLTYPE SupportsDataField(REFPROPERTYKEY key,VARIANT_BOOL* out) override {if(!out)return E_POINTER;*out=IsEqualGUID(key.fmtid,kClawHidMotion)&&key.pid>=7&&key.pid<=9?VARIANT_TRUE:VARIANT_FALSE;return S_OK;}
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
            ComPtr<ISensor> accel;accel.Attach(new Sensor(state,Kind::Accel,2));collection->sensors.push_back(accel);}
    }
    ~Manager(){state->destroyedThread=GetCurrentThreadId();--state->managers;}
    HRESULT STDMETHODCALLTYPE GetSensorsByCategory(REFSENSOR_CATEGORY_ID,ISensorCollection** out) override {return collection.CopyTo(out);}
    HRESULT STDMETHODCALLTYPE GetSensorsByType(REFSENSOR_TYPE_ID,ISensorCollection** out) override {if(out)*out=nullptr;return E_NOTIMPL;}
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
    return out;
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
    std::cout<<"cases="<<cases<<" failed="<<failed<<"\n";return failed?3:0;
}

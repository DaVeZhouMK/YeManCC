#pragma once
// MSI Sensor API fallback. WinRT remains the preferred path (HC parity).
// Native SDK interfaces, not CTW's hand-written vtable calls. No driver writes,
// permission prompts, axis remap, synthetic readings, or synchronous BUS I/O.
#include <windows.h>
#include <sensorsapi.h>
#include <sensors.h>
#include <PortableDeviceTypes.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cmath>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace ymcc::msi_sensor {
using Microsoft::WRL::ComPtr;
enum class Kind { Gyro, Accel };
struct Layout {
    GUID format{};
    std::array<DWORD, 3> pids{};
    const char* name = "";
};
inline constexpr GUID kSdkMotion = {0x3f8a69a2,0x07c5,0x4e48,{0xa9,0x65,0xcd,0x79,0x7a,0xab,0x56,0xd5}};
// CTW 0.4.2 shipped SensorApiGyroSourceAdapter (identical to 0.4.0), measured on the Claw 8 EX. This is NOT
// the SDK motion PROPERTYKEY format; keep its provenance and layout separate.
inline constexpr GUID kClawHidMotion = {0xb14c764f,0x07cf,0x41e8,{0x9d,0x82,0xeb,0xe3,0xd0,0x77,0x6a,0x6f}};
inline constexpr GUID kAllSensors = {0xc317c286,0xc468,0x4288,{0x99,0x75,0xd4,0xc4,0x58,0x7c,0x44,0x2c}};
inline constexpr GUID kGyroType = {0x09485f5a,0x759e,0x42c2,{0xbd,0x4b,0xa3,0x49,0xb7,0x5c,0x86,0x43}};
inline constexpr GUID kAccelType = {0xc2fb0f5f,0xe2d2,0x4c78,{0xbc,0xd0,0x35,0x2a,0x95,0x82,0x81,0x9d}};
inline std::vector<Layout> layouts(Kind kind, bool clawHidCompatible) {
    std::vector<Layout> result;
    if (kind == Kind::Gyro) result.push_back({kSdkMotion,{10,11,12},"sdk-motion-gyro-dps"});
    else result.push_back({kSdkMotion,{2,3,4},"sdk-motion-accel-g"});
    if (clawHidCompatible) {
        result.push_back({kClawHidMotion,{7,8,9},kind == Kind::Gyro ? "ctw-claw-hid-gyro-7-8-9-dps" : "ctw-claw-hid-accel-7-8-9-g"});
        result.push_back({kClawHidMotion,kind == Kind::Gyro ? std::array<DWORD,3>{8,9,10} : std::array<DWORD,3>{2,3,4},
            kind == Kind::Gyro ? "ctw-claw-hid-gyro-8-9-10-dps" : "ctw-claw-hid-accel-2-3-4-g"});
    }
    return result;
}
inline bool numeric(const PROPVARIANT& value, double& result) {
    switch (value.vt) {
    case VT_R8: result=value.dblVal; break;
    case VT_R4: result=value.fltVal; break;
    case VT_I4: result=value.lVal; break;
    case VT_UI4: result=value.ulVal; break;
    default: return false; // Never turn a boolean/state/string into motion.
    }
    return std::isfinite(result);
}
inline bool readTriple(ISensorDataReport* report, const Layout& layout, std::array<double,3>& values) {
    if (!report) return false;
    std::array<double,3> candidate{};
    for (size_t axis=0; axis<3; ++axis) {
        PROPVARIANT value{};
        const PROPERTYKEY key{layout.format,layout.pids[axis]};
        const HRESULT hr=report->GetSensorValue(key,&value);
        const bool ok=SUCCEEDED(hr) && numeric(value,candidate[axis]);
        PropVariantClear(&value);
        if (!ok) return false;
    }
    values=candidate;
    return true;
}
inline bool timestamp(ISensorDataReport* report, UINT64& value) {
    SYSTEMTIME system{}; FILETIME file{};
    if (!report || FAILED(report->GetTimestamp(&system)) || !SystemTimeToFileTime(&system,&file)) return false;
    value=(static_cast<UINT64>(file.dwHighDateTime)<<32)|file.dwLowDateTime;
    return value!=0; // No receipt-clock substitution for a missing device timestamp.
}
struct TimestampGate {
    UINT64 last=0;
    bool admit(UINT64 timestamp100ns) {
        if (!timestamp100ns || timestamp100ns<=last) return false;
        last=timestamp100ns; return true;
    }
};
inline std::string guidString(const GUID& id) {
    wchar_t text[40]{}; StringFromGUID2(id,text,40);
    std::string result;
    for (const wchar_t* p=text; *p; ++p) result.push_back(static_cast<char>(*p));
    return result;
}
inline std::wstring propertyString(ISensor* sensor, DWORD pid) {
    // SENSOR_PROPERTY_* format, SDK sensors.h. Avoid linking external GUID data.
    static constexpr GUID format={0x7f8383ec,0xd3ec,0x495c,{0xa8,0xcf,0xb8,0xbb,0xe8,0x5c,0x29,0x20}};
    const PROPERTYKEY key{format,pid}; PROPVARIANT value{};
    std::wstring result;
    if (SUCCEEDED(sensor->GetProperty(key,&value)) && value.vt==VT_LPWSTR && value.pwszVal) result=value.pwszVal;
    PropVariantClear(&value); return result;
}
inline UINT32 reportInterval(ISensor* sensor) {
    static constexpr GUID format={0x7f8383ec,0xd3ec,0x495c,{0xa8,0xcf,0xb8,0xbb,0xe8,0x5c,0x29,0x20}};
    PROPVARIANT value{}; UINT32 interval=0;
    if (SUCCEEDED(sensor->GetProperty(PROPERTYKEY{format,13},&value)) && value.vt==VT_UI4) interval=value.ulVal;
    PropVariantClear(&value); return interval;
}
inline bool namedPhysical(const std::wstring& name, Kind kind) {
    const wchar_t* expected=kind==Kind::Gyro ? L"Physical Gyrometer" : L"Physical Accelerometer";
    return _wcsicmp(name.c_str(),expected)==0;
}
struct Descriptor {
    GUID id{}; GUID type{};
    std::wstring name; std::wstring devicePath;
    bool documentedGyroFields=false, documentedAccelFields=false;
    HRESULT stateResult=E_FAIL; SensorState state=SENSOR_STATE_NOT_AVAILABLE;
    UINT32 reportIntervalMs=0;
};
inline int candidateRank(const Descriptor& descriptor, Kind kind, bool clawHidCompatible) {
    const bool documented=kind==Kind::Gyro ? descriptor.documentedGyroFields : descriptor.documentedAccelFields;
    if (IsEqualGUID(descriptor.type,kind==Kind::Gyro ? kGyroType : kAccelType)) return 3;
    if (clawHidCompatible && namedPhysical(descriptor.name,kind)) return 2;
    return documented ? 1 : 0;
}
inline int chooseUnique(const std::vector<Descriptor>& inventory, Kind kind, bool clawHidCompatible) {
    int best=0, index=-1; bool ambiguous=false;
    for (size_t i=0; i<inventory.size(); ++i) {
        const int rank=candidateRank(inventory[i],kind,clawHidCompatible);
        if (rank>best) { best=rank; index=static_cast<int>(i); ambiguous=false; }
        else if (rank && rank==best) ambiguous=true;
    }
    return ambiguous ? -1 : index; // No first-device guess across equal candidates.
}
// An explicit request is a complete standard-SDK pair. Failure never falls
// back to another ID or to the Claw compatibility layout.
inline std::array<int,2> chooseExplicitPair(const std::vector<Descriptor>& inventory,
    const std::array<GUID,2>& ids,bool enabled) {
    const GUID empty{};
    if(!enabled||IsEqualGUID(ids[0],empty)||IsEqualGUID(ids[1],empty))return {-1,-1};
    std::array<int,2> found{-1,-1};
    for(unsigned k=0;k<2;++k)for(size_t i=0;i<inventory.size();++i)if(IsEqualGUID(inventory[i].id,ids[k])) {
        if(found[k]>=0)return {-1,-1};
        if(candidateRank(inventory[i],k==0?Kind::Gyro:Kind::Accel,false)<=0)return {-1,-1};
        found[k]=static_cast<int>(i);
    }
    return found[0]>=0&&found[1]>=0 ? found : std::array<int,2>{-1,-1};
}
struct Sample {
    Kind kind=Kind::Gyro;
    std::array<double,3> axes{};
    UINT64 timestamp100ns=0, generation=0;
    ULONGLONG receiptTick=0;
    Descriptor descriptor;
    std::string layout;
};
struct Options {
    bool wantGyro=true, wantAccel=true, clawHidCompatible=false;
    UINT64 generation=0;
    // Unset preserves the existing unique-source policy, including MSI callers.
    std::optional<std::array<GUID,2>> explicitPair;
    // Preserve the existing MSI floor; standard Windows callers can request their source cadence.
    UINT32 accelPollFloorMs=100;
    std::function<void(const Sample&)> sample;
    std::function<void(const std::vector<Descriptor>&,int,int)> inventory;
    std::function<void(const char*,HRESULT)> diagnostic;
#ifdef YMCC_FAULT_INJECT
    // SDK-interface test transport only; absent from production builds.
    std::function<HRESULT(ISensorManager**)> managerFactory;
#endif
};
class Provider {
    struct State {
        Options options;
        HANDLE stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        std::atomic<bool> cancelled{false};
        ~State() { if(stop) CloseHandle(stop); }
        bool stopping() const { return cancelled.load(std::memory_order_acquire); }
        void diag(const char* event,HRESULT hr) const {
            if (!stopping() && options.diagnostic) options.diagnostic(event,hr);
        }
    };
    // Lifecycle calls share one owner; queries never wait behind stop/COM.
    mutable std::mutex lifecycle_;
    HANDLE thread_=nullptr;
    DWORD threadId_=0;
    std::shared_ptr<State> state_;
    static bool documentedFields(ISensor* sensor,Kind kind) {
        ComPtr<IPortableDeviceKeyCollection> keys;
        if (FAILED(sensor->GetSupportedDataFields(&keys)) || !keys) return false;
        DWORD count=0; if (FAILED(keys->GetCount(&count))) return false;
        const auto layout=layouts(kind,false)[0];
        std::array<bool,3> found{};
        for (DWORD i=0; i<count && i<512; ++i) {
            PROPERTYKEY key{}; if (FAILED(keys->GetAt(i,&key))) continue;
            for(size_t axis=0;axis<3;++axis) if(IsEqualGUID(key.fmtid,layout.format) && key.pid==layout.pids[axis]) found[axis]=true;
        }
        return found[0]&&found[1]&&found[2];
    }
    static void poll(const std::shared_ptr<State>& state) {
        ComPtr<ISensorManager> manager;
        HRESULT hr=E_FAIL;
#ifdef YMCC_FAULT_INJECT
        if(state->options.managerFactory) hr=state->options.managerFactory(manager.ReleaseAndGetAddressOf());
        else
#endif
        hr=CoCreateInstance(__uuidof(SensorManager),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&manager));
        if (FAILED(hr)||!manager) { state->diag("manager-unavailable",FAILED(hr)?hr:E_POINTER); return; }
        ComPtr<ISensorCollection> collection;
        hr=manager->GetSensorsByCategory(kAllSensors,&collection);
        if (FAILED(hr)||!collection) { state->diag("enumeration-failed",FAILED(hr)?hr:E_FAIL); return; }
        ULONG count=0; hr=collection->GetCount(&count);
        if (FAILED(hr)) { state->diag("enumeration-count-failed",hr); return; }
        std::vector<ComPtr<ISensor>> sensors;
        std::vector<Descriptor> inventory;
        for (ULONG i=0;i<count && i<256 && !state->stopping();++i) {
            ComPtr<ISensor> sensor;
            if (FAILED(collection->GetAt(i,&sensor)) || !sensor) continue;
            Descriptor descriptor;
            if (FAILED(sensor->GetID(&descriptor.id))) continue;
            sensor->GetType(&descriptor.type);
            descriptor.name=propertyString(sensor.Get(),9);
            descriptor.devicePath=propertyString(sensor.Get(),15);
            descriptor.reportIntervalMs=reportInterval(sensor.Get());
            descriptor.stateResult=sensor->GetState(&descriptor.state);
            descriptor.documentedGyroFields=documentedFields(sensor.Get(),Kind::Gyro);
            descriptor.documentedAccelFields=documentedFields(sensor.Get(),Kind::Accel);
            sensors.push_back(sensor); inventory.push_back(descriptor);
        }
        const auto exactPair=state->options.explicitPair
            ? chooseExplicitPair(inventory,*state->options.explicitPair,state->options.wantGyro&&state->options.wantAccel&&!state->options.clawHidCompatible)
            : std::array<int,2>{-1,-1};
        const int gyro=state->options.explicitPair ? exactPair[0] : (state->options.wantGyro ? chooseUnique(inventory,Kind::Gyro,state->options.clawHidCompatible) : -1);
        const int accel=state->options.explicitPair ? exactPair[1] : (state->options.wantAccel ? chooseUnique(inventory,Kind::Accel,state->options.clawHidCompatible) : -1);
        if (!state->stopping() && state->options.inventory) state->options.inventory(inventory,gyro,accel);
        if (gyro<0 && accel<0) { state->diag(state->options.explicitPair?"explicit-motion-source-pair-not-selected":"no-unambiguous-motion-source",HRESULT_FROM_WIN32(ERROR_NOT_FOUND)); return; }
        std::array<int,2> indices{gyro,accel};
        std::array<int,2> selectedLayouts{-1,-1};
        std::array<TimestampGate,2> clocks{};
        const ULONGLONG started=GetTickCount64();
        ULONGLONG lastAccelRead=0;
        bool anySample=false, noSampleLogged=false;
        while (!state->stopping()) {
            const ULONGLONG tick=GetTickCount64();
            bool fresh=false;
            for(size_t slot=0;slot<2 && !state->stopping();++slot) {
                const int index=indices[slot];
                if(index<0) continue;
                // Same-device accelerometers can block too; a 100ms cadence
                // matches CTW without blocking the gyro/BUS every read.
                if(slot==1 && gyro>=0 && lastAccelRead && tick-lastAccelRead<state->options.accelPollFloorMs) continue;
                if(slot==1) lastAccelRead=tick;
                ComPtr<ISensorDataReport> report;
                hr=sensors[index]->GetData(&report); // Only this dedicated cancellable COM thread.
                if(FAILED(hr)||!report || state->stopping()) continue;
                const Kind kind=slot==0 ? Kind::Gyro : Kind::Accel;
                const bool compat=state->options.clawHidCompatible && namedPhysical(inventory[index].name,kind);
                const auto candidates=layouts(kind,compat);
                std::array<double,3> axes{};
                int layout=selectedLayouts[slot];
                if(layout>=0) {
                    if(!readTriple(report.Get(),candidates[layout],axes)) continue;
                } else {
                    for(size_t i=0;i<candidates.size();++i) if(readTriple(report.Get(),candidates[i],axes)) { layout=static_cast<int>(i); break; }
                    if(layout<0) continue;
                }
                UINT64 timestamp100ns=0;
                if(!timestamp(report.Get(),timestamp100ns) || !clocks[slot].admit(timestamp100ns)) continue;
                selectedLayouts[slot]=layout; // Layout only signed after a complete real reading.
                Sample sample{kind,axes,timestamp100ns,state->options.generation,GetTickCount64(),inventory[index],candidates[layout].name};
                if(!state->stopping() && state->options.sample) state->options.sample(sample);
                fresh=anySample=true;
            }
            if(!anySample && !noSampleLogged && GetTickCount64()-started>=3000) {
                state->diag("motion-source-present-no-fresh-reading",HRESULT_FROM_WIN32(ERROR_TIMEOUT)); noSampleLogged=true;
            }
            // Respect a fast cached-data driver as well as a blocking driver.
            const DWORD elapsed=static_cast<DWORD>(GetTickCount64()-tick);
            const UINT32 interval=gyro>=0 ? inventory[gyro].reportIntervalMs : inventory[accel].reportIntervalMs;
            const DWORD desired=interval ? interval : 10;
            const DWORD wait=fresh ? (desired>elapsed ? desired-elapsed : 1) : 20;
            if(WaitForSingleObject(state->stop,wait)==WAIT_OBJECT_0) break;
        }
    }
    static DWORD WINAPI entry(LPVOID raw) noexcept {
        std::unique_ptr<std::shared_ptr<State>> argument(static_cast<std::shared_ptr<State>*>(raw));
        const auto state=*argument; argument.reset();
        const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        if(FAILED(com)) { try {state->diag("com-initialization-failed",com);} catch(...) {} return 1; }
        CoEnableCallCancellation(nullptr);
        try { poll(state); } catch(...) { try {state->diag("provider-exception",E_FAIL);} catch(...) {} }
        CoDisableCallCancellation(nullptr); CoUninitialize();
        return 0;
    }
public:
    Provider()=default;
    Provider(const Provider&)=delete;
    Provider& operator=(const Provider&)=delete;
    // A false stop result requires the caller to retain this owner and retry.
    // Keep the legacy best-effort destructor; closing its last handle is not
    // proof that an arbitrary COM call exited. Completion requires stopped().
    ~Provider() { stop(); if(thread_) CloseHandle(thread_); }
    bool running() const {
        std::unique_lock<std::mutex> lock(lifecycle_,std::try_to_lock);
        return lock.owns_lock() && thread_ && WaitForSingleObject(thread_,0)==WAIT_TIMEOUT;
    }
    bool stopped() const noexcept {
        try {
            std::unique_lock<std::mutex> lock(lifecycle_,std::try_to_lock);
            return lock.owns_lock() && !thread_ && !threadId_ && !state_;
        } catch(...) { return false; }
    }
private:
    // Requires the lifecycle lock and an actually signalled thread (if any).
    // A failed close keeps its exact handle and state for a later stop/start.
    bool closeStopped() {
        if(thread_) {
            if(!CloseHandle(thread_)) return false;
            thread_=nullptr; threadId_=0;
        }
        if(state_) {
            if(state_->stop) {
                if(!CloseHandle(state_->stop)) return false;
                state_->stop=nullptr;
            }
            state_.reset();
        }
        return true;
    }
public:
    bool start(Options options) {
        std::unique_lock<std::mutex> lock(lifecycle_,std::try_to_lock);
        if(!lock.owns_lock()) return false;
        // WAIT_FAILED is unknown, never permission to replace the old owner.
        if(thread_ && WaitForSingleObject(thread_,0)!=WAIT_OBJECT_0) return false;
        if(!closeStopped()) return false;
        state_=std::make_shared<State>(); state_->options=std::move(options);
        if(!state_->stop) {state_.reset();return false;}
        auto argument=std::make_unique<std::shared_ptr<State>>(state_);
        thread_=CreateThread(nullptr,0,entry,argument.get(),0,&threadId_);
        if(!thread_) {threadId_=0;closeStopped();return false;}
        argument.release(); return true;
    }
    bool stop(DWORD waitMs=500) {
        std::unique_lock<std::mutex> lock(lifecycle_,std::try_to_lock);
        if(!lock.owns_lock()) return false;
        if(thread_) {
            state_->cancelled.store(true,std::memory_order_release); SetEvent(state_->stop);
            if(WaitForSingleObject(thread_,0)==WAIT_TIMEOUT) CoCancelCall(threadId_,0);
            if(WaitForSingleObject(thread_,waitMs)!=WAIT_OBJECT_0) return false;
        }
        return closeStopped();
    }
};
} // namespace ymcc::msi_sensor

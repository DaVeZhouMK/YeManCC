#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include "msi_legacy_sensor.h"
#include "sensor_inventory_cache.h"

// Metadata only: no GetData, subscriptions, interval writes, source selection or
// processing. The single worker stays owned until its actual handle is signalled.
namespace ymcc::sensors::sensorapi_inventory {
using inventory::Ticket;
struct Entry {
    msi_sensor::Descriptor descriptor;
    HRESULT idError=E_FAIL,typeError=E_FAIL,nameError=E_FAIL,pathError=E_FAIL,intervalError=E_FAIL,fieldsError=E_FAIL;
    bool nameTruncated=false,pathTruncated=false;
    bool gyro() const noexcept {return (SUCCEEDED(typeError)&&IsEqualGUID(descriptor.type,msi_sensor::kGyroType))||descriptor.documentedGyroFields;}
    bool accel() const noexcept {return (SUCCEEDED(typeError)&&IsEqualGUID(descriptor.type,msi_sensor::kAccelType))||descriptor.documentedAccelFields;}
};
struct Observation {
    inventory::Snapshot receipt;
    std::optional<std::uint64_t> reportedTotal;
    std::vector<Entry> entries;
    std::uint64_t visited=0,unavailableEntries=0;
    HRESULT error=S_OK;
    std::string phase="not-started";
    bool truncated=false;
};
class Census final {
    struct Scope {
        std::weak_ptr<const std::atomic<std::uint64_t>> owner;
        std::uint64_t generation=0,power=0;
        Ticket ticket() const {return {{owner.lock(),generation},power};}
    };
    struct Worker {
        Scope scope;
        HANDLE thread=nullptr;
        std::atomic<bool> cancelled{false};
        const std::uint64_t requested=GetTickCount64();
#ifdef YMCC_FAULT_INJECT
        std::function<HRESULT(ISensorManager**)> factory;
#endif
        ~Worker(){if(thread)CloseHandle(thread);}
    };
    struct Registry {
        std::mutex mutex;
        std::shared_ptr<Worker> worker;
        inventory::Schedule schedule;
        inventory::ObservationStore<Observation> observations;
        bool pending=false;
    };
    // A blocked COM call must not lose its strong worker/handle owner during
    // static teardown. Settled workers are reaped normally, including on query.
    static Registry& registry(){static Registry* value=new Registry;return *value;}
    static void reap(Registry& value) {
        if(!value.worker || !value.worker->thread) return;
        if(WaitForSingleObject(value.worker->thread,0)!=WAIT_OBJECT_0) return;
        if(!CloseHandle(value.worker->thread)) return;
        value.worker->thread=nullptr; // Worker destruction must not close twice.
        value.worker.reset();
    }
    static void guard(const std::shared_ptr<Worker>& worker,std::uint64_t deadline) {
        if(worker->cancelled.load(std::memory_order_acquire))throw inventory::Error{inventory::State::Cancelled,HRESULT_FROM_WIN32(ERROR_CANCELLED)};
        if(!windows_motion::current(worker->scope.ticket().binding))throw inventory::Error{inventory::State::Retired,HRESULT_FROM_WIN32(ERROR_CANCELLED)};
        if(GetTickCount64()>=deadline)throw inventory::Error{inventory::State::TimedOut,HRESULT_FROM_WIN32(ERROR_TIMEOUT)};
    }
    static HRESULT text(ISensor* sensor,DWORD pid,std::wstring& out,bool& truncated) {
        static constexpr GUID format={0x7f8383ec,0xd3ec,0x495c,{0xa8,0xcf,0xb8,0xbb,0xe8,0x5c,0x29,0x20}};
        PROPVARIANT value{};const auto hr=sensor->GetProperty(PROPERTYKEY{format,pid},&value);
        HRESULT result=hr;
        if(SUCCEEDED(hr)){
            if(value.vt==VT_LPWSTR&&value.pwszVal){const auto size=wcsnlen(value.pwszVal,4097);truncated=size>4096;out.assign(value.pwszVal,(std::min)(size,size_t{4096}));}
            else if(value.vt==VT_BSTR&&value.bstrVal){const auto size=SysStringLen(value.bstrVal);truncated=size>4096;out.assign(value.bstrVal,(std::min)(size,UINT{4096}));}
            else result=E_UNEXPECTED;
        }
        PropVariantClear(&value);return result;
    }
    static void fields(ISensor* sensor,Entry& entry,const std::shared_ptr<Worker>& worker,std::uint64_t deadline) {
        msi_sensor::ComPtr<IPortableDeviceKeyCollection> keys;
        entry.fieldsError=sensor->GetSupportedDataFields(&keys);guard(worker,deadline);
        if(FAILED(entry.fieldsError)||!keys){if(SUCCEEDED(entry.fieldsError))entry.fieldsError=E_POINTER;return;}
        DWORD count=0;entry.fieldsError=keys->GetCount(&count);guard(worker,deadline);if(FAILED(entry.fieldsError))return;
        std::array<std::array<bool,3>,2> found{};
        if(count>512)entry.fieldsError=HRESULT_FROM_WIN32(ERROR_MORE_DATA);
        for(DWORD i=0;i<count&&i<512;++i){
            guard(worker,deadline);PROPERTYKEY key{};const auto hr=keys->GetAt(i,&key);guard(worker,deadline);
            if(FAILED(hr)){entry.fieldsError=hr;continue;}
            for(unsigned kind=0;kind<2;++kind){const auto layout=msi_sensor::layouts(kind==0?msi_sensor::Kind::Gyro:msi_sensor::Kind::Accel,false)[0];
                for(unsigned axis=0;axis<3;++axis)if(IsEqualGUID(key.fmtid,layout.format)&&key.pid==layout.pids[axis])found[kind][axis]=true;}
        }
        entry.descriptor.documentedGyroFields=found[0][0]&&found[0][1]&&found[0][2];
        entry.descriptor.documentedAccelFields=found[1][0]&&found[1][1]&&found[1][2];
    }
    static Observation collect(const std::shared_ptr<Worker>& worker) {
        Observation result;auto& receipt=result.receipt;receipt.started=GetTickCount64();receipt.budget=1500;
        const auto deadline=receipt.started+receipt.budget;
        try {
            guard(worker,deadline);result.phase="manager";msi_sensor::ComPtr<ISensorManager> manager;HRESULT hr=E_FAIL;
#ifdef YMCC_FAULT_INJECT
            if(worker->factory)hr=worker->factory(manager.ReleaseAndGetAddressOf());else
#endif
            hr=CoCreateInstance(__uuidof(SensorManager),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&manager));
            guard(worker,deadline);if(FAILED(hr)||!manager)throw inventory::Error{inventory::State::Failed,FAILED(hr)?hr:E_POINTER};
            result.phase="collection";msi_sensor::ComPtr<ISensorCollection> collection;hr=manager->GetSensorsByCategory(msi_sensor::kAllSensors,&collection);
            guard(worker,deadline);if(FAILED(hr)||!collection)throw inventory::Error{inventory::State::Failed,FAILED(hr)?hr:E_POINTER};
            ULONG count=0;hr=collection->GetCount(&count);guard(worker,deadline);if(FAILED(hr))throw inventory::Error{inventory::State::Failed,hr};
            result.reportedTotal=count;result.truncated=count>256;result.phase="descriptors";
            for(ULONG i=0;i<count&&i<256;++i){
                guard(worker,deadline);msi_sensor::ComPtr<ISensor> sensor;hr=collection->GetAt(i,&sensor);guard(worker,deadline);++result.visited;
                if(FAILED(hr)||!sensor){++result.unavailableEntries;result.error=FAILED(hr)?hr:E_POINTER;continue;}
                Entry entry;auto& d=entry.descriptor;entry.idError=sensor->GetID(&d.id);guard(worker,deadline);
                const GUID empty{};if(SUCCEEDED(entry.idError)&&IsEqualGUID(d.id,empty))entry.idError=E_INVALIDARG;
                entry.typeError=sensor->GetType(&d.type);guard(worker,deadline);
                entry.nameError=text(sensor.Get(),9,d.name,entry.nameTruncated);guard(worker,deadline);
                entry.pathError=text(sensor.Get(),15,d.devicePath,entry.pathTruncated);guard(worker,deadline);
                d.stateResult=sensor->GetState(&d.state);guard(worker,deadline);
                static constexpr GUID format={0x7f8383ec,0xd3ec,0x495c,{0xa8,0xcf,0xb8,0xbb,0xe8,0x5c,0x29,0x20}};
                PROPVARIANT interval{};entry.intervalError=sensor->GetProperty(PROPERTYKEY{format,13},&interval);
                if(SUCCEEDED(entry.intervalError)){if(interval.vt==VT_UI4)d.reportIntervalMs=interval.ulVal;else entry.intervalError=E_UNEXPECTED;}
                PropVariantClear(&interval);guard(worker,deadline);fields(sensor.Get(),entry,worker,deadline);result.entries.push_back(std::move(entry));
            }
            bool identitiesKnown=true;for(const auto& entry:result.entries)identitiesKnown=identitiesKnown&&SUCCEEDED(entry.idError)&&SUCCEEDED(entry.typeError);
            receipt.state=result.truncated?inventory::State::Truncated:result.unavailableEntries||!identitiesKnown?inventory::State::Partial:inventory::State::Complete;
            result.phase="complete";
        }catch(const inventory::Error& error){receipt.state=error.state;result.error=static_cast<HRESULT>(error.code);}
        catch(...){receipt.state=inventory::State::Failed;result.error=E_FAIL;}
        receipt.finished=GetTickCount64();receipt.sourceCurrent=windows_motion::current(worker->scope.ticket().binding);
        if(!receipt.sourceCurrent){receipt.state=inventory::State::Retired;result.error=HRESULT_FROM_WIN32(ERROR_CANCELLED);}
        else if(worker->cancelled.load(std::memory_order_acquire)){receipt.state=inventory::State::Cancelled;result.error=HRESULT_FROM_WIN32(ERROR_CANCELLED);}
        return result;
    }
    static DWORD WINAPI entry(void* raw) noexcept {
        std::unique_ptr<std::shared_ptr<Worker>> holder(static_cast<std::shared_ptr<Worker>*>(raw));const auto worker=*holder;holder.reset();
        const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        try {
            Observation result;
            if(FAILED(com)){result.receipt.started=result.receipt.finished=GetTickCount64();result.receipt.state=inventory::State::Failed;result.receipt.sourceCurrent=windows_motion::current(worker->scope.ticket().binding);result.error=com;result.phase="com-initialization";}
            else result=collect(worker);
            const auto receipt=result.receipt;registry().observations.publish(worker->scope.ticket(),receipt,std::move(result));
        }catch(...){/* Metadata failure cannot tear down the selected source. */}
        if(SUCCEEDED(com))CoUninitialize();return 0;
    }
public:
#ifdef YMCC_FAULT_INJECT
    using Factory=std::function<HRESULT(ISensorManager**)>;
    static Factory& factoryForTest(){static thread_local Factory value;return value;}
#endif
    struct View {
        inventory::ObservationStore<Observation>::View observation;
        bool workerRunning=false,workerRetireRequested=false,pendingCurrentContext=false,workerOverBudget=false;
        std::uint64_t workerBindingGeneration=0,workerPowerGeneration=0;
    };
    // Called only by the existing acquisition owner's metadata schedule. Query
    // and export never call request. A busy old COM call cannot stack workers.
    static bool request(const Ticket& ticket
#ifdef YMCC_FAULT_INJECT
        ,std::function<HRESULT(ISensorManager**)> factory={}
#endif
    ) {
        if(!ticket.power||!windows_motion::current(ticket.binding))return false;
        auto& value=registry();std::lock_guard<std::mutex> lock(value.mutex);reap(value);
        if(value.worker){
            const bool same=inventory::sameScope(value.worker->scope.ticket(),ticket)&&
                !value.worker->cancelled.load(std::memory_order_acquire);
            if(!same)value.worker->cancelled.store(true,std::memory_order_release);
            value.pending=!same;return false;
        }
        value.pending=false;if(!value.schedule.claim(ticket.binding,ticket.power))return false;
        auto worker=std::make_shared<Worker>();worker->scope={ticket.binding.activeBinding,ticket.binding.bindingGeneration,ticket.power};
#ifdef YMCC_FAULT_INJECT
        worker->factory=factory?std::move(factory):factoryForTest();
#endif
        auto argument=std::make_unique<std::shared_ptr<Worker>>(worker);
        worker->thread=CreateThread(nullptr,0,&entry,argument.get(),0,nullptr);
        if(!worker->thread){const auto error=GetLastError();Observation failed;failed.receipt.started=failed.receipt.finished=GetTickCount64();failed.receipt.sourceCurrent=windows_motion::current(ticket.binding);failed.receipt.state=inventory::State::Failed;failed.error=HRESULT_FROM_WIN32(error);failed.phase="thread-create";value.observations.publish(ticket,failed.receipt,failed);return false;}
        argument.release();value.worker=std::move(worker);return true;
    }
    static void cancel() noexcept {
        auto& value=registry();std::lock_guard<std::mutex> lock(value.mutex);reap(value);
        if(value.worker)value.worker->cancelled.store(true,std::memory_order_release);
        value.pending=false;value.schedule.reset();
    }
    // Cancel plus one nonblocking observation. A blocked/unknown/unclosed
    // worker remains owned; a later retire may retry after actual completion.
    static bool retire() noexcept {
        try {
            auto& value=registry();std::unique_lock<std::mutex> lock(value.mutex,std::try_to_lock);
            if(!lock.owns_lock()) return false;
            if(value.worker)value.worker->cancelled.store(true,std::memory_order_release);
            value.pending=false;value.schedule.reset();reap(value);
            return !value.worker;
        } catch(...) { return false; }
    }
    static bool stopped() noexcept {
        try {
            auto& value=registry();std::unique_lock<std::mutex> lock(value.mutex,std::try_to_lock);
            return lock.owns_lock() && !value.worker && !value.pending;
        } catch(...) { return false; }
    }
    static bool pending() noexcept {auto& value=registry();std::lock_guard<std::mutex> lock(value.mutex);return value.pending;}
    static View inspect(const Ticket& current,std::uint64_t now) {
        auto& value=registry();std::lock_guard<std::mutex> lock(value.mutex);reap(value);View result;
        result.observation=value.observations.inspect(current,now);result.pendingCurrentContext=value.pending;
        if(value.worker){result.workerRunning=true;result.workerRetireRequested=value.worker->cancelled.load(std::memory_order_acquire);
            result.workerBindingGeneration=value.worker->scope.generation;result.workerPowerGeneration=value.worker->scope.power;
            result.workerOverBudget=now>=value.worker->requested&&now-value.worker->requested>=1500;}
        return result;
    }
};
} // namespace ymcc::sensors::sensorapi_inventory

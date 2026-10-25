#pragma once
#include <algorithm>
#include <array>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include "msi_legacy_sensor.h"
#include "windows_source_eligibility.h"

// Owner-thread facade. Worker callbacks only copy real descriptors/reports into
// this bounded mailbox; they never call the STA selection or processing owner.
namespace ymcc::sensors::sensorapi {
inline std::string sourceId(const GUID& value){return "sensorapi:"+msi_sensor::guidString(value);}
inline bool sourceGuid(const std::string& value,GUID& result) {
    if(!selection::validSensorApiId(value))return false;
    const auto suffix=value.substr(10);const std::wstring text(suffix.begin(),suffix.end());
    return SUCCEEDED(CLSIDFromString(text.c_str(),&result));
}
class PrivateReader final {
public:
    struct Snapshot {
        bool inventoryObserved=false,pairSelected=false,retired=false;
        std::array<msi_sensor::Descriptor,2> descriptors;
        std::uint64_t generation=0,queued=0,dropped=0;
        HRESULT error=S_OK;
    };
#ifdef YMCC_FAULT_INJECT
    using Factory=std::function<HRESULT(ISensorManager**)>;
    static Factory& factoryForTest(){static thread_local Factory value;return value;}
#endif
private:
    struct Mailbox {
        std::mutex mutex;
        Snapshot value;
        std::vector<msi_sensor::Sample> reports;
    };
    struct Worker {
        msi_sensor::Provider provider;
        const std::array<GUID,2> ids;
        const std::shared_ptr<Mailbox> mailbox=std::make_shared<Mailbox>();
        bool retired=false; // Facade/registry owner only; callbacks use mailbox.value.retired.
        explicit Worker(std::array<GUID,2> value):ids(value){}
    };
    struct Registry {std::mutex mutex;std::vector<std::shared_ptr<Worker>> owners;};
    // Registry lifetime includes static owner teardown. Normal close/reap still
    // releases settled workers; only actually blocked workers remain owned.
    static Registry& registry(){static Registry* value=new Registry;return *value;}
    std::shared_ptr<Worker> worker_;
    static void reap(Registry& value) {
        for(auto it=value.owners.begin();it!=value.owners.end();) {
            if((*it)->retired&&!(*it)->provider.running()){
                (*it)->provider.stop(0);it=value.owners.erase(it);
            }else ++it;
        }
    }
    static bool overlaps(const std::array<GUID,2>& a,const std::array<GUID,2>& b)noexcept {
        for(const auto& x:a)for(const auto& y:b)if(IsEqualGUID(x,y))return true;
        return false;
    }
public:
    PrivateReader()=default;
    PrivateReader(const PrivateReader&)=delete;
    PrivateReader& operator=(const PrivateReader&)=delete;
    ~PrivateReader(){close();}
    static unsigned quarantined() {
        auto& value=registry();std::lock_guard<std::mutex> lock(value.mutex);reap(value);
        return static_cast<unsigned>(std::count_if(value.owners.begin(),value.owners.end(),[](const auto& owner){return owner->retired;}));
    }
    bool start(const selection::PairChoice& pair,std::uint64_t generation
#ifdef YMCC_FAULT_INJECT
        ,Factory factory={}
#endif
    ) {
        if(worker_||!generation)return false;
        std::array<GUID,2> ids{};
        if(!sourceGuid(pair.gyroId,ids[0])||!sourceGuid(pair.accelId,ids[1]))return false;
        auto owner=std::make_shared<Worker>(ids);const auto mailbox=owner->mailbox;
        mailbox->value.generation=generation;mailbox->reports.reserve(32);
        msi_sensor::Options options;options.generation=generation;options.explicitPair=ids;
        options.clawHidCompatible=false;options.accelPollFloorMs=8;
#ifdef YMCC_FAULT_INJECT
        options.managerFactory=factory?std::move(factory):factoryForTest();
#endif
        options.inventory=[mailbox,ids](const auto& inventory,int gyro,int accel){
            std::lock_guard<std::mutex> lock(mailbox->mutex);auto& snapshot=mailbox->value;
            if(snapshot.retired)return;
            snapshot.inventoryObserved=true;
            const bool valid=gyro>=0&&accel>=0&&static_cast<size_t>(gyro)<inventory.size()&&static_cast<size_t>(accel)<inventory.size()&&
                IsEqualGUID(inventory[gyro].id,ids[0])&&IsEqualGUID(inventory[accel].id,ids[1]);
            snapshot.pairSelected=valid;
            if(valid)snapshot.descriptors={inventory[gyro],inventory[accel]};
            else snapshot.error=HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        };
        options.diagnostic=[mailbox](const char* phase,HRESULT error){
            std::lock_guard<std::mutex> lock(mailbox->mutex);
            if(!mailbox->value.retired&&FAILED(error))mailbox->value.error=error;
        };
        options.sample=[mailbox,ids,generation](const msi_sensor::Sample& report){
            std::lock_guard<std::mutex> lock(mailbox->mutex);auto& snapshot=mailbox->value;
            if(snapshot.retired||!snapshot.pairSelected||report.generation!=generation)return;
            const auto kind=report.kind==msi_sensor::Kind::Gyro?0u:1u;
            if(!IsEqualGUID(report.descriptor.id,ids[kind])||report.layout!=selection::standardSdkLayout(kind))return;
            if(mailbox->reports.size()==32){mailbox->reports.erase(mailbox->reports.begin());++snapshot.dropped;}
            mailbox->reports.push_back(report);snapshot.queued=mailbox->reports.size();
        };
        auto& value=registry();std::lock_guard<std::mutex> lock(value.mutex);reap(value);
        if(value.owners.size()>=16)return false;
        for(const auto& prior:value.owners)if(prior->retired&&overlaps(prior->ids,ids))return false;
        // Register before starting the thread. close() never allocates storage
        // in order to retain an actually blocked COM owner and its handle.
        value.owners.push_back(owner);
        if(!owner->provider.start(std::move(options))){owner->retired=true;reap(value);return false;}
        worker_=std::move(owner);return true;
    }
    Snapshot snapshot()const {
        if(!worker_)return {};
        const auto mailbox=worker_->mailbox;std::lock_guard<std::mutex> lock(mailbox->mutex);return mailbox->value;
    }
    std::vector<msi_sensor::Sample> take() {
        if(!worker_)return {};
        const auto mailbox=worker_->mailbox;std::lock_guard<std::mutex> lock(mailbox->mutex);
        if(mailbox->value.retired)return {};
        std::vector<msi_sensor::Sample> result;result.swap(mailbox->reports);mailbox->value.queued=0;return result;
    }
    bool running()const{return worker_&&!worker_->retired&&worker_->provider.running();}
    void close()noexcept {
        if(!worker_)return;
        auto owner=std::move(worker_);const auto mailbox=owner->mailbox;
        {std::lock_guard<std::mutex> lock(mailbox->mutex);mailbox->value.retired=true;mailbox->reports.clear();mailbox->value.queued=0;}
        auto& value=registry();std::lock_guard<std::mutex> lock(value.mutex);
        owner->retired=true;owner->provider.stop(0);reap(value);
    }
};
} // namespace ymcc::sensors::sensorapi
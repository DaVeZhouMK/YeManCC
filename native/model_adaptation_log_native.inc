// Logging-only consumers of existing observations. Included after model/catalog
// definitions; producer callbacks only offer bounded value objects.
#include "model_adaptation_log.h"
static ymcc::adaptlog::Signals& modelAdaptationSignals() { static ymcc::adaptlog::Signals value; return value; }
static ymcc::adaptlog::Journal& modelAdaptationJournal() { static ymcc::adaptlog::Journal value; return value; }
static ymcc::adaptlog::Facts& modelAdaptationFacts() { static ymcc::adaptlog::Facts value; return value; }
static json modelAdaptationMachine() {
    return {{"manufacturer",g_machineIdentity.manufacturer},{"product",g_machineIdentity.product},
        {"systemProduct",g_machineIdentity.systemProduct},{"systemVersion",g_machineIdentity.systemVersion},
        {"processor",g_machineIdentity.processor},{"deviceClass",g_machineIdentity.deviceClass},
        {"family",ymccFamilyName(g_machineIdentity.family)},{"resolved",g_machineIdentity.resolved}};
}
#include "virtual_adaptation_log_native.inc"
static void modelAdaptationChord(int profile, uint32_t chord, uint64_t mapped, bool mouse, int physical) noexcept {
    if(!mapped&&!physical)return;
    ymcc::adaptlog::Signal value;
    value.type={1,static_cast<uint32_t>(profile),mouse?1u:0u,chord,0,mapped};
    value.pressed=true;value.mapped=mapped!=0;modelAdaptationSignals().offer(value);
}
static void modelAdaptationProducer(unsigned source, uint64_t mask, unsigned code) noexcept {
    if(!mask&&!code)return;
    ymcc::adaptlog::Signal value;
    value.type={16u+source,0,code?1u:0u,code,0,mask};
    value.pressed=true;value.mapped=mask!=0;modelAdaptationSignals().offer(value);
}
static void modelAdaptationOnexReport(const BYTE* raw,size_t bytes,bool orange) noexcept {
    const auto* frame=ymcc::capabilities::oem::onex::inputFrame(raw,bytes);
    if(!frame||frame[0]!=0xB2)return; // Status/telemetry is not a button type.
    ymcc::adaptlog::Signal value;
    const auto code=frame[6];uint64_t mask=0;
    if(code==0x23)mask=kOemBitL4;else if(code==0x22)mask=kOemBitR4;
    else if(code==0x21)mask=orange?kOemBitOrange:kOemBitSpecial;
    else if(code==0x24)mask=kOemBitKeyboard;
    value.type={2,frame==raw?0u:1u,0xB2,code,static_cast<uint32_t>(bytes),mask};
    value.type.reportId=frame==raw?0u:raw[0];
    value.rawBytes=static_cast<unsigned>((std::min)(bytes,value.raw.size()));
    std::copy_n(raw,value.rawBytes,value.raw.begin());value.pressed=frame[12]==1;value.mapped=mask!=0;
    modelAdaptationSignals().offer(value); // Before the business parser's unknown-ID return.
}
static void modelAdaptationRogReport(const BYTE* raw,size_t bytes,unsigned code,uint64_t mapped) noexcept {
    if(!raw||!bytes||bytes>512)return;
    ymcc::adaptlog::Signal value;
    value.type={3,raw[0]==0x5A&&bytes>=2?1u:0u,0,code,static_cast<uint32_t>(bytes),mapped};
    value.type.reportId=value.type.profile?raw[0]:0;
    value.rawBytes=static_cast<unsigned>(bytes);std::copy_n(raw,bytes,value.raw.begin());
    value.pressed=true;value.mapped=mapped!=0;modelAdaptationSignals().offer(value);
}
static void modelAdaptationFlush() noexcept {
    virtualAdaptationFlush();
    if(!modelAdaptationSignals().pending()&&!modelAdaptationFacts().pending())return;
    try {
        static std::mutex flushMx;
        std::unique_lock<std::mutex> flushLock(flushMx,std::try_to_lock);if(!flushLock)return;
        static ULONGLONG retryAt=0;const auto now=GetTickCount64();if(now<retryAt||!g_machineIdentity.attempted)return;
        const auto path=fspath::path(yemancc_data_dir())/L"model-adaptation.log";
        for(unsigned n=0;n<8;++n){size_t index=0;ymcc::adaptlog::Fact value;
            if(!modelAdaptationFacts().take(index,value))break;
            auto result=ymcc::adaptlog::RecordResult::retry;
            try{value.sample["capturedTickMs"]=value.capturedAt;result=modelAdaptationJournal().recordResult(path,value.model,value.category.c_str(),value.identity,value.sample,nativeLifecycleTimestamp());}catch(...){}
            modelAdaptationFacts().finish(index,result!=ymcc::adaptlog::RecordResult::retry);
            if(result==ymcc::adaptlog::RecordResult::retry){retryAt=now+5000;return;}
        }
        for(unsigned n=0;n<8;++n){size_t index=0;ymcc::adaptlog::Signal value;
            if(!modelAdaptationSignals().take(index,value))break;
            auto result=ymcc::adaptlog::RecordResult::retry;
            try {
            const auto& type=value.type;
            json identity={{"policy","oem-type-v1"},{"sourceId",type.source},{"profileId",type.source==1?json(type.profile):json(nullptr)},
                {"frameOffset",type.source==2||type.source==3?json(type.profile):json(nullptr)},
                {"command",type.command},{"code",type.code},{"reportBytes",type.shape},{"reportId",type.reportId},{"canonicalMask",type.canonicalMask}};
            if(type.source==2)identity["protocolSources"]={{"x1",ymcc::capabilities::oem::onex::kX1SourceSha256},
                {"mini",ymcc::capabilities::oem::onex::kMiniSourceSha256},{"apex",ymcc::capabilities::oem::onex::kApexSourceSha256}};
            json sample={{"capturedTickMs",value.capturedAt},{"pressedAtFirstWitness",type.source==3&&!value.mapped?json(nullptr):json(value.pressed)},{"mappingFound",value.mapped},
                {"reason",value.mapped?"existing-rule-interpreted":"source-code-not-in-current-mapping"},
                {"sourceScope",type.source==1?"existing-model-chord;keyboard-device-origin-unproven":
                    type.source==2?"current-owner-vendor-HID-before-key-mapping":type.source==3?"current-owner-ROG-HID;unknown-codes-retained":type.source==32?"current-WMI-subscription-before-code-mapping":"existing-current-producer-receipt-after-admission"},
                {"mappingSource",type.source==2?"capabilities/HcOneXVendorInputContract.h + native/main.cpp:onexBackParse":
                    type.source==1?"native/main.cpp:oemChordEvalRules":type.source==3?"native/main.cpp:rogOemKeyParse":type.source==32?"native/main.cpp:msiWmiHandleEvent":"native/main.cpp:nativeOemProducerDrain"},
                {"consumerActionProven",false},{"pressReleaseMergedAsOneType",true}};
            if(value.rawBytes){std::string hex;hex.reserve(value.rawBytes*2);
                constexpr char digits[]="0123456789abcdef";for(unsigned i=0;i<value.rawBytes;++i){hex+=digits[value.raw[i]>>4];hex+=digits[value.raw[i]&15];}
                sample["firstRawReportHex"]=hex;sample["rawBytes"]=value.rawBytes;
}
            result=modelAdaptationJournal().recordResult(fspath::path(yemancc_data_dir())/L"model-adaptation.log",
                modelAdaptationMachine(),"oem-signal-type",identity,sample,nativeLifecycleTimestamp());
            }catch(...){}
            modelAdaptationSignals().finish(index,result!=ymcc::adaptlog::RecordResult::retry);
            if(result==ymcc::adaptlog::RecordResult::retry){retryAt=now+5000;break;} // Event-driven retry, no timer or new thread.
        }
    }catch(...){}
}
static void modelAdaptationObserveRecord(const char* event,const json& detail) noexcept {
    if(!event||!detail.is_object())return;
    if(std::strcmp(event,"machine-identity")&&std::strcmp(event,"oem-key-usage-query")&&
       std::strcmp(event,"gamepad.feedbackCaps")&&std::strcmp(event,"onex-back-bind")&&
       std::strcmp(event,"msi-dinput-bind")&&std::strcmp(event,"msi-wmi-source-capability")&&
       std::strcmp(event,"oem-producer-edge"))return; // No allocation for other event families.
    try {
    if(std::strcmp(event,"oem-producer-edge")==0&&detail.value("status","")!="refused"&&
       detail.value("status","")!="failed"&&detail.value("status","")!="unconsumed")return;
        json identity=json::object();const char* category=nullptr;
        const auto copy=[&](std::initializer_list<const char*> keys){for(const auto key:keys)if(detail.contains(key))identity[key]=detail[key];};
        if(std::strcmp(event,"machine-identity")==0){category="machine-match";copy({"manufacturer","product","systemProduct","systemVersion","processor","family","deviceClass","inputHidKey","resolved"});}
        else if(std::strcmp(event,"oem-key-usage-query")==0){category="oem-catalog-and-route";copy({"schemaVersion","familyId","deviceClass","captureClass","bindingSource","supported","keys","runtime"});}
        else if(std::strcmp(event,"gamepad.feedbackCaps")==0){category="feedback-capability";copy({"machineFamily","vibrationAvailable","vibrationBackend","ledAvailable","lightingBackend","lightingBackendImplemented","lightSetterPresent"});}
        else if(std::strcmp(event,"onex-back-bind")==0){category="oem-HID-binding";copy({"ok","deviceClass","initProfile","path","inputReportBytes","outputReportBytes","protocol"});}
        else if(std::strcmp(event,"msi-dinput-bind")==0){category="oem-DInput-binding";copy({"vendorId","productId","product","interfacePath","baseContainer","instanceGuid","buttons","hasBtn15","hasBtn16","mainInterfaceMatched","selectionRule"});}
        else if(std::strcmp(event,"msi-wmi-source-capability")==0){category="oem-WMI-capability";copy({"deviceClass","eventClass","availability","beginHresult","waitHresult","requestIssued","classObject","authorityGranted","subscriptionPolicy","scope"});}
        else if(std::strcmp(event,"oem-producer-edge")==0){category="oem-consumer-rejection";copy({"source","status","reason","code","mask","requiredMask","trigger","legacyWindowDefault"});}
        else return;
        if(identity.empty())return;
        modelAdaptationFacts().offer({modelAdaptationMachine(),std::move(identity),
            {{"observedFrom",event},{"observedOnly",true},{"hardwareQueryAdded",false},{"capabilityIsNotPhysicalSuccess",true}},category});
    }catch(...){}
}
// Called once from the existing machine resolver, even when no diagnostics UI opens.
// Reads the same immutable binding/catalog used by OEM consumers; no source start/probe.
static void modelAdaptationCatalog() noexcept {
    try {
        const auto& binding=oemUnifiedInputBindingFor(g_machineIdentity.family,g_machineIdentity.deviceClass);
        const std::string cls=binding.model?std::string(binding.model->consumerClass):g_machineIdentity.deviceClass;
        json keys=json::array();
        for(int i=0;i<binding.catalog.count;++i){const auto& key=binding.catalog.keys[i];
            json item={{"keyId",key.keyId},{"backIndex",key.backIndex},{"clickOnly",key.clickOnly},
                {"physicalForm",key.form==OemKeyForm::KeyboardVk?"keyboard-vk":key.form==OemKeyForm::HidButton?"hid-button":key.form==OemKeyForm::WmiEvent?"wmi-event":"embedded"}};
            if(ymcc::capabilities::oem::onex::descriptorFor(cls).valid){
                item["captureRoute"]=ymcc::capabilities::oem::onex::captureRoute(cls,key.keyId);
                const auto physical=ymcc::capabilities::oem::onex::physicalKeyIds(cls,key.keyId);
                for(const auto id:physical)if(!id.empty())item["physicalKeyIds"].push_back(std::string(id));
            }
            keys.push_back(std::move(item));
        }
        json identity={{"deviceClass",g_machineIdentity.deviceClass},{"captureClass",cls},{"profileId",binding.profileId},
            {"bindingSource","oemUnifiedInputBindingFor/current-total-catalog"},{"catalogSource","capabilities/hc-device-catalog.generated.h"},
            {"catalogBound",binding.model!=nullptr},{"keys",std::move(keys)}};
        if(binding.model){identity["deviceId"]=binding.model->deviceId;identity["factoryType"]=binding.model->factoryType;
            identity["declaredMinimum"]=binding.model->declaredMinimum;identity["declaredPossible"]=binding.model->declaredPossible;}
        modelAdaptationFacts().offer({modelAdaptationMachine(),std::move(identity),
            {{"observedFrom","existing-machine-resolver"},{"catalogDeclarationOnly",true},{"inputObserved",false},
                {"physicalDirectionVerified",false},{"hardwareQueryAdded",false},{"consumerActionProven",false}},"model-catalog-baseline"});
    }catch(...){}
}
static json modelAdaptationStatus() {
    return {{"schema","ymcc.model-adaptation-status/1"},{"modelContext","existing-once-resolved-machine-cache;unresolved-identity-is-unknown"},{"file","model-adaptation.log"},{"alwaysOn",true},
        {"detailGateRequired",false},{"uniqueness","model+source+protocol+code/chord+canonical-mapping;no-time-count-generation-or-up/down"},
        {"facts",modelAdaptationFacts().status()},{"signals",modelAdaptationSignals().status()},{"journal",modelAdaptationJournal().status()},
        {"scope","no-observation-is-not-proof-of-missing-hardware;bounded-gaps-and-first-witnesses-only"}};
}

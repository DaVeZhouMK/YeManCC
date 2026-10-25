static int checks=0,reviewedExtraChecks=0;
static json failures=json::array(), groups=json::object();
static void check(bool ok,const std::string& group,const std::string& label) {
    ++checks; groups[group]=groups.value(group,0)+1;
    if(!ok) failures.push_back({{"group",group},{"label",label}});
}
static void checkReviewed(bool ok,const std::string& group,const std::string& label){++reviewedExtraChecks;check(ok,group,label);}
static bool near(double a,double b) { return std::abs(a-b)<=1e-12*std::max({1.0,std::abs(a),std::abs(b)}); }
static bool vectorsEqual(const std::array<double,6>& actual,const json& g,const json& a) {
    for(int i=0;i<3;++i) if(!near(actual[i],g[i].get<double>())||!near(actual[i+3],a[i].get<double>())) return false;
    return true;
}

static const json* tupleExpectation(const json& oracle,const json& identity) {
    for(const auto& r:oracle["legacy"]["recipes"])
        if(r["deviceClass"]==identity["class"]&&r["gyroMatrixId"]==identity["gyro"]&&r["accelMatrixId"]==identity["accel"])return &r;
    return nullptr;
}
template<class Recipe> static bool reviewedTupleMatches(const Recipe& recipe,const std::string& gyro,const std::string& accel) {
    if constexpr(requires { recipe.identityGyroMatrixId; recipe.identityAccelMatrixId; })
        return recipe.identityGyroMatrixId==gyro && recipe.identityAccelMatrixId==accel;
    return false;
}
static void exerciseLegacy(const json& row,const json& r,bool firstTuple,json& results) {
    using namespace candidate;
    const auto label=row["id"].get<std::string>();const bool want=!r["controllerPreferred"].get<bool>();
    const bool reviewed=r.contains("selectedRecipe");const auto& motion=reviewed?r["selectedRecipe"]:r;
    const auto old=before::observe(row);
    setup(row);inputCaptureResolveHcMotionSelector();
    const auto id=g_machineIdentity;const auto selected=g_hcMotionSelector;
    check(identityJson(id)==old["identity"],"legacy-static",label+": frozen identity retained");
    check(id.deviceClass==r["deviceClass"].get<std::string>()&&id.gyroMatrixId==r["gyroMatrixId"].get<std::string>()&&id.accelMatrixId==r["accelMatrixId"].get<std::string>(),"legacy-static",label+": independent exact tuple");
    const auto* recipe=ymcc::hc_legacy_motion::find(id.deviceClass,id.gyroMatrixId,id.accelMatrixId);
    check(recipe!=nullptr&&ymcc::hc_legacy_motion::containsClass(id.deviceClass),"legacy-catalog",label+": actual catalog tuple");
    if(recipe)check(recipe->selectorId==r["selectorId"].get<std::string>()&&recipe->sourceScope==r["sourceScope"].get<std::string>()&&recipe->sourcePath==r["sourcePath"].get<std::string>()&&recipe->sourceSha256==r["sourceSha256"].get<std::string>()&&recipe->controllerPreferred==r["controllerPreferred"].get<bool>(),"legacy-catalog",label+": independently rebuilt C# bundle");
    HcMotionSelectorReceipt direct;
    const bool cold=resolveLegacyForTest(id,direct),bound=inputCaptureMotionSelectorBound();
    const auto active=probeActive(true,true,true,"p","p",73,73);
    if(reviewed) {
        const auto* modern=ymcc::hc_model_motion::find(id.deviceClass);
        checkReviewed(modern&&reviewedTupleMatches(*modern,id.gyroMatrixId,id.accelMatrixId),"reviewed-tuple",label+": exact canonical tuple retained");
        checkReviewed(selected.selectorId==motion["selectorId"].get<std::string>()&&selected.gyroMatrixId==motion["gyroMatrixId"].get<std::string>()&&selected.accelMatrixId==motion["accelMatrixId"].get<std::string>(),"reviewed-selection",label+": selected resource IDs");
        HcMotionSelectorReceipt rejected=selected;
        checkReviewed(!resolveOldOnlyForTest(id,rejected)&&receiptJson(rejected)==receiptJson(selected),"reviewed-no-fallback",label+": old resolver cannot reacquire superseded recipe");
        g_hcMotionSelector.selectorId=r["selectorId"].get<std::string>();g_hcMotionSelector.gyroMatrixId=r["gyroMatrixId"].get<std::string>();g_hcMotionSelector.accelMatrixId=r["accelMatrixId"].get<std::string>();g_hcMotionSelector.matrixSourceSha256=r["sourceSha256"].get<std::string>();
        checkReviewed(!inputCaptureMotionSelectorBound()&&probeActive(true,true,true,"p","p",73,73).processCalls==0,"reviewed-no-fallback",label+": stale valid legacy receipt rejected");g_hcMotionSelector=selected;
    }
    const std::string group=want?"legacy-admission":"legacy-controller";
    check(cold==want&&bound==want&&active.admitted==want,group,label+": cold to bound to actual admission");
    check(active.processCalls==(want?1U:0U),want?"legacy-effect":"legacy-controller",label+": real ProcessMotion effect gate");
    if(want) {
        check(resolveLegacyAvailable&&receiptJson(direct)==receiptJson(selected),"legacy-cold",label+": exact cold receipt");
        const auto cached=receiptJson(selected);
        check(cached["processor"]==id.processor&&cached["systemVersion"]==id.systemVersion&&cached["nativeFamily"]==static_cast<int>(id.family),"legacy-cold",label+": processor/version/native family cached");
        check(mockRegistryReads==0,"legacy-no-io",label+": no registry fallback");
    } else {
        g_hcMotionSelector.resolved=true;g_hcMotionSelector.selectorId="asus-rc73xa";
        check(!inputCaptureMotionSelectorBound()&&probeActive(true,true,true,"p","p",73,73).processCalls==0,"legacy-controller-cache",label+": old ASUS cache cannot admit controller-preferred class");
        g_hcMotionSelector=selected;
    }
    g_sensorMeta.gyroFamily="mock-winrt";g_sensorMeta.accelFamily="mock-winrt";
    const auto declared=inputCaptureCandidateMatrixReceipt(g_sensorMeta);
    for(const std::string sensor:{"gyro","accel"}) {
        if(want)check(declared[sensor]["sourceSha256"]==motion["sourceSha256"]&&declared[sensor]["sourceScope"]==motion["sourceScope"]&&declared[sensor].value(reviewed?"sourcePath":"sourceBundle",std::string{})==motion["sourcePath"].get<std::string>()&&!declared[sensor].contains(reviewed?"sourceBundle":"sourcePath"),"legacy-receipt",label+": selected source provenance");
        else check(declared[sensor]["sourceSha256"]==""&&!declared[sensor].contains("sourceBundle"),"legacy-unbound-receipt",label+": controller source never proves readiness");
    }
    if(reviewed)for(const std::string sensor:{"gyro","accel"})checkReviewed(declared[sensor]["id"]==motion[sensor=="gyro"?"gyroMatrixId":"accelMatrixId"],"reviewed-receipt",label+": effective matrix ID in evidence");
    const auto& gm=motion["matrices"][0];const auto& am=motion["matrices"][1];
    for(size_t i=0;i<gm["samples"].size();++i) {
        const auto& g=gm["samples"][i];const auto& a=am["samples"][i];
        const std::array<double,6> raw{g["input"][0],g["input"][1],g["input"][2],a["input"][0],a["input"][1],a["input"][2]};
        check(vectorsEqual(mapped(raw),g["output"],a["output"]),"legacy-matrices",label+": independent C# basis/signed sample");
        const auto sample=probeActive(true,true,true,"p","p",73,73,raw);
        if(want) {
            check(sample.admitted&&vectorsEqual(sample.active,g["output"],a["output"])&&sample.matrixIdentity=="hc-active-"+selected.selectorId,"legacy-active-axes",label+": actual six-axis activation");
            bool effect=sample.processCalls==1;for(int j=0;j<6;j++)effect=effect&&sample.processAxes[j]==static_cast<float>(sample.active[j]);
            check(effect,"legacy-effect",label+": actual mapped ProcessMotion arguments");
        } else check(!sample.admitted&&sample.processCalls==0,"legacy-controller",label+": declared matrix never activates controller route");
    }
    if(want) {
        for(const std::string field:{"resolved","class","family","gyro","accel","manufacturer","product","systemProduct","processor","systemVersion"}) {
            g_machineIdentity=id;g_hcMotionSelector=selected;
            if(field=="resolved")g_machineIdentity.resolved=false;
            else if(field=="class")g_machineIdentity.deviceClass="unknown";
            else if(field=="family")g_machineIdentity.family=YmccFamily::Unknown;
            else if(field=="gyro")g_machineIdentity.gyroMatrixId="hc-forged:gyro:x-y-z";
            else if(field=="accel")g_machineIdentity.accelMatrixId="hc-forged:accel:x-y-z";
            else if(field=="manufacturer")g_machineIdentity.manufacturer+="-CHANGED";
            else if(field=="product")g_machineIdentity.product+="-CHANGED";
            else if(field=="systemProduct")g_machineIdentity.systemProduct+="-CHANGED";
            else if(field=="processor")g_machineIdentity.processor+="-CHANGED";
            else g_machineIdentity.systemVersion+="-CHANGED";
            const auto rejected=probeActive(true,true,true,"p","p",73,73);
            check(!inputCaptureMotionSelectorBound()&&!rejected.admitted&&rejected.processCalls==0,"legacy-stale-identity",label+": rejects changed "+field);
        }
        g_machineIdentity=id;
        for(const std::string field:{"attempted","resolved","selector","asus-selector","class","gyro","accel","sha","manufacturer","product","systemProduct","processor","systemVersion","nativeFamily"}) {
            g_hcMotionSelector=selected;
            if(field=="attempted")g_hcMotionSelector.attempted=false;
            else if(field=="resolved")g_hcMotionSelector.resolved=false;
            else if(field=="selector")g_hcMotionSelector.selectorId="forged";
            else if(field=="asus-selector")g_hcMotionSelector.selectorId="asus-rc73ya";
            else if(field=="class")g_hcMotionSelector.deviceClass="forged";
            else if(field=="gyro")g_hcMotionSelector.gyroMatrixId="forged";
            else if(field=="accel")g_hcMotionSelector.accelMatrixId="forged";
            else if(field=="sha")g_hcMotionSelector.matrixSourceSha256=std::string(64,'0');
            else if(field=="manufacturer")g_hcMotionSelector.dmiManufacturer="forged";
            else if(field=="product")g_hcMotionSelector.dmiProduct="forged";
            else if(field=="systemProduct")g_hcMotionSelector.dmiSystemProduct="forged";
            else corruptLegacyReceipt(g_hcMotionSelector,field);
            const auto denied=probeActive(true,true,true,"p","p",73,73);
            check(!inputCaptureMotionSelectorBound()&&!denied.admitted&&denied.processCalls==0,"legacy-stale-cache",label+": rejects receipt "+field);
            const auto unbound=inputCaptureCandidateMatrixReceipt(g_sensorMeta);
            for(const std::string sensor:{"gyro","accel"})check(unbound[sensor]["sourceSha256"]==""&&unbound[sensor]["sourceScope"]==(reviewed?"unbound-new-model-resource-json":"unbound-legacy-model-matrix")&&!unbound[sensor].contains("sourceBundle"),"legacy-unbound-receipt",label+": source digest hidden on rejected cache");
        }
        g_hcMotionSelector=selected;
        for(const std::string field:{"resolved","family","class","gyro","accel"}) {
            auto bad=id;
            if(field=="resolved")bad.resolved=false;
            else if(field=="family")bad.family=YmccFamily::Unknown;
            else if(field=="class")bad.deviceClass="unknown";
            else if(field=="gyro")bad.gyroMatrixId="hc-forged:gyro:x-y-z";
            else bad.accelMatrixId="hc-forged:accel:x-y-z";
            HcMotionSelectorReceipt denied=selected;
            check(!resolveLegacyForTest(bad,denied)&&receiptJson(denied)==receiptJson(selected),"legacy-cold",label+": rejects forged cold "+field);
        }
        auto forged=id;forged.manufacturer="UNKNOWN";forged.product="UNKNOWN";forged.systemProduct="UNKNOWN";HcMotionSelectorReceipt rejected=selected;
        check(!resolveLegacyForTest(forged,rejected)&&receiptJson(rejected)==receiptJson(selected),"legacy-cold",label+": real raw DMI recheck rejects copied tuple");
        if(firstTuple)for(int bits=0;bits<64;bits++)for(const UINT64 gg:{UINT64(0),UINT64(72),UINT64(73),UINT64(74)})for(const UINT64 ag:{UINT64(72),UINT64(73)}) {
            g_realStickTest.enabled=(bits&1)!=0;const bool live=(bits&2)!=0,gyro=(bits&4)!=0,accel=(bits&8)!=0;
            const std::string gp=(bits&16)?"p":"",ap=(bits&32)?"p":"q";
            const bool expected=g_realStickTest.enabled&&live&&gyro&&accel&&!gp.empty()&&gp==ap&&gg==73&&ag==73;
            const auto effect=probeActive(live,gyro,accel,gp,ap,gg,ag);
            check(effect.admitted==expected&&effect.processCalls==(expected?1U:0U),"legacy-pair-grid",label+": actual pair/effect gate");
        }
        g_realStickTest.enabled=true;mockSink.activeBindingGeneration.store(0);
        check(probeActive(true,true,true,"p","p",0,0).processCalls==0,"legacy-resume",label+": zero generation");
        mockSink.activeBindingGeneration.store(74);
        for(const auto& pair:std::array<std::array<UINT64,2>,3>{{{73,73},{74,73},{73,74}}}) {
            const auto effect=probeActive(true,true,true,"p","p",pair[0],pair[1]);
            check(!effect.admitted&&effect.processCalls==0,"legacy-resume",label+": old/incomplete generation rejected");
        }
        check(probeActive(false,true,true,"p","p",74,74).processCalls==0,"legacy-resume",label+": suspended");
        check(probeActive(true,true,true,"p","q",74,74).processCalls==0,"legacy-resume",label+": new generation mixed provider");
        const auto resumed=probeActive(true,true,true,"p","p",74,74);
        check(resumed.admitted&&resumed.processCalls==1,"legacy-resume",label+": complete same new generation pair");
        check(mockRegistryReads==0,"legacy-no-io",label+": repeated bounds have no registry I/O");
    }
    results.push_back({{"identity",row},{"recipe",r["selectorId"]},{"controllerPreferred",r["controllerPreferred"]},{"bound",bound},{"admitted",active.admitted},{"processCalls",active.processCalls},{"receipt",receiptJson(selected)},{"sourceReceipt",declared}});
}


// Software class policy is independent of the candidate helper/headers. Only
// the bound projection is delegated to the real selector, as specified.
static bool expectedDefaultWindows(const json& oracle,const candidate::MachineIdentity& id){
 if(!candidate::defaultWindowsAvailable||!id.resolved||!id.gyroMatrixId.empty()||!id.accelMatrixId.empty())return false;
 // Independent fixed software policy; candidate selectors/catalogs never provide expected values.
 const auto clean=[](std::string s){s.erase(std::remove(s.begin(),s.end(),'\0'),s.end());const auto a=s.find_first_not_of(" \t\r\n"),b=s.find_last_not_of(" \t\r\n");if(a==std::string::npos)return std::string{};s=s.substr(a,b-a+1);for(auto& c:s)c=(char)std::toupper((unsigned char)c);return s;};
 for(const auto& row:oracle["motionCapabilityPolicy"]["defaultWindows"]){
  const auto f=row["family"].get<std::string>();const bool family=f=="Gpd"?id.family==candidate::YmccFamily::Gpd:id.family==candidate::YmccFamily::ValveSteamMachine;
  if(id.deviceClass==row["deviceClass"].get<std::string>()&&family&&clean(id.manufacturer)==row["manufacturer"].get<std::string>()&&clean(id.product)==row["product"].get<std::string>())return true;
 }return false;
}
static int defaultTransitionCheckDelta=0;
static void checkDefaultTransition(const json& old,const json& now,const std::string& label,int priorChecks=1){
 defaultTransitionCheckDelta+=2-priorChecks;
 check(old["identity"]==now["identity"]&&now["bound"].get<bool>()&&now["admitted"].get<bool>(),"default-transition",label+": exact model retains raw identity with independently tested default route");
 check(now["mapped"]==json::array({2, -3, 5, 11, 13, -17}),"default-transition",label+": unit axes are explicit");
}
// This expectation comes from the frozen generic contract, not the candidate
// catalog/helper. Recheck raw DMI with the retained independent preimage resolver.
static const json* expectedGenericRecipe(const json& oracle,const candidate::MachineIdentity& id) {
 if(!id.attempted||id.resolved||id.family!=candidate::YmccFamily::Unknown||!id.deviceClass.empty()||!id.gyroMatrixId.empty()||!id.accelMatrixId.empty()||!id.inputHidKey.empty())return nullptr;
 before::MachineIdentity fixed;fixed.manufacturer=id.manufacturer;fixed.product=id.product;fixed.systemProduct=id.systemProduct;fixed.processor=id.processor;fixed.systemVersion=id.systemVersion;before::ymccResolveFamily(fixed);
 if(fixed.resolved||fixed.family!=before::YmccFamily::Unknown||!fixed.deviceClass.empty()||!fixed.gyroMatrixId.empty()||!fixed.accelMatrixId.empty()||!fixed.inputHidKey.empty())return nullptr;
 const std::string wanted=before::normalizeHcDmi(id.manufacturer)=="ASUSTEK COMPUTER INC."?"ASUSTEK COMPUTER INC.":"*";
 for(const auto& row:oracle["genericFallbackPolicy"]["recipes"])if(row["manufacturer"]==wanted)return &row;
 return nullptr;
}
static json expectedGenericReceipt(const json& recipe,const candidate::MachineIdentity& id) {
 return {{"attempted",true},{"resolved",true},{"manufacturer",id.manufacturer},{"product",id.product},{"systemProduct",id.systemProduct},{"processor",id.processor},{"systemVersion",id.systemVersion},{"nativeFamily",0},{"deviceClass",recipe["sourceClass"]},{"selectorId",recipe["selectorId"]},{"gyro",recipe["gyroMatrixId"]},{"accel",recipe["accelMatrixId"]},{"sha256",recipe["sha256"]}};
}
static bool expectedGenericBound(const json& recipe,const candidate::MachineIdentity& id) {
 return candidate::receiptJson(candidate::g_hcMotionSelector)==expectedGenericReceipt(recipe,id);
}
static bool expectedGenericObservation(const json& oracle,const json& old,const json& now) {
 const auto* recipe=expectedGenericRecipe(oracle,candidate::g_machineIdentity);if(!recipe)return false;
 if(old["identity"]!=now["identity"]||old["identity"]["class"]!=""||old["identity"]["resolved"]!=false)return false;
 json expected=old;expected["receipt"]=expectedGenericReceipt(*recipe,candidate::g_machineIdentity);expected["bound"]=true;expected["admitted"]=true;expected["mapped"]=(*recipe)["expectedAxes"];expected["legacyMapped"]=(*recipe)["expectedAxes"];expected["registryReads"]=0;
 json matrix={{"admissionIndependent",true}};
 for(const std::string sensor:{"gyro","accel"})matrix[sensor]={{"id",(*recipe)[sensor=="gyro"?"gyroMatrixId":"accelMatrixId"]},{"sourceSha256",(*recipe)["sha256"]},{"sourcePath",(*recipe)["source"]},{"sourceScope","hc-generic-fallback-declared-transform"},{"provider","sensorapi"},{"legacyFieldOverride",false},{"validationScope","declared-generic-fallback;physical-axis-not-claimed"}};
 expected["recipeReceipt"]=matrix;expected["legacyRecipeReceipt"]=matrix;return expected==now;
}
static json withoutUnifiedEvidence(json value) {
    value.erase("unifiedCapabilityId");value.erase("unifiedCatalogSha256");return value;
}
static json expectedCapability(const json& oracle,const candidate::MachineIdentity& id,const candidate::InputCaptureSensorMeta& meta,bool observed,bool actualSelectorBound) {
    const auto& policy=oracle["motionCapabilityPolicy"];
    if(const auto* recipe=expectedGenericRecipe(oracle,id)) {
      const bool bound=expectedGenericBound(*recipe,id);
      return {{"schema",1},{"deviceClass",(*recipe)["sourceClass"]},{"machineDeviceClass",id.deviceClass},{"machineIdentityResolved",false},{"genericFallback",true},{"matrixProvenance","hc-generic-fallback-declared-transform"},{"modelSpecificAxesVerified",false},{"sourceRoute","windows-sensors"},{"declaredMatrixPair",false},{"modelSelectorImplemented",true},{"modelSelectorBound",bound},{"inputConsumerImplemented",true},{"controllerReportCaptureImplemented",false},{"controllerSampleObserved",false},{"calibrationOwnerImplemented",true},{"calibrationOwner","windows-source-qualified"},{"observedWindowsGyro",meta.gyroPresent},{"observedWindowsAccel",meta.accelPresent},{"reason",bound?"generic-transform-bound;observed-complete-current-pair-required":"awaiting-generic-source-binding"},{"virtualOutputProven",false},{"physicalAcceptanceVerified",false},{"verificationScope","generic-source-route;not-specific-handheld-or-physical-acceptance"}};
    }
    const bool controller=policy["controllers"].contains(id.deviceClass);
    const bool reader=controller&&policy["controllers"][id.deviceClass].get<bool>();
    bool windows=false;
    if(!controller) {
        windows=std::any_of(oracle["models"].begin(),oracle["models"].end(),[&](const json& m){return m["admitted"].get<bool>()&&m["fullName"].get<std::string>()==id.deviceClass;});
        windows=windows||std::any_of(oracle["reviewed"].begin(),oracle["reviewed"].end(),[&](const json& m){return m["deviceClass"].get<std::string>()==id.deviceClass;});
        windows=windows||std::any_of(policy["existingWindowsClasses"].begin(),policy["existingWindowsClasses"].end(),[&](const json& cls){return cls.get<std::string>()==id.deviceClass;});
        windows=windows||std::any_of(oracle["legacy"]["recipes"].begin(),oracle["legacy"]["recipes"].end(),[&](const json& r){return !r["controllerPreferred"].get<bool>()&&r["deviceClass"].get<std::string>()==id.deviceClass&&r["gyroMatrixId"].get<std::string>()==id.gyroMatrixId&&r["accelMatrixId"].get<std::string>()==id.accelMatrixId;});
    }
    const bool defaults=expectedDefaultWindows(oracle,id);windows=windows||defaults;
    const bool bound=windows&&actualSelectorBound;
    auto expected=policy["routes"][reader?"legionController":controller?"controller":windows?"windows":"unknown"];
    expected["schema"]=policy["schema"];expected["deviceClass"]=id.deviceClass;
    expected["declaredMatrixPair"]=!id.gyroMatrixId.empty()&&!id.accelMatrixId.empty();
    expected["modelSelectorBound"]=bound;expected["controllerReportCaptureImplemented"]=reader;
    expected["controllerSampleObserved"]=reader&&observed;
    expected["observedWindowsGyro"]=meta.gyroPresent;expected["observedWindowsAccel"]=meta.accelPresent;
    expected["reason"]=policy["reasons"][reader?"legionController":controller?"controller":windows?(bound?"bound":"unbound"):"unknown"];
    expected["virtualOutputProven"]=false;expected["physicalAcceptanceVerified"]=false;
    expected["verificationScope"]=policy["verificationScope"];
    if(defaults){expected["matrixProvenance"]="hc-default-unit-axes-no-device-resource";expected["modelSpecificAxesVerified"]=false;expected["calibrationOwner"]="windows-source-qualified";expected["reason"]=bound?"default-transform-bound;observed-complete-current-pair-required":"awaiting-exact-default-model-binding";}
    return expected;
}

static json exerciseEvidenceGeneration(const json& oracle) {
    using namespace candidate;
    check(evidenceDueAvailable,"capability-evidence-availability","actual pure generation helper extracted");
    if(!evidenceDueAvailable)return {{"available",false}};
    struct Case {uint64_t lastPower,lastBinding,power,binding;bool due;const char* label;};
    const std::array<Case,14> cases{{
      {0,0,0,0,false,"all zero"},{7,1,0,2,false,"power zero cannot publish"},
      {0,0,1,0,true,"first nonzero power"},{1,0,1,0,false,"same zero binding"},
      {1,0,1,1,true,"binding 0 to 1"},{1,1,1,1,false,"same complete key"},
      {1,1,1,2,true,"binding 1 to 2"},{1,1,1,0,true,"binding 1 to 0"},
      {1,2,2,2,true,"power changes with same binding"},{2,2,2,2,false,"repeat new key"},
      {2,2,1,2,true,"power rollback is a different key"},{0,99,1,99,true,"power appears with retained binding"},
      {UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX,false,"same max key"},
      {UINT64_MAX,0,UINT64_MAX,UINT64_MAX,true,"large binding change"},
    }};
    for(const auto& row:cases)check(evidenceDueForTest(row.lastPower,row.lastBinding,row.power,row.binding)==row.due,"capability-evidence-pure",row.label);
    setup(oracle["identities"][0]);inputCaptureResolveHcMotionSelector();
    g_sensorMeta.gyroPresent=true;g_sensorMeta.accelPresent=true;
    g_motionCapabilityLoggedPower=0;g_motionCapabilityLoggedBinding=0;mockCapabilityEvents.clear();
    struct Step {uint64_t power,binding;bool event;const char* label;};
    const std::array<Step,12> steps{{
      {0,0,false,"initial power zero"},{1,0,true,"start before sensor binding"},
      {1,0,false,"same key"},{1,1,true,"sensor first bound"},{1,1,false,"bound repeat"},
      {1,2,true,"sensor rebound"},{1,0,true,"sensor revoked"},{0,9,false,"power-zero suppression"},
      {1,0,false,"suppressed event did not overwrite last key"},{2,0,true,"new power epoch"},
      {2,1,true,"new power sensor binding"},{2,1,false,"repeat after resume"},
    }};
    json events=json::array();
    for(const auto& step:steps) {
      mockCapabilityPower=step.power;mockSink.activeBindingGeneration.store(step.binding);g_inputCaptureSink=&mockSink;
      const auto state=capabilityStateForTest();const auto oldPower=g_motionCapabilityLoggedPower,oldBinding=g_motionCapabilityLoggedBinding;
      const auto beforeCount=mockCapabilityEvents.size();
      const auto before=probeActive(true,true,true,"p","p",step.binding,step.binding);
      capabilityEvidenceLogForTest();
      check(mockCapabilityEvents.size()==beforeCount+(step.event?1:0),"capability-evidence-event",step.label);
      check(g_motionCapabilityLoggedPower==(step.event?step.power:oldPower)&&g_motionCapabilityLoggedBinding==(step.event?step.binding:oldBinding),"capability-evidence-event",std::string(step.label)+": BUS-owned key updates only with actual event");
      if(step.event) {
        const auto& event=mockCapabilityEvents.back();
        check(event["kind"]=="hc-motion-selector-receipt"&&event["powerGeneration"]==step.power&&event["sensorBindingGeneration"]==step.binding,"capability-evidence-event",std::string(step.label)+": actual generation fields");
        check(event["motionCapability"]==capabilityForTest(g_sensorMeta,false)&&!event["providerBindingProven"].get<bool>()&&!event["hostOutputAdmitted"].get<bool>(),"capability-evidence-event",std::string(step.label)+": diagnostic projection never grants provider/output admission");
        events.push_back(event);
      }
      const auto after=probeActive(true,true,true,"p","p",step.binding,step.binding);
      check(state==capabilityStateForTest()&&before.admitted==after.admitted&&before.active==after.active&&before.processCalls==after.processCalls,"capability-evidence-readonly",std::string(step.label)+": log gate leaves input binding/pair/effect unchanged");
    }
    g_inputCaptureSink=nullptr;mockCapabilityPower=3;capabilityEvidenceLogForTest();
    check(mockCapabilityEvents.back()["sensorBindingGeneration"]==0&&mockCapabilityEvents.back()["powerGeneration"]==3,"capability-evidence-event","absent sink uses atomic-reader fallback zero");
    g_inputCaptureSink=&mockSink;
    std::ofstream("motion-evidence-events.json")<<events.dump(2)<<'\n';
    return {{"available",true},{"pureCases",cases.size()},{"sequenceSteps",steps.size()},{"emittedEvents",mockCapabilityEvents.size()}};
}

static json exerciseCapabilities(const json& oracle) {
    using namespace candidate;
    check(capabilityAvailable,"capability-availability","actual production capability helper extracted");
    if(!capabilityAvailable)return {{"available",false},{"cases",0}};
    json rows=json::array(),results=json::array(),counts=json::object();std::set<std::string> controllerClasses;
    for(const std::string set:{"identities","legacyIdentities"})for(const auto& row:oracle[set])rows.push_back(row);
    for(const auto& row:oracle["legacy"]["cases"])rows.push_back(row);
    for(const auto& row:json::array({json{{"manufacturer","UNKNOWN"},{"product","UNKNOWN"}},json{{"manufacturer","AYANEO"},{"product","UNKNOWN"}},json{{"manufacturer","ONE-NETBOOK"},{"product","UNKNOWN"}}}))rows.push_back(row);
    int executions=0;
    for(size_t index=0;index<rows.size();index++) {
        const auto& row=rows[index];setup(row);inputCaptureResolveHcMotionSelector();
        const auto baseId=g_machineIdentity;const auto baseReceipt=g_hcMotionSelector;
        for(const std::string state:{"selected","unattempted","unresolved-cache","forged-sha","wrong-gyro","wrong-accel","unresolved-identity","wrong-family","asus-cache","receipt-product"}) {
            g_machineIdentity=baseId;g_hcMotionSelector=baseReceipt;
            if(state=="unattempted")g_hcMotionSelector={};
            else if(state=="unresolved-cache")g_hcMotionSelector.resolved=false;
            else if(state=="forged-sha")g_hcMotionSelector.matrixSourceSha256=std::string(64,'0');
            else if(state=="wrong-gyro")g_machineIdentity.gyroMatrixId="forged:gyro:x-y-z";
            else if(state=="wrong-accel")g_machineIdentity.accelMatrixId="forged:accel:x-y-z";
            else if(state=="unresolved-identity")g_machineIdentity.resolved=false;
            else if(state=="wrong-family")g_machineIdentity.family=YmccFamily::Unknown;
            else if(state=="asus-cache"){g_hcMotionSelector.attempted=true;g_hcMotionSelector.resolved=true;g_hcMotionSelector.selectorId="asus-rc73xa";}
            else if(state=="receipt-product")g_hcMotionSelector.dmiProduct+="-FORGED";
            const auto cls=g_machineIdentity.deviceClass;
            if(oracle["motionCapabilityPolicy"]["controllers"].contains(cls))controllerClasses.insert(cls);
            for(int bits=0;bits<8;bits++) {
                InputCaptureSensorMeta meta;meta.gyroPresent=(bits&1)!=0;meta.accelPresent=(bits&2)!=0;
                meta.gyroFamily="memory-windows";meta.accelFamily="memory-windows";
                const bool observed=(bits&4)!=0;
                g_sensorMeta=meta;g_sensorMeta.gyroPresent=!meta.gyroPresent;g_sensorMeta.accelPresent=!meta.accelPresent;
                mockControllerObservation=observed?std::optional<MockControllerMotionObservation>{MockControllerMotionObservation{}}:std::nullopt;
                mockControllerSnapshotReads=0;mockSink.activeBindingGeneration.store(73);g_realStickTest.enabled=true;
                const auto snapshot=capabilityStateForTest();const auto reads=mockRegistryReads;
                const auto effectBefore=probeActive(true,meta.gyroPresent,meta.accelPresent,"p","p",73,73);
                const bool realBound=inputCaptureMotionSelectorBound();
                const auto expected=expectedCapability(oracle,g_machineIdentity,meta,observed,realBound);
                const auto actual=capabilityForTest(meta,observed);
                const auto initial=capabilityInitialLogForTest(),detail=capabilitySampleLogForTest(meta);
                const std::string label=cls+":"+state+":"+std::to_string(bits);
                check(withoutUnifiedEvidence(actual)==expected,"capability-classification",label+": independent software/owner/presence projection");
                check(withoutUnifiedEvidence(initial["motionCapability"])==expectedCapability(oracle,g_machineIdentity,g_sensorMeta,false,realBound),"capability-initial-log",label+": actual initial log snapshots Windows meta without observing lease");
                check(withoutUnifiedEvidence(detail["motionCapability"])==expected,"capability-sample-log",label+": actual detail log uses memory lease snapshot has_value");
                check(snapshot==capabilityStateForTest(),"capability-readonly",label+": diagnostic does not mutate identity/receipt/readiness/generation");
                const auto effectAfter=probeActive(true,meta.gyroPresent,meta.accelPresent,"p","p",73,73);
                check(effectBefore.admitted==effectAfter.admitted&&effectBefore.active==effectAfter.active&&effectBefore.matrixIdentity==effectAfter.matrixIdentity&&effectBefore.processCalls==effectAfter.processCalls&&effectBefore.processAxes==effectAfter.processAxes,"capability-effect-unchanged",label+": helper/log projection does not change existing pair/axes/effect");
                check(mockRegistryReads==reads&&mockControllerSnapshotReads==1,"capability-memory-boundary",label+": no registry I/O; only actual detailed-log memory lease read");
                const auto route=expected["sourceRoute"].get<std::string>();counts[route]=counts.value(route,0)+1;++executions;
                if(state=="selected"&&(bits==0||bits==4))results.push_back({{"inputIndex",index},{"identity",row},{"receipt",actual},{"initialLog",initial},{"detailLog",detail}});
            }
        }
    }
    check(controllerClasses.size()==oracle["motionCapabilityPolicy"]["controllers"].size(),"capability-controller-coverage","all Lenovo four classes and SteamDeck execute both observation states");
    std::ofstream("motion-capability-results.json")<<results.dump(2)<<'\n';
    return {{"available",true},{"rawIdentityCases",rows.size()},{"executions",executions},{"routes",counts},{"controllerClasses",controllerClasses}};
}

#include "unified_binding.cpp"
int main(int argc,char** argv) {
  try {
    if(argc!=2) throw std::runtime_error("Expected oracle path");
    json oracle; std::ifstream(argv[1])>>oracle;
    using namespace candidate;
    json aliasResults=json::array(), legacyResults=json::array(), legacyClasses=json::object();
    check(std::size(ymcc::hc_model_motion::recipes)==9+oracle["reviewed"].size(),"catalog","nine new plus exactly eighteen reviewed resource recipes");
    for(const std::string cls:{"", "unknown", "HandheldCompanion.Devices.AYANEO3suffix", "HandheldCompanion.Devices.ayaneo3"})
      check(ymcc::hc_model_motion::find(cls)==nullptr,"catalog","lookup rejects unknown/partial/incorrect case class");
    for(const auto& row:oracle["identities"]) {
      const auto name=row["deviceClass"].get<std::string>();
      const auto it=std::find_if(oracle["models"].begin(),oracle["models"].end(),[&](const json& m){return m["name"]==name;});
      if(it==oracle["models"].end()) throw std::runtime_error("Missing model oracle");
      const auto& model=*it; const bool expected=model["admitted"].get<bool>();
      const auto frozenFeature=before::observe(row),historicalFeature=historical::observe(row);
      check(frozenFeature["identity"]==historicalFeature["identity"],"feature-preimages",name+": both before identity resolvers execute");
      const bool wantedBefore=model["admitted"].get<bool>();
      check(frozenFeature["bound"].get<bool>()==wantedBefore&&!historicalFeature["bound"].get<bool>(),"feature-preimages",name+": LegacyMotion before has new9 while historical before lacks it");
      setup(row); inputCaptureResolveHcMotionSelector();
      const bool bound=inputCaptureMotionSelectorBound();
      const bool admitted=probeConsumer(true,true,true,"provider-a","provider-a",73,73);
      check(g_machineIdentity.deviceClass==model["fullName"].get<std::string>(),"aliases",name+": real identity class");
      check(g_machineIdentity.family==(model["family"]=="Ayanero"?YmccFamily::Ayanero:(model["family"]=="OneXPlayer"?YmccFamily::OneXPlayer:YmccFamily::ValveSteamMachine)),"aliases",name+": family");
      const bool defaults=expectedDefaultWindows(oracle,g_machineIdentity);
      check(bound==(expected||defaults),"aliases",name+": selector bound");
      check(admitted==(expected||defaults),"aliases",name+": actual admission consumer");
      aliasResults.push_back({{"identity",row},{"bound",bound},{"admitted",admitted},{"receipt",receiptJson(g_hcMotionSelector)}});
      if(defaults){checkDefaultTransition(frozenFeature,candidate::observe(row),name,2);continue;}
      if(!expected) {
        check(!g_hcMotionSelector.resolved,"excluded",name+": no selector");
        check(ymcc::hc_model_motion::find(model["fullName"].get<std::string>())==nullptr,"excluded",name+": absent catalog");
        continue;
      }
      const auto& gm=model["matrices"][0]; const auto& am=model["matrices"][1];
      const auto* recipe=ymcc::hc_model_motion::find(model["fullName"].get<std::string>());
      check(recipe!=nullptr,"catalog",name+": present");
      if(recipe) {
        check(recipe->selectorId==model["selectorId"].get<std::string>(),"catalog",name+": selector id");
        check(recipe->gyroMatrixId==gm["id"].get<std::string>()&&recipe->accelMatrixId==am["id"].get<std::string>(),"catalog",name+": matrices");
        check(recipe->sourcePath==gm["source"].get<std::string>()&&recipe->sourcePath==am["source"].get<std::string>(),"catalog",name+": resource path");
        check(recipe->sourceSha256==gm["sourceSha256"].get<std::string>()&&recipe->sourceSha256==am["sourceSha256"].get<std::string>(),"catalog",name+": fixed resource SHA256");
      }
      check(mockRegistryReads==0,"entry",name+": new identity precedes registry fallback");
      check(g_hcMotionSelector.selectorId==model["selectorId"].get<std::string>(),"entry",name+": selector receipt");
      check(g_hcMotionSelector.matrixSourceSha256==gm["sourceSha256"].get<std::string>(),"entry",name+": resource digest receipt");
      check(resolveNewAvailable,"entry",name+": new resolver definition available");
      const auto identity=g_machineIdentity; const auto receipt=g_hcMotionSelector;
      HcMotionSelectorReceipt direct;
      check(resolveNewForTest(identity,direct)&&receiptJson(direct)==receiptJson(receipt),"resolver",name+": direct real resolver");
      // No second registry read or hidden rebinding when the once-only entry is called again.
      inputCaptureResolveHcMotionSelector();
      check(receiptJson(g_hcMotionSelector)==receiptJson(receipt)&&mockRegistryReads==0,"entry",name+": once-only entry");
      for(size_t i=0;i<gm["samples"].size();++i) {
        const auto& g=gm["samples"][i]; const auto& a=am["samples"][i];
        std::array<double,6> raw{g["input"][0],g["input"][1],g["input"][2],a["input"][0],a["input"][1],a["input"][2]};
        const auto activated=probeActive(true,true,true,"p","p",73,73,raw);
        check(activated.admitted&&vectorsEqual(activated.active,g["output"],a["output"])&&activated.matrixIdentity=="hc-active-"+receipt.selectorId,"active-axes",name+": actual six-axis activation sample");
        bool effectOk=activated.processCalls==1;
        for(int j=0;j<6;++j) effectOk=effectOk&&activated.processAxes[j]==static_cast<float>(activated.active[j]);
        check(effectOk,"effect-sink",name+": actual GamepadMotion call prefix receives activated axes");
        check(vectorsEqual(mapped(raw),g["output"],a["output"]),"matrices",name+": independent gyro/accel sample "+std::to_string(i));
        double x=0,y=0,z=0;
        check(ymccApplyNamedImuMatrix(gm["id"],raw[0],raw[1],raw[2],x,y,z)&&near(x,g["output"][0])&&near(y,g["output"][1])&&near(z,g["output"][2]),"matrices",name+": named gyro sample");
        check(ymccApplyNamedImuMatrix(am["id"],raw[3],raw[4],raw[5],x,y,z)&&near(x,a["output"][0])&&near(y,a["output"][1])&&near(z,a["output"][2]),"matrices",name+": named accel sample");
      }
      // A single declared source cannot assert device/provider readiness.
      g_sensorMeta.gyroFamily="mock-winrt"; g_sensorMeta.accelFamily="mock-winrt";
      const auto declared=inputCaptureCandidateMatrixReceipt(g_sensorMeta);
      for(const std::string sensor:{"gyro","accel"}) {
        const auto& matrix=sensor=="gyro"?gm:am;
        check(declared[sensor]["id"]==matrix["id"]&&declared[sensor]["sourceSha256"]==matrix["sourceSha256"],"receipts",name+": "+sensor+" source");
        check(declared[sensor]["sourceScope"]=="hc-a250b19a-resource-json"&&declared[sensor].value("sourcePath",std::string{})==matrix["source"].get<std::string>(),"receipts",name+": bound resource scope/path");
        check(declared[sensor]["provider"]=="mock-winrt"&&!declared[sensor]["legacyFieldOverride"].get<bool>(),"receipts",name+": "+sensor+" format");
      }
      check(declared["admissionIndependent"].get<bool>(),"receipts",name+": provenance independent");
      // Forged resolved state must be rederived from real raw DMI by the new resolver.
      for(const std::string field:{"resolved","family","class","gyro","accel","manufacturer","product","systemProduct"}) {
        auto bad=identity;
        if(field=="resolved") bad.resolved=false;
        else if(field=="family") bad.family=YmccFamily::MsiClaw;
        else if(field=="class") bad.deviceClass="HandheldCompanion.Devices.AYANEO3"==identity.deviceClass?"HandheldCompanion.Devices.OneXPlayer3":"HandheldCompanion.Devices.AYANEO3";
        else if(field=="gyro") bad.gyroMatrixId="hc-a250b19a:forged:gyro:x-y-z";
        else if(field=="accel") bad.accelMatrixId="hc-a250b19a:forged:accel:x-y-z";
        else if(field=="manufacturer") bad.manufacturer="UNKNOWN";
        else if(field=="product") bad.product="UNKNOWN";
        else bad.systemProduct="CHANGED";
        HcMotionSelectorReceipt rejected=receipt;
        if(field!="systemProduct") {
          check(!resolveNewForTest(bad,rejected),"forged",name+": resolver rejects "+field);
          check(receiptJson(rejected)==receiptJson(receipt),"forged",name+": failed resolution leaves caller receipt untouched");
        }
        g_machineIdentity=bad; g_hcMotionSelector=receipt;
        check(!inputCaptureMotionSelectorBound(),"identity-change",name+": cached binding rejects "+field);
      }
      const auto otherRow=std::find_if(oracle["identities"].begin(),oracle["identities"].end(),[&](const json& x){return x["deviceClass"]!=name&&x["deviceClass"]!="SteamMachine";});
      if(otherRow!=oracle["identities"].end()) {
        g_machineIdentity=rawIdentity(*otherRow);ymccResolveFamily(g_machineIdentity);g_hcMotionSelector=receipt;
        check(!inputCaptureMotionSelectorBound()&&!probeConsumer(true,true,true,"p","p",73,73),"identity-change",name+": newly resolved different valid identity rejects old receipt");
      }
      g_machineIdentity=identity;
      for(const std::string field:{"attempted","resolved","selector","asus-selector","class","gyro","accel","sha","manufacturer","product","systemProduct"}) {
        g_hcMotionSelector=receipt;
        if(field=="attempted") g_hcMotionSelector.attempted=false;
        else if(field=="resolved") g_hcMotionSelector.resolved=false;
        else if(field=="asus-selector") g_hcMotionSelector.selectorId="asus-rc73xa";
        else if(field=="selector") g_hcMotionSelector.selectorId="forged";
        else if(field=="class") g_hcMotionSelector.deviceClass="forged";
        else if(field=="gyro") g_hcMotionSelector.gyroMatrixId="forged";
        else if(field=="accel") g_hcMotionSelector.accelMatrixId="forged";
        else if(field=="sha") g_hcMotionSelector.matrixSourceSha256=std::string(64,'0');
        else if(field=="manufacturer") g_hcMotionSelector.dmiManufacturer="forged";
        else if(field=="product") g_hcMotionSelector.dmiProduct="forged";
        else g_hcMotionSelector.dmiSystemProduct="forged";
        check(!inputCaptureMotionSelectorBound(),"receipt-tamper",name+": rejects "+field);
        check(!probeConsumer(true,true,true,"p","p",73,73),"receipt-tamper",name+": consumer rejects "+field);
        const auto unbound=inputCaptureCandidateMatrixReceipt(g_sensorMeta);
        for(const std::string sensor:{"gyro","accel"}) check(unbound[sensor]["sourceSha256"]==""&&unbound[sensor]["sourceScope"]=="unbound-new-model-resource-json"&&!unbound[sensor].contains("sourcePath"),"unbound-receipt",name+": hides unattested source "+field);
      }
      g_hcMotionSelector=receipt;
      // Exercise the full selected boolean/provider/generation grid (512 cases per alias).
      for(int bits=0;bits<64;++bits) for(const UINT64 gg:{UINT64(0),UINT64(72),UINT64(73),UINT64(74)}) for(const UINT64 ag:{UINT64(72),UINT64(73)}) {
        g_realStickTest.enabled=(bits&1)!=0;
        const bool live=(bits&2)!=0, gyro=(bits&4)!=0, accel=(bits&8)!=0;
        const std::string gp=(bits&16)?"p":"", ap=(bits&32)?"p":"q";
        const bool want=g_realStickTest.enabled&&live&&gyro&&accel&&!gp.empty()&&gp==ap&&gg==73&&ag==73;
        const auto active=probeActive(live,gyro,accel,gp,ap,gg,ag);
        check(active.admitted==want,"admission-grid",name+": enabled/live/components/provider/current generation");
        check(active.processCalls==(want?1U:0U),"effect-grid",name+": admitted complete pair reaches finite effect sink");
      }
      g_realStickTest.enabled=true;
      mockSink.activeBindingGeneration.store(0);
      check(!probeConsumer(true,true,true,"p","p",0,0),"resume",name+": all-zero generation never admits");
      g_inputCaptureSink=nullptr;
      check(!probeConsumer(true,true,true,"p","p",73,73),"resume",name+": absent backend binding never admits");
      g_inputCaptureSink=&mockSink;mockSink.activeBindingGeneration.store(73);
      check(probeConsumer(true,true,true,"p","p",73,73),"resume",name+": before pause complete pair");
      mockSink.activeBindingGeneration.store(74);
      check(!probeConsumer(false,true,true,"p","p",73,73),"resume",name+": suspended old pair");
      check(!probeConsumer(true,true,true,"p","p",73,73),"resume",name+": resumed old pair");
      check(!probeConsumer(true,true,true,"p","p",74,73),"resume",name+": only new gyro");
      check(!probeConsumer(true,true,true,"p","q",74,74),"resume",name+": new mixed provider");
      check(probeConsumer(true,true,true,"p","p",74,74),"resume",name+": complete same new generation pair");
      check(mockRegistryReads==0,"cache-no-io",name+": repeated bounds/admission never read registry");
    }
    for(const auto& row:oracle["legacyIdentities"]) {
      const auto old=before::observe(row), now=candidate::observe(row);
      const auto label=row["deviceClass"].get<std::string>();
      const auto* recipe=tupleExpectation(oracle,old["identity"]);
      if(recipe) {
        check(old["identity"]==now["identity"]&&((*recipe).contains("selectedRecipe")||(old["mapped"]==now["mapped"]&&old["legacyMapped"]==now["legacyMapped"])),"legacy-static",label+": static identity retained; unrelated matrix routes unchanged");
        if((*recipe).contains("selectedRecipe")){const auto& selected=(*recipe)["selectedRecipe"];checkReviewed(now["receipt"]["selectorId"]==selected["selectorId"]&&now["receipt"]["gyro"]==selected["gyroMatrixId"]&&now["receipt"]["accel"]==selected["accelMatrixId"],"reviewed-selection",label+": actual selected matrix IDs");}
        const bool want=!(*recipe)["controllerPreferred"].get<bool>();
        check(now["bound"].get<bool>()==want&&now["admitted"].get<bool>()==want,want?"legacy-admission":"legacy-controller",label+": expected route adaptation");
      } else if(expectedDefaultWindows(oracle,candidate::g_machineIdentity))checkDefaultTransition(old,now,label);
      else if(expectedGenericRecipe(oracle,candidate::g_machineIdentity))check(expectedGenericObservation(oracle,old,now),"generic-transition",label+": frozen generic source contract; raw machine identity stays unresolved");
      else check(old==now,"preserved-routes",label+": unchanged excluded/no-matrix route");
      legacyResults.push_back({{"identity",row},{"before",old},{"after",now}});
      if(!legacyClasses.contains(label)) legacyClasses[label]={{"aliases",0},{"allUnboundBefore",true},{"allUnboundAfter",true},{"selectorPresentBefore",false},{"selectorPresentAfter",false}};
      auto& entry=legacyClasses[label];entry["aliases"]=entry["aliases"].get<int>()+1;
      entry["allUnboundBefore"]=entry["allUnboundBefore"].get<bool>()&&!old["bound"].get<bool>();
      entry["allUnboundAfter"]=entry["allUnboundAfter"].get<bool>()&&!now["bound"].get<bool>();
      entry["selectorPresentBefore"]=entry["selectorPresentBefore"].get<bool>()||old["receipt"]["resolved"].get<bool>();
      entry["selectorPresentAfter"]=entry["selectorPresentAfter"].get<bool>()||now["receipt"]["resolved"].get<bool>();
    }
    json legacyCases=json::array();std::set<std::string> exercisedTuples;
    check(std::size(ymcc::hc_legacy_motion::recipes)==oracle["legacy"]["recipes"].size(),"legacy-catalog","exact independent proven tuple count");
    for(const auto& row:oracle["legacy"]["cases"]) {
      if(row["expectedRecipe"].is_null()) {
        const auto old=before::observe(row),now=candidate::observe(row);
        const auto* existingTuple=tupleExpectation(oracle,old["identity"]);
        if(existingTuple) exerciseLegacy(row,*existingTuple,exercisedTuples.insert((*existingTuple)["selectorId"].get<std::string>()).second,legacyCases);
        else if(expectedDefaultWindows(oracle,candidate::g_machineIdentity))checkDefaultTransition(old,now,row["id"].get<std::string>());
        else if(expectedGenericRecipe(oracle,candidate::g_machineIdentity))check(expectedGenericObservation(oracle,old,now),"generic-transition",row["id"].get<std::string>()+": fixed generic source contract without exact model claim");
        else check(old==now,"preserved-routes",row["id"].get<std::string>()+": old MSI/Xbox/no-source route");
        continue;
      }
      const auto it=std::find_if(oracle["legacy"]["recipes"].begin(),oracle["legacy"]["recipes"].end(),[&](const json& r){return r["selectorId"]==row["expectedRecipe"];});
      if(it==oracle["legacy"]["recipes"].end())throw std::runtime_error("Missing legacy tuple oracle");
      exerciseLegacy(row,*it,exercisedTuples.insert((*it)["selectorId"].get<std::string>()).second,legacyCases);
    }
    for(const auto& a:oracle["legacy"]["recipes"])for(const auto& b:oracle["legacy"]["recipes"]) {
      if(a["deviceClass"]!=b["deviceClass"]||a["selectorId"]==b["selectorId"])continue;
      const auto cls=a["deviceClass"].get<std::string>(),g=a["gyroMatrixId"].get<std::string>(),acc=b["accelMatrixId"].get<std::string>();
      const bool legal=std::any_of(oracle["legacy"]["recipes"].begin(),oracle["legacy"]["recipes"].end(),[&](const json& r){return r["deviceClass"]==cls&&r["gyroMatrixId"]==g&&r["accelMatrixId"]==acc;});
      check((ymcc::hc_legacy_motion::find(cls,g,acc)!=nullptr)==legal,"legacy-crosspair","GPD exact ordered pair lookup");
      const auto row=std::find_if(oracle["legacy"]["cases"].begin(),oracle["legacy"]["cases"].end(),[&](const json& r){return r["expectedRecipe"]==a["selectorId"];});
      setup(*row);inputCaptureResolveHcMotionSelector();g_machineIdentity.gyroMatrixId=b["gyroMatrixId"];g_machineIdentity.accelMatrixId=b["accelMatrixId"];
      HcMotionSelectorReceipt rejected;
      check(!resolveLegacyForTest(g_machineIdentity,rejected)&&!inputCaptureMotionSelectorBound()&&probeActive(true,true,true,"p","p",73,73).processCalls==0,"legacy-crosspair","valid alternate tuple cannot replace original raw DMI/cache");
      if(!legal) {
        g_machineIdentity.gyroMatrixId=g;g_machineIdentity.accelMatrixId=acc;
        check(!resolveLegacyForTest(g_machineIdentity,rejected)&&!inputCaptureMotionSelectorBound()&&probeActive(true,true,true,"p","p",73,73).processCalls==0,"legacy-crosspair","arbitrary mixed tuple cannot enter actual consumer");
      }
    }
    for(const auto& excluded:oracle["legacy"]["excluded"])check(!ymcc::hc_legacy_motion::containsClass(excluded.get<std::string>()),"legacy-catalog","MSI/Xbox/new9/SteamMachine excluded from legacy catalog");
    std::ofstream("legacy-tuple-consumers.json")<<legacyCases.dump(2)<<'\n';
    // Unknowns and forged classes cannot be promoted by availability of a parseable recipe.
    for(const auto& row:json::array({json{{"manufacturer","UNKNOWN"},{"product","UNKNOWN"}},json{{"manufacturer","AYANEO"},{"product","UNKNOWN"}},json{{"manufacturer","ONE-NETBOOK"},{"product","UNKNOWN"}}})) {
      setup(row); inputCaptureResolveHcMotionSelector();
      if(expectedGenericRecipe(oracle,g_machineIdentity)) {
        const auto old=before::observe(row),now=candidate::observe(row);
        check(expectedGenericObservation(oracle,old,now),"unknown-generic","unknown machine remains unknown while its explicit generic motion owner is independently checked");
      } else check(!inputCaptureMotionSelectorBound()&&!probeConsumer(true,true,true,"p","p",73,73),"unknown","unknown model safe");
      g_machineIdentity.deviceClass="HandheldCompanion.Devices.AYANEO3";g_machineIdentity.family=YmccFamily::Ayanero;g_machineIdentity.resolved=true;
      g_machineIdentity.gyroMatrixId=oracle["models"][0]["matrices"][0]["id"];g_machineIdentity.accelMatrixId=oracle["models"][0]["matrices"][1]["id"];
      HcMotionSelectorReceipt r;
      check(!resolveNewForTest(g_machineIdentity,r),"unknown","forged class/family/matrices with unknown DMI");
    }
    // Explicit ROG fallback normalization and exact old MSI behavior remain observable.
    for(const std::string board:{"RC73XA","RC73YA","RC71L","RC72LA","RC73XX"}) {
      json row={{"manufacturer","ASUSTEK COMPUTER INC."},{"product",board}};
      if(board!="RC71L"&&board!="RC72LA") {
        const auto old=before::observe(row),now=candidate::observe(row);
        if(expectedGenericRecipe(oracle,g_machineIdentity))check(expectedGenericObservation(oracle,old,now),"generic-transition",board+": unknown ASUS uses only the frozen generic motion contract");
        else check(old==now,"preserved-routes",board+": existing Xbox route unchanged");
      }
      setup(row);g_machineIdentity={};mockRegistry[L"BaseBoardManufacturer"]=std::string(" asustek computer inc. ")+std::string(1,'\0');mockRegistry[L"BaseBoardProduct"]=" "+board+std::string(1,'\0')+" ";
      inputCaptureResolveHcMotionSelector();
      check(g_hcMotionSelector.resolved==(board=="RC73XA"||board=="RC73YA"),"legacy-rog",board+": memory registry fallback normalization");
    }
    json legacyUnbound=json::array(),legacyBound=json::array(),aliasSummary=json::object();
    for(auto it=legacyClasses.begin();it!=legacyClasses.end();++it) {
      auto item=it.value();item["deviceClass"]=it.key();
      if(item["allUnboundBefore"].get<bool>())legacyUnbound.push_back(item);else legacyBound.push_back(item);
    }
    for(const auto& item:aliasResults) {
      const auto name=item["identity"]["deviceClass"].get<std::string>();
      if(!aliasSummary.contains(name))aliasSummary[name]={{"aliases",0},{"bound",0},{"admitted",0}};
      auto& summary=aliasSummary[name];summary["aliases"]=summary["aliases"].get<int>()+1;
      summary["bound"]=summary["bound"].get<int>()+(item["bound"].get<bool>()?1:0);
      summary["admitted"]=summary["admitted"].get<int>()+(item["admitted"].get<bool>()?1:0);
    }
    json remainingUnbound=json::array();for(auto it=legacyClasses.begin();it!=legacyClasses.end();++it)if(it.value()["allUnboundAfter"].get<bool>())remainingUnbound.push_back(it.key());
    json legacyCoverage={{"priorUnboundClassCount",legacyUnbound.size()},{"remainingUnboundClasses",remainingUnbound},{"remainingUnboundClassCount",remainingUnbound.size()},{"tupleCount",exercisedTuples.size()},{"classes",legacyClasses.size()},{"aliases",oracle["legacyIdentities"].size()},
      {"unboundClassCount",legacyUnbound.size()},{"unboundClasses",legacyUnbound},{"boundClasses",legacyBound},
      {"interpretation","Old selectors/no-matrix routes retain prior behavior; proven non-controller legacy tuples now exercise actual Windows consumers. Controller-preferred tuples remain blocked. Remaining unbound entries require route classification, not automatic bug attribution."}};
    std::ofstream("legacy-runtime-coverage.json")<<legacyCoverage.dump(2)<<'\n';
    std::ofstream("alias-results.json")<<aliasResults.dump(2)<<'\n';
    std::ofstream("legacy-results.json")<<legacyResults.dump(2)<<'\n';
    const int consumerChecks=checks;
    const auto unifiedBindingCoverage=consumerOnlyControl?json{{"skipped",true}}:exerciseUnifiedBindings(oracle);
    const auto capabilityCoverage=consumerOnlyControl?json{{"skipped",true},{"reason","control isolates the original 114560 consumer assertions"}}:exerciseCapabilities(oracle);
    const auto evidenceCoverage=consumerOnlyControl?json{{"skipped",true}}:exerciseEvidenceGeneration(oracle);
    check(consumerChecks-reviewedExtraChecks-defaultTransitionCheckDelta==114560,"capability-prior-regression","original consumer assertions plus explicitly counted reviewed resource checks");
    json report={{"reviewedResourceModels",oracle["reviewed"].size()},{"reviewedResourceChecks",reviewedExtraChecks},{"consumerChecks",consumerChecks},{"capabilityChecks",checks-consumerChecks},{"unifiedBindingCoverage",unifiedBindingCoverage},{"capabilityCoverage",capabilityCoverage},{"evidenceGenerationCoverage",evidenceCoverage},{"status",failures.empty()?"pass":"fail"},{"checks",checks},{"groups",groups},{"failures",failures},
      {"legacyConsumerCases",legacyCases.size()},{"legacyFeatureAvailable",resolveLegacyAvailable},{"new9FeatureAvailable",resolveNewAvailable},{"aliasCoverage",aliasSummary},{"legacyRuntimeCoverage",legacyCoverage},{"aliases",oracle["identities"].size()},{"imuClasses",9},{"legacyAliases",oracle["legacyIdentities"].size()},
      {"fullBusExecuted",false},{"realSleepExecuted",false},{"realDevicesAccessed",false}};
    std::cout<<report.dump()<<'\n';return failures.empty()?0:1;
  } catch(const std::exception& e) { std::cerr<<"HARNESS_ERROR: "<<e.what()<<'\n';return 2; }
}

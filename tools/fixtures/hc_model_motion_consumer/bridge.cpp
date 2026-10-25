// Test-only state setup/serialization; all decisions/remapping run extracted C++.
static MachineIdentity rawIdentity(const json& row) {
    MachineIdentity id;
    id.manufacturer=ymccDmiNormalize(row.value("manufacturer",std::string{})); id.product=ymccDmiNormalize(row.value("product",std::string{}));
    id.systemProduct=ymccDmiNormalize(row.value("systemProduct",std::string{})); id.processor=ymccDmiNormalize(row.value("processor",std::string{}));
    id.systemVersion=ymccDmiNormalize(row.value("systemVersion",std::string{})); return id;
}
static void setup(const json& row) {
    g_machineIdentity=rawIdentity(row);
    ymccResolveFamily(g_machineIdentity);
    g_machineIdentity.attempted=true;
    g_hcMotionSelector={}; g_sensorMeta={};g_inputCaptureSink=&mockSink;
    mockRegistry={{L"BaseBoardManufacturer",g_machineIdentity.manufacturer},
        {L"BaseBoardProduct",g_machineIdentity.product},{L"SystemProductName",g_machineIdentity.systemProduct}};
    mockRegistryReads=0; mockSink.activeBindingGeneration.store(73); g_realStickTest.enabled=true;
}
template<class Receipt> static json receiptJson(const Receipt& r) {
    json result= {{"attempted",r.attempted},{"resolved",r.resolved},{"manufacturer",r.dmiManufacturer},
        {"product",r.dmiProduct},{"systemProduct",r.dmiSystemProduct},{"selectorId",r.selectorId},
        {"deviceClass",r.deviceClass},{"gyro",r.gyroMatrixId},{"accel",r.accelMatrixId},
        {"sha256",r.matrixSourceSha256}};
    if constexpr(requires {r.dmiProcessor;r.dmiSystemVersion;r.nativeFamily;}) {
        result["processor"]=r.dmiProcessor;result["systemVersion"]=r.dmiSystemVersion;result["nativeFamily"]=r.nativeFamily;
    } else {result["processor"]="";result["systemVersion"]="";result["nativeFamily"]=0;}
    return result;
}
static json identityJson(const MachineIdentity& id) {
    return {{"class",id.deviceClass},{"family",ymccFamilyName(id.family)},{"gyro",id.gyroMatrixId},
        {"accel",id.accelMatrixId},{"hid",id.inputHidKey},{"resolved",id.resolved}};
}
static std::array<double,6> mapped(std::array<double,6> raw) {
    std::array<double,6> result{};
    inputCaptureApplyHcMatrixCandidate(raw[0],raw[1],raw[2],raw[3],raw[4],raw[5],
        result[0],result[1],result[2],result[3],result[4],result[5]); return result;
}
static json observe(const json& row) {
    setup(row); inputCaptureResolveHcMotionSelector();
    g_sensorMeta.gyroFamily="sensorapi"; g_sensorMeta.accelFamily="sensorapi";
    json result={{"identity",identityJson(g_machineIdentity)},{"receipt",receiptJson(g_hcMotionSelector)},
        {"bound",inputCaptureMotionSelectorBound()},{"mapped",mapped({2,-3,5,11,13,-17})},
        {"recipeReceipt",inputCaptureCandidateMatrixReceipt(g_sensorMeta)},{"registryReads",mockRegistryReads}};
    result["admitted"]=probeConsumer(true,true,true,"provider-a","provider-a",73,73);
    g_sensorMeta.gyroHcLegacyFields=true; g_sensorMeta.accelHcLegacyFields=true;
    result["legacyMapped"]=mapped({2,-3,5,11,13,-17});
    result["legacyRecipeReceipt"]=inputCaptureCandidateMatrixReceipt(g_sensorMeta);
    return result;
}

template<class Receipt> static void corruptLegacyReceipt(Receipt& r,const std::string& field) {
    if constexpr(requires {r.dmiProcessor;r.dmiSystemVersion;r.nativeFamily;}) {
        if(field=="processor")r.dmiProcessor+="-CHANGED";
        else if(field=="systemVersion")r.dmiSystemVersion+="-CHANGED";
        else if(field=="nativeFamily")r.nativeFamily=99;
    }
}

static json capabilityStateForTest() {
    return {{"identity",identityJson(g_machineIdentity)},{"receipt",receiptJson(g_hcMotionSelector)},
        {"manufacturer",g_machineIdentity.manufacturer},{"product",g_machineIdentity.product},
        {"systemProduct",g_machineIdentity.systemProduct},{"processor",g_machineIdentity.processor},
        {"systemVersion",g_machineIdentity.systemVersion},{"identityAttempted",g_machineIdentity.attempted},
        {"enabled",g_realStickTest.enabled},{"generation",mockSink.activeBindingGeneration.load()},
        {"metaGyro",g_sensorMeta.gyroPresent},{"metaAccel",g_sensorMeta.accelPresent}};
}

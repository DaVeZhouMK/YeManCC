template<class Recipe> static bool unifiedControllerPreferred(const Recipe& recipe) {
    if constexpr(requires {recipe.controllerPreferred;}) return recipe.controllerPreferred;
    return false;
}
// Every assertion executes the generated registration against the original native
// table reference. The end-to-end cases below also execute production Bound and
// the original pair-admission consumer, including removal/corruption of binding.
static json exerciseUnifiedBindings(const json& oracle) {
    using namespace candidate;
    namespace motion=ymcc::capabilities::motion;
    const auto start=checks;unsigned registered=0,blockedController=0,selectedConsumers=0;
    auto table=[&](motion::Kind kind,const auto& recipes) {
        for(const auto& recipe:recipes) {
            ++registered;const auto binding=motion::bind(kind,&recipe,recipes);
            const bool blocked=unifiedControllerPreferred(recipe);
            if(blocked){++blockedController;check(!binding.registration,"unified-controller-boundary","controller-preferred recipe cannot bind Windows consumer");continue;}
            const auto* row=motion::descriptor(binding);
            check(row&&row->kind==kind&&row->deviceClass==recipe.deviceClass&&row->selectorId==recipe.selectorId&&row->gyroMatrixId==recipe.gyroMatrixId&&row->accelMatrixId==recipe.accelMatrixId&&row->sourceSha256==recipe.sourceSha256,"unified-registration",std::string(recipe.deviceClass)+": original exact recipe metadata");
            check(motion::consume(binding,kind,recipes)==&recipe,"unified-reference",std::string(recipe.deviceClass)+": consumer gets original table address");
            auto copy=recipe;check(!motion::bind(kind,&copy,recipes).registration,"unified-copy-rejected","matching copied values cannot substitute original reference");
            auto bad=binding;bad.recipe=&copy;check(!motion::consume(bad,kind,recipes),"unified-copy-rejected","copied recipe pointer rejected at consumption");
            bad=binding;bad.slot=motion::count(kind);check(!motion::consume(bad,kind,recipes),"unified-slot-rejected","out of range slot rejected");
            bad=binding;bad.registration=nullptr;check(!motion::consume(bad,kind,recipes),"unified-registration-rejected","missing registration rejected");
            bad=binding;bad.kind=motion::Kind::None;check(!motion::consume(bad,kind,recipes),"unified-kind-rejected","route kind mismatch rejected");
            const auto crossed=motion::bindOne(kind,binding.slot,&copy);
            copy.gyroMatrixId="wrong-ordered-gyro";check(!motion::bindOne(kind,binding.slot,&copy).registration,"unified-tuple-rejected","different gyro cannot rebind");
            copy=recipe;copy.accelMatrixId="wrong-ordered-accel";check(!motion::bindOne(kind,binding.slot,&copy).registration,"unified-tuple-rejected","different accel cannot rebind");
            copy=recipe;copy.sourceSha256="wrong-source";check(!motion::bindOne(kind,binding.slot,&copy).registration,"unified-source-rejected","different lineage cannot rebind");
            (void)crossed;
        }
    };
    table(motion::Kind::Reviewed,ymcc::hc_model_motion::recipes);
    table(motion::Kind::Legacy,ymcc::hc_legacy_motion::recipes);
    table(motion::Kind::Default,ymcc::hc_default_motion::recipes);
    table(motion::Kind::Private,ymcc::hc_oem_windows_motion::recipes);
    for(unsigned slot=0;slot<2;++slot) {
        const auto& recipe=slot==0?ymcc::hc_generic_motion::defaultDevice:ymcc::hc_generic_motion::asus;++registered;
        const auto b=motion::bindOne(motion::Kind::Generic,slot,&recipe);const auto* d=motion::descriptor(b);
        check(d&&d->origin=="generic-transform-only"&&d->deviceId.find("ymcc.motion.generic.")==0,"unified-generic-origin","generic transform never claims handheld identity");
        check(motion::consumeOne(b,motion::Kind::Generic,slot,recipe)==&recipe,"unified-generic-reference","generic consumer retains actual recipe");
        auto copy=recipe;auto bad=b;bad.recipe=&copy;
        check(!motion::consumeOne(bad,motion::Kind::Generic,slot,recipe),"unified-generic-copy","generic matching copy rejected");
    }
    // Exercise real identity selectors, per-frame Bound, pair admission and
    // recovery with the same source. No synthetic ready/authorization cache.
    for(const char* key:{"identities","legacyIdentities"})for(const auto& identity:oracle[key]) {
        setup(identity);inputCaptureResolveHcMotionSelector();
        const auto selected=g_hcMotionSelector;const auto* d=motion::descriptor(selected.unifiedBinding);
        if(!d)continue;++selectedConsumers;
        check(inputCaptureMotionSelectorBound(),"unified-actual-bound","actual selected model consumes unified binding");
        const auto evidence=inputCaptureMotionCapabilityReceipt(g_sensorMeta,false);
        check(evidence["unifiedCapabilityId"].get<std::string>()==d->capabilityId&&evidence["unifiedCatalogSha256"].get<std::string>()==motion::catalogSha256,"unified-actual-evidence","actual log carries total identity and catalog hash");
        g_hcMotionSelector.unifiedBinding={};
        check(!inputCaptureMotionSelectorBound(),"unified-actual-missing","missing total binding blocks actual Bound");
        check(!probeConsumer(true,true,true,"provider-a","provider-a",73,73),"unified-actual-pair","missing total binding blocks actual paired-source consumer");
        g_hcMotionSelector=selected;g_hcMotionSelector.unifiedBinding.registration=nullptr;
        check(!inputCaptureMotionSelectorBound(),"unified-actual-corrupt","removed descriptor blocks actual Bound");
        g_hcMotionSelector=selected;
        check(inputCaptureMotionSelectorBound(),"unified-actual-recovery","same valid binding recovers without hardware side effects");
        const auto reads=mockRegistryReads;for(unsigned n=0;n<100;++n)(void)inputCaptureMotionSelectorBound();
        check(mockRegistryReads==reads,"unified-no-extra-io","stable total consumption adds no registry I/O");
    }
    unsigned privateSelected=0;
    struct PrivateCase { const char* board;const char* model;bool msi;bool sub; };
    for(const auto item:{PrivateCase{"MS-1T41","ClawA1M",true,false},
        {"MS-1T42","ClawA2VM",true,true},{"MS-1T52","ClawA2VM",true,true},
        {"MS-1T8K","ClawBZ2EM",true,true},{"MS-1T91","ClawCG3EM",true,true},
        {"RC73YA","XboxROGAlly",false,false},{"RC73XA","XboxROGAllyX",false,false}}) {
        const std::string label=item.model;setup({{"manufacturer",item.msi?"MICRO-STAR INTERNATIONAL CO., LTD.":"ASUSTEK COMPUTER INC."},{"product",item.board}});
        inputCaptureResolveHcMotionSelector();const auto selected=g_hcMotionSelector;const auto identity=g_machineIdentity;
        const auto* registration=motion::descriptor(selected.unifiedBinding);
        check(registration&&registration->kind==motion::Kind::Private&&registration->deviceClass=="HandheldCompanion.Devices."+label,"private-registration",label+": exact total binding on actual selector");
        const auto* original=ymcc::hc_oem_windows_motion::find(identity.deviceClass);
        check(inputCapturePrivateHcMotionSelectorRecipe()==original&&original,"private-consumption",label+": original recipe pointer reaches actual Bound");
        check(inputCaptureMotionSelectorBound(),"private-bound",label+": exact live model bound");
        const auto active=probeActive(true,true,true,"p","p",73,73);
        const std::array<double,6> expected=item.sub?std::array<double,6>{2,-3,-5,-11,17,13}:std::array<double,6>{2,5,3,-11,17,13};
        check(active.admitted&&active.processCalls==1&&active.active==expected,"private-final-effect",label+": admitted original six axes reach real production call prefix");
        const auto evidence=inputCaptureMotionCapabilityReceipt(g_sensorMeta,false);
        check(registration&&evidence["unifiedCapabilityId"].get<std::string>()==registration->capabilityId,"private-observability",label+": actual receipt contains total capability identity");
        for(const std::string fault:{"registration","recipe","slot","kind","selector","digest","model","manufacturer","board","system-product","identity-gyro","identity-accel","family","attempted"}) {
            g_machineIdentity=identity;g_hcMotionSelector=selected;
            if(fault=="registration")g_hcMotionSelector.unifiedBinding.registration=nullptr;
            if(fault=="recipe")g_hcMotionSelector.unifiedBinding.recipe=nullptr;
            if(fault=="slot")g_hcMotionSelector.unifiedBinding.slot=motion::count(motion::Kind::Private);
            if(fault=="kind")g_hcMotionSelector.unifiedBinding.kind=motion::Kind::Legacy;
            if(fault=="selector")g_hcMotionSelector.selectorId+="changed";
            if(fault=="digest")g_hcMotionSelector.matrixSourceSha256+="changed";
            if(fault=="model")g_machineIdentity.deviceClass="HandheldCompanion.Devices.Unknown";
            if(fault=="manufacturer")g_machineIdentity.manufacturer="UNKNOWN";
            if(fault=="board")g_machineIdentity.product="UNKNOWN";
            if(fault=="system-product")g_machineIdentity.systemProduct="changed";
            if(fault=="identity-gyro")g_machineIdentity.gyroMatrixId="changed";
            if(fault=="identity-accel")g_machineIdentity.accelMatrixId="changed";
            if(fault=="family")g_machineIdentity.family=YmccFamily::Unknown;
            if(fault=="attempted")g_hcMotionSelector.attempted=false;
            check(!inputCaptureMotionSelectorBound()&&!probeConsumer(true,true,true,"p","p",73,73),"private-refusal",label+":"+fault+": corrupted current binding cannot reach pair consumer");
        }
        g_machineIdentity=identity;g_hcMotionSelector=selected;
        check(!probeConsumer(true,true,true,"p","other",73,73)&&!probeConsumer(true,true,true,"p","p",72,73)&&!probeConsumer(true,true,false,"p","p",73,73),"private-live-pair",label+": provider/generation/missing axes remain independent live gates");
        const auto reads=mockRegistryReads;for(unsigned n=0;n<100;++n)check(inputCaptureMotionSelectorBound(),"private-stable-bound",label+": retained original binding");
        check(mockRegistryReads==reads,"private-no-io",label+": repeated binding consumes without registry IO");
        check(inputCaptureMotionSelectorBound()&&probeConsumer(true,true,true,"p","p",73,73),"private-recovery",label+": restored current identity recovers");
        ++privateSelected;
    }
    return {{"registeredRecipes",registered},{"privateSelectedBoardCases",privateSelected},{"controllerPreferredWindowsBlocked",blockedController},{"actualSelectedConsumers",selectedConsumers},{"checks",checks-start},{"completeMotionDomain",false},{"physicalAcceptance",false}};
}

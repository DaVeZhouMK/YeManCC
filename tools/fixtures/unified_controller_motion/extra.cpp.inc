static void unifiedProviderProtocol() {
    const char* names[]={"LegionGoTablet","LegionGoTablet2","LegionGoSZ1","LegionGoSZ2"};
    for(unsigned n=0;n<4;++n){
        g_machineIdentity.family=YmccFamily::LenovoLegionGo;
        g_machineIdentity.deviceClass=std::string("HandheldCompanion.Devices.")+names[n];
        auto source=physical("USB\\UNIFIED-PROTOCOL");const bool goS=n>=2;
        source.vendorId=goS?0x1A86:0x17EF;source.productId=goS?0xE310:0x6182;
        const auto binding=legionMotionModelBinding();CHECK(binding.known());CHECK(binding.token()==n+1);CHECK(binding.bankCount()==(goS?1u:2u));
        CHECK(legionMotionHardwareAdmitted(source.vendorId,source.productId));
        CHECK(!legionMotionHardwareAdmitted(source.vendorId,0x9999));
        CHECK(!legionMotionHardwareAdmitted(goS?0x17EF:0x1A86,source.productId));
        std::array<BYTE,60> bytes{};for(unsigned i=2;i<60;++i)bytes[i]=static_cast<BYTE>(i*7u+3u);bytes[0]=0x7E;bytes[1]=0x21;
        for(unsigned bank=0;bank<2;++bank){
            const auto original=ymcc::legion_input::decodeLogical(goS?ymcc::legion_input::Protocol::GoS:ymcc::legion_input::Protocol::Tablet,bytes.data(),bytes.size(),bytes.size(),bank);
            const auto actual=legionMotionDecodeReport(source,bytes.data(),bytes.size(),bytes.size(),bank);
            CHECK(bool(actual)==bool(original));
            if(actual&&original)CHECK(actual->gx==original->gx&&actual->gy==original->gy&&actual->gz==original->gz&&actual->ax==original->ax&&actual->ay==original->ay&&actual->az==original->az);
        }
        const auto tagged=legionMotionBoundSource(source,epoch(),0);CHECK(tagged.capabilityToken==n+1);CHECK(!tagged.physicalIdentity.empty());
        source.productId=0x9999;const auto unknown=legionMotionBoundSource(source,epoch(),0);CHECK(unknown.physicalIdentity.empty()&&unknown.capabilityToken==0);
    }
    g_machineIdentity.deviceClass="hc.device.LegionGoTablet";CHECK(!inputCaptureLegionControllerMotionRoute());CHECK(!legionMotionModelBinding().known());
    g_machineIdentity.deviceClass="HandheldCompanion.Devices.LegionGo";CHECK(!legionMotionModelBinding().known());
    g_machineIdentity.deviceClass="HandheldCompanion.Devices.LegionGoTablet";g_machineIdentity.family=YmccFamily::Unknown;CHECK(!legionMotionModelBinding().known());
}
static void unifiedSourceLease() {
    rejectSameTickThenAdvance();const auto good=sample(clockMs);CHECK(good.source.capabilityToken!=0);CHECK(publish(good));
    for(const std::uint16_t token:{std::uint16_t(0),std::uint16_t(2),std::uint16_t(65535)}){
        auto bad=good;bad.source.capabilityToken=token;CHECK(!publish(bad));
        const auto retained=legionMotionSnapshot(clockMs);CHECK(retained.has_value());if(retained)CHECK(retained->source.capabilityToken==good.source.capabilityToken);
    }
    const auto result=consume(good);CHECK(result.has_value());CHECK(processCalls>0);
    const auto oldEpoch=epoch();legionMotionRevoke();CHECK(epoch()!=oldEpoch);CHECK(!legionMotionSnapshot(clockMs));CHECK(!legionMotionSubmissionCurrent(good,clockMs));
}
static void unifiedFinalGuards() {
    rejectSameTickThenAdvance();const auto good=sample(clockMs);CHECK(legionMotionSubmissionCurrent(good,clockMs));
    auto bad=good;bad.source.capabilityToken=0;CHECK(!legionMotionSubmissionCurrent(bad,clockMs));
    bad.source.capabilityToken=2;CHECK(!legionMotionSubmissionCurrent(bad,clockMs));
    g_machineIdentity.deviceClass="HandheldCompanion.Devices.LegionGoTablet2";CHECK(!legionMotionSubmissionCurrent(good,clockMs));
    g_machineIdentity.deviceClass="HandheldCompanion.Devices.LegionGoTablet";g_machineIdentity.family=YmccFamily::Unknown;CHECK(!legionMotionSubmissionCurrent(good,clockMs));
}
static void unifiedBankEvidence() {
    rejectSameTickThenAdvance();const auto good=sample(clockMs);ymcc::legion_motion::BankEvidence banks;
    std::array<std::optional<ymcc::legion_input::MotionAxes>,2> values;
    values[0]=ymcc::legion_input::MotionAxes{1,2,3,4,5,6};values[1]=ymcc::legion_input::MotionAxes{7,8,9,10,11,12};
    const auto context=good.context;const auto selected=good.source;const auto start=g_legionMotionEpochStartedAt.load();
    ymcc::legion_motion::BankPacket packet{context,selected,good.capturedAt,0,60,2};
    CHECK(banks.observe(values,packet,context,selected,true,start,2));
    auto wrong=packet;wrong.captureSelection.capabilityToken=2;CHECK(!banks.observe(values,wrong,context,selected,true,start,2));
    auto view=banks.inspect(context,selected,true,start,2,clockMs,ymcc::kVirtualOutputSourceWindowMaxAgeMs);
    CHECK(view.reportContextCurrent);for(unsigned i=0;i<2;++i){CHECK(view.banks[i].observation.sampleCurrent);if(view.banks[i].observation.lastAccepted)CHECK(view.banks[i].observation.lastAccepted->source.capabilityToken==selected.capabilityToken);}
    auto wrongSelection=selected;wrongSelection.capabilityToken=2;view=banks.inspect(context,wrongSelection,true,start,2,clockMs,ymcc::kVirtualOutputSourceWindowMaxAgeMs);CHECK(!view.reportContextCurrent);CHECK(!view.banks[0].observation.sampleCurrent&&!view.banks[1].observation.sampleCurrent);
}
static void unifiedBackgroundGuard() {
    rejectSameTickThenAdvance();auto good=sample(clockMs);good.source.sensorIndex=1;
    const auto lifetime=g_legionMotionProcessorLifetime.load();CHECK(inputCaptureLegionBankWorkCurrent(good,lifetime,0,clockMs));
    auto bad=good;bad.source.capabilityToken=0;CHECK(!inputCaptureLegionBankWorkCurrent(bad,lifetime,0,clockMs));
    bad.source.capabilityToken=2;CHECK(!inputCaptureLegionBankWorkCurrent(bad,lifetime,0,clockMs));
    CHECK(!inputCaptureLegionBankWorkCurrent(good,lifetime,1,clockMs));
    CHECK(!inputCaptureLegionBankWorkCurrent(good,lifetime+1,0,clockMs));
}
static void unifiedModelCacheCost() {
    ymcc::capabilities::controller_motion::ModelBindingCache cache;
    const std::string tablet="HandheldCompanion.Devices.LegionGoTablet",gos="HandheldCompanion.Devices.LegionGoSZ1";
    CHECK(cache.bind(tablet).token()==1);CHECK(!cache.bind("hc.device.LegionGoTablet").known());CHECK(!cache.bind("unregistered-source").known());CHECK(cache.bind(gos).token()==3);CHECK(cache.bind(tablet).token()==1);
    volatile std::uint16_t observed=0;const auto start=std::chrono::steady_clock::now();
    for(unsigned i=0;i<10000;++i)observed=cache.bind(tablet).token();
    const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
    CHECK(observed==1);CHECK(!cache.bind("hc.device.LegionGo").known());
    std::printf("LOCAL_BINDING_COST calls=10000 elapsedNs=%lld sourceBytes=%zu wholeProcessCpuMeasured=false\n",static_cast<long long>(elapsed),sizeof(ymcc::legion_motion::Source));
}

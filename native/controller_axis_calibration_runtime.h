#pragma once
#include "controller_axis_calibration.h"

// AXIS-CALIBRATION-NATIVE-20261025-R1. Included by main.cpp after the existing
// frame/crypto/source owners. No DLL, device, thread, timer, target, or retry owner.
// Raw source facts remain immutable. Only explicit Apply commits a profile via
// the original unified-settings writer. All final neutral gates remain later.
namespace axisCalibrationRuntime {
using Math = ymcc::axisCalibration::Config;
using Raw = ymcc::axisCalibration::RawPad;
inline constexpr const char* protocol = "ymcc.axis-calibration.v1";
struct Binding {
    std::shared_ptr<const std::string> key;
    std::uint64_t connection=0,ownership=0,power=0,identity=0,topology=0;
    const char* route="unknown";
    bool valid() const noexcept {return key&&!key->empty()&&connection&&ownership&&power;}
};
static bool same(const Binding& a,const Binding& b) noexcept {
    return a.valid()&&b.valid()&&*a.key==*b.key&&a.connection==b.connection&&
        a.ownership==b.ownership&&a.power==b.power&&a.identity==b.identity&&
        a.topology==b.topology&&std::strcmp(a.route,b.route)==0;
}
template<class Sample>
static Binding binding(const Sample& s,std::uint64_t now) noexcept {
    if(!s.live||!s.axisCalibrationDeviceKey||!s.readContextStable||!s.readAttemptSequence||
       !s.readStartedTick||s.readCompletedTick<s.readStartedTick||s.readCompletedTick>now||
       now-s.readStartedTick>ymcc::kVirtualOutputSourceWindowMaxAgeMs||
       s.readPowerGeneration!=currentPowerGeneration()||!g_inputReady.load(std::memory_order_acquire)||
       g_powerLifecycle.load(std::memory_order_acquire)!=PowerLifecycle::Ready||
       g_exitRequested.load(std::memory_order_acquire)||
       s.readTopologyGeneration!=g_xinputTopologyEpoch.load(std::memory_order_acquire)||
       s.readIdentityGeneration!=g_xinputSlotIdentityEpoch.load(std::memory_order_acquire))return {};
    const bool msi=s.feedbackSource.backend==ymcc::touchhaptic::Backend::Msi;
    const bool xinput=s.feedbackSource.backend==ymcc::touchhaptic::Backend::XInput;
    if(!msi&&!xinput)return {}; // provider was admitted once by the existing read owner
    if(msi&&(s.readMsiSourceRevision!=g_msiMainSourceRevision.load(std::memory_order_acquire)||
       s.readMsiModeGeneration!=g_msiFeedbackModeGeneration.load(std::memory_order_acquire)))return {};
    return {s.axisCalibrationDeviceKey,s.feedbackSource.generation,s.readContinuityGeneration,
        s.readPowerGeneration,s.readIdentityGeneration,s.readTopologyGeneration,msi?"msi-dinput":"xinput"};
}
static json bindingJson(const Binding& b) {
    if(!b.valid())return nullptr;
    return {{"deviceKey",*b.key},{"connectionEpoch",std::to_string(b.connection)},
        {"ownershipEpoch",std::to_string(b.ownership)},{"powerGeneration",std::to_string(b.power)},
        {"route",b.route},{"identityGeneration",std::to_string(b.identity)},
        {"topologyGeneration",std::to_string(b.topology)}};
}
static bool matches(const json& request,const Binding& b) {
    return b.valid()&&request.is_object()&&request==bindingJson(b);
}
static Raw raw(const XINPUT_GAMEPAD& pad) noexcept {
    return {pad.sThumbLX,pad.sThumbLY,pad.sThumbRX,pad.sThumbRY,pad.bLeftTrigger,pad.bRightTrigger};
}
static double axis(short v) noexcept {return v<0?static_cast<double>(v)/32768.0:static_cast<double>(v)/32767.0;}
static json axes(const Raw& p) {return {{"left",{{"x",axis(p.lx)},{"y",axis(p.ly)}}},
    {"right",{{"x",axis(p.rx)},{"y",axis(p.ry)}}},{"lt",p.lt/255.0},{"rt",p.rt/255.0}};}
static json defaults() {const auto s=[](){return json{{"enabled",true},{"centerX",0},{"centerY",0},{"outputShape","original"},
    {"innerDeadzonePct",0},{"outerDeadzonePct",0},{"antiDeadzonePct",0}};};
    return {{"schemaVersion",1},{"enabled",false},{"sticksEnabled",true},{"triggersEnabled",true},{"applyToVirtual",true},{"applyToLocal",false},
        {"sticks",{{"left",s()},{"right",s()}}},{"triggers",{{"lt",{{"startDeadzonePct",0},{"fullScalePct",100}}},
        {"rt",{{"startDeadzonePct",0},{"fullScalePct",100}}}}}};}
static bool parse(const json& j,Math& c) noexcept {
    try {
        if(!j.is_object()||!j.contains("schemaVersion")||j["schemaVersion"]!=1||
           !j.at("applyToVirtual").is_boolean()||!j.at("applyToLocal").is_boolean())return false;
        for(const char* key:{"enabled","sticksEnabled","triggersEnabled"})if(j.contains(key)&&!j.at(key).is_boolean())return false;
        c.enabled=j.value("enabled",false); // Missing legacy master is OFF, never implicit activation.
        c.sticksEnabled=j.value("sticksEnabled",true);c.triggersEnabled=j.value("triggersEnabled",true);
        c.applyToVirtual=j.at("applyToVirtual").get<bool>();c.applyToLocal=j.at("applyToLocal").get<bool>();
        auto stick=[](const json& v,ymcc::axisCalibration::Stick& s){
            if(!v.at("centerX").is_number()||!v.at("centerY").is_number())return false;
            if(v.contains("enabled")&&!v.at("enabled").is_boolean())return false;
            s.enabled=v.value("enabled",true);
            s.centerX=v.at("centerX").get<double>();s.centerY=v.at("centerY").get<double>();
            const auto integer=[](const json& v,const char* key,int& out){
                const auto& n=v.at(key);if(!n.is_number())return false;
                const double d=n.get<double>();if(!std::isfinite(d)||d<0||d>100||std::floor(d)!=d)return false;
                out=static_cast<int>(d);return true;};
            if(!integer(v,"innerDeadzonePct",s.innerDeadzonePct)||!integer(v,"outerDeadzonePct",s.outerDeadzonePct)||
               !integer(v,"antiDeadzonePct",s.antiDeadzonePct))return false;
            const auto shape=v.value("outputShape",std::string("original"));
            if(shape=="original")s.outputShape=ymcc::axisCalibration::Shape::Original;
            else if(shape=="circle-to-square")s.outputShape=ymcc::axisCalibration::Shape::CircleToSquare;
            else if(shape=="square-to-circle")s.outputShape=ymcc::axisCalibration::Shape::SquareToCircle;
            else return false;return true;};
        if(!stick(j.at("sticks").at("left"),c.left)||!stick(j.at("sticks").at("right"),c.right))return false;
        auto trigger=[](const json& v,ymcc::axisCalibration::Trigger& t){
            const auto& a=v.at("startDeadzonePct");const auto& b=v.at("fullScalePct");
            if(!a.is_number()||!b.is_number())return false;
            const auto x=a.get<double>(),y=b.get<double>();
            if(!std::isfinite(x)||!std::isfinite(y)||x<0||x>99||y<1||y>100||std::floor(x)!=x||std::floor(y)!=y)return false;
            t.startDeadzonePct=static_cast<int>(x);t.fullScalePct=static_cast<int>(y);return true;};
        return trigger(j.at("triggers").at("lt"),c.lt)&&trigger(j.at("triggers").at("rt"),c.rt)&&ymcc::axisCalibration::valid(c);
    }catch(...){return false;}
}
static json publicConfig(json value) {
    value.erase("deviceKey");
    if(!value.contains("enabled"))value["enabled"]=false;
    if(!value.contains("sticksEnabled"))value["sticksEnabled"]=true;
    if(!value.contains("triggersEnabled"))value["triggersEnabled"]=true;
    for(const char* side:{"left","right"}){
        auto& stick=value["sticks"][side];if(!stick.contains("enabled"))stick["enabled"]=true;
        if(!stick.contains("outputShape"))stick["outputShape"]="original";
    }
    return value;
}
// Match InputConfigHash.v1's existing ECMAScript number/string/key projection.
// Runs only on explicit reads/settings commits, never on the frame lane.
static std::string number(double v) {
    if(!std::isfinite(v))throw std::runtime_error("axis-config-non-finite");
    if(v==0)return "0";
    char buffer[64];const auto formatted=std::to_chars(buffer,buffer+sizeof(buffer),v,std::chars_format::general);
    if(formatted.ec!=std::errc{})throw std::runtime_error("axis-config-number");
    std::string s(buffer,formatted.ptr);bool negative=s.front()=='-';if(negative)s.erase(0,1);
    int exponent=0;const auto e=s.find_first_of("eE");
    if(e!=std::string::npos){exponent=std::stoi(s.substr(e+1));s.resize(e);}
    auto point=s.find('.');if(point==std::string::npos)point=s.size();else s.erase(point,1);
    int decimal=static_cast<int>(point)+exponent;
    while(s.size()>1&&s.front()=='0'){s.erase(0,1);--decimal;}
    while(s.size()>1&&s.back()=='0')s.pop_back();
    std::string result;
    if(decimal>0&&decimal<=21){result=s;if(static_cast<int>(s.size())<=decimal)result.append(decimal-s.size(),'0');else result.insert(decimal,".");}
    else if(decimal<=0&&decimal>-6)result="0."+std::string(-decimal,'0')+s;
    else {result.assign(1,s.front());if(s.size()>1)result+='.'+s.substr(1);const int exp=decimal-1;
        result+='e';if(exp>=0)result+='+';result+=std::to_string(exp);}
    return negative?'-'+result:result;
}
static void canonical(std::string& out,const json& j) {
    if(j.is_number()){out+=number(j.get<double>());return;}
    if(j.is_array()){out+='[';bool first=true;for(const auto& item:j){if(!first)out+=',';first=false;canonical(out,item);}out+=']';return;}
    if(j.is_object()){
        std::vector<std::pair<std::wstring,std::string>> keys;keys.reserve(j.size());
        for(auto it=j.begin();it!=j.end();++it)keys.emplace_back(U2W(it.key()),it.key());
        std::sort(keys.begin(),keys.end());out+='{';bool first=true;
        for(const auto& k:keys){if(!first)out+=',';first=false;out+=json(k.second).dump();out+=':';canonical(out,j.at(k.second));}out+='}';return;
    }out+=j.dump();
}
static std::string inputHash(json input) {
    for(const char* key:{"appliedRevision","appliedConfigHash","pendingConfigHash","hostAcknowledgedRevision","hostAcknowledgedConfigHash","applyStatus"})input.erase(key);
    std::string text;canonical(text,json{{"schema","InputConfigHash.v1"},{"input",std::move(input)}});
    const auto digest=inputHostSha256(text);return digest.empty()?std::string{}:"sha256:"+digest;
}
struct Profile {Math config{};ymcc::axisCalibration::CompiledRaw plan{};
    std::string deviceKey,hash;json document;long long revision=0;};
// One immutable publication couples input/hash/profile; readers cannot mix revisions.
struct Published {std::shared_ptr<const Profile> profile;json input;std::string hash;long long revision=0;};
static std::shared_ptr<const Published> published;
// Reject-only hint, NEVER dereferenced or used as ownership. refresh publishes
// the owned immutable state before this pointer; an effective value retains a
// shared owner, so a compared Profile address cannot be recycled underneath it.
// Null makes the master-OFF/no-profile path skip the shared_ptr atomic lock.
static std::atomic<const Profile*> virtualProfileHint{nullptr};
static std::mutex refreshMx;
static std::atomic<long long> inputRevision{0};
static std::mutex mx;
// Only scalars and an immutable key are retained from the actual accepted
// read. No diagnostic strings/containers/IMU payloads are copied every frame.
struct ReadWitness {
    std::shared_ptr<const std::string> axisCalibrationDeviceKey;
    ymcc::touchhaptic::SourceToken feedbackSource{};
    std::uint64_t readAttemptSequence=0,readStartedTick=0,readCompletedTick=0,readPowerGeneration=0;
    std::uint64_t readContinuityGeneration=0,readMsiSourceRevision=0,readMsiModeGeneration=0;
    unsigned readTopologyGeneration=0,readIdentityGeneration=0;
    bool live=false,readContextStable=false;
};
static ReadWitness witness(const InputCaptureGamepadSnapshot& s) noexcept {
    return {s.axisCalibrationDeviceKey,s.feedbackSource,s.readAttemptSequence,s.readStartedTick,s.readCompletedTick,
        s.readPowerGeneration,s.readContinuityGeneration,s.readMsiSourceRevision,s.readMsiModeGeneration,
        s.readTopologyGeneration,s.readIdentityGeneration,s.live,s.readContextStable};
}
struct Accepted {std::shared_ptr<const Profile> config;Binding source;Raw raw{},output{};
    ReadWitness sample;std::uint64_t host=0,power=0,sequence=0;bool neutral=false;};
// Existing g_inputHostMx owns this record. The sole production accepted caller
// already holds that mutex around SUBMIT+ACK; no extra per-frame mutex is taken.
static Accepted accepted;
// stop invalidates eligibility before its nonblocking cleanup attempt. A busy
// existing Host owner may retain one bounded record, never a usable old ACK.
static std::atomic<bool> acceptedCurrent{false};
static std::atomic<bool> centerActive{false},watchActive{false};
static std::atomic<std::uint64_t> ordinal{0};
struct Center {bool active=false;std::string id;Binding source;long long revision=0;bool right=false;
    std::uint64_t started=0,lastSequence=0,lastStored=0;std::size_t count=0;
    std::array<double,512> xs{},ys{};};
static Center center;
static std::string watchId;
static std::uint64_t lastWatchAt=0;
static void refresh(const json& input) {
    std::lock_guard<std::mutex> refreshLock(refreshMx);
    const auto previous=std::atomic_load_explicit(&published,std::memory_order_acquire);
    if(previous&&previous->input==input)return;
    const auto rev=input.value("revision",0LL);
    if(rev<0||rev>9007199254740991LL||(previous&&rev<previous->revision))return;
    const auto fingerprint=inputHash(input);if(fingerprint.empty())return;
    std::shared_ptr<const Profile> next;
    const auto it=input.find("axisCalibration");
    if(it!=input.end()){
        auto p=std::make_shared<Profile>();
        if(!parse(*it,p->config)){
            // An explicit/migrated master OFF must remain a safety gate even
            // when unrelated stored sub-parameters are corrupt.
            if(it->is_object()&&(!it->contains("enabled")||((*it)["enabled"].is_boolean()&&!(*it)["enabled"].get<bool>())))p->config=Math{};
            else return;
        }
        if(it->contains("deviceKey")&&(*it)["deviceKey"].is_string())p->deviceKey=it->at("deviceKey").get<std::string>();
        if(p->deviceKey.size()>8192){if(p->config.enabled)return;p->deviceKey.clear();}
        if(p->config.enabled&&p->deviceKey.empty())return;
        p->revision=rev;p->hash=fingerprint;
        p->document=publicConfig(*it);
        Math publicMath;if(!parse(p->document,publicMath))p->document=defaults();
        p->plan=ymcc::axisCalibration::prepareRaw(p->config);
        if(!p->plan.valid())return; // only validated immutable plans reach frames
        next=std::move(p);
    }
    auto state=std::make_shared<Published>();state->input=input;state->hash=fingerprint;state->revision=rev;state->profile=std::move(next);
    const Profile* hint=state->profile&&state->profile->config.enabled&&state->profile->config.applyToVirtual
        ? state->profile.get():nullptr;
    std::atomic_store_explicit(&published,std::shared_ptr<const Published>(std::move(state)),std::memory_order_release);
    virtualProfileHint.store(hint,std::memory_order_release);
    inputRevision.store(rev,std::memory_order_release);
}
struct Effective {XINPUT_GAMEPAD pad{};std::shared_ptr<const Profile> config;Binding source;
    const InputCaptureGamepadSnapshot* sample=nullptr;bool applied=false,ltEdited=false,rtEdited=false;};
static Effective effective(const XINPUT_GAMEPAD& original,bool sourceAdmitted) noexcept {
    Effective result;result.pad=original;
    // No-profile/OFF/unrequested source does not acquire shared ownership,
    // bind a source, normalize values, or execute correction math.
    if(!sourceAdmitted||!t_commonFrameSource||!virtualProfileHint.load(std::memory_order_acquire))return result;
    const auto state=std::atomic_load_explicit(&published,std::memory_order_acquire);
    if(!state||!state->profile)return result;
    const auto& p=state->profile;
    if(!p->config.enabled||!p->config.applyToVirtual||
       virtualProfileHint.load(std::memory_order_acquire)!=p.get())return result;
    auto source=binding(*t_commonFrameSource,GetTickCount64());
    if(!source.valid()||*source.key!=p->deviceKey)return result;
    if((p->plan.ltEdited()&&!t_commonFrameSource->axisCalibrationLtAnalog)||
       (p->plan.rtEdited()&&!t_commonFrameSource->axisCalibrationRtAnalog))return result;
    const auto converted=ymcc::axisCalibration::applyRaw(raw(original),p->plan);
    if(!converted.ok())return result;
    result.pad.sThumbLX=converted.value.lx;result.pad.sThumbLY=converted.value.ly;
    result.pad.sThumbRX=converted.value.rx;result.pad.sThumbRY=converted.value.ry;
    result.pad.bLeftTrigger=converted.value.lt;result.pad.bRightTrigger=converted.value.rt;
    result.config=p;result.source=std::move(source);result.sample=t_commonFrameSource;result.applied=true;
    result.ltEdited=p->plan.ltEdited();
    result.rtEdited=p->plan.rtEdited();
    return result;
}
static void noteAccepted(const Effective& value,const FocusFrameContent& content,std::uint64_t sequence,bool neutral) noexcept {
    if(!value.applied||!value.sample||!value.config)return;
    // Caller holds g_inputHostMx: this receipt is for the frame just accepted
    // by that exact Host. Keep current-profile/source checks; publication can
    // revoke eligibility but cannot destroy value.config's retained owner.
    try {
        if(virtualProfileHint.load(std::memory_order_acquire)!=value.config.get())return;
        if(!same(value.source,binding(*value.sample,GetTickCount64())))return;
        accepted.config=value.config;accepted.source=value.source;accepted.raw=raw(value.sample->pad);
        accepted.output={content.lxShort,content.lyShort,content.rxShort,content.ryShort,
            static_cast<std::uint8_t>(std::clamp(std::floor(content.ltRaw*255.0+0.5),0.0,255.0)),
            static_cast<std::uint8_t>(std::clamp(std::floor(content.rtRaw*255.0+0.5),0.0,255.0))};
        accepted.sample=witness(*value.sample);accepted.host=g_inputHostEpoch;accepted.power=currentPowerGeneration();
        accepted.sequence=sequence;accepted.neutral=neutral;
        acceptedCurrent.store(true,std::memory_order_release);
    }catch(...){/* Evidence cannot alter actual frame acceptance or ownership. */}
}
static json snapshot(bool includeInput=false) {
    const auto state=std::atomic_load_explicit(&published,std::memory_order_acquire);
    const auto p=state?state->profile:nullptr;
    InputCaptureGamepadSnapshot sample;{std::lock_guard<std::mutex> lock(g_inputCaptureGamepadSnapshotMx);sample=g_inputCaptureGamepadSnapshot;}
    const auto source=binding(sample,GetTickCount64());
    const auto revision=state?state->revision:0LL;
    const auto hash=state?state->hash:std::string{};
    json context;std::uint64_t host=0;bool hostCurrent=false;Accepted acceptedView;
    {std::unique_lock<InputHostMutex> hostLock(g_inputHostMx,std::try_to_lock);
        if(hostLock.owns_lock()&&g_inputHostEpoch&&!g_inputHostRunId.empty()){
            host=g_inputHostEpoch;hostCurrent=g_inputHostPrepared&&!g_inputHostTransportFault&&g_inputHostActive;
            if(acceptedCurrent.load(std::memory_order_acquire))acceptedView=accepted;
            // short cached receipt under the existing Host mutex
            context={{"requestId",std::to_string(g_virtualStartProtection.view(inputHostStartProtectionNow()).requestId)},
                {"runId",g_inputHostRunId},{"hostEpoch",std::to_string(host)},
                {"requestedPersona",inputHostConfiguredPersona()},{"boundPersona",inputHostBoundPersonaValue()}};
        }}
    const bool profileBound=p&&source.valid()&&p->deviceKey==*source.key;
    // Never seed a new physical device's draft with the previous device's center.
    // Durable revision/hash still describe the complete original input document.
    // A disconnected source still needs its saved master state/parameters so
    // explicit OFF can be submitted and its ACK matched. Only a positively
    // identified DIFFERENT device receives defaults; no-source is never active.
    json config=p&&(!source.valid()||profileBound)?p->document:defaults();
    auto route=[&](const char* state,const char* reason){return json{{"state",state},{"reason",reason},
        {"appliedInputRevision",nullptr},{"appliedConfigHash",nullptr},{"source",nullptr}};};
    const bool enabled=config.value("enabled",false);
    json vr=route(!enabled||!config.value("applyToVirtual",true)?"bypass":"pending",!enabled?"master-disabled":!source.valid()?"physical-source-unconfirmed":"awaiting-current-accepted-output");
    json lr=route(enabled&&config.value("applyToLocal",false)?"unsupported":"bypass",enabled?"no-verified-local-axis-writer":"master-disabled");
    json axisValues;auto readSequence=sample.readAttemptSequence,readStarted=sample.readStartedTick,readCompleted=sample.readCompletedTick;
    if(source.valid())axisValues={{"raw",axes(raw(sample.pad))},{"virtual",nullptr},{"local",nullptr}};
    if(profileBound&&enabled&&config.value("applyToVirtual",true)&&acceptedView.config==p&&source.valid()&&
       same(source,acceptedView.source)&&same(source,binding(acceptedView.sample,GetTickCount64()))&&
       hostCurrent&&host==acceptedView.host&&acceptedView.power==currentPowerGeneration()){
        vr=route("active",acceptedView.neutral?"current-local-submit-accepted-expected-neutral":"current-local-submit-accepted");
        vr["appliedInputRevision"]=p->revision;vr["appliedConfigHash"]=p->hash;vr["source"]=bindingJson(source);
        axisValues={{"raw",axes(acceptedView.raw)},{"virtual",axes(acceptedView.output)},{"local",nullptr}};
        readSequence=acceptedView.sample.readAttemptSequence;readStarted=acceptedView.sample.readStartedTick;readCompleted=acceptedView.sample.readCompletedTick;
    }
    if(!profileBound&&config.value("applyToVirtual",true))vr=route("bypass",p?"profile-belongs-to-another-or-unconfirmed-source":"no-saved-source-bound-calibration");
    json result={{"protocol",protocol},{"inputRevision",revision},{"inputConfigHash",hash},{"config",config},
        {"source",bindingJson(source)},{"capabilities",{{"sticks",source.valid()?"standard-independent":"unavailable"},
        {"lt",source.valid()?(sample.axisCalibrationLtAnalog?"independent-analog":"unknown"):"unavailable"},
        {"rt",source.valid()?(sample.axisCalibrationRtAnalog?"independent-analog":"unknown"):"unavailable"},
        {"virtualWrite",source.valid()?"available":"unknown"},{"localWrite","unsupported"}}},
        {"routes",{{"virtual",vr},{"local",lr}}}};
    if(!context.is_null())result["virtualContext"]=std::move(context);
    if(source.valid()){
        result["sample"]={{"source",bindingJson(source)},{"readSequence",std::to_string(readSequence)},
            {"readStartedTick",std::to_string(readStarted)},{"readCompletedTick",std::to_string(readCompleted)}};
        result["axes"]=std::move(axisValues);
    }
    if(includeInput&&state)result["inputDocument"]=state->input;
    return result;
}
static json read(const json&) {refresh(ymSettingsSection("input"));return snapshot(true);}
static json failure(const json& request,const char* reason) {
    return {{"ok",false},{"saved",false},{"requestId",request.value("requestId",std::string{})},
        {"reason",reason},{"snapshot",snapshot()}};
}
static json apply(const json& request) {
    Math config;
    if(!request.is_object()||request.value("protocol",std::string{})!=protocol||!request.contains("config")||
       !parse(request["config"],config)||!request.contains("expectedInputRevision")||
       !request["expectedInputRevision"].is_number_integer()||(config.enabled&&!request.contains("source"))||
       !request.contains("requestId")||!request["requestId"].is_string()||request["requestId"].get<std::string>().empty()||
       request["requestId"].get_ref<const std::string&>().size()>256)return failure(request,"invalid-config");
    if(config.enabled&&config.applyToLocal)return failure(request,"unsupported-route");
    const auto expected=request["expectedInputRevision"].get<long long>();
    if(expected<0||expected>=9007199254740991LL)return failure(request,"invalid-config");
    json committed;const char* rejected=nullptr;bool written=false;
    {
        SettingsFileGuard settingsLock;
        const auto current=ymSettingsReadUnlocked();const auto input=current.value("input",json::object());
        if(input.value("revision",0LL)!=expected)rejected="revision-conflict";
        else {
            InputCaptureGamepadSnapshot sample;Binding source;
            const bool requestedSource=request.contains("source")&&!request["source"].is_null();
            if(config.enabled||requestedSource){
                {std::lock_guard<std::mutex> sourceLock(g_inputCaptureGamepadSnapshotMx);sample=g_inputCaptureGamepadSnapshot;}
                source=binding(sample,GetTickCount64());
            }
            const bool bindCurrent=requestedSource&&source.valid();
            if((config.enabled||bindCurrent)&&!matches(request["source"],source))rejected="source-changed";
            else if(config.enabled&&config.triggersEnabled&&(config.lt.startDeadzonePct||config.lt.fullScalePct!=100)&&!sample.axisCalibrationLtAnalog)rejected="unsupported-route";
            else if(config.enabled&&config.triggersEnabled&&(config.rt.startDeadzonePct||config.rt.fullScalePct!=100)&&!sample.axisCalibrationRtAnalog)rejected="unsupported-route";
            else {
                auto next=current;auto nextInput=input;auto stored=publicConfig(request["config"]);
                std::string key;
                const auto previous=input.find("axisCalibration");
                if(previous!=input.end()&&previous->is_object()&&previous->contains("deviceKey")&&(*previous)["deviceKey"].is_string())key=(*previous)["deviceKey"].get<std::string>();
                // OFF on a proven current device may save that device's draft.
                // Only source-less OFF retains the previous stored identity.
                stored["deviceKey"]=bindCurrent?*source.key:key;
                nextInput["axisCalibration"]=std::move(stored);nextInput["revision"]=expected+1;
                for(const char* key:{"appliedRevision","appliedConfigHash","hostAcknowledgedRevision","hostAcknowledgedConfigHash"})nextInput[key]=nullptr;
                const auto hash=inputHash(nextInput);
                if(hash.empty())rejected="write-failed";
                else {
                    nextInput["pendingConfigHash"]=hash;nextInput["applyStatus"]="saved-pending";next["input"]=nextInput;
                    // Recheck the source at commit admission, without holding its
                    // realtime snapshot mutex across backup/hash/file I/O. A later
                    // physical change cannot apply this immutable device-bound
                    // profile to another source; its late ACK remains saved-only.
                    InputCaptureGamepadSnapshot latest;
                    if(config.enabled||bindCurrent){std::lock_guard<std::mutex> sourceLock(g_inputCaptureGamepadSnapshotMx);latest=g_inputCaptureGamepadSnapshot;}
                    if((config.enabled||bindCurrent)&&!same(source,binding(latest,GetTickCount64())))rejected="source-changed";
                    else {
                        bool conflict=false,protectedSwitch=false;
                        written=ymSettingsWriteDocumentUnlocked(next.dump(),nullptr,&current,&committed,false,expected,&conflict,&protectedSwitch);
                        if(!written)rejected=conflict?"revision-conflict":protectedSwitch?"ownership-denied":"write-failed";
                    }
                }
            }
        }
    }
    if(!written){refresh(ymSettingsSection("input"));return failure(request,rejected?rejected:"write-failed");}
    refresh(committed.at("input"));
    const auto result=snapshot(true);
    appendNativeLifecycleLog("controller-axis-calibration-apply",{{"requestId",request["requestId"]},
        {"inputRevision",expected+1},{"inputConfigHash",result["inputConfigHash"]},{"saved",true},{"outputAccepted",false}});
    ipc_emit("controller.axisCalibration.changed",result);
    return {{"ok",true},{"saved",true},{"requestId",request["requestId"]},{"snapshot",result}};
}
static json startCenter(const json& request) {
    refresh(ymSettingsSection("input"));
    if(request.value("protocol",std::string{})!=protocol||!request.contains("source")||
       !request.contains("expectedInputRevision")||!request["expectedInputRevision"].is_number_integer())throw std::runtime_error("invalid-center-request");
    const auto side=request.value("side",std::string{});if(side!="left"&&side!="right")throw std::runtime_error("invalid-center-side");
    InputCaptureGamepadSnapshot sample;{std::lock_guard<std::mutex> lock(g_inputCaptureGamepadSnapshotMx);sample=g_inputCaptureGamepadSnapshot;}
    const auto b=binding(sample,GetTickCount64());if(!matches(request["source"],b))throw std::runtime_error("center-source-changed");
    const auto rev=request["expectedInputRevision"].get<long long>();if(rev!=inputRevision.load(std::memory_order_acquire))throw std::runtime_error("center-revision-conflict");
    std::lock_guard<std::mutex> lock(mx);if(center.active)throw std::runtime_error("center-session-busy");
    center={};center.active=true;center.id="axis-center-"+std::to_string(GetTickCount64())+"-"+std::to_string(++ordinal);
    center.source=b;center.revision=rev;center.right=side=="right";center.started=GetTickCount64();
    centerActive.store(true,std::memory_order_release);return {{"sessionId",center.id}};
}
static json cancelCenter(const json& request) {
    std::lock_guard<std::mutex> lock(mx);
    if(center.active&&request.value("sessionId",std::string{})==center.id){center.active=false;centerActive.store(false,std::memory_order_release);}
    return {{"ok",true}};
}
static json observe(const json& request) {
    std::lock_guard<std::mutex> lock(mx);
    if(request.value("active",false)){
        if(g_exitRequested.load(std::memory_order_acquire)||g_inputCaptureStop.load(std::memory_order_acquire))return {{"watchId",""},{"reason","input-stopped"}};
        // Same WebView/UI IPC owner executes observe synchronously and in
        // request order. A client-known token lets a local response timeout
        // cancel the already-created watch without waiting for its lost ACK.
        // Legacy clients may omit the token; late stop remains exact-match.
        std::string requested;
        if(request.contains("watchId")){
            if(!request["watchId"].is_string())throw std::runtime_error("invalid-axis-watch-token");
            requested=request["watchId"].get<std::string>();
            if(requested.empty()||requested.size()>256||
               !std::all_of(requested.begin(),requested.end(),[](unsigned char c){return c>=0x21&&c<=0x7e;}))
                throw std::runtime_error("invalid-axis-watch-token");
        }else requested="axis-watch-"+std::to_string(++ordinal);
        watchId=std::move(requested);lastWatchAt=0;watchActive.store(true,std::memory_order_release);return {{"watchId",watchId}};}
    if(request.value("watchId",std::string{})==watchId){watchActive.store(false,std::memory_order_release);watchId.clear();}
    return {{"ok",true}};
}
static void tick(const InputCaptureGamepadSnapshot& sample) noexcept {
    if(!centerActive.load(std::memory_order_acquire)&&!watchActive.load(std::memory_order_acquire))return;
    try {
        const auto now=GetTickCount64();json receipt;bool publish=false;std::string observedWatch;
        {std::lock_guard<std::mutex> lock(mx);
            if(center.active){
                const auto b=binding(sample,now);const char* reason=nullptr;
                if(!same(b,center.source))reason="source-changed";
                else if(inputRevision.load(std::memory_order_acquire)!=center.revision)reason="revision-conflict";
                if(!reason&&sample.readAttemptSequence!=center.lastSequence&&(!center.lastStored||now-center.lastStored>=5)&&center.count<center.xs.size()){
                    center.lastSequence=sample.readAttemptSequence;center.lastStored=now;
                    center.xs[center.count]=axis(center.right?sample.pad.sThumbRX:sample.pad.sThumbLX);
                    center.ys[center.count]=axis(center.right?sample.pad.sThumbRY:sample.pad.sThumbLY);++center.count;
                }
                if(reason||now-center.started>=2200){
                    receipt={{"ok",false},{"sessionId",center.id},{"reason",reason?reason:"insufficient-samples"}};
                    if(!reason&&center.count>=30){
                        auto xs=center.xs,ys=center.ys;const auto n=center.count;
                        std::sort(xs.begin(),xs.begin()+n);std::sort(ys.begin(),ys.begin()+n);
                        const double cx=n%2?xs[n/2]:(xs[n/2-1]+xs[n/2])*0.5;
                        const double cy=n%2?ys[n/2]:(ys[n/2-1]+ys[n/2])*0.5;
                        std::array<double,512> noise{};for(std::size_t i=0;i<n;++i)noise[i]=std::hypot(center.xs[i]-cx,center.ys[i]-cy);
                        std::sort(noise.begin(),noise.begin()+n);const auto index=static_cast<std::size_t>(std::ceil(n*0.99))-1;
                        if(noise[index]<=0.02&&std::hypot(cx,cy)<=0.15)receipt={{"ok",true},{"sessionId",center.id},
                            {"source",bindingJson(center.source)},{"expectedInputRevision",center.revision},
                            {"side",center.right?"right":"left"},{"sampleCount",n},{"centerX",cx},{"centerY",cy},{"noiseP99",noise[index]}};
                        else receipt["reason"]="source-not-stably-centered";
                    }
                    center.active=false;centerActive.store(false,std::memory_order_release);
                }
            }
            if(watchActive.load(std::memory_order_acquire)&&gamepadUiInputEligible()&&(!lastWatchAt||now-lastWatchAt>=100)){
                lastWatchAt=now;publish=true;observedWatch=watchId;
            }
        }
        if(!receipt.is_null()){
            appendNativeLifecycleLog("controller-axis-calibration-center",receipt);
            ipc_emit("controller.axisCalibration.changed",receipt);
        }
        if(publish){
            auto output=snapshot();
            std::lock_guard<std::mutex> lock(mx);
            if(watchActive.load(std::memory_order_acquire)&&watchId==observedWatch&&
               !g_exitRequested.load(std::memory_order_acquire)&&!g_inputCaptureStop.load(std::memory_order_acquire)){
                output["observerId"]=observedWatch;
                // The exact token travels with queued delivery; the bridge also
                // rejects a delayed old-view event after unmount/new subscription.
                ipc_emit("controller.axisCalibration.changed",output);
            }
        }
    }catch(...){/* Inspection is passive; it cannot skip normal input publication. */}
}
static void stop() noexcept {
    // Shutdown drains the producer first. Never wait behind a long existing
    // Host transaction just to retire diagnostic evidence. A later real ACK
    // can republish eligibility; an old cached record cannot do so by itself.
    acceptedCurrent.store(false,std::memory_order_release);
    try {std::unique_lock<InputHostMutex> hostLock(g_inputHostMx,std::try_to_lock);
        if(hostLock.owns_lock())accepted={};}catch(...){}
    try {std::lock_guard<std::mutex> lock(mx);center.active=false;centerActive.store(false,std::memory_order_release);
        watchActive.store(false,std::memory_order_release);watchId.clear();}catch(...){}
}
} // namespace axisCalibrationRuntime
static void axisCalibrationRefreshSettings(const json& input) {
    try {axisCalibrationRuntime::refresh(input);}catch(...){/* Optional correction cannot break the existing settings lifecycle. */}
}

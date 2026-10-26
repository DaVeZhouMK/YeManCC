// Pure policy consumer for the EXISTING sleep.residency-evidence-consumer.
// In-memory ports plus extracted actual armed-list helpers; no process launch, registry or device API.
#include "sleep_static_wake_policy.h"
#include "sleep_static_wake_receipt.h"
#include <atomic>
#include <thread>
#include <map>
#include <mutex>
#include <chrono>
#include <stdexcept>
#ifndef YMCC_STATIC_WAKE_NATIVE_PARSER_BOUND
#include "static_wake_parser_binding.inc"
#endif
namespace static_wake_fixture {
using Json=ymcc::staticwake::Json;
using Policy=ymcc::staticwake::Policy;
inline Json item(const std::string& key,const std::string& kind,const Json& expected) {
    return {{"key",key},{"kind",kind},{"name",key},{"target",{{"id",key}}},{"expected",expected}};
}
struct Memory {
    Json disk=nullptr,candidates=Json::array(),protectedItems=Json::array(),collectionErrors=Json::array();
    std::map<std::string,Json> current;
    unsigned saves=0,reads=0,writes=0,failSaveAt=0,failReadAt=0,raceReadAt=0;
    unsigned failWriteCount=0,readStep=0;
    std::uint64_t tick=100,epoch=1792886400;
    bool source=false,loadFails=false,collectComplete=true,effectOnWriteFailure=false,mismatch=false;
    Json raceValue=false;
    std::vector<std::string> order;
    using Values=std::map<std::string,Json>;
    ymcc::staticwake::ObservationCache<Values> softwareCache,deviceCache;
    ymcc::staticwake::SoftwareSnapshotCircuit softwareCircuit;
    unsigned snapshotLoads=0,softwareLoads=0,deviceLoads=0,observations=0;
    ymcc::staticwake::Ports ports() {
        ymcc::staticwake::Ports p;
        p.load=[this](Json& j,std::string& e){if(loadFails){e="injected-load-failure";return false;}j=disk;return true;};
        p.persist=[this](const Json& j,std::string& e){++saves;if(saves==failSaveAt){e="injected-disk-failure";return false;}disk=j;order.push_back("save");return true;};
        p.collect=[this](Json& a,Json& b,Json& c,std::string& e){a=candidates;b=protectedItems;c=collectionErrors;if(!collectComplete)e="injected-collection-failure";return collectComplete;};
        p.read=[this](const Json& a,Json& b,std::string& e){++reads;tick+=readStep;const auto k=a["key"].get<std::string>();if(reads==raceReadAt)current[k]=raceValue;if(reads==failReadAt){e="injected-read-failure";return false;}b=current.at(k);return true;};
        p.write=[this](const Json& a,const Json& v,std::string& e){
            ++writes;order.push_back("write");const auto k=a["key"].get<std::string>();
            bool prepared=false;
            if(!disk.is_null())for(const auto& r:disk["entries"])if(r["key"]==k&&r.value("owned",false)&&r.contains("baseline")&&(r["stage"]=="prepared"||r["stage"]=="restore-prepared"))prepared=true;
            if(!prepared)throw std::runtime_error("write without durable baseline/intent");
            const bool failed=failWriteCount!=0;if(failed)--failWriteCount;
            if(!mismatch&&(!failed||effectOnWriteFailure))current[k]=v;
            if(failed)e="injected-write-failure";return !failed;
        };
        p.monotonicMs=[this](){return tick;};p.epoch=[this](){return epoch;};p.sourceReady=[this](){return source;};return p;
    }
    // Count real injected snapshot loads. This consumes the production cache,
    // not a separate cache model; fresh reads still invoke the original port.
    ymcc::staticwake::Ports snapshotPorts(bool globalCircuit=false) {
        auto p=ports();const auto fresh=p.read;const auto write=p.write;
        const auto sample=[this,fresh,globalCircuit](const Json& a,Json& b,std::string& e,bool observation) {
            const auto kind=a["kind"].get<std::string>();
            if(kind!="software"&&kind!="devices")return fresh(a,b,e);
            auto& cache=kind=="software"?softwareCache:deviceCache;
            ymcc::staticwake::SoftwareSnapshotFailure failure;
            if(globalCircuit&&kind=="software"&&softwareCircuit.failure(failure)){e=failure.error;return false;}
            const auto load=[&](Values& values,std::string& why) {
                const auto actual=[&](std::string& error) {
                    ++snapshotLoads;if(kind=="software")++softwareLoads;else ++deviceLoads;
                    Json ignored;if(!fresh(a,ignored,error))return false;
                    for(const auto& candidate:candidates)if(candidate["kind"]==kind)values[candidate["key"].get<std::string>()]=current.at(candidate["key"].get<std::string>());
                    return true;
                };
                return globalCircuit&&kind=="software"?softwareCircuit.query(actual,why):actual(why);
            };
            const bool ok=observation?cache.observe(tick,load,e):cache.refresh(tick,load,e);
            if(!ok)return false;
            b=cache.value().at(a["key"].get<std::string>());return true;
        };
        p.observe=[this,sample](const Json& a,Json& b,std::string& e){++observations;return sample(a,b,e,true);};
        p.read=[sample](const Json& a,Json& b,std::string& e){return sample(a,b,e,false);};
        p.write=[this,write](const Json& a,const Json& b,std::string& e){softwareCache.invalidate();deviceCache.invalidate();return write(a,b,e);};
        p.beginOperation=[this](){softwareCache.invalidate();deviceCache.invalidate();softwareCircuit.reset();};
        if(globalCircuit)p.softwareSnapshotFailure=[this](ymcc::staticwake::SoftwareSnapshotFailure& failure){return softwareCircuit.failure(failure);};
        return p;
    }
    void add(const Json& a,const Json& baseline){candidates.push_back(a);current[a["key"].get<std::string>()]=baseline;}
};
inline unsigned armedParserTests() {
    unsigned checks=0;
    const auto expect=[&](bool ok,const char* name){++checks;if(!ok)throw std::runtime_error(std::string("STATIC_WAKE_CHECK_FAILED: ")+name);};
    using namespace ymcc::staticwake::native;
    Inventory inv;Node node;node.dn=1;node.id=L"USB\\physical";node.labels={L"known device"};inv.nodes={node};
    for(const std::string marker:{std::string("NONE"),std::string("\xE6\x97\xA0")}) {
        std::set<std::string> names={"stale"};std::string error;
        expect(parseArmed(marker,inv,names,error)&&names.empty(),"actual armed parser accepts supported single empty marker without inventory collision");
        expect(emptyArmedList(lines(marker),inv),"collection and readback share actual supported empty-list predicate");
        Inventory collision=inv;Node named=node;named.dn=2;named.labels={lower(U2W(marker))};collision.nodes.push_back(named);
        expect(!emptyArmedList(lines(marker),collision),"inventory real-name collision has priority over empty marker");
        expect(parseArmed(marker,collision,names,error)&&names==std::set<std::string>{keyName(marker)},"actual parser preserves real NONE or Chinese marker-named device as armed");
        expect(parseArmed(marker+"\nknown device\n",collision,names,error)&&names.size()==2,"mixed actual known labels remain a real list when marker is a real device");
    }
    {
        std::set<std::string> names;std::string error;
        expect(parseArmed("\xE6\x97\xA0\r\n",inv,names,error)&&names.empty(),"actual production parser accepts Chinese empty wake list");
        expect(parseArmed("\r\n NONE \r\n\r\n",inv,names,error)&&names.empty(),"only a single nonempty trimmed marker line is empty");
    }
    for(const auto& text:std::vector<std::string>{""," \r\n","none","UNKNOWN","Keine","\xE6\x97\xA0\nknown device","NONE\nknown device","known device\nunknown","\xE6\x97\xA0\n\xE6\x97\xA0",std::string("NONE\0",5),std::string("\xE6\x97\xA0\0",4),std::string("\xff",1)}) {
        std::set<std::string> names={"unchanged"};std::string error;
        expect(!parseArmed(text,inv,names,error),"unknown, mixed, duplicate marker, NUL or empty output fails closed");
        expect(names==std::set<std::string>{"unchanged"},"failed parse cannot publish partially trusted armed names");
    }
    const auto device=item("device\nphysical","devices",false);
    for(const std::string marker:{std::string("NONE"),std::string("\xE6\x97\xA0")}) {
        Memory m;m.add(device,true);auto ports=m.ports();const auto fresh=ports.read;
        ports.collect=[&](Json& a,Json& b,Json& c,std::string& e){
            std::set<std::string> names;const auto text=m.current[device["key"]].get<bool>()?std::string("known device"):marker;
            if(!parseArmed(text,inv,names,e))return false;
            a=names.empty()?Json::array():m.candidates;b=Json::array();c=Json::array();return true;
        };
        ports.read=[&](const Json& a,Json& value,std::string& e){
            Json current;if(!fresh(a,current,e))return false;std::set<std::string> names;
            if(!parseArmed(current.get<bool>()?std::string("known device"):marker,inv,names,e))return false;
            value=names.count("known device")!=0;return true;
        };
        Policy on(ports);const auto applied=on.scan();
        expect(applied["ok"]==true&&m.writes==1&&m.reads==3,"actual localized parser drives fresh observe prewrite and readback through production policy");
        expect(m.disk["entries"][0]["baseline"]==true&&m.disk["entries"][0]["owned"]==true&&m.disk["entries"][0]["stage"]=="applied","last disarmed device keeps durable true baseline with confirmed localized empty readback");
        expect(on.scan()["ok"]==true&&m.writes==1&&m.disk["protectedDevices"].empty(),"empty collection is idempotent and never protects a fake marker device");
        Policy off(ports);expect(off.restore()["ok"]==true&&m.writes==2&&m.current[device["key"]]==true,"OFF freshly parses localized empty state and restores only its original true baseline");
        expect(off.restore()["ok"]==true&&m.writes==2,"localized OFF remains idempotent");
    }
    {
        Memory m;m.add(device,true);auto ports=m.ports();const auto fresh=ports.read;bool badReadback=true;
        ports.read=[&](const Json& a,Json& value,std::string& e){
            Json current;if(!fresh(a,current,e))return false;std::set<std::string> names;
            const auto text=current.get<bool>()?std::string("known device"):(badReadback?std::string("\xE6\x97\xA0\nunknown"):std::string("\xE6\x97\xA0"));
            if(!parseArmed(text,inv,names,e))return false;value=names.count("known device")!=0;return true;
        };
        Policy on(ports);const auto failed=on.scan();
        expect(failed["ok"]==false&&m.writes==1&&failed["status"]["restorePending"]==1,"mixed localized readback keeps failure and pending ownership");
        expect(m.disk["entries"][0]["baseline"]==true&&m.disk["entries"][0]["owned"]==true&&m.disk["entries"][0]["stage"]=="retry-pending","invalid readback cannot discard saved restore intent");
        badReadback=false;Policy off(ports);expect(off.restore()["ok"]==true&&m.writes==2,"next independent OFF can recover prior unknown readback with strict localized evidence");
    }
    {
        Memory m;m.add(device,true);auto ports=m.ports();const auto fresh=ports.read;
        ports.read=[&](const Json& a,Json& value,std::string& e){Json current;if(!fresh(a,current,e))return false;std::set<std::string> names;if(!parseArmed(m.reads==2?"known device\nunknown":"known device",inv,names,e))return false;value=names.count("known device")!=0;return true;};
        Policy p(ports);expect(p.scan()["ok"]==false&&m.writes==0,"invalid fresh prewrite list never grants a new device write");
    }
    return checks;
}
inline unsigned run() {
    unsigned checks=armedParserTests();
    const auto expect=[&](bool ok,const char* name){++checks;if(!ok)throw std::runtime_error(std::string("STATIC_WAKE_CHECK_FAILED: ")+name);};
    const auto software=item("PROCESS\nexample.exe","software",Json::array({"AWAYMODE","DISPLAY","SYSTEM"}));
    const auto device=item("device\nusb-id","devices",false);
    const auto ac=item("timer\nscheme\nAC","timers",0u),dc=item("timer\nscheme\nDC","timers",0u);
    {
        const auto caller=[](const std::string& key,const std::string& type,const std::string& name) {
            auto value=item(key,"software",Json::array({"AWAYMODE","DISPLAY","SYSTEM"}));
            value["target"]={{"type",type},{"name",name}};return value;
        };
        const auto ordinary=caller("ordinary-a","PROCESS","system-worker.exe"),ordinary2=caller("ordinary-b","PROCESS","other.exe");
        const auto steam=caller("steam-process","PROCESS","StEaM.ExE"),web=caller("steam-web","PROCESS","steamwebhelper.exe");
        const auto service=caller("steam-service","SERVICE","STEAM CLIENT SERVICE");
        Json candidates=Json::array({ordinary,steam,device,ordinary2,ac,service,dc,web});const auto original=candidates;
        ymcc::staticwake::orderScanCandidates(candidates);
        expect(candidates==Json::array({ac,dc,device,steam,service,web,ordinary,ordinary2}),"scan order retains timers and devices before stable Steam callers and ordinary tail");
        auto repeated=candidates;ymcc::staticwake::orderScanCandidates(repeated);
        expect(repeated==candidates,"scan ordering is idempotent and never creates or removes a caller");
        const auto count=[&](const Json& values,const std::string& key){return std::count_if(values.begin(),values.end(),[&](const Json& value){return value["key"]==key;});};
        expect(std::all_of(original.begin(),original.end(),[&](const Json& value){return count(original,value["key"])==count(candidates,value["key"]);}),"scan ordering preserves every original candidate occurrence");
        expect(ymcc::staticwake::preferredSoftwareCaller(caller("steam-helper","PROCESS","STEAMSERVICE.EXE")),"Steam helper name ordering is case insensitive");
        expect(!ymcc::staticwake::preferredSoftwareCaller(caller("driver","DRIVER","steam.exe"))&&
            !ymcc::staticwake::preferredSoftwareCaller(caller("path","PROCESS","C:\\elsewhere\\steam.exe"))&&
            !ymcc::staticwake::preferredSoftwareCaller(caller("prefix","PROCESS","steam.exe.bak")),"scan ordering does not broaden exact caller name or caller type matching");
        expect(!ymcc::staticwake::preferredSoftwareCaller(caller("fan","PROCESS","YeManFanHost.exe"))&&
            ymcc::staticwake::protectedFanProcess(caller("fan","PROCESS","YeManFanHost.exe")),"scan order leaves the original fan protection unchanged");
        Memory m;m.add(ordinary,ordinary["expected"]);m.add(ordinary2,ordinary2["expected"]);m.add(steam,steam["expected"]);m.add(web,web["expected"]);
        ymcc::staticwake::orderScanCandidates(m.candidates);m.readStep=60000;
        Policy policy(m.ports());const auto result=policy.scan(180000);
        expect(result["ok"]==false&&m.writes==0&&m.reads==3&&m.disk["scanInProgress"]==true,"priority never extends the original scan deadline or writes through expired reads");
        expect(m.disk["entries"].size()==2&&m.disk["entries"][0]["key"]==steam["key"]&&m.disk["entries"][1]["key"]==web["key"],"original budget visits Steam before ordinary software tail without claiming full coverage");
    }
    // Continue old uncertain device results even when successful disabling
    // removes the device from the next wake_armed candidate snapshot.
    const auto absentPending=[&](Memory& m) {
        m.add(device,true);m.failReadAt=3;Policy first(m.ports());first.scan();
        expect(m.disk["entries"][0]["stage"]=="retry-pending"&&m.disk["entries"][0]["owned"]==true&&m.current[device["key"]]==false,"absent recheck starts from an actual uncertain completed device write");
        m.candidates=Json::array();m.failReadAt=0;m.reads=0;m.writes=0;m.saves=0;
    };
    {
        Memory m;absentPending(m);Policy p(m.ports());const auto result=p.scan();
        expect(result["ok"]==true&&m.reads==1&&m.writes==0,"absent old pending device receives one fresh read and no write");
        expect(m.disk["entries"][0]["stage"]=="applied"&&m.disk["entries"][0]["error"]==""&&m.disk["entries"][0]["owned"]==true&&m.disk["entries"][0]["baseline"]==true,"fresh expected state clears stale error while preserving OFF recovery originals");
        Policy reloaded(m.ports());expect(m.disk["entries"][0]["lastVerificationEpoch"]==m.epoch&&m.disk["entries"][0]["lastVerificationResult"]=="verified"&&reloaded.status()["devices"]==1,"absent verification epoch and result are durable and reloadable");
        expect(p.restore()["ok"]==true&&m.writes==1&&m.current[device["key"]]==true,"OFF after absent recheck restores exactly the original true baseline");
    }
    {
        Memory m;absentPending(m);m.disk["entries"][0]["stage"]="prepared";m.disk["entries"][0]["error"]="";Policy p(m.ports());
        expect(p.scan()["ok"]==true&&m.reads==1&&m.writes==0,"absent durable prepared intent is confirmed by read only");
    }
    {
        Memory m;absentPending(m);m.current[device["key"]]=true;Policy p(m.ports());
        expect(p.scan()["ok"]==false&&m.reads==1&&m.writes==0&&m.disk["entries"][0]["stage"]=="retry-pending"&&m.disk["entries"][0]["owned"]==true,"absent device at baseline remains uncertain and is never silently disabled or released");
        expect(m.disk["entries"][0]["lastVerificationEpoch"]==m.epoch&&m.disk["entries"][0]["lastVerificationResult"]=="mismatch","fresh true records a failed verification without relinquishing ownership");
        expect(p.restore()["ok"]==true&&m.writes==0,"OFF can settle preserved debt at baseline without rewriting it");
    }
    {
        Memory m;absentPending(m);m.failReadAt=1;Policy p(m.ports());
        expect(p.scan()["ok"]==false&&m.reads==1&&m.writes==0&&m.disk["entries"][0]["owned"]==true&&m.disk["entries"][0]["stage"]=="retry-pending","failed absent fresh read keeps original ownership and pending state");
        expect(m.disk["entries"][0]["lastVerificationEpoch"]==m.epoch&&m.disk["entries"][0]["lastVerificationResult"]=="read-failed","failed fresh read also durably records its attempted verification epoch");
    }
    {
        Memory m;absentPending(m);auto ports=m.ports();bool canceled=false;ports.canceled=[&](){return canceled;};const auto read=ports.read;
        ports.read=[&](const Json& a,Json& value,std::string& error){const bool ok=read(a,value,error);canceled=true;return ok;};Policy p(ports);p.scan();
        expect(m.reads==1&&m.writes==0&&m.saves==2&&m.disk["entries"][0]["stage"]=="retry-pending"&&m.disk["scanInProgress"]==true,"cancel after absent read prevents result persistence and retains incomplete ownership");
    }
    {
        Memory m;absentPending(m);m.readStep=180000;Policy p(m.ports());p.scan();
        expect(m.reads==1&&m.writes==0&&m.saves==2&&m.disk["entries"][0]["stage"]=="retry-pending"&&m.disk["scanInProgress"]==true,"absent read cannot renew operation deadline or confirm after expiry");
    }
    {
        Memory m;absentPending(m);m.failSaveAt=3;Policy p(m.ports());p.scan();
        expect(m.reads==1&&m.writes==0&&m.disk["entries"][0]["stage"]=="retry-pending","failed absent-result journal persist never loses original recovery debt");
    }
    {
        Memory m;absentPending(m);m.candidates.push_back(device);Policy p(m.ports());
        expect(p.scan()["ok"]==true&&m.reads==1&&m.writes==0,"existing candidate handles old pending once without duplicate recheck");
    }
    {
        Memory m;absentPending(m);m.disk["entries"][0]["stage"]="applied";m.disk["entries"][0]["error"]="";Policy p(m.ports());
        expect(p.scan()["ok"]==true&&m.reads==0&&m.writes==0,"already-applied absent records do not add repeat inventory work");
    }

    // Required boundaries for the same absent-pending consumer, no new build.
    for(const std::string stage:{"restore-prepared","external-change"}) {
        Memory m;absentPending(m);m.disk["entries"][0]["stage"]=stage;const auto saved=m.disk["entries"][0];Policy p(m.ports());p.scan();
        expect(m.reads==0&&m.writes==0&&m.disk["entries"][0]==saved,"absent recheck cannot hijack restore preparation or external change");
    }
    {
        Memory m;absentPending(m);m.disk["enabled"]=false;const auto saved=m.disk;Policy p(m.ports());
        expect(p.scan()["ok"]==false&&m.reads==0&&m.writes==0&&m.saves==0&&m.disk==saved,"OFF ownership blocks absent verification and a new ON generation");
    }
    {
        Memory m;absentPending(m);m.disk["entries"][0]["owned"]=false;const auto saved=m.disk["entries"][0];Policy p(m.ports());p.scan();
        expect(m.reads==0&&m.writes==0&&m.disk["entries"][0]==saved,"unowned absent record cannot acquire verification or ownership");
    }
    {
        Memory m;absentPending(m);const Json target={{"name","known device"},{"instanceId","USB\\owned-instance"}};m.disk["entries"][0]["target"]=target;
        auto ports=m.ports();const auto read=ports.read;Json requested;
        ports.observe=[&](const Json&,Json& value,std::string&){++m.observations;value=false;return true;};
        ports.read=[&](const Json& a,Json& value,std::string& error){requested=a["target"];read(a,value,error);value=false;error="device-name-or-instance-identity-changed";return false;};
        Policy p(ports);const auto result=p.scan();const auto saved=m.disk["entries"][0];
        expect(result["ok"]==false&&requested==target&&m.reads==1&&m.observations==0&&m.writes==0&&saved["owned"]==true&&saved["stage"]=="retry-pending"&&saved["lastVerificationResult"]=="read-failed"&&saved["error"].get<std::string>().find("identity-changed")!=std::string::npos,"identity failure with false output cannot use cached observation or claim verification");
    }
    for(const auto& unknown:std::vector<Json>{nullptr,"false",0u}) {
        Memory m;absentPending(m);m.current[device["key"]]=unknown;Policy p(m.ports());const auto result=p.scan();
        expect(result["ok"]==false&&m.reads==1&&m.writes==0&&m.disk["entries"][0]["owned"]==true&&m.disk["entries"][0]["stage"]=="retry-pending"&&m.disk["entries"][0]["lastVerificationResult"]=="invalid-state","unknown or nonboolean fresh value retains recovery ownership");
    }
    {
        Memory m;absentPending(m);const auto saved=m.disk;auto ports=m.ports();ports.canceled=[](){return true;};Policy p(ports);
        expect(p.scan()["ok"]==false&&m.reads==0&&m.writes==0&&m.saves==0&&m.disk==saved,"canceled prior-pending operation records no verification or hardware write");
    }
    {
        Memory m;absentPending(m);m.protectedItems.push_back({{"name",device["name"]},{"reason","human-power-button-or-ancestor"}});Policy p(m.ports());const auto result=p.scan();
        expect(result["ok"]==true&&result["status"]["protectedDevices"]==1&&m.reads==1&&m.writes==0&&m.disk["entries"][0]["owned"]==true&&m.disk["entries"][0]["stage"]=="applied","new device protection permits only fresh verification and preserves original ownership");
    }
    {
        Memory m;absentPending(m);m.collectComplete=false;m.collectionErrors.push_back({{"error","device enumeration incomplete"}});Policy p(m.ports());const auto result=p.scan();
        expect(result["ok"]==false&&result["status"]["failed"]==2&&result["status"]["devices"]==1&&m.reads==1&&m.writes==0&&m.disk["entries"][0]["lastVerificationResult"]=="verified","verified old device never clears actual collection failures");
    }
    for(const std::string kind:{"software","timers"}) {
        Memory m;absentPending(m);auto& saved=m.disk["entries"][0];saved["kind"]=kind;
        saved["expected"]=kind=="software"?software["expected"]:ac["expected"];saved["baseline"]=kind=="software"?Json::array():Json(2u);
        if(kind=="software")saved["target"]={{"type","PROCESS"},{"name","YeManFanHost.exe"}};
        const auto before=saved;Policy p(m.ports());p.scan();
        expect(m.reads==0&&m.writes==0&&m.disk["entries"][0]==before,"absent software fan and timer debt remain outside DEVICE verification");
    }
    for(const bool invalidExpected:{false,true}) {
        Memory m;absentPending(m);m.disk["entries"][0][invalidExpected?"expected":"baseline"]=invalidExpected;const auto before=m.disk["entries"][0];Policy p(m.ports());p.scan();
        expect(m.reads==0&&m.writes==0&&m.disk["entries"][0]==before,"absent verification requires the original true baseline and false device intent");
    }

    {
        Memory m;m.add(software,Json::array({"DISPLAY"}));m.add(device,true);m.add(ac,1u);m.add(dc,2u);
        m.protectedItems.push_back({{"name","physical power button"},{"reason","human-power-button-or-ancestor"}});
        Policy p(m.ports());const auto status=p.status();
        expect(m.saves==0&&m.reads==0&&m.writes==0,"get status never enumerates or mutates");
        expect(status["failed"]==0&&status["restorePending"]==0,"summary uses numeric zero counters");
        const auto result=p.scan();
        expect(result["ok"]==true,"successful scan ok numeric comparison");
        expect(result["status"]["phase"]=="applied"&&result["status"]["enabled"]==true,"scan applied state");
        expect(result["status"]["software"]==1&&result["status"]["devices"]==1&&result["status"]["timers"]==2,"independent confirmed category counts");
        expect(result["status"]["protectedDevices"]==1,"protected devices counted separately");
        expect(result["status"]["lastScanEpoch"]==m.epoch,"scan records epoch");
        expect(m.disk["entries"][0]["baseline"]==Json::array({"DISPLAY"}),"preexisting software baseline preserved");
        expect(m.disk["entries"][1]["baseline"]==true,"device baseline persisted");
        expect(m.disk["entries"][2]["baseline"]==1&&m.disk["entries"][3]["baseline"]==2,"distinct AC DC baselines");
        expect(m.writes==4,"only four planned writes");
        const auto priorSaves=m.saves;
        const auto count=m.writes;expect(p.scan()["ok"]==true&&m.writes==count,"rescan idempotent no repeated writes");
        expect(m.saves-priorSaves<=3,"rescan avoids unchanged per-item journal flushes");
        expect(m.disk["entries"][0]["baseline"]==Json::array({"DISPLAY"}),"rescan does not replace original baseline");
        m.source=true;expect(p.status()["diagnosticSourceReady"]==true,"diagnostic readiness read-only injectable");
        expect(p.restore()["ok"]==true,"OFF restores successfully");
        expect(m.current[software["key"]]==Json::array({"DISPLAY"})&&m.current[device["key"]]==true,"OFF restores exact caller and wake baseline");
        expect(m.current[ac["key"]]==1&&m.current[dc["key"]]==2,"OFF restores original AC DC independently");
        expect(p.status()["phase"]=="idle"&&p.status()["enabled"]==false,"OFF idle after all owned values settled");
        const auto offWrites=m.writes;expect(p.restore()["ok"]==true&&m.writes==offWrites,"repeated OFF is idempotent");
        expect(p.scan()["ok"]==true&&m.disk["history"].size()==1,"new ON archives closed generation");
    }
    {
        Memory m;m.add(device,false);Policy p(m.ports());expect(p.scan()["ok"]==true&&m.writes==0,"preexisting desired value is never claimed");
        expect(m.disk["entries"][0]["owned"]==false,"no ownership without own change");
        m.current[device["key"]]=true;p.restore();expect(m.current[device["key"]]==true&&m.writes==0,"OFF preserves external change of preexisting item");
    }
    {
        Memory m;m.add(ac,2u);Policy p(m.ports());p.scan();m.current[ac["key"]]=1u;
        expect(p.restore()["ok"]==true,"external timer modification is not a restore error");
        expect(m.writes==1&&m.current[ac["key"]]==1&&p.status()["externalChanges"].size()==1,"external timer is preserved and released");
    }
    {
        Memory m;m.add(software,Json::array());Policy p(m.ports());p.scan();m.current[software["key"]]=Json::array({"SYSTEM"});
        p.scan();expect(m.writes==1&&m.current[software["key"]]==Json::array({"SYSTEM"}),"rescan never overwrites external software edits");
        p.scan();expect(m.writes==1&&m.disk["entries"][0]["baseline"].empty(),"rescan cannot reacquire abandoned generation");
        p.restore();expect(m.writes==1,"OFF skips abandoned ownership");
    }
    {
        Memory m;m.add(device,true);m.failSaveAt=5;Policy p(m.ports());const auto r=p.scan();
        expect(r["ok"]==false&&m.writes==0,"failed durable intent blocks mutation");
        expect(m.disk["entries"][0]["baseline"]==true,"baseline survived intent commit failure");
        expect(r["status"]["lastError"].get<std::string>().find("journal-persist")!=std::string::npos,"persistence error is visible");
    }
    {
        Memory m;m.add(device,true);m.failSaveAt=4;Policy p(m.ports());expect(p.scan()["ok"]==false&&m.writes==0,"failed baseline commit blocks mutation");
    }
    {
        Memory m;m.add(device,true);m.failSaveAt=6;Policy p(m.ports());expect(p.scan()["ok"]==false&&m.current[device["key"]]==false,"post-write commit failure remains visible");
        expect(m.disk["entries"][0]["stage"]=="prepared"&&m.disk["entries"][0]["owned"]==true,"durable intent survives crash window");
        Policy restarted(m.ports());expect(restarted.restore()["ok"]==true&&m.current[device["key"]]==true,"restart restores pending write intent by expected comparison");
    }
    {
        Memory m;m.add(device,true);m.failWriteCount=1;Policy p(m.ports());const auto r=p.scan();
        expect(r["ok"]==false&&r["status"]["phase"]=="partial"&&r["status"]["restorePending"]==1,"write failure is partial pending");
        expect(p.scan()["ok"]==true&&m.current[device["key"]]==false,"explicit retry completes failed write");
        expect(m.disk["entries"][0]["baseline"]==true,"retry retains first baseline");
        m.failWriteCount=1;const auto off=p.restore();expect(off["ok"]==false&&off["status"]["phase"]=="restore-pending","failed OFF retains ownership");
        const auto blocked=p.scan();expect(blocked["ok"]==false&&m.current[device["key"]]==false,"pending OFF cannot start new generation");
        Policy again(m.ports());expect(again.restore()["ok"]==true&&m.current[device["key"]]==true,"failed OFF retryable after restart");
    }
    {
        Memory m;m.add(ac,2u);m.failWriteCount=1;m.effectOnWriteFailure=true;Policy p(m.ports());
        expect(p.scan()["ok"]==false&&m.current[ac["key"]]==0,"failed command with side effect not claimed successful");
        expect(p.restore()["ok"]==true&&m.current[ac["key"]]==2,"failed command side effect can be restored");
    }
    {
        Memory m;m.add(device,true);m.mismatch=true;Policy p(m.ports());const auto r=p.scan();
        expect(r["ok"]==false&&r["status"]["devices"]==0,"exit success without matching readback fails");
        expect(r["status"]["lastError"]=="readback-mismatch","readback mismatch reason retained");
        m.mismatch=false;expect(p.scan()["ok"]==true,"readback mismatch retryable");
    }
    {
        Memory m;m.add(device,true);m.failReadAt=3;Policy p(m.ports());const auto r=p.scan();
        expect(r["ok"]==false&&m.current[device["key"]]==false,"readback unavailable leaves recovery intent");
        Policy again(m.ports());expect(again.restore()["ok"]==true,"unreadable post-write result retryable on OFF");
    }
    {
        Memory m;m.add(device,true);m.raceReadAt=2;m.raceValue=false;Policy p(m.ports());p.scan();
        expect(m.writes==0&&m.disk["entries"][0]["owned"]==false,"prewrite race preserves external state and relinquishes claim");
    }
    {
        Memory m;m.add(ac,2u);Policy p(m.ports());p.scan();m.raceReadAt=m.reads+2;m.raceValue=1u;
        p.restore();expect(m.writes==1&&m.current[ac["key"]]==1,"restore rechecks ownership after journal preparation");
    }
    {
        Memory m;m.add(device,true);m.readStep=180000;Policy p(m.ports());const auto r=p.scan();
        expect(r["ok"]==false&&m.writes==0,"shared deadline prevents late write");
    }
    {
        Memory m;m.add(ac,2u);m.collectComplete=false;m.collectionErrors.push_back({{"error","service enumeration denied"}});Policy p(m.ports());const auto r=p.scan();
        expect(r["ok"]==false&&m.current[ac["key"]]==0&&r["status"]["timers"]==1,"partial enumeration still applies independent safe subset");
        expect(r["status"]["failed"]==2,"collection failure and missing subset reported numerically");
    }
    {
        Memory m;m.protectedItems.push_back({{"name","ambiguous power ancestor"},{"reason","fail-safe"}});Policy p(m.ports());const auto r=p.scan();
        expect(r["status"]["protectedDevices"]==1&&m.writes==0,"protected-only discovery creates no mutation");
    }
    {
        Memory m;m.loadFails=true;m.add(device,true);Policy p(m.ports());expect(p.scan()["ok"]==false&&m.writes==0&&m.saves==0,"unreadable journal fails closed");
        m.loadFails=false;m.disk={{"schema",99}};Policy corrupt(m.ports());expect(corrupt.restore()["ok"]==false&&m.saves==0,"unsupported journal schema never overwritten");
    }
    {
        Memory m;m.add(device,true);m.candidates.push_back(device);Policy p(m.ports());const auto r=p.scan();
        expect(r["ok"]==false&&m.writes==1,"duplicate candidate rejected without duplicate write");
    }
    {
        Memory m;m.add(device,true);bool canceled=true;auto ports=m.ports();ports.canceled=[&](){return canceled;};Policy p(ports);
        expect(p.scan()["ok"]==false&&m.saves==0&&m.reads==0&&m.writes==0,"already canceled scan performs no port writes or reads");
    }
    {
        Memory m;m.add(device,true);bool canceled=false;auto ports=m.ports();ports.canceled=[&](){return canceled;};const auto persist=ports.persist;
        ports.persist=[&](const Json& j,std::string& e){const bool ok=persist(j,e);if(ok&&!j["entries"].empty()&&j["entries"][0]["stage"]=="prepared")canceled=true;return ok;};
        Policy p(ports);expect(p.scan()["ok"]==false&&m.writes==0&&m.reads==1,"cancellation after durable intent blocks prewrite read and write");
        expect(m.disk["entries"][0]["baseline"]==true&&m.disk["entries"][0]["owned"]==true,"canceled intent preserves original baseline");
        Policy restarted(m.ports());
        expect(restarted.status()["phase"]=="partial"&&restarted.status()["failed"].get<unsigned>()>0,"interrupted scan marker survives restart without false applied status");
        expect(restarted.restore()["ok"]==true&&m.writes==0,"unexecuted canceled intent releases on baseline equality");
    }
    {
        Memory m;m.add(device,true);bool canceled=false;auto ports=m.ports();ports.canceled=[&](){return canceled;};const auto write=ports.write;
        ports.write=[&](const Json& j,const Json& v,std::string& e){const bool ok=write(j,v,e);canceled=true;return ok;};
        Policy p(ports);const auto r=p.scan();
        expect(r["ok"]==false&&m.writes==1&&m.reads==2,"cancellation after mutation stops readback and further work");
        expect(m.disk["entries"][0]["stage"]=="prepared"&&r["status"]["restorePending"]==1,"cancellation retains uncertain side effect as recovery intent");
        Policy restarted(m.ports());expect(restarted.restore()["ok"]==true&&m.current[device["key"]]==true,"next authorized OFF restores canceled side effect");
    }
    {
        Memory m;m.add(ac,static_cast<std::uint64_t>(~std::uint64_t{0}));Policy p(m.ports());
        expect(p.scan()["ok"]==false&&m.writes==0,"out-of-DWORD timer baseline rejected without truncation");
    }
    {
        Memory m;const auto fan=item("PROCESS\nYeManFanHost.exe","software",Json::array({"AWAYMODE","DISPLAY","SYSTEM"}));
        auto target=fan;target["target"]={{"type","PROCESS"},{"name","YeManFanHost.exe"}};m.add(target,Json::array());Policy p(m.ports());const auto r=p.scan();
        expect(r["ok"]==true&&m.writes==0&&m.reads==0,"exact YMCC fan process excluded before any request snapshot or write");
        expect(r["status"]["protectedDevices"]==0&&r["status"]["software"]==0&&r["status"]["protectedSoftwareDetails"].size()==1,"fan software skip never inflates device protection counts");
        expect(m.disk["protectedSoftware"][0]["reason"]=="ymcc-fanhost-owner","fan skip preserved in required journal");
        expect(ymcc::staticwake::fanHostProcessName("YEMANFANHOST.EXE")&&!ymcc::staticwake::fanHostProcessName("OtherFan.exe")&&!ymcc::staticwake::fanHostProcessName("YeManFanHost.exe.bak"),"fan exclusion is exact case-insensitive basename only");
    }
    // Reverse-check parameterization of the original ownership scenario:
    // a transient read failure must not erase proof of prior successful apply.
    for(const auto& candidate:std::vector<Json>{device,ac,software}) {
        const auto kind=candidate["kind"].get<std::string>();
        const Json baseline=kind=="devices"?Json(true):(kind=="timers"?Json(2u):Json::array());
        Memory m;m.add(candidate,baseline);Policy p(m.ports());expect(p.scan()["ok"]==true,"reverse prior successful application");
        const auto writes=m.writes;m.failReadAt=m.reads+1;
        expect(p.scan()["ok"]==false,"reverse transient read failure remains visible");
        m.current[candidate["key"]]=baseline; // External edit after that failed observation.
        Policy restarted(m.ports());const auto r=restarted.scan();
        expect(m.writes==writes&&m.current[candidate["key"]]==baseline,"prior successful apply survives read failure and never overwrites external baseline");
        expect(r["ok"]==false&&m.disk["entries"][0]["owned"]==false&&m.disk["entries"][0]["stage"]=="external-change","reverse external edit relinquishes ownership and reports conflict");
        expect(restarted.restore()["ok"]==true&&m.writes==writes,"reverse OFF preserves externally edited baseline");
    }
    // CPU-cost checks use actual snapshot-load counters, with the existing
    // ownership/readback/cancellation assertions above unchanged.
    for(const auto& kind:std::vector<std::string>{"software","devices"}) {
        const Json expected=kind=="software"?Json::array({"AWAYMODE","DISPLAY","SYSTEM"}):Json(false);
        Memory m;for(unsigned i=0;i<64;++i)m.add(item(kind+std::to_string(i),kind,expected),expected);
        Memory uncached;for(const auto& candidate:m.candidates)uncached.add(candidate,expected);
        Policy baselinePolicy(uncached.ports());const auto baselineResult=baselinePolicy.scan();
        Policy p(m.snapshotPorts());const auto r=p.scan();
        expect(baselineResult["ok"]==true&&uncached.reads==64&&uncached.writes==0,"same-input uncached 64-item baseline executes 64 reads");
        expect(r["ok"]==true&&m.writes==0,"snapshot unchanged batch never mutates");
        expect(m.snapshotLoads==1&&m.reads==1&&m.observations==64,"64 unchanged observations use one actual snapshot load");
        const auto saves=m.saves;expect(p.scan()["ok"]==true&&m.snapshotLoads==2,"new scan invalidates observations even with unchanged epoch");
        expect(m.saves-saves<=3,"snapshot scan retains no-per-item unchanged journal flush optimization");
    }
    for(const auto& kind:std::vector<std::string>{"software","devices"}) {
        const Json expected=kind=="software"?Json::array({"AWAYMODE","DISPLAY","SYSTEM"}):Json(false);
        const Json baseline=kind=="software"?Json::array():Json(true);
        Memory m;for(unsigned i=0;i<32;++i)m.add(item(kind+std::to_string(i),kind,expected),baseline);
        Memory uncached;for(const auto& candidate:m.candidates)uncached.add(candidate,baseline);
        Policy baselinePolicy(uncached.ports());const auto baselineResult=baselinePolicy.scan();
        Policy p(m.snapshotPorts());const auto r=p.scan();
        expect(baselineResult["ok"]==true&&uncached.reads==96&&uncached.writes==32,"same-input uncached 32-item mutation baseline executes 96 reads");
        expect(r["ok"]==true&&m.writes==32,"snapshot mutation batch still applies each item once");
        expect(m.snapshotLoads==65&&m.reads==65,"32 writes retain 64 fresh prewrite/readback loads plus first observation");
        const auto loads=m.snapshotLoads;
        expect(p.restore()["ok"]==true&&m.writes==64,"snapshot-enabled ports restore exact baselines");
        expect(m.snapshotLoads-loads==96,"OFF uses fresh initial/prewrite/readback for all 32 owned items");
    }
    {
        Memory m;const auto first=item("first","software",software["expected"]),second=item("second","software",software["expected"]);
        m.add(first,first["expected"]);m.add(second,Json::array());auto ports=m.snapshotPorts();const auto persist=ports.persist;
        ports.persist=[&](const Json& j,std::string& e){const bool ok=persist(j,e);if(ok)for(const auto& record:j["entries"])if(record["key"]=="second"&&record["stage"]=="prepared")m.current["second"]=Json::array({"SYSTEM"});return ok;};
        Policy p(ports);const auto r=p.scan();
        expect(r["ok"]==false&&m.writes==0&&m.current["second"]==Json::array({"SYSTEM"}),"cached baseline never authorizes overwriting fresh external change");
        expect(m.snapshotLoads==2&&m.disk["entries"][1]["owned"]==false,"mandatory fresh prewrite load detects race after durable intent");
    }
    {
        Memory m;m.add(software,Json::array());bool canceled=false;auto ports=m.snapshotPorts();const auto write=ports.write;
        ports.canceled=[&](){return canceled;};ports.write=[&](const Json& a,const Json& b,std::string& e){const bool ok=write(a,b,e);canceled=true;return ok;};
        Policy p(ports);const auto r=p.scan();
        expect(r["ok"]==false&&m.writes==1&&m.snapshotLoads==2,"snapshot caching never reads back or continues after write-side cancellation");
        expect(m.disk["entries"][0]["stage"]=="prepared"&&m.disk["entries"][0]["owned"]==true,"snapshot cancellation retains original durable recovery intent");
        Policy restarted(m.snapshotPorts());expect(restarted.restore()["ok"]==true&&m.current[software["key"]].empty(),"canceled cached scan remains recoverable by fresh OFF");
    }
    {
        Memory m;for(unsigned i=0;i<64;++i)m.add(item("sw"+std::to_string(i),"software",software["expected"]),software["expected"]);
        m.failReadAt=1;Policy p(m.snapshotPorts());const auto r=p.scan();
        expect(r["ok"]==false&&r["status"]["failed"]==64&&m.snapshotLoads==1&&m.writes==0,"one failed snapshot is reported for all affected items without 64 command attempts");
        expect(p.scan()["ok"]==true&&m.snapshotLoads==2,"next explicit operation retries failed snapshot instead of retaining failure cache");
    }
    {
        using Cache=ymcc::staticwake::ObservationCache<unsigned>;Cache cache;unsigned loads=0;std::string error;
        const auto load=[&](unsigned& value,std::string&){value=++loads;return true;};
        for(unsigned i=0;i<64;++i)if(!cache.observe(100,load,error))throw std::runtime_error("snapshot cache failed");
        expect(loads==1&&cache.value()==1,"production cache serves at most 64 bounded observations");
        expect(cache.observe(100,load,error)&&loads==2,"65th observation must reload");
        expect(cache.observe(1100,load,error)&&loads==3,"1000ms age limit must reload");
        expect(cache.observe(50,load,error)&&loads==4,"clock regression cannot reuse observation");
        expect(cache.refresh(50,load,error)&&loads==5,"fresh prewrite/readback API never hits observation cache");
        cache.invalidate();expect(cache.observe(50,load,error)&&loads==6,"write or operation invalidation always forces reload");
    }
    {
        Memory m;m.add(software,Json::array());m.readStep=180000;Policy p(m.snapshotPorts());
        expect(p.scan()["ok"]==false&&m.writes==0&&m.snapshotLoads==1,"observation load still shares original 180-second budget");
    }
    {
        using Receipts=ymcc::staticwake::OperationReceipts;Receipts receipts;
        const auto unknown=receipts.get("missing");
        expect(unknown==Json({{"requestId","missing"},{"action",""},{"state","unknown"}}),"missing receipt has explicit unknown state without ok");
        for(const auto& id:std::vector<std::string>{"",std::string(97,'a'),std::string(4096,'a'),"a b","a/b","a.b","a\nb",std::string("a\0b",3),std::string(1,static_cast<char>(0x80)),std::string("\xc3\xa9",2)}) {
            expect(!Receipts::validRequestId(id)&&!receipts.begin(id,"scan"),"invalid or empty receipt ID explicitly rejects admission");
            expect(!receipts.complete(id,"scan",true)&&receipts.get(id)==Json({{"requestId",""},{"action",""},{"state","unknown"}}),"invalid receipt ID cannot complete or produce success evidence");
        }
        for(const auto& action:std::vector<std::string>{"","Scan","restore ","status"})
            expect(!Receipts::validAction(action)&&!receipts.begin("valid",action)&&!receipts.complete("valid",action,true),"only exact scan and restore actions are accepted");
        expect(receipts.get("valid")["state"]=="unknown","rejected action reserves no ID");
        const std::string maxId=std::string(90,'a')+"AZ09-_";
        expect(Receipts::validRequestId(maxId)&&receipts.begin(maxId,"scan"),"96-byte ASCII ID including uppercase digits dash underscore is accepted");
        expect(!receipts.begin(maxId,"scan")&&!receipts.begin(maxId,"restore"),"running ID rejects replay across both actions");
        expect(receipts.get(maxId)==Json({{"requestId",maxId},{"action","scan"},{"state","running"}}),"running receipt never exposes ok");
        expect(!receipts.complete(maxId,"restore",true)&&!receipts.complete("missing","scan",true),"wrong action or unknown ID cannot finish a running request");
        const auto& view=receipts;
        for(unsigned i=0;i<32;++i)expect(view.get(maxId)["state"]=="running"&&!view.get(maxId).contains("ok"),"repeated status reads never unlock an unfinished request");
        expect(receipts.complete(maxId,"scan",false),"explicit failed operation still completes");
        const auto failed=receipts.get(maxId);
        expect(failed==Json({{"requestId",maxId},{"action","scan"},{"state","completed"},{"ok",false}}),"failed completion retains boolean false");
        expect(!receipts.complete(maxId,"scan",true)&&!receipts.complete(maxId,"restore",true)&&receipts.get(maxId)==failed,"duplicate or mismatched completion cannot overwrite failure");
        expect(!receipts.begin(maxId,"scan")&&!receipts.begin(maxId,"restore"),"completed ID remains protected from replay");
        auto detached=receipts.get(maxId);detached["ok"]=true;
        expect(receipts.get(maxId)==failed,"returned JSON is an independent snapshot");
        expect(receipts.begin("a","restore")&&receipts.begin("A","scan")&&receipts.complete("a","restore",true),"IDs remain case-sensitive and restore completes");
        expect(receipts.get("a")==Json({{"requestId","a"},{"action","restore"},{"state","completed"},{"ok",true}})&&receipts.get("A")["state"]=="running","matching completion leaves other request running");
    }
    {
        ymcc::staticwake::OperationReceipts receipts;
        for(unsigned i=0;i<16;++i)expect(receipts.begin("running-"+std::to_string(i),"scan"),"all sixteen running receipt slots admit");
        expect(!receipts.begin("overflow","restore")&&receipts.get("overflow")["state"]=="unknown","sixteen running records reject overflow without admitting it");
        expect(!receipts.complete("running-7","restore",true)&&!receipts.begin("overflow","scan"),"wrong action cannot free capacity or unlock a running request");
        for(unsigned i=0;i<16;++i)expect(receipts.get("running-"+std::to_string(i))["state"]=="running","capacity pressure never evicts running requests");
        expect(receipts.complete("running-9","scan",false)&&receipts.complete("running-2","scan",true),"completion order differs from admission order");
        expect(receipts.get("running-9")["ok"]==false&&!receipts.begin("running-9","restore"),"completed failure lookup and replay leave oldest completion unchanged");
        expect(receipts.begin("overflow","restore")&&receipts.get("running-9")["state"]=="unknown"&&!receipts.get("running-9").contains("ok"),"full table evicts oldest completed record even when it failed");
        expect(receipts.get("running-2")["state"]=="completed"&&receipts.get("running-0")["state"]=="running","newer completed and oldest running records survive eviction");
        expect(!receipts.complete("running-9","scan",true)&&!receipts.complete("running-2","scan",false),"evicted and already completed IDs cannot be completed again");
        expect(receipts.begin("next","scan")&&receipts.get("running-2")["state"]=="unknown","next admission evicts the next oldest completion");
        expect(!receipts.begin("extra","scan")&&receipts.get("overflow")["state"]=="running","table returns to sixteen running records without false unlock");
    }
    {
        ymcc::staticwake::OperationReceipts receipts;std::atomic<unsigned> ready{0},admitted{0};std::atomic<bool> start{false};
        const auto race=[&](){++ready;while(!start.load())std::this_thread::yield();if(receipts.begin("shared","scan"))++admitted;};
        std::thread first(race),second(race);while(ready.load()!=2)std::this_thread::yield();start=true;first.join();second.join();
        expect(admitted.load()==1&&receipts.get("shared")["state"]=="running","concurrent duplicate admission has exactly one owner");
        bool completed=false;Json observed;
        std::thread worker([&](){completed=receipts.complete("shared","scan",false);}),reader([&](){observed=receipts.get("shared");});worker.join();reader.join();
        expect(completed&&((observed["state"]=="running"&&!observed.contains("ok"))||(observed["state"]=="completed"&&observed["ok"]==false)),"concurrent status sees a complete running or completed snapshot");
        expect(receipts.get("shared")["state"]=="completed"&&receipts.get("shared")["ok"]==false,"worker completion becomes visible after join");
    }
    // Same production circuit/cache consumed by explicit global-failure ports.
    // Slow loads advance the clock inside the loader, including >= original TTL.
    for(const unsigned delay:std::vector<unsigned>{0,999,1000,2500}) {
        Memory m;for(unsigned i=0;i<68;++i)m.add(item("slow"+std::to_string(i),"software",software["expected"]),Json::array());
        m.readStep=delay;m.failReadAt=1;Policy p(m.snapshotPorts(true));const auto r=p.scan();
        expect(r["ok"]==false&&r["status"]["phase"]=="partial"&&m.writes==0,"global snapshot failure cannot be reported as applied or authorize writes");
        expect(m.softwareLoads==1&&m.reads==1&&m.observations==1,"one slow global failure stops all remaining software reads in this operation");
        expect(m.disk["scanErrors"].size()==1&&m.saves==5,"one global failure is aggregated once without per-caller journal flushes");
        const auto& error=m.disk["scanErrors"][0];
        expect(error["scope"]=="global-software-snapshot"&&error["queryAttempts"]==1&&error["planned"]==68,"global snapshot scope and actual query attempts are distinct from candidate failures");
        expect(error["affected"]==68&&error["visited"]==1&&error["unvisited"]==67&&error["coverage"]=="UNKNOWN"&&!error["writeFailuresClaimed"].get<bool>(),"global aggregate truthfully labels affected and unvisited coverage UNKNOWN");
        expect(r["status"]["failed"]==1&&r["status"]["software"]==0&&!m.disk["scanInProgress"].get<bool>(),"completed partial has one failure record and no fictional successful software entries");
        m.tick+=100000;Json value;std::string why;auto stillSameOperation=m.snapshotPorts(true);
        expect(!stillSameOperation.read(m.candidates[0],value,why)&&m.softwareLoads==1,"TTL expiration does not reopen a tripped global circuit inside the operation");
        // Fresh new Policy calls beginOperation; old partial journal is preserved.
        m.readStep=0;Policy retry(m.snapshotPorts(true));
        expect(retry.scan()["ok"]==true&&m.writes==68&&m.disk["scanErrors"].empty(),"next independent scan retries global snapshot and can recover normally");
    }
    for(const unsigned delay:std::vector<unsigned>{0,999,1000,2500}) {
        Memory m;for(unsigned i=0;i<68;++i)m.add(item("success"+std::to_string(i),"software",software["expected"]),software["expected"]);
        m.readStep=delay;Policy p(m.snapshotPorts(true));const auto r=p.scan();
        expect(r["ok"]==true&&m.writes==0,"successful snapshot circuit adds no mutation");
        expect(m.softwareLoads==(delay<1000?2u:68u),"successful data still expires at original 1000ms or 64-use limit");
    }
    {
        Memory m;for(unsigned i=0;i<3;++i)m.add(item("ordinary"+std::to_string(i),"software",software["expected"]),Json::array());
        m.failReadAt=1;Policy p(m.ports());const auto r=p.scan();
        expect(r["ok"]==false&&m.writes==2&&m.reads==7,"unclassified per-item read failure does not suppress unrelated software items");
        expect(m.disk["scanErrors"].size()==1&&!m.disk["scanErrors"][0].contains("scope"),"ordinary item failure is not mislabeled global snapshot failure");
    }
    {
        Memory m;for(unsigned i=0;i<3;++i)m.add(item("write-fail"+std::to_string(i),"software",software["expected"]),Json::array());
        m.failWriteCount=1;Policy p(m.snapshotPorts(true));const auto r=p.scan();
        expect(r["ok"]==false&&m.writes==3&&m.current["write-fail1"]==software["expected"],"ordinary setting-command failure does not trip global read circuit");
        ymcc::staticwake::SoftwareSnapshotFailure failure;expect(!m.softwareCircuit.failure(failure),"only actual snapshot load failure closes the software category");
    }
    for(const unsigned failureRead:std::vector<unsigned>{2,3}) {
        Memory m;const auto a=item("pending-a","software",software["expected"]),b=item("pending-b","software",software["expected"]);
        m.add(a,Json::array());m.add(b,Json::array());m.failReadAt=failureRead;m.readStep=1000;
        Policy p(m.snapshotPorts(true));const auto r=p.scan();
        expect(r["ok"]==false&&m.writes==(failureRead==3?1u:0u)&&m.softwareLoads==failureRead,"fresh prewrite or readback failure stops subsequent software writes");
        expect(m.disk["entries"].size()==1&&m.disk["entries"][0]["owned"]==true&&m.disk["entries"][0]["stage"]=="prepared"&&r["status"]["restorePending"]==1,"global fresh-read failure retains durable intent and ownership");
        expect(m.disk["scanErrors"][0]["phase"]==(failureRead==2?"scan-prewrite":"scan-readback"),"global failure receipt preserves exact prewrite/readback phase");
        Policy off(m.snapshotPorts(true));expect(off.restore()["ok"]==true&&m.current["pending-a"].empty()&&m.current["pending-b"].empty(),"fresh independent OFF settles uncertain intent without touching unvisited item");
    }
    {
        Memory m;for(unsigned i=0;i<3;++i)m.add(item("owned"+std::to_string(i),"software",software["expected"]),Json::array());
        Policy on(m.snapshotPorts(true));expect(on.scan()["ok"]==true,"restore circuit scenario first applies normally");
        const auto writes=m.writes,loads=m.softwareLoads;m.failReadAt=m.reads+1;m.readStep=2500;
        Policy off(m.snapshotPorts(true));const auto r=off.restore();
        expect(r["ok"]==false&&r["status"]["phase"]=="restore-pending"&&r["status"]["restorePending"]==3,"global OFF read failure never relinquishes owned records");
        expect(m.writes==writes&&m.softwareLoads-loads==1&&m.disk["scanErrors"].size()==1,"OFF global failure is one fresh query and one aggregate, without repeated commands");
        m.current["owned1"]=Json::array({"SYSTEM"});m.readStep=0;Policy retry(m.snapshotPorts(true));
        expect(retry.restore()["ok"]==true&&m.current["owned1"]==Json::array({"SYSTEM"})&&m.current["owned0"].empty()&&m.current["owned2"].empty(),"next OFF retries freshly and preserves independent external edit");
    }
    {
        Memory m;const auto a=item("global","software",software["expected"]);m.add(a,Json::array());m.add(ac,2u);m.add(device,true);m.failReadAt=1;m.readStep=2500;
        Policy p(m.snapshotPorts(true));const auto r=p.scan();
        expect(r["ok"]==false&&m.current["global"].empty()&&m.current[ac["key"]]==0&&m.current[device["key"]]==false,"software global failure leaves independent timer/device subset eligible");
    }
    {
        Memory m;m.add(software,Json::array());m.failReadAt=1;m.readStep=2500;bool canceled=false;
        auto ports=m.snapshotPorts(true);const auto observe=ports.observe;ports.canceled=[&](){return canceled;};
        ports.observe=[&](const Json& a,Json& b,std::string& e){const bool ok=observe(a,b,e);canceled=true;return ok;};
        Policy p(ports);const auto r=p.scan();
        expect(r["ok"]==false&&m.writes==0&&m.softwareLoads==1&&m.disk["scanInProgress"]==true,"cancel after slow global failure stops further access and keeps incomplete marker");
    }
    {
        Memory m;m.add(software,Json::array());m.failReadAt=1;m.readStep=180000;Policy p(m.snapshotPorts(true));const auto r=p.scan();
        expect(r["ok"]==false&&m.writes==0&&m.softwareLoads==1&&m.tick==180100,"slow global failure never extends original operation budget or writes after expiry");
    }
    return checks;
}
inline unsigned circuitEvidenceTests() {
    unsigned checks=0;
    const auto expect=[&](bool ok,const char* name){++checks;if(!ok)throw std::runtime_error(std::string("STATIC_WAKE_CHECK_FAILED: ")+name);};
    // Same compiled consumer: evidence counts loader invocations, including
    // caller validation, never powercfg child processes or verified coverage.
    {
        ymcc::staticwake::SoftwareSnapshotCircuit circuit;
        unsigned loads=0,blockedLoads=0;std::string error="prior-caller-error";
        const Json empty={{"loadAttempts",0},{"stoppedOnGlobalFailure",false},
            {"attemptScope","loader-invocations-including-caller-validation;not-powercfg-process-count"}};
        const auto initial=circuit.evidence();
        expect(initial==empty&&loads==0&&error=="prior-caller-error","initial circuit evidence is read-only zero with no fabricated first failure");
        const auto success=[&](std::string& why){++loads;why="loader-success-note";return true;};
        expect(circuit.query(success,error)&&loads==1&&error.empty(),"first successful snapshot loader runs and clears old caller error");
        expect(circuit.query(success,error)&&loads==2&&error.empty(),"second successful query invokes loader again instead of caching success");
        auto twoExpected=empty;twoExpected["loadAttempts"]=2;
        const auto twice=circuit.evidence();
        expect(twice==twoExpected,"two successful loader invocations report exactly two attempts without a firstFailure");
        expect(circuit.evidence()==twice&&circuit.evidence()==twice&&loads==2,"repeated successful evidence reads do not load reset or mutate state");
        const auto firstFailure=[&](std::string& why){++loads;why="operation-one-global-failure";return false;};
        expect(!circuit.query(firstFailure,error)&&loads==3&&error=="operation-one-global-failure","first global failure is the third actual loader invocation");
        auto stoppedExpected=empty;stoppedExpected["loadAttempts"]=3;stoppedExpected["stoppedOnGlobalFailure"]=true;
        stoppedExpected["firstFailure"]="operation-one-global-failure";
        const auto stopped=circuit.evidence();
        expect(stopped==stoppedExpected,"stopped evidence retains exact attempt count scope and first global failure");
        error="new-caller-error";
        expect(!circuit.query([&](std::string& why){++blockedLoads;why="must-not-run";return true;},error)&&
            blockedLoads==0&&loads==3&&error=="operation-one-global-failure","query after global stop never executes loader or replaces first failure");
        auto localCopy=stopped;localCopy["firstFailure"]="caller-edited-copy";
        expect(circuit.evidence()==stopped&&circuit.evidence()==stopped&&loads==3&&blockedLoads==0,
            "repeated failure evidence and caller edits to a returned copy do not mutate circuit or increment attempts");
        expect(!twice.contains("ok")&&!twice.contains("success")&&!twice.contains("coverage")&&
            !stopped.contains("ok")&&!stopped.contains("success")&&!stopped.contains("coverage"),
            "loader evidence never claims operation success or verified software coverage");
        circuit.reset();
        expect(circuit.evidence()==empty&&loads==3&&!circuit.evidence().contains("firstFailure"),
            "operation reset restores zero evidence removes first failure and performs no load");
        error="operation-one-global-failure";
        const bool loaded=circuit.query(success,error);auto freshExpected=empty;freshExpected["loadAttempts"]=1;
        expect(loaded&&loads==4&&error.empty()&&circuit.evidence()==freshExpected,
            "new operation loads afresh and neither caches nor leaks previous operation error");
        const auto nextFailure=[&](std::string& why){++loads;why="operation-two-global-failure";return false;};
        expect(!circuit.query(nextFailure,error)&&loads==5&&error=="operation-two-global-failure",
            "next operation records its own first failure after its successful load");
        freshExpected["loadAttempts"]=2;freshExpected["stoppedOnGlobalFailure"]=true;freshExpected["firstFailure"]="operation-two-global-failure";
        expect(circuit.evidence()==freshExpected&&stopped["firstFailure"]=="operation-one-global-failure"&&initial==empty,
            "new operation evidence is isolated while earlier evidence snapshots retain original identity");
    }
    return checks;
}
// Same production run body and Policy, with fake uptime and machine ownership.
// The process-local mutex is uncontended; no recursive-lock scenario is used.
namespace native_run_evidence_fixture {
static Memory* operationMemory=nullptr;
static bool allowMachine=true;
static DWORD machineWait=0;
static std::timed_mutex inProcess;
static ULONGLONG runTick(){return operationMemory->tick;}
struct MachineLock {
    bool acquired=false;
    explicit MachineLock(DWORD wait):acquired(allowMachine){machineWait=wait;}
};
struct Native {
    Memory& memory;
    ymcc::staticwake::SoftwareSnapshotCircuit& softwareCircuit;
    std::uint64_t deadline;
    std::function<bool()> canceled;
    Native():memory(*operationMemory),softwareCircuit(memory.softwareCircuit),deadline(memory.tick+180000){}
    ymcc::staticwake::Ports ports(){auto p=memory.snapshotPorts(true);p.canceled=canceled;return p;}
};
#define GetTickCount64 runTick
#include "static_wake_run_binding.inc"
#undef GetTickCount64
inline unsigned tests() {
    unsigned checks=0;
    const auto expect=[&](bool ok,const char* name){++checks;if(!ok)throw std::runtime_error(std::string("STATIC_WAKE_CHECK_FAILED: ")+name);};
    const auto software=item("native-evidence-a","software",Json::array({"AWAYMODE","DISPLAY","SYSTEM"}));
    const auto second=item("native-evidence-b","software",software["expected"]);
    {
        Memory m;m.add(software,software["expected"]);m.add(second,second["expected"]);m.readStep=4;
        operationMemory=&m;allowMachine=true;const auto result=run(false,[](){return false;});const auto evidence=result["operationEvidence"];
        expect(result["ok"]==true&&evidence["action"]=="scan"&&evidence["elapsedMs"]==4&&
            evidence["elapsedScope"]=="operation-uptime;may-include-suspension;not-CPU-time"&&
            evidence["diagnosticOnly"]==true&&evidence["mandatoryEvidence"]==true&&evidence["detailLogsRequired"]==false,
            "exact native scan returns required uptime-scoped ordinary operation evidence");
        expect(evidence["softwareSnapshot"]["loadAttempts"]==m.softwareLoads&&m.softwareLoads==1&&m.reads==1&&m.observations==2&&m.writes==0,
            "exact native scan reports actual shared snapshot loads rather than candidate or process counts");
        const auto snapshot=m.softwareCircuit.evidence();
        expect(snapshot==evidence["softwareSnapshot"]&&m.softwareCircuit.evidence()==snapshot&&m.reads==1&&m.softwareLoads==1&&m.writes==0,
            "reading completed native receipt and circuit evidence performs no extra loader or write");
    }
    {
        Memory m;m.add(software,Json::array());m.readStep=7;operationMemory=&m;allowMachine=true;
        const auto result=run(false,[&](){return m.softwareLoads!=0;});const auto evidence=result["operationEvidence"];
        expect(result["ok"]==false&&evidence["action"]=="scan"&&evidence["elapsedMs"]==7&&
            evidence["softwareSnapshot"]["loadAttempts"]==1&&m.softwareLoads==1&&m.reads==1&&m.writes==0&&m.disk["scanInProgress"]==true,
            "exact native cancellation after first load retains one attempt and performs no extra query or write");
        expect(evidence["softwareSnapshot"]==m.softwareCircuit.evidence()&&evidence["softwareSnapshot"]["stoppedOnGlobalFailure"]==false&&
            !evidence["softwareSnapshot"].contains("firstFailure")&&m.softwareLoads==1&&m.reads==1,
            "canceled native receipt preserves loader counters without inventing a global query failure");
    }
    {
        Memory m;m.add(software,Json::array());m.readStep=180000;operationMemory=&m;allowMachine=true;
        const auto result=run(false,[](){return false;});const auto evidence=result["operationEvidence"];
        expect(result["ok"]==false&&evidence["elapsedMs"]==180000&&m.tick==180100&&
            evidence["softwareSnapshot"]["loadAttempts"]==1&&m.softwareLoads==1&&m.reads==1&&m.writes==0,
            "exact native180s exhaustion records original uptime and one load without renewing budget");
        expect(result["status"]["lastError"].get<std::string>().find("deadline")!=std::string::npos&&m.disk["scanInProgress"]==true&&
            evidence["softwareSnapshot"]==m.softwareCircuit.evidence()&&m.tick==180100&&m.reads==1,
            "deadline failure and incomplete ownership survive read-only native terminal evidence");
    }
    {
        Memory m;m.add(software,Json::array());operationMemory=&m;allowMachine=false;
        const auto result=run(false,[](){return false;});const auto evidence=result["operationEvidence"];
        expect(result["ok"]==false&&result["status"]["lastError"]=="machine-journal-owner-lock-unavailable"&&
            evidence["action"]=="scan"&&evidence["softwareSnapshot"]["loadAttempts"]==0&&m.softwareLoads==0&&m.reads==0&&m.writes==0&&m.saves==0,
            "machine lock refusal retains original busy error and reports zero loader invocations");
        expect(evidence["elapsedMs"]==0&&evidence["mandatoryEvidence"]==true&&evidence["detailLogsRequired"]==false&&
            !evidence["softwareSnapshot"].contains("firstFailure")&&machineWait==1,
            "machine lock early return carries required ordinary evidence without query or invented failure");
    }
    {
        Memory m;m.add(software,Json::array());m.add(second,Json::array());operationMemory=&m;allowMachine=true;
        const auto applied=run(false,[](){return false;});
        expect(applied["ok"]==true&&applied["operationEvidence"]["softwareSnapshot"]["loadAttempts"]==m.softwareLoads&&m.softwareLoads>0,
            "exact native restore scenario begins with verified owned software changes");
        const auto priorLoads=m.softwareLoads,priorWrites=m.writes;const auto priorReceipt=applied["operationEvidence"];
        std::string previousError;m.softwareCircuit.query([](std::string& why){why="previous-operation-error";return false;},previousError);
        m.current[second["key"]]=Json::array({"SYSTEM"});
        const auto restored=run(true,[](){return false;});const auto evidence=restored["operationEvidence"];
        expect(restored["ok"]==true&&evidence["action"]=="restore"&&evidence["softwareSnapshot"]["loadAttempts"]==m.softwareLoads-priorLoads&&
            m.softwareLoads>priorLoads&&evidence["softwareSnapshot"]["stoppedOnGlobalFailure"]==false&&
            !evidence["softwareSnapshot"].contains("firstFailure")&&machineWait==5000&&priorReceipt==applied["operationEvidence"],
            "exact native restore resets per-operation load evidence and cannot leak previous operation error");
        expect(m.writes==priorWrites+1&&m.current[software["key"]].empty()&&m.current[second["key"]]==Json::array({"SYSTEM"})&&
            m.disk["enabled"]==false&&restored["status"]["restorePending"]==0&&restored["status"]["externalChanges"].size()==1,
            "native restore evidence preserves baseline recovery and independent external-edit rule");
    }
    operationMemory=nullptr;allowMachine=true;return checks;
}
} // namespace native_run_evidence_fixture
} // namespace static_wake_fixture
static unsigned staticWakePolicyTests() {
    // Keep original negative controls first with explicit execution order.
    auto checks=static_wake_fixture::run();
    checks+=static_wake_fixture::circuitEvidenceTests();
    checks+=static_wake_fixture::native_run_evidence_fixture::tests();
    return checks;
}

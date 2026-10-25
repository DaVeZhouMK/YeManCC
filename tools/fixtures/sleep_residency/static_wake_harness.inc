// Pure policy consumer for the EXISTING sleep.residency-evidence-consumer.
// In-memory ports only: no native runtime, process launch, registry or device API.
#include "sleep_static_wake_policy.h"
#include <map>
#include <stdexcept>
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
    void add(const Json& a,const Json& baseline){candidates.push_back(a);current[a["key"].get<std::string>()]=baseline;}
};
inline unsigned run() {
    unsigned checks=0;
    const auto expect=[&](bool ok,const char* name){++checks;if(!ok)throw std::runtime_error(std::string("STATIC_WAKE_CHECK_FAILED: ")+name);};
    const auto software=item("PROCESS\nexample.exe","software",Json::array({"AWAYMODE","DISPLAY","SYSTEM"}));
    const auto device=item("device\nusb-id","devices",false);
    const auto ac=item("timer\nscheme\nAC","timers",0u),dc=item("timer\nscheme\nDC","timers",0u);
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
        const auto count=m.writes;expect(p.scan()["ok"]==true&&m.writes==count,"rescan idempotent no repeated writes");
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
    return checks;
}
} // namespace static_wake_fixture
static unsigned staticWakePolicyTests() {return static_wake_fixture::run();}

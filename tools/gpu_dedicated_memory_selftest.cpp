#include "../native/gpu_dedicated_memory_contract.h"
#include <functional>
#include <iostream>
#include <stdexcept>
using namespace ymcc::dedicatedmemory;
static int cases = 0;
static void check(bool ok,const char* text) { if (!ok) throw std::runtime_error(text); ++cases; }
static Probe valid() {
    Probe p; p.hardware=true; p.integrated=true; p.identityConfirmed=true;
    p.provider="intel-gmm";p.pnpId="PCI\\intel-igpu";p.driverVersion="32.0.test";p.deviceClass="G3E";
    p.bootId="windows-loader-v1|{00000001-0000-0000-0000-000000000000}|1";p.adapterName="Intel Arc";p.luid="luid-a";p.registryKey="validated-key";
    p.capacityRead=true;p.dedicatedBytes=4096*mib;p.installedBytes=32768*mib;p.configuredRaw=4096;p.options=intelOptions(p.installedBytes);
    return p;
}
struct Fake final : Ops {
    Probe current=valid(); JournalRead saved{}; int probes=0,saves=0,writes=0;
    int failSave=0; bool acknowledged=true,changeConfig=true;
    std::function<void(Probe&)> duringWrite;
    Probe probe() override { ++probes;return current; }
    JournalRead journal() override { return saved; }
    bool save(const Journal& j) override { ++saves;if(saves==failSave)return false;saved.value=j;return true; }
    bool write(const Probe&,const Option& o) override {
        ++writes;if(changeConfig){if(o.removeOverride)current.configuredRaw.reset();else current.configuredRaw=o.raw;}
        if(duringWrite)duringWrite(current);return acknowledged;
    }
};
int main() {
 try {
    const auto base=valid();check(admitted(base),"valid reader admitted");
    {Fake f;f.current.dedicatedBytes=0;check(read(f).ok,"confirmed Intel zero is a real read");
     auto r=apply(f,fence(f.current),"mib:8192",true);
     check(r.ok&&r.snapshot.dedicatedBytes==0&&verification(r.snapshot,r.journal)=="pending_restart","zero read permits configuration but never claims capacity changed");
     f.current.bootId="windows-loader-v1|{00000001-0000-0000-0000-000000000000}|2";check(verification(f.current,r.journal)=="not_effective","zero after reboot does not verify numeric setting");
     f.current.capacityRead=false;check(!read(f).ok,"unread zero remains hidden");}
    {Fake f;auto r=apply(f,fence(f.current),"mib:8192",true);auto legacy=r.journal;legacy->bootId="{00000001-0000-0000-0000-000000000000}";
     f.current.bootId="windows-loader-v1|{00000001-0000-0000-0000-000000000000}|2";f.current.dedicatedBytes=8192*mib;
     check(verification(f.current,legacy)=="unconfirmed"&&bootComparison(f.current,*legacy)=="uncomparable","legacy environment GUID cannot prove restart even with matching current capacity");
     auto another=r.journal;another->bootId="windows-loader-v1|{00000002-0000-0000-0000-000000000000}|1";
     check(verification(f.current,another)=="unconfirmed","different boot environment cannot compare loader counters");}
    for(const auto& bad:std::vector<std::string>{"", "{00000001-0000-0000-0000-000000000000}","windows-loader-v1|{bad}|1",
        "windows-loader-v1|{00000001-0000-0000-0000-000000000000}|-1","windows-loader-v1|{00000001-0000-0000-0000-000000000000}|1junk",
        "windows-loader-v1|{00000001-0000-0000-0000-000000000000}|4294967296"})
     check(!loaderBootIdentity(bad),"malformed or legacy loader identities cannot establish restart");
    check(loaderBootIdentity("windows-loader-v1|{00000001-0000-0000-0000-000000000000}|0")->sequence==0,"genuinely read zero boot counter is valid");
    {Fake f;f.current.installedBytes=0;check(!read(f).ok,"zero RAM is not a valid capacity bound");}
    for(auto mutate : std::vector<std::function<void(Probe&)>>{
        [](Probe&p){p.capacityRead=false;},[](Probe&p){p.hardware=false;},[](Probe&p){p.integrated=false;},
        [](Probe&p){p.identityConfirmed=false;},[](Probe&p){p.bootId.clear();},[](Probe&p){p.luid.clear();},
        [](Probe&p){p.driverVersion.clear();},[](Probe&p){p.registryKey.clear();},[](Probe&p){p.provider="amd-unsupported";},
        [](Probe&p){p.options.clear();},[](Probe&p){p.configuredRaw=0;},[](Probe&p){p.dedicatedBytes=p.installedBytes+1;}}) {
        Fake f;mutate(f.current);auto r=apply(f,fence(f.current),"mib:8192",true);
        check(!r.ok&&f.writes==0&&f.saves==0,"invalid data cannot write");
    }
    {Fake f;auto r=apply(f,fence(f.current),"mib:8192",false);check(!r.ok&&f.writes==0,"confirmation required");}
    {Fake f;auto r=apply(f,fence(f.current),"mib:65536",true);check(!r.ok&&f.writes==0,"unoffered value rejected");}
    for(auto mutate : std::vector<std::function<void(Probe&)>>{
        [](Probe&p){p.pnpId="other";},[](Probe&p){p.luid="luid-b";},[](Probe&p){p.bootId="windows-loader-v1|{00000001-0000-0000-0000-000000000000}|2";},
        [](Probe&p){p.registryKey="other-key";},[](Probe&p){p.driverVersion="new-driver";},
        [](Probe&p){p.dedicatedBytes=2048*mib;},[](Probe&p){p.configuredRaw=2048;},
        [](Probe&p){p.deviceClass="changed-class";},[](Probe&p){p.installedBytes+=mib;},
        [](Probe&p){p.options.pop_back();}}) {
        Fake f;const auto token=fence(f.current);mutate(f.current);
        auto r=apply(f,token,"mib:8192",true);check(!r.ok&&f.writes==0,"stale identity/data rejected");
    }
    {Fake f;f.failSave=1;auto r=apply(f,fence(f.current),"mib:8192",true);check(!r.ok&&f.writes==0,"backup failure prevents write");}
    {Fake f;f.saved.ok=false;auto r=read(f);check(!r.ok,"unreadable journal hides controls");auto a=apply(f,fence(f.current),"mib:8192",true);check(!a.ok&&f.writes==0,"journal failure prevents write");}
    {Fake f;auto r=apply(f,fence(f.current),"mib:4096",true);check(r.ok&&f.writes==0&&f.saves==0,"same configured value has no write");}
    {Fake f;f.current.configuredRaw=8192;f.current.dedicatedBytes=128*mib;
     auto initial=read(f);
     check(initial.ok&&verification(initial.snapshot,initial.journal)=="unconfirmed","existing 8 GiB configuration and actual 128 MiB without history is unconfirmed");
     auto unconfirmed=apply(f,fence(f.current),"mib:8192",false);
     check(!unconfirmed.ok&&f.writes==0&&f.saves==0,"same-target reapply still requires explicit confirmation");
     auto r=apply(f,fence(f.current),"mib:8192",true);
     check(r.ok&&f.writes==1&&f.saves==2,"existing numeric mismatch can be explicitly reapplied through backup and write");
     check(r.snapshot.dedicatedBytes==128*mib&&r.journal->originalRaw==8192u&&r.journal->beforeBytes==128*mib,"reapply preserves original configuration and actual capacity");
     check(verification(r.snapshot,r.journal)=="pending_restart","reapply same boot requires restart verification");
     auto repeated=apply(f,fence(f.current),"mib:8192",true);
     check(repeated.ok&&f.writes==1&&f.saves==2,"same-boot pending target cannot start repeated writes");
     f.current.bootId="windows-loader-v1|{00000001-0000-0000-0000-000000000000}|2";auto ignored=read(f);
     check(verification(ignored.snapshot,ignored.journal)=="not_effective","unchanged 128 MiB after restart never confirms existing 8 GiB config");
     const auto blocked=apply(f,fence(f.current),"mib:8192",true);
     check(!blocked.ok&&f.writes==1&&f.saves==2,"confirmed ineffective target cannot be repeatedly reapplied");
     f.current.dedicatedBytes=8192*mib;auto effective=read(f);
     check(verification(effective.snapshot,effective.journal)=="verified","exact 8 GiB on new boot confirms reapplication");
     auto noOp=apply(f,fence(f.current),"mib:8192",true);
     check(noOp.ok&&f.writes==1&&f.saves==2,"verified identical target remains a no-op");}
    {Fake f;f.current.configuredRaw.reset();f.current.dedicatedBytes=128*mib;auto r=apply(f,fence(f.current),"auto",true);
     check(r.ok&&f.writes==0&&f.saves==0&&verification(r.snapshot,r.journal)=="none","Auto has no numeric promise and remains an idempotent no-op");}
    {Fake f;f.current.configuredRaw=8192;f.current.dedicatedBytes=128*mib;Journal j;j.provider="other-provider";j.pnpId=f.current.pnpId;f.saved.value=j;
     auto r=read(f);check(verification(r.snapshot,r.journal)=="unconfirmed","unrelated history cannot conceal current Intel capacity discrepancy");}
    {Fake f;f.current.configuredRaw=8192;f.current.dedicatedBytes=128*mib;f.failSave=1;auto r=apply(f,fence(f.current),"mib:8192",true);
     check(!r.ok&&f.writes==0,"same-target mismatch never bypasses backup failure");}
    {Fake f;auto r=apply(f,fence(f.current),"mib:8192",true);
     check(r.ok&&f.writes==1&&f.saves==2,"write requires backup and independent read");
     check(r.snapshot.dedicatedBytes==4096*mib,"request never overwrites effective capacity");
     check(r.journal->originalRaw==4096u&&r.journal->beforeBytes==4096*mib,"original config backed up");
     check(verification(r.snapshot,r.journal)=="pending_restart","same boot stays pending");
     f.current.bootId="windows-loader-v1|{00000001-0000-0000-0000-000000000000}|2";auto unchanged=read(f);check(verification(unchanged.snapshot,unchanged.journal)=="not_effective","ignored driver setting not success");
     f.current.dedicatedBytes=8192*mib;auto live=read(f);check(verification(live.snapshot,live.journal)=="verified","new boot and exact live capacity verified");
     f.current.dedicatedBytes-=mib;auto partial=read(f);check(verification(partial.snapshot,partial.journal)=="not_effective","no invented tolerance");
     f.current.driverVersion="updated";auto newer=read(f);check(verification(newer.snapshot,newer.journal)=="driver_changed","driver upgrade invalidates proof");}
    {Fake f;f.acknowledged=false;auto r=apply(f,fence(f.current),"mib:8192",true);check(!r.ok&&verification(r.snapshot,r.journal)=="unconfirmed","failed ack cannot become success");}
    {Fake f;f.changeConfig=false;auto r=apply(f,fence(f.current),"mib:8192",true);check(!r.ok,"write ack alone is insufficient");}
    {Fake f;f.failSave=2;auto r=apply(f,fence(f.current),"mib:8192",true);check(!r.ok&&verification(r.snapshot,r.journal)=="unconfirmed","final record failure remains unconfirmed");}
    {Fake f;f.duringWrite=[](Probe&p){p.luid="swapped";};auto r=apply(f,fence(f.current),"mib:8192",true);check(!r.ok,"adapter change during write invalidates ack");}
    for (auto mutate : std::vector<std::function<void(Probe&)>>{
        [](Probe&p){p.deviceClass="changed-class";},[](Probe&p){p.installedBytes+=mib;},
        [](Probe&p){p.options.pop_back();},[](Probe&p){p.options[1].capacityMiB=1;}}) {
        Fake f;f.duringWrite=mutate;auto r=apply(f,fence(f.current),"mib:8192",true);
        check(!r.ok,"write readback must preserve the full route and offered options");
    }
    {Fake f;auto r=apply(f,fence(f.current),"mib:8192",true);f.current.bootId="windows-loader-v1|{00000001-0000-0000-0000-000000000000}|2";f.current.dedicatedBytes=8192*mib;
     auto j=r.journal;j->target.capacityMiB=4096;
     check(verification(f.current,j)=="unconfirmed","journal cannot invent an offered target capacity");
     j=r.journal;j->target.id="mib:4096";
     check(verification(f.current,j)=="unconfirmed","journal ID and raw value must describe the same option");
     j=r.journal;j->target.removeOverride=true;
     check(verification(f.current,j)=="unconfirmed","journal cannot change set into delete semantics");}
    {auto p=valid();p.options.push_back(p.options[1]);check(!admitted(p),"duplicate options reject admission");
     p=valid();p.options[1].raw=1;check(!admitted(p),"Intel raw value must equal capacity in MiB");}
    {Fake f;auto r=apply(f,fence(f.current),"auto",true);check(r.ok&&!r.snapshot.configuredRaw&&r.snapshot.dedicatedBytes==4096*mib,"Auto clears only override and retains real capacity");}
    {auto a=valid(),b=a;b.luid="luid-b";
     check(uniquePhysicalProbe({b,a})->luid==a.luid,"matching aliases use stable LUID");
     b.pnpId="different-iGPU";check(!uniquePhysicalProbe({a,b}),"different physical adapters rejected");
     b=a;b.dedicatedBytes-=mib;check(!uniquePhysicalProbe({a,b}),"conflicting capacity reports rejected");
     b=a;b.options[1].raw+=1;check(!uniquePhysicalProbe({a,b}),"conflicting alias setting options rejected");}
    {Fake f;auto r=apply(f,fence(f.current),"auto",true);f.current.bootId="windows-loader-v1|{00000001-0000-0000-0000-000000000000}|2";auto after=read(f);
     check(r.ok&&verification(after.snapshot,after.journal)=="default_read","Auto reports live default capacity without claiming a requested GB");}
    check(intelOptions(8192*mib).back().capacityMiB==4096u,"options bounded to half installed RAM");
    check(decodeRogApu(0x10105)==0x105u,"known ROG encoding read");
    check(decodeRogApu(0x10000)==0u,"Auto zero is protocol mode, not missing data");
    check(!decodeRogApu(0)&&!decodeRogApu(0x101ff),"unknown ROG responses have no default fallback");
    {std::vector<std::uint8_t> b(16,0);b[0]=3;b[2]=3;b[8]=4;b[10]=8;
     std::uint32_t current=0;auto options=rogVramOptions(b,32768*mib,current);
     check(options&&options->size()==3&&options->at(1).capacityMiB==2048u,"ROG options and units decoded");
     b[4]=6;check(!rogVramOptions(b,32768*mib,current),"unknown current ROG option rejected");
     b[4]=0;b[0]=32;check(!rogVramOptions(b,32768*mib,current),"truncated option list rejected");
     b[0]=3;b[2]=0;check(!rogVramOptions(b,32768*mib,current),"unsupported ROG status rejected");}
    {Fake f;f.current.provider="rog-apu";f.current.options=rogApuOptions(f.current.installedBytes);f.current.configuredRaw=0x106;f.current.dedicatedBytes=128*mib;
     check(admitted(f.current)&&verification(f.current,std::nullopt)=="unconfirmed","external ROG numeric configuration with different actual capacity is unconfirmed without journal");
     auto r=apply(f,fence(f.current),"mib:8192",true);check(r.ok&&f.writes==0&&f.saves==0&&verification(r.snapshot,r.journal)=="unconfirmed","ROG capacity discrepancy cannot enter Intel same-target reapply path");}
    std::cout<<"GPU_DEDICATED_MEMORY_SELFTEST_OK cases="<<cases<<" hardware=none registry=fake\n";
 } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}

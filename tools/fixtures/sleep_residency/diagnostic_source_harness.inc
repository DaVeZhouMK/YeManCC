#include "sleep_diagnostic_source.h"
static unsigned diagnosticSourceTests() {
    using namespace ymcc::sleepdiagnostic;
    struct Ports {
        Load state=Load::Missing;bool enabled=false,allow=true,readOk=true,saveOk=true,writeOk=true,clearOk=true,confirm=true,externalOnAfterSave=false;
        unsigned reads=0,writes=0,saves=0,clears=0;Journal saved;
        Load load(Journal& j){j=saved;return state;}
        bool read(bool& v){++reads;if(!readOk)return false;if(externalOnAfterSave&&saves)enabled=true;v=enabled;return true;}
        bool allowed(bool){return allow;}
        bool save(Journal j){++saves;if(!saveOk)return false;saved=j;state=Load::Valid;return true;}
        bool clear(){++clears;if(!clearOk)return false;saved={};state=Load::Missing;return true;}
        bool write(bool v){++writes;if(!writeOk)return false;if(confirm)enabled=v;return true;}
    };
    unsigned checks=0;
    const auto require=[&](bool ok,const char* label){++checks;if(!ok)throw std::runtime_error(label);};
    {Ports p;auto r=reconcile(p,true);require(r.ok&&r.ready&&r.owned&&p.saves==1&&p.writes==1,"diagnostic durable baseline before first enable");
        r=reconcile(p,true);require(r.ok&&p.saves==1&&p.writes==1,"diagnostic repeated ON does not rewrite or lose original");
        r=reconcile(p,false);require(r.ok&&!r.ready&&!r.owned&&p.writes==2&&p.clears==1,"diagnostic OFF restores only owned original false");}
    {Ports p;p.enabled=true;auto r=reconcile(p,true);require(r.ok&&!r.owned&&!p.saves&&!p.writes,"diagnostic preexisting ON is not acquired");r=reconcile(p,false);require(r.ok&&p.enabled&&!p.writes,"diagnostic OFF preserves preexisting channel");}
    {Ports p;p.saveOk=false;auto r=reconcile(p,true);require(!r.ok&&!p.writes&&!p.enabled,"diagnostic baseline failure prohibits configuration write");}
    {Ports p;p.readOk=false;auto r=reconcile(p,true);require(!r.ok&&!p.saves&&!p.writes,"diagnostic unavailable read stays UNKNOWN without mutation");}
    {Ports p;p.state=Load::Invalid;auto r=reconcile(p,true);require(!r.ok&&!p.reads&&!p.writes,"diagnostic corrupt journal fail closed");}
    {Ports p;p.writeOk=false;auto r=reconcile(p,true);require(!r.ok&&r.owned&&p.state==Load::Valid,"diagnostic enable failure retains recovery ownership");r=reconcile(p,false);require(r.ok&&!p.saved.owned&&p.clears==1,"diagnostic failed enable OFF clears unapplied intent without write");}
    {Ports p;p.confirm=false;auto r=reconcile(p,true);require(!r.ok&&!r.ready&&r.owned,"diagnostic API success without readback is not source readiness");}
    {Ports p;p.allow=false;auto r=reconcile(p,true);require(!r.ok&&!p.saves&&!p.writes,"diagnostic superseded request does not mutate");}
    {Ports p;auto r=reconcile(p,true);p.enabled=false;r=reconcile(p,false);require(r.ok&&p.writes==1,"diagnostic external OFF is retained without another write");}
    {Ports p;p.externalOnAfterSave=true;auto r=reconcile(p,true);require(r.ok&&r.ready&&!r.owned&&!p.writes&&p.clears==1,"diagnostic intervening external ON is not owned");}
    {Ports p;auto r=reconcile(p,true);p.writeOk=false;r=reconcile(p,false);require(!r.ok&&r.owned&&p.saved.owned,"diagnostic restore denial retains pending journal");p.writeOk=true;r=reconcile(p,false);require(r.ok&&!r.owned,"diagnostic pending restore may be explicitly retried");}
    {Ports p;auto r=reconcile(p,true);p.clearOk=false;r=reconcile(p,false);require(!r.ok&&r.owned&&!p.enabled,"diagnostic failed journal cleanup retains evidence");p.clearOk=true;r=reconcile(p,false);require(r.ok&&!r.owned&&p.writes==2,"diagnostic cleanup retry does not repeat restored write");}
    return checks;
}

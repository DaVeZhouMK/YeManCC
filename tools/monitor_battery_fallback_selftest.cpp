// Deterministic injected telemetry only: no driver discovery or live battery reads.
#include "../native/monitor_battery_windows.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
using namespace ymcc::monitorbattery;
static int checks = 0;
static void check(bool ok, const char* name) { ++checks; if (!ok) { std::fprintf(stderr,"FAIL: %s\n",name); std::exit(1); } }
static Observation observation(std::uint64_t tick, double capacity, bool ac=false, bool charging=false) {
    return {tick,1,capacity,true,true,ac,charging,!ac};
}
static Power window(EnergyCounter& energy, double watts, bool ac=false, bool charging=false) {
    Power result;
    for (int i=0; i<=31; ++i) result=energy.update(observation(1000+i*1000,50000+watts*i/3.6,ac,charging));
    return result;
}
int main() {
    check(eligible(true,true,0)&&eligible(true,true,100),"all percentage-confirmed battery devices incl 0/100%, no OEM gate");
    check(!eligible(false,true,50)&&!eligible(true,false,50)&&!eligible(true,true,-1)&&!eligible(true,true,101),"no demand/desktop/unknown percent => no fallback");
    EnergyCounter energy;
    check(!energy.update(observation(1000,50000)).valid,"first observation is unknown, not fake zero");
    for(int i=1;i<31;++i) check(!energy.update(observation(1000+i*1000,50000-200.0*i/30)).valid,"wait whole change-anchored 30 second window");
    const auto discharge=energy.update(observation(32000,50000-200.0*31/30));
    check(discharge.valid&&discharge.estimated&&std::abs(discharge.watts+24)<.001&&discharge.windowMs==30000,"30 s mWh delta => -24 W estimated");
    check(energy.update(observation(33000,49780)).watts==discharge.watts,"one-second publisher keeps last window estimate");
    check(!energy.update(observation(34000,49780,true,true)).valid,"AC/DC transition invalidates old estimate");
    energy.reset(); auto charge=window(energy,12,true,true);
    check(charge.valid&&charge.estimated&&std::abs(charge.watts-12)<.001,"charging uses positive signed average");
    energy.reset(); auto idle=window(energy,0,true,false);
    check(idle.valid&&idle.estimated&&idle.watts==0,"valid zero capacity delta retained");
    check(!energy.update(observation(32000,50000,true,true)).valid,"charging-state transition resets same-AC window");
    energy.reset(); window(energy,-24);
    check(!energy.update(observation(120000,49000)).valid,"sleep/long collection gap resets baseline");
    energy.reset(); window(energy,-24); auto changed=observation(32000,49800); changed.identity=2;
    check(!energy.update(changed).valid,"battery identity change resets baseline");
    changed.valid=false;check(!energy.update(changed).valid,"invalid capacity sample clears estimate");
    changed=observation(32000,49800);changed.acKnown=false;check(!energy.update(changed).valid,"unknown AC/DC cannot produce estimate");
    energy.reset(); auto spike=window(energy,-300);check(!spike.valid,"impossible power rejected");
    check(measuredRate(-9,"hwinfo",true,false,false,true).valid,"healthy HWiNFO discharge wins");
    check(!measuredRate(0,"hwinfo",true,false,false,true).valid,"active DC firmware zero invokes fallback");
    check(measuredRate(0,"hwinfo",true,true,false,false).valid,"confirmed AC idle zero remains real data");
    check(!measuredRate(0,"hwinfo",true,true,true,false).valid,"active charging zero is missing rate");
    check(!measuredRate(10,"hwinfo",true,false,false,true).valid,"wrong-sign rate rejected");
    check(!measuredRate(std::numeric_limits<double>::quiet_NaN(),"hwinfo",true,false,false,true).valid,"NaN is unavailable");
    BatteryReading readings[2]={{true,20000,-8,false,true},{true,10000,-4,false,true}};
    auto combined=combine(readings,2,true,false);
    check(combined.valid&&combined.capacityValid&&combined.capacityMwh==30000&&combined.power.valid&&combined.power.watts==-12,"complete dual-battery rate and energy totals");
    readings[1].watts=std::numeric_limits<double>::quiet_NaN();combined=combine(readings,2,true,false);
    check(combined.valid&&combined.capacityValid&&!combined.power.valid,"one missing rate => capacity fallback, never partial measured power");
    readings[1].capacityMwh=-1;combined=combine(readings,2,true,false);
    check(!combined.capacityValid&&!combined.power.valid,"incomplete capacity cannot be estimated");
    readings[1].absoluteCapacity=false;check(!combine(readings,2,true,false).valid,"relative battery units not passed off as mWh/W");
    check(!combine(nullptr,0,true,false).valid,"no battery yields unknown");

    // A coarse 80 Wh / 1% battery reporting every 96 s at 30 W.
    energy.reset(); Power coarse;
    for (int sec=0;sec<=212;++sec) {
        const int steps=sec<20?0:1+(sec-20)/96;
        coarse=energy.update(observation(1000+sec*1000,60000-steps*800));
        if(sec<116) check(!coarse.valid,"one coarse capacity drop is not a 96 W estimate or a false DC zero");
        if(coarse.valid) check(std::abs(coarse.watts+30)<.001,"complete coarse intervals produce -30 W, never -96/-110 W");
    }
    check(coarse.valid&&coarse.windowMs==96000,"coarse integration may exceed 60 seconds without resetting every minute");
    for(int sec=213;sec<=405;++sec)coarse=energy.update(observation(1000+sec*1000,57600));
    check(!coarse.valid,"flat capacity eventually invalidates an old discharge estimate");
    energy.reset();for(int sec=0;sec<=60;++sec)coarse=energy.update(observation(1000+sec*1000,60000));
    check(!coarse.valid,"DC flat capacity is not real 0 W");
    energy.reset();window(energy,-24);check(!energy.update(observation(33000,51000)).valid,"capacity recalibration cannot become wrong-sign power");

    PackagePowerModel model;
    auto modelObs=[](double watts,std::uint64_t tick=1000){return ModelObservation{tick,true,true,false,false,true,watts};};
    for(const auto& point : {std::pair<double,double>{5,7.5},{10,15},{20,34},{30,57},{40,80},{60,132}}) {
        model.reset();const auto p=model.update(modelObs(point.first));
        check(p.valid&&p.estimated&&std::abs(p.watts+point.second)<.001,"user heuristic knots produce explicitly approximate signed DC watts");
        check(std::string(p.source)=="package-power-model"&&p.windowMs==0,"model is never mislabeled capacity-derived or measured");
    }
    for(const auto watts : {0.,-1.,66.,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()})
        check(!model.update(modelObs(watts)).valid,"invalid/stale/out-of-handheld-range package data cannot fabricate watts");
    for(int flag=0;flag<5;++flag){auto o=modelObs(30);if(flag==0)o.eligible=false;if(flag==1)o.acKnown=false;if(flag==2)o.ac=true;if(flag==3)o.charging=true;if(flag==4)o.packageFresh=false;
        check(!model.update(o).valid,"no battery/demand or AC/charging/unknown/stale input => no DC model");}
    model.reset();model.update(modelObs(30));auto smooth=model.update(modelObs(40,2000));
    check(smooth.watts<-57&&smooth.watts>-80,"low-frequency smoothing uses live package watts, not TDP setting");
    check(model.update(modelObs(60,2000)).watts==smooth.watts,"duplicate timestamp cannot advance smoothing");
    check(std::abs(model.update(modelObs(40,20000)).watts+80)<.001,"long gap reinitializes model, not stale smoothing");
    model.reset();const auto modeled=model.update(modelObs(30));
    const Power suspect{true,true,-110,30000,"energy-delta"};
    check(selectFallback(suspect,modeled).watts==modeled.watts,"suspect derived spike rejected in favor of labeled model, never clipped");
    check(selectFallback(measured(-110,"windows-rate"),modeled).watts==-110,"real measured discharge never clamped to a package/TDP ratio");
    check(selectFallback(Power{true,true,110,30000,"energy-delta"},modeled).watts==110,"charging estimate not constrained by DC package ratio");
    check(!selectFallback({},{}).valid,"unavailable energy and CPU power remain unavailable");

    const auto realEnergy=remainingEnergy(50000,80000,75),percentEnergy=remainingEnergy(-1,80000,75);
    check(realEnergy.valid&&!realEnergy.estimated&&realEnergy.remainingMwh==50000,"direct remaining mWh takes precedence over percentage");
    check(percentEnergy.valid&&percentEnergy.estimated&&percentEnergy.remainingMwh==60000,"percentage uses reported full charge capacity, no invented battery size");
    const auto zeroMissing = remainingEnergy(0,80000,75);
    check(zeroMissing.valid && zeroMissing.estimated && zeroMissing.remainingMwh==60000,"zero capacity with positive SOC uses only reported full capacity, not fake empty battery");
    check(!remainingEnergy(0,-1,75).valid,"zero missing counter without full capacity has unknown energy, never fake 0 min");
    check(remainingTime(modeled,zeroMissing,true,true,false,false).minutes>60,"zero missing counter no longer breaks model runtime into 0 min");
    check(remainingTime(measured(-24,"windows-rate"),zeroMissing,true,true,false,false).minutes==150,"zero missing counter also supports true rate runtime");
    check(!remainingEnergy(-1,-1,75).valid,"percentage alone cannot imply Wh or runtime");
    check(!remainingEnergy(-1,80000,-1).valid,"unknown percentage cannot become full capacity");
    auto time=remainingTime(measured(-24,"windows-rate"),realEnergy,true,true,false,false);
    check(time.valid&&time.estimated&&std::abs(time.minutes-125)<.001,"real -24 W and 50 Wh => approximate 125 min");
    time=remainingTime(modeled,realEnergy,true,true,false,false);
    check(time.valid&&time.estimated&&std::abs(time.minutes-3000./57)<.001,"model -57 W and 50 Wh runtime uses same selected watts");
    check(!remainingTime(modeled,{},true,true,false,false,200).valid,"missing capacity cannot reuse unrelated sensor time for model");
    time=remainingTime(measured(-24,"hwinfo"),{},true,true,false,false,120);
    check(time.valid&&time.estimated&&time.minutes==120,"fresh sensor runtime retained only for real discharge");
    check(!remainingTime({},realEnergy,true,true,false,false).valid,"missing power cannot create runtime");
    check(!remainingTime(measured(0,"windows-rate"),realEnergy,true,true,false,false).valid,"zero power cannot divide into infinite runtime");
    check(!remainingTime(modeled,realEnergy,false,true,false,false).valid,"no battery cannot create runtime");
    check(!remainingTime(modeled,realEnergy,true,false,false,false).valid,"unknown AC/DC cannot create runtime");
    check(!remainingTime(modeled,realEnergy,true,true,true,false).valid,"AC has no battery-use runtime");
    check(!remainingTime(modeled,realEnergy,true,true,false,true).valid,"charging has no remaining discharge runtime");
    time=remainingTime(modeled,remainingEnergy(0,80000,0),true,true,false,false);
    check(time.valid&&time.minutes==0,"confirmed empty battery yields valid zero remaining time");
    std::printf("monitor battery fallback: %d assertions PASS (synthetic telemetry; no device I/O)\n",checks);
    return 0;
}

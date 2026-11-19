#define NOMINMAX
#include "../native/background_controller_ui.h"
#include "../deps/json/json.hpp"
#include <fstream>
#include <iostream>
#include <vector>
#include <stdexcept>
using json=nlohmann::json;
using namespace ymcc::backgroundui;
static unsigned checks=0;
static void check(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}
static std::vector<Datum> neutral(){return {{0,32767},{1,32768},{2,32767},{3,32768},{4,32768},{15,0}};}
static json read(const char* path){std::ifstream s(path);json d;s>>d;return d;}
int main(int argc,char** argv)try {
    if(argc!=2)return 2;
    const auto input=read(argv[1]);const auto& source=input.at("descriptor");
    Descriptor d;d.vendor=source.at("vendorId");d.product=source.at("productId");d.page=source.at("usagePage");
    d.usage=source.at("usage");d.reportBytes=source.at("inputReportBytes");d.dataIndices=source.at("numberInputDataIndices");
    d.buttonCaps=static_cast<unsigned>(source.at("buttons").size());const auto& bc=source.at("buttons")[0];
    d.buttonFirst=bc.at("usageMin");d.buttonLast=bc.at("usageMax");d.buttonIndex=bc.at("dataMin");
    d.buttonEndIndex=bc.at("dataMax");d.buttonReport=bc.at("reportId");d.buttonLink=bc.at("linkCollection");
    d.valueCount=static_cast<unsigned>(source.at("values").size());
    for(unsigned i=0;i<d.valueCount && i<6;++i){const auto& c=source.at("values")[i];
        d.values[i]={c.at("dataMin"),c.at("usageMin"),c.at("linkCollection"),c.at("bitSize"),c.at("reportId"),c.at("logicalMin"),c.at("logicalMax")};}
    check(qualified(d),"field descriptor rejected");
    auto bad=d;bad.product++;check(!qualified(bad),"different product");bad=d;bad.values[4].usage=0x35;check(!qualified(bad),"old ROG axis mapping");
    bad=d;bad.values[0].report=1;check(!qualified(bad),"other report ID");bad=d;bad.values[0].maximum=32767;check(!qualified(bad),"signed layout");
    bad=d;bad.buttonEndIndex=15;check(!qualified(bad),"different button data indices");
    bad=d;bad.values[5].minimum=0;check(!qualified(bad),"different hat origin");
    XINPUT_GAMEPAD pad{};auto data=neutral();check(decode(data,pad)&&!active(pad),"neutral");
    for(unsigned h=0;h<=8;++h){data=neutral();data.back().value=h;check(decode(data,pad),"hat domain");
        if(h==8)check(pad.wButtons==(XINPUT_GAMEPAD_DPAD_UP|XINPUT_GAMEPAD_DPAD_LEFT),"northwest was dropped");}
    data.back().value=9;check(!decode(data,pad),"bad hat");data=neutral();data.pop_back();check(!decode(data,pad),"missing hat");
    data=neutral();data.push_back(data[0]);check(!decode(data,pad),"duplicate index");data=neutral();data[1].value=65536;check(!decode(data,pad),"out of range axis");
    data=neutral();data[0].value=0;data[1].value=65535;data[2].value=65535;data[3].value=0;
    check(decode(data,pad)&&pad.sThumbLX==32767&&pad.sThumbLY==32767&&pad.sThumbRX==-32768&&pad.sThumbRY==-32767,"axis range and polarity");
    const Epoch epoch{1,2,3,4,0};Lane lane;XINPUT_GAMEPAD native{},ui{};XINPUT_GAMEPAD a{};a.wButtons=XINPUT_GAMEPAD_A;
    lane.observe(epoch,a);check(lane.apply(epoch,true,native,ui)&&ui.wButtons==XINPUT_GAMEPAD_A,"first fresh post-registration input must work");
    lane.observe(epoch,{});lane.observe(epoch,a);check(lane.apply(epoch,true,native,ui)&&ui.wButtons==XINPUT_GAMEPAD_A,"native-neutral raw takeover");
    for(unsigned tick=0;tick<40;++tick)check(lane.apply(epoch,true,native,ui)&&ui.wButtons==XINPUT_GAMEPAD_A,"event driven hold must not expire before 500ms summon");
    native.wButtons=XINPUT_GAMEPAD_B;check(lane.apply(epoch,true,native,ui)&&ui.wButtons==XINPUT_GAMEPAD_A,"no second source while raw owns UI");
    native.bLeftTrigger=11;native.bRightTrigger=22;check(lane.apply(epoch,true,native,ui)&&ui.bLeftTrigger==11&&ui.bRightTrigger==22,"preserve independent native triggers");
    lane.observe(epoch,{});check(lane.apply(epoch,true,native,ui)&&!ui.wButtons,"release must not replay delayed native button");
    auto replacement=epoch;++replacement.episode;check(!lane.apply(replacement,true,native,ui),"slot occupant change retires raw");
    lane.observe(epoch,{});lane.observe(epoch,a);check(!lane.apply(epoch,false,native,ui),"disabled route clears raw");
    check(!lane.apply(epoch,true,{},ui),"re-enable does not replay stale sample");
    for(unsigned field=0;field<5;++field){auto e=epoch;
        if(field==0)++e.power;else if(field==1)++e.topology;else if(field==2)++e.identity;else if(field==3)++e.episode;else ++e.slot;
        lane.observe(epoch,{});lane.observe(epoch,a);check(lane.apply(epoch,true,{},ui),"arm before revoke");check(!lane.apply(e,true,{},ui),"epoch revocation");}
    unsigned reports=0,buttonTransitions=0;WORD previous=0;std::vector<json> decoded;
    for(const auto& r:input.at("reports")) {
        data.clear();for(const auto& item:r.at("hid").at("data"))data.push_back({item.at("index"),item.value("value",0u),item.value("down",false)});
        check(decode(data,pad),"actual field parsed report rejected");++reports;
        if(reports>1 && previous!=pad.wButtons)++buttonTransitions;previous=pad.wButtons;
        decoded.push_back({{"tickMs",r.at("tickMs")},{"buttons",pad.wButtons},{"lx",pad.sThumbLX},{"ly",pad.sThumbLY},{"rx",pad.sThumbRX},{"ry",pad.sThumbRY}});
    }
    check(reports==32,"field report count");check((previous&0x300)==0x300,"last real LB+RB report");
    std::cout<<json{{"checks",checks},{"fieldReports",reports},{"digitalTransitionsAfterBaseline",buttonTransitions},
        {"decoded",decoded},{"physicalAcceptance",false},{"triggerFallback","not-inferred"}}.dump()<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}

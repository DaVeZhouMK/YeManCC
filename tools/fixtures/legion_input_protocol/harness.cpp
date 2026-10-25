#include "legion_input_protocol.h"
#include "oracle.generated.h"

#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace ymcc::legion_input;
namespace {
unsigned checks = 0;
void require(bool value, const std::string& message) { ++checks; if (!value) throw std::runtime_error(message); }
std::uint32_t bits(float value) { std::uint32_t result; std::memcpy(&result,&value,sizeof result); return result; }
std::array<float,6> array(MotionAxes a) { return {a.gx,a.gy,a.gz,a.ax,a.ay,a.az}; }
void compare(MotionAxes actual, MotionAxes expected, const std::string& message) {
    const auto a=array(actual), e=array(expected);
    for(unsigned axis=0;axis<6;++axis)require(bits(a[axis])==bits(e[axis]),message+" axis="+std::to_string(axis));
}
void word(std::array<std::uint8_t,96>& data,std::size_t at,int value,bool little=false) {
    const unsigned u=static_cast<unsigned>(value)&65535u;
    data[at]=static_cast<std::uint8_t>(little?u:(u>>8));data[at+1]=static_cast<std::uint8_t>(little?(u>>8):u);
}
std::array<std::uint8_t,96> manual(bool byte1Zero=false) {
    std::array<std::uint8_t,96> data{};data[0]=0;data[1]=byte1Zero?0:0x21;
    const int left[]{100,-200,300,-400,500,-600},right[]{700,-800,900,-1000,1100,-1200};
    for(unsigned i=0;i<6;++i){word(data,(i<3?35:41)+(i%3)*2,left[i]);word(data,(i<3?48:54)+(i%3)*2,right[i]);}
    return data;
}
MotionAxes decode(const std::array<std::uint8_t,96>& data,Protocol p,unsigned index,std::size_t actual=60) {
    auto result=decodeLogical(p,data.data(),data.size(),actual,index);require(result.has_value(),"valid full logical core rejected");return *result;
}
void testExecutedCSharpOracle() {
    for(const auto& item:oracleCases){const auto result=decodeLogical(item.protocol==0?Protocol::Tablet:Protocol::GoS,item.bytes.data(),item.bytes.size(),item.actual,item.index);
        require(result.has_value(),std::string("C# oracle full core rejected: ")+item.name);const auto values=array(*result);
        for(unsigned axis=0;axis<6;++axis)require(bits(values[axis])==item.expected[axis],std::string("actual C# body bit oracle mismatch: ")+item.name+" axis="+std::to_string(axis));}
}
void testHandPins() {
    const float a=4.0f/32767.0f,g=2000.0f/32767.0f;
    for(bool byte1Zero:{false,true}){const auto data=manual(byte1Zero);
        compare(decode(data,Protocol::Tablet,0),{400*g,600*g,-500*g,-100*a,-300*a,200*a},"left side hand pin mismatch");
        compare(decode(data,Protocol::Tablet,1),{-1100*g,1200*g,-1000*g,800*a,-900*a,700*a},"right side hand pin mismatch");}
    std::array<std::uint8_t,96> gos{};const int raw[]{100,-200,300,-400,500,-600};for(unsigned i=0;i<6;++i)word(gos,14+i*2,raw[i],true);
    compare(decode(gos,Protocol::GoS,0,26),{400*g,600*g,500*g,-100*a,-300*a,200*a},"GoS little endian hand pin mismatch");
}
void testAllShortCounts() {
    for(const auto p:{Protocol::Tablet,Protocol::GoS})for(unsigned side=0;side<(p==Protocol::Tablet?2u:1u);++side)for(bool byte1Zero:{false,true}){
        auto data=manual(byte1Zero);const std::size_t minimum=p==Protocol::Tablet?(side==0?47u:60u):26u;
        for(std::size_t actual=0;actual<minimum;++actual)require(!decodeLogical(p,data.data(),data.size(),actual,side),"short logical actual count was admitted");
        require(decodeLogical(p,data.data(),data.size(),minimum,side).has_value(),"exact minimum full core rejected");}
}
void testCapacityAndActual() {
    const auto data=manual();
    for(const auto p:{Protocol::Tablet,Protocol::GoS}){const std::size_t minimum=p==Protocol::Tablet?47:26;
        for(std::size_t capacity=0;capacity<minimum;++capacity)require(!decodeLogical(p,data.data(),capacity,minimum,0),"actual larger than capacity was admitted");
        require(!decodeLogical(p,data.data(),data.size(),0,0),"zero actual count borrowed buffer capacity");
        require(decodeLogical(p,data.data(),minimum,minimum,0).has_value(),"capacity equal actual minimum rejected");
        require(!decodeLogical(p,data.data(),0,std::numeric_limits<std::size_t>::max(),0),"overflowing actual count admitted");}
}
void testNoLengthWhitelist() {
    const auto data=manual();
    for(std::size_t actual=26;actual<=data.size();++actual)require(decodeLogical(Protocol::GoS,data.data(),data.size(),actual,0).has_value(),"GoS full core length was incorrectly whitelisted");
    for(unsigned side=0;side<2;++side)for(std::size_t actual=side==0?47:60;actual<=data.size();++actual)require(decodeLogical(Protocol::Tablet,data.data(),data.size(),actual,side).has_value(),"Tablet full core length was incorrectly whitelisted");
    require(decodeLogical(Protocol::Tablet,data.data(),data.size(),47,0).has_value(),"complete left 47-byte core was rejected by obsolete all60 gate");
}
void testIdentityAndIndices() {
    const auto data=manual();
    for(unsigned side:{1u,2u,127u,std::numeric_limits<unsigned>::max()})require(!decodeLogical(Protocol::GoS,data.data(),data.size(),33,side),"GoS sensorIndex other than zero must be rejected");
    for(unsigned side:{2u,127u,std::numeric_limits<unsigned>::max()})require(!decodeLogical(Protocol::Tablet,data.data(),data.size(),60,side),"Tablet unknown sensor index admitted");
    for(auto p:{static_cast<Protocol>(-1),static_cast<Protocol>(2),static_cast<Protocol>(999)})require(!decodeLogical(p,data.data(),data.size(),60,0),"unknown protocol admitted");
    require(!decodeLogical(Protocol::Tablet,nullptr,96,60,0) && !decodeLogical(Protocol::GoS,nullptr,33,26,0),"null logical pointer admitted");
    require(!decodeLogical(Protocol::Tablet,nullptr,0,0,0),"empty null logical pointer admitted");
}
void testLogicalByteZeroIsData() {
    auto data=manual();const auto left=decode(data,Protocol::Tablet,0);data[0]=0x71;compare(decode(data,Protocol::Tablet,0),left,"decoder guessed/removed logical byte0 prefix");
    data={};const int values[]{17,-31,47,-83,131,-197};for(unsigned i=0;i<6;++i)word(data,14+i*2,values[i],true);
    auto gos=decode(data,Protocol::GoS,0,26);data[0]=0x71;data[1]=0xff;
    compare(decode(data,Protocol::GoS,0,26),gos,"GoS guessed prefix or Tablet misalignment");
}
void testSignedExtremes() {
    const float a=4.0f/32767.0f,g=2000.0f/32767.0f;
    for(int value:{-32768,-32767,-1,0,1,32767}){
        auto data=manual();for(unsigned i=0;i<6;++i)word(data,(i<3?35:41)+(i%3)*2,value);
        const float v=static_cast<float>(value);compare(decode(data,Protocol::Tablet,0),{v*-g,v*-g,v*-g,v*-a,v*-a,v*-a},"signed int16 extremes or scale mismatch");
        data={};for(unsigned i=0;i<6;++i)word(data,14+i*2,value,true);
        compare(decode(data,Protocol::GoS,0,26),{v*-g,v*-g,v*g,v*-a,v*-a,v*-a},"GoS signed int16 extremes or scale mismatch");
        if(value==-32768)require(decode(data,Protocol::GoS,0,26).gx>2000.0f,"-32768 must not be clamped to symmetric fullscale");}
}
void testZeroStationary() {
    std::array<std::uint8_t,96> data{};
    for(const auto p:{Protocol::Tablet,Protocol::GoS})for(unsigned index=0;index<(p==Protocol::Tablet?2u:1u);++index){auto result=decodeLogical(p,data.data(),data.size(),p==Protocol::Tablet?(index==0?47u:60u):26u,index);
        require(result.has_value(),"all-zero stationary logical sample rejected");for(float value:array(*result))require(value==0.0f && std::isfinite(value),"stationary axis fabricated");}
}
void testSelectedSideAndByte1Opaque() {
    auto data=manual();const auto originalLeft=decode(data,Protocol::Tablet,0),originalRight=decode(data,Protocol::Tablet,1);
    for(std::size_t i=48;i<60;++i)data[i]^=0xff;compare(decode(data,Protocol::Tablet,0),originalLeft,"right bytes affected left selection");
    data=manual();for(std::size_t i=35;i<47;++i)data[i]^=0xff;compare(decode(data,Protocol::Tablet,1),originalRight,"left bytes affected right selection");
    compare(decode(manual(true),Protocol::Tablet,0),originalLeft,"Tablet byte1 incorrectly shifted left IMU");
    compare(decode(manual(true),Protocol::Tablet,1),originalRight,"Tablet byte1 incorrectly shifted right IMU");
}
void testSwappedEndianIsNotGuessed() {
    auto normal=manual(),swapped=normal;for(unsigned i=0;i<6;++i){const auto at=(i<3?35u:41u)+(i%3)*2;std::swap(swapped[at],swapped[at+1]);}
    const auto a=array(decode(normal,Protocol::Tablet,0)),b=array(decode(swapped,Protocol::Tablet,0));
    for(unsigned axis=0;axis<6;++axis)require(a[axis]!=b[axis],"Tablet guessed swapped endian");
    std::array<std::uint8_t,96> g{};for(unsigned i=0;i<6;++i)word(g,14+i*2,0x1234+static_cast<int>(i),true);auto gs=g;
    for(unsigned i=0;i<6;++i)std::swap(gs[14+i*2],gs[15+i*2]);const auto c=array(decode(g,Protocol::GoS,0,26)),d=array(decode(gs,Protocol::GoS,0,26));
    for(unsigned axis=0;axis<6;++axis)require(c[axis]!=d[axis],"GoS guessed swapped endian");
}
} // namespace
int main(int argc,char**argv){const std::vector<std::pair<std::string,std::function<void()>>> cases{
    {"executed-csharp-oracle",testExecutedCSharpOracle},{"manual-pins",testHandPins},{"all-short-actual-counts",testAllShortCounts},
    {"capacity-versus-actual",testCapacityAndActual},{"lengths-not-whitelist",testNoLengthWhitelist},{"protocol-and-sensor-indices",testIdentityAndIndices},
    {"logical-byte0-is-data",testLogicalByteZeroIsData},{"signed-extremes",testSignedExtremes},{"zero-stationary",testZeroStationary},
    {"selected-side-and-byte1",testSelectedSideAndByte1Opaque},{"swapped-endian",testSwappedEndianIsNotGuessed}};
    const std::string filter=argc==3&&std::string(argv[1])=="--case"?argv[2]:"";if(argc!=1&&filter.empty()){std::cerr<<"invalid arguments\n";return 2;}unsigned ran=0;
    for(const auto& item:cases){if(!filter.empty()&&filter!=item.first)continue;try{const auto before=checks;item.second();++ran;std::cout<<"PASS "<<item.first<<" checks="<<checks-before<<'\n';}
        catch(const std::exception&e){std::cerr<<"FAIL "<<item.first<<": "<<e.what()<<'\n';return 1;}}
    if(!ran){std::cerr<<"unknown case\n";return 2;}std::cout<<"SUMMARY cases="<<ran<<" checks="<<checks<<'\n';return 0;}

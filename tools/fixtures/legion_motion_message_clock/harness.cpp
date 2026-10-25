#include "legion_motion_message_clock.h"

#include <array>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace ymcc::legion_motion_clock;
namespace {
constexpr std::uint64_t ring=std::uint64_t{1}<<32;
constexpr std::uint64_t half=ring/2;
constexpr auto maximum=std::numeric_limits<std::uint64_t>::max();
unsigned checks=0;
void require(bool condition,const std::string& message){++checks;if(!condition)throw std::runtime_error(message);}
void expect(std::uint64_t now,std::uint32_t message,std::uint64_t limit,std::optional<std::uint64_t> wanted,const std::string& why){
    const auto actual=messageReceiptTick(now,message,limit);
    require(actual.has_value()==wanted.has_value(),why+" presence");
    if(wanted)require(*actual==*wanted,why+" tick");
}
void testExactAgeAndThreshold(){
    expect(1000,990,10,990,"age equal maxAge must be accepted");
    expect(1000,990,9,std::nullopt,"expired age must be rejected");
    expect(1000,1000,0,1000,"zero age and zero window must be accepted");
    expect(1000,999,0,std::nullopt,"nonzero age with zero window must expire");
    expect(2000,500,1500,500,"exact caller stale boundary must be accepted");
    expect(2001,500,1500,std::nullopt,"one beyond stale boundary must expire");
    expect(maximum,0xffffffffu,0,maximum,"full64 now must not narrow returned receipt");
    expect(maximum,0xfffffffeu,1,maximum-1,"full64 receipt must preserve high bits");
}
void test32BitWrap(){
    expect(ring+5,0xfffffffeu,7,ring-2,"low32 rollover must preserve short age");
    expect(ring+5,0xfffffffeu,6,std::nullopt,"rollover expiry must use modulo age");
    expect(ring+5,0,5,ring,"messageTime32 zero is valid at rollover");
    expect(ring,0xffffffffu,1,ring-1,"exact rollover preceding message");
    expect(ring,0,0,ring,"exact rollover current message");
    expect(ring-1,0xfffffffeu,1,ring-2,"last32 tick preceding message");
    expect(5*ring+123,120,3,5*ring+120,"multiwrap receipt uses current high64 epoch");
    expect(0x80000003ull,0x7fffffffu,4,0x7fffffffull,"signed bit crossing is unsigned time");
}
void testFutureAndBeforeBoot(){
    expect(1000,1001,maximum,std::nullopt,"future low32 before first wrap must be rejected");
    expect(ring+1000,1001,1500,std::nullopt,"near future with bounded age must be rejected");
    expect(0,1,maximum,std::nullopt,"pre-boot candidate must be rejected");
    expect(5,0xfffffffeu,100,std::nullopt,"age greater than uptime must be rejected before subtraction");
    expect(1,0xffffffffu,maximum,std::nullopt,"before boot subtraction must not wrap uint64");
}
void testZeroTicks(){
    expect(0,0,0,0,"optional zero receipt is distinct from no receipt");
    expect(1,0,1,0,"age equal uptime may return optional zero");
    expect(5,0,5,0,"boot tick zero message remains a zero witness");
    expect(5,0,4,std::nullopt,"zero receipt can still be expired");
    require(!belongsToEpoch(0,0)&&!belongsToEpoch(0,1)&&!belongsToEpoch(0,maximum),"zero received tick must never witness epoch membership");
    require(belongsToEpoch(1,0),"zero epochStart must allow nonzero received tick");
    require(belongsToEpoch(ring,ring-1),"32bit-zero low word is not full64 zero");
    require(!belongsToEpoch(ring,ring),"32bit-zero low word still rejects equal cutover");
}
void testEpochCutover(){
    require(!belongsToEpoch(999,1000),"receipt before cutover must be rejected");
    require(!belongsToEpoch(1000,1000),"equal nonzero cutover must be rejected");
    require(belongsToEpoch(1001,1000),"receipt after cutover must be accepted");
    require(!belongsToEpoch(maximum,maximum),"maximum equal cutover must be rejected");
    require(!belongsToEpoch(ring+5,2*ring+4),"epoch comparison must not truncate to32bits");
    const std::array<std::uint64_t,9> ticks{0,1,2,ring-1,ring,ring+1,2*ring+5,std::uint64_t{1}<<63,maximum};
    for(auto received:ticks)for(auto cutover:ticks)require(belongsToEpoch(received,cutover)==(received!=0&&received>cutover),"epoch uint64 comparison mismatch");
}
void testQueueReceiptNotProcessingNow(){
    const auto queued=messageReceiptTick(1020,1000,100);require(queued&&*queued==1000,"queued receipt must retain message time rather than processing now");
    require(!belongsToEpoch(*queued,1001),"pre-cutover queued event crossed epoch by processing late");
    const auto equal=messageReceiptTick(1020,1001,100);require(equal&&!belongsToEpoch(*equal,1001),"equal queued cutover must be rejected");
    const auto after=messageReceiptTick(1020,1002,100);require(after&&belongsToEpoch(*after,1001),"post-cutover queued event rejected");
    const auto old=messageReceiptTick(ring+6,0xfffffffeu,20),fresh=messageReceiptTick(ring+6,0,20);
    require(old&&fresh&&!belongsToEpoch(*old,ring-1)&&belongsToEpoch(*fresh,ring-1),"queue cutover across32 wrap mismatch");
    const auto zero=messageReceiptTick(1,0,1);require(zero&&!belongsToEpoch(*zero,0),"optional zero must not bypass epoch rejection");
}
void testModuloAmbiguityIsExplicit(){
    const std::uint64_t now=5*ring+100;
    expect(now,95,5,5*ring+95,"most recent congruent receipt is selected");
    // A real event at 4*ring+95 would have the same 32-bit field. No function of
    // these inputs can distinguish it from 5*ring+95: this is not uniqueness proof.
    require(static_cast<std::uint32_t>(4*ring+95)==static_cast<std::uint32_t>(5*ring+95),"full-ring alias demonstration invalid");
    expect(now,101,1500,std::nullopt,"short window rejects ordinary nearby future");
    expect(now,101,ring-1,4*ring+101,"large window exposes past interpretation of future low32");
    expect(now,95,ring,5*ring+95,"maxAge wider than32bits must not truncate");
    expect(now,95,ring+1,5*ring+95,"large maxAge literal contract must not be clamped");
    const std::uint64_t oppositeNow=3*ring+64;const auto opposite=static_cast<std::uint32_t>(64+half);
    expect(oppositeNow,opposite,half-1,std::nullopt,"half-ring exceeds a short caller window");
    expect(oppositeNow,opposite,half,oppositeNow-half,"half-ring equality follows explicit age contract");
    expect(oppositeNow,opposite,maximum,oppositeNow-half,"no invented half-ring or huge-age uniqueness gate");
}
std::optional<std::uint64_t> wideOracle(std::uint64_t now,std::uint32_t message,std::uint64_t window){
    // Independent uint64 modular arithmetic: no narrowing/subtraction-wrap idiom.
    const auto low=now%ring;const auto age=(low+ring-static_cast<std::uint64_t>(message))%ring;
    if(age>now||age>window)return std::nullopt;return now-age;
}
void testWideIntegerOracle(){
    std::uint64_t state=0x61e30ffa017ef123ull;
    const auto random=[&state](){state^=state<<13;state^=state>>7;state^=state<<17;return state;};
    const std::array<std::uint64_t,10> windows{0,1,150,1500,half-1,half,ring-1,ring,ring+7,maximum};
    for(unsigned i=0;i<512;++i){const auto value=random();const std::uint64_t now=i%4==0?value%2000:value;
        const auto message=i%3==0?static_cast<std::uint32_t>(now)-static_cast<std::uint32_t>(random()%1600):static_cast<std::uint32_t>(random());
        for(auto window:windows){const auto wanted=wideOracle(now,message,window),actual=messageReceiptTick(now,message,window);
            require(actual.has_value()==wanted.has_value(),"independent wide modulo oracle presence mismatch");
            if(actual){require(*actual==*wanted,"independent wide modulo oracle receipt mismatch");require(*actual<=now&&now-*actual<=window,"accepted receipt violated age bounds");
                require(static_cast<std::uint32_t>(*actual)==message,"accepted receipt lost original low32 congruence");}}}
}
} // namespace
int main(int argc,char**argv){const std::vector<std::pair<std::string,std::function<void()>>> cases{
    {"age-and-threshold",testExactAgeAndThreshold},{"32bit-wrap",test32BitWrap},{"future-and-before-boot",testFutureAndBeforeBoot},
    {"zero-ticks",testZeroTicks},{"epoch-cutover",testEpochCutover},{"queue-receipt-not-processing-now",testQueueReceiptNotProcessingNow},
    {"modulo-ambiguity-boundaries",testModuloAmbiguityIsExplicit},{"wide-integer-oracle",testWideIntegerOracle}};
    const std::string filter=argc==3&&std::string(argv[1])=="--case"?argv[2]:"";if(argc!=1&&filter.empty()){std::cerr<<"invalid arguments\n";return 2;}unsigned ran=0;
    for(const auto& item:cases){if(!filter.empty()&&filter!=item.first)continue;try{const auto before=checks;item.second();++ran;std::cout<<"PASS "<<item.first<<" checks="<<checks-before<<'\n';}
        catch(const std::exception&e){std::cerr<<"FAIL "<<item.first<<": "<<e.what()<<'\n';return 1;}}
    if(!ran){std::cerr<<"unknown case\n";return 2;}std::cout<<"SUMMARY cases="<<ran<<" checks="<<checks<<'\n';return 0;}

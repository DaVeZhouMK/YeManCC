#include "../native/steam_shortcut_metadata_baseline_policy.h"
#include "../native/steam_native_current_bracket.h"
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace sb=ymcc::shortcutbaseline;namespace cr=ymcc::currentrecord;
unsigned checks=0,cases=0;
void expect(bool x){++checks;if(!x)throw std::runtime_error("baseline proof policy rejected expectation");}
cr::Proof proof(std::uint64_t seq=1,std::uint64_t start=1000){
 cr::Proof p;p.account64=0x0110000155667788ULL;p.account=static_cast<std::uint32_t>(p.account64);p.gameId64=0xf123456702000000ULL;p.appid=0xf1234567;
 p.steamPid=101;p.steamBirth=111;p.gamePid=202;p.gameBirth=222;p.windowsSession=1;p.steamRoot="d:/steam";p.gamePath="d:/game.exe";
 p.generation=3;p.epoch=4;p.rulesEpoch=5;p.serial=6;p.sequence=seq;p.uiStartedTick=start;p.uiFinishedTick=start+50;
 p.uiStartedFileTime=1000000000ULL+start*10000;p.uiFinishedFileTime=p.uiStartedFileTime+500000;
 p.available=p.accountPinned=p.liveAccountPinned=p.localAccountSource=p.replyOk=p.replyUnmutated=true;return p;
}
int main(){try{
 sb::Key key;key.account64=11;key.gameId64=22;key.steamPid=1;key.gamePid=2;key.steamBirth=3;key.gameBirth=4;key.generation=5;key.epoch=6;key.rulesEpoch=7;key.serial=8;key.steamRoot="d:/steam";key.gamePath="d:/game";
 {++cases;sb::Attempt a;expect(sb::take(a,key,1000,true,false,true,false));expect(!sb::take(a,key,30999,true,false,true,false));expect(sb::take(a,key,31000,true,false,true,false));expect(!sb::take(a,key,500,true,false,true,false));}
 {++cases;sb::Attempt a;for(int i=0;i<4;++i){const auto before=a;expect(!sb::take(a,key,1000,i!=0,i==1,i!=2,i==3));expect(a.valid==before.valid);}expect(!a.valid);}
 {++cases;sb::Attempt a;expect(sb::take(a,key,1000,true,false,true,false));auto changed=key;changed.gameBirth++;expect(sb::take(a,changed,1001,true,false,true,false));}
 {++cases;expect(!sb::fresh(100,0));expect(!sb::fresh(100,101));expect(sb::fresh(45999,1000));expect(!sb::fresh(46000,1000));}
 const auto stale=proof();const auto now=proof(2,6050);
 {++cases;expect(cr::maximumBracketMs==5000);expect(!cr::canRead(stale,6050,now.uiFinishedFileTime,true,false));expect(cr::canRead(now,6101,now.uiFinishedFileTime+10000,true,false));}
 auto post=proof(3,6201);const std::uint64_t rs=6101,rf=6200,commit=6260;
 const auto ft=[](std::uint64_t tick){return 1000000000ULL+tick*10000;};
 const auto valid=[&](const cr::Proof& p,bool owner=true,bool captured=false){return cr::canCommit(now,p,rs,rf,ft(rs),ft(rf),commit,ft(commit),owner,captured);};
 {++cases;expect(valid(post));expect(!valid(post,false));expect(!valid(post,true,true));expect(!cr::canRead(now,6101,ft(6101),true,true));}
 const std::vector<std::function<void(cr::Proof&)>> changes={
  [](auto& p){++p.account64;},[](auto& p){++p.account;},[](auto& p){++p.gameId64;},[](auto& p){++p.appid;},
  [](auto& p){++p.steamPid;},[](auto& p){++p.steamBirth;},[](auto& p){++p.gamePid;},[](auto& p){++p.gameBirth;},
  [](auto& p){++p.generation;},[](auto& p){++p.epoch;},[](auto& p){++p.rulesEpoch;},[](auto& p){++p.serial;},
  [](auto& p){p.steamRoot+="changed";},[](auto& p){p.gamePath+="changed";},[](auto& p){++p.windowsSession;},
  [](auto& p){++p.sequence;},[](auto& p){p.replyUnmutated=false;},[](auto& p){p.replyOk=false;},[](auto& p){p.accountPinned=false;},[](auto& p){p.localAccountSource=false;}};
 for(const auto& edit:changes){++cases;auto altered=post;edit(altered);expect(!valid(altered));}
 {++cases;expect(!cr::canCommit(now,post,rs,rf,ft(rs),ft(rf),now.uiStartedTick+5001,ft(now.uiStartedTick+5001),true,false));}
 {++cases;auto p=post;p.uiStartedFileTime=ft(rf)-1;expect(!valid(p));}
 {++cases;auto p=post;p.sequence=now.sequence+2;expect(!valid(p));}
 std::cout<<"{\"suite\":\"shortcut-baseline-policy\",\"cases\":"<<cases<<",\"assertions\":"<<checks<<",\"failed\":0,\"windowMs\":"<<cr::maximumBracketMs<<"}\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}

#pragma once
#include <cstdint>
#include <string>
namespace ymcc::shortcutbaseline {
inline constexpr std::uint64_t freshForMs=45000,retryAfterMs=30000;
struct Key {
 std::uint64_t account64=0,gameId64=0,steamBirth=0,gameBirth=0,generation=0,epoch=0,rulesEpoch=0,serial=0;
 std::uint32_t steamPid=0,gamePid=0,windowsSession=0;
 std::string steamRoot,gamePath;
 bool operator==(const Key&)const=default;
};
struct Attempt {Key key;std::uint64_t tick=0;bool valid=false;};
inline bool fresh(std::uint64_t now,std::uint64_t observed){return observed&&now>=observed&&now-observed<freshForMs;}
inline bool take(Attempt& last,const Key& key,std::uint64_t now,bool owned,bool capturedOrPrepared,bool eligible,bool cachedFresh){
 if(!owned||capturedOrPrepared||!eligible||cachedFresh)return false;
 if(last.valid&&last.key==key&&(now<last.tick||now-last.tick<retryAfterMs))return false;
 last={key,now,true};return true;
}
}

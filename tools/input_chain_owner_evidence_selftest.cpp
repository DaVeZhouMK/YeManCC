// Production owner metadata + real self-query only. No controller or host I/O.
#include "../native/input_chain_owner_evidence.h"
#include <type_traits>
#include <iostream>
#include <vector>
using namespace ymcc::inputChainOwner;
int main(){Json tests=Json::array();auto check=[&](const char* n,bool ok){tests.push_back({{"name",n},{"passed",ok}});};
static_assert(std::is_trivially_copyable_v<Raw>);static_assert(noexcept(project(Raw{})));
const auto exact=134364220808351763ULL;auto good=project({9740,exact,true});
check("native-self-identity-contract",good["schema"]==1&&good["authority"]=="native-self-process-identity"&&good["creationSource"]=="GetProcessTimes-self"&&good["status"]=="observed");
check("full-FILETIME-is-decimal-string-with-no-rounding",good["creationFiletime"]=="134364220808351763"&&good["creationFiletime"].is_string()&&good["pid"]==9740);
check("owner-never-means-game-or-Steam-consumption",good["gameInputUnverified"]==true&&good["steamConsumptionProven"]==false&&good["readOnly"]==true&&!good.contains("gameInputVerified"));
check("self-query-failed-cannot-borrow-nonzero-timestamp",project({9740,exact,false})["creationFiletime"].is_null()&&project({9740,exact,false})["status"]=="unknown");
check("zero-creation-keeps-unknown",project({9740,0,true})["status"]=="unknown"&&project({9740,0,true})["creationFiletime"].is_null());
check("zero-PID-keeps-unknown",project({0,exact,true})["status"]=="unknown");
check("u64-upper-bound-remains-exact",project({0xffffffff,18446744073709551615ULL,true})["creationFiletime"]=="18446744073709551615");
check("births-one-tick-apart-stay-distinct",project({9740,exact,true})["creationFiletime"]!=project({9740,exact+1,true})["creationFiletime"]);
#ifdef _WIN32
FILETIME c{},e{},k{},u{};ULARGE_INTEGER value{};bool ok=GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u)!=FALSE;value.LowPart=c.dwLowDateTime;value.HighPart=c.dwHighDateTime;
auto live=readSelf();check("actual-self-process-query-matches-second-GetProcessTimes",ok&&live["status"]=="observed"&&live["pid"]==GetCurrentProcessId()&&live["creationFiletime"]==std::to_string(value.QuadPart));
check("actual-self-query-remains-local-diagnostic",live["gameInputUnverified"]==true&&live["steamConsumptionProven"]==false);
#endif
bool pass=true;for(auto&t:tests)pass&=t["passed"].get<bool>();std::cout<<Json{{"suite","native-owner-production-header"},{"passed",pass},{"cases",tests.size()},{"tests",tests},{"selfOSQueryOnly",true},{"deviceOperations",0},{"actualGameConsumption","unknown"}}.dump(2)<<std::endl;return pass?0:1;}

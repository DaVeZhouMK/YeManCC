#pragma once
// Pure deltas for YMCC's EXISTING Windows CPU sampler. No APIs, IO or clocks.
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>
#include <vector>
namespace ymcc::cpusample {
constexpr std::size_t maxItems=128;
struct Times {std::uint64_t idle=0,kernel=0,user=0;};
struct Sample {double total=0;bool totalValid=false;std::vector<std::optional<double>> cores;};
// Optional sinks receive valid cores only; callers keep unknown slots empty.
struct DiscardValidCore {void operator()(std::size_t,double)const noexcept{}};
class Deltas {
 std::vector<Times> previous;std::uint64_t previousStamp=0;
public:
 void reset(){previous.clear();previousStamp=0;}
 // The caller validates its existing bounded snapshot before admission. A
 // nonthrowing reader supplies each item once; committing history in this loop
 // avoids a second full copy. Aggregate-only consumers need no core array.
 template<class Read,class ValidCore=DiscardValidCore>
 Sample acceptIndexed(std::size_t count,std::uint64_t stamp,Read read,bool captureCores=true,ValidCore validCore={}){
  static_assert(std::is_nothrow_invocable_r_v<Times,Read,std::size_t>);
  static_assert(std::is_nothrow_invocable_r_v<void,ValidCore,std::size_t,double>);
  Sample result;if(!count||count>maxItems){reset();return result;}
  if(captureCores)result.cores.resize(count);
  const bool recent=!stamp||!previousStamp||(stamp>previousStamp&&stamp-previousStamp<=6000);
  const bool baseline=recent&&previous.size()==count;
  if(previous.size()!=count)previous.resize(count);
  long double total=0,busy=0;std::size_t valid=0;
  for(std::size_t i=0;i<count;++i){
   const Times a=previous[i],b=read(i);previous[i]=b;
   if(!baseline||b.idle<a.idle||b.kernel<a.kernel||b.user<a.user)continue;
   const auto idle=b.idle-a.idle,kernel=b.kernel-a.kernel,user=b.user-a.user;
   if(kernel>std::numeric_limits<std::uint64_t>::max()-user)continue;
   const auto interval=kernel+user;if(!interval||idle>interval)continue;
   if(captureCores||!std::is_same_v<ValidCore,DiscardValidCore>){
    const double value=std::clamp(100.0*static_cast<double>(interval-idle)/static_cast<double>(interval),0.0,100.0);
    if(captureCores)result.cores[i]=value;
    validCore(i,value);
   }
   total+=static_cast<long double>(interval);busy+=static_cast<long double>(interval-idle);++valid;
  }
  previousStamp=stamp;if(total>0){result.total=static_cast<double>(100*busy/total);result.totalValid=valid==count;}return result;
 }
 Sample accept(const std::vector<Times>& current,std::uint64_t stamp=0){
  return acceptIndexed(current.size(),stamp,[&current](std::size_t i) noexcept {return current[i];});
 }
};
}

#pragma once
// Pure deltas for YMCC's EXISTING Windows CPU sampler. No APIs, IO or clocks.
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>
namespace ymcc::cpusample {
constexpr std::size_t maxItems=128;
struct Times {std::uint64_t idle=0,kernel=0,user=0;};
struct Sample {double total=0;bool totalValid=false;std::vector<std::optional<double>> cores;};
class Deltas {
 std::vector<Times> previous;std::uint64_t previousStamp=0;
public:
 void reset(){previous.clear();previousStamp=0;}
 Sample accept(const std::vector<Times>& current,std::uint64_t stamp=0){
  Sample result;if(current.empty()||current.size()>maxItems){reset();return result;}
  result.cores.resize(current.size());
  const bool recent=!stamp||!previousStamp||(stamp>previousStamp&&stamp-previousStamp<=6000);
  const bool baseline=recent&&previous.size()==current.size();
  long double total=0,busy=0;std::size_t valid=0;
  if(baseline)for(std::size_t i=0;i<current.size();++i){
   const auto& a=previous[i];const auto& b=current[i];
   if(b.idle<a.idle||b.kernel<a.kernel||b.user<a.user)continue;
   const auto idle=b.idle-a.idle,kernel=b.kernel-a.kernel,user=b.user-a.user;
   if(kernel>std::numeric_limits<std::uint64_t>::max()-user)continue;
   const auto interval=kernel+user;if(!interval||idle>interval)continue;
   result.cores[i]=std::clamp(100.0*static_cast<double>(interval-idle)/static_cast<double>(interval),0.0,100.0);
   total+=static_cast<long double>(interval);busy+=static_cast<long double>(interval-idle);++valid;
  }
  previous=current;previousStamp=stamp;if(total>0){result.total=static_cast<double>(100*busy/total);result.totalValid=valid==current.size();}return result;
 }
};
}
#pragma once
// A process-lifetime retry budget, not a timer or telemetry collector.
namespace ymcc::hudrecovery {
class Budget {
 unsigned count=0;
public:
 static constexpr unsigned limit=3; // first failure + at most two re-creations
 void failed(){if(count<limit)++count;}
 bool available()const{return count<limit;}
 unsigned failures()const{return count;}
};
}

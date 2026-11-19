#define NOMINMAX
#include "sleep_power_dispatch.h"
#include <memory>
using Dispatch=ymcc::sleeppower::Dispatch;
static std::unique_ptr<Dispatch> dispatch;
extern "C" void returnedPublicReset(){dispatch=std::make_unique<Dispatch>();}
extern "C" void returnedPublicPublish(unsigned long type,unsigned long long time,bool posted){dispatch->callback(type,time,[posted](const auto&){return posted;},time);}
extern "C" void returnedPublicRead(unsigned long long* sequence,unsigned long long* time){const auto r=dispatch->latestResumeBoundary();*sequence=r.sequence;*time=r.fileTime100ns;}

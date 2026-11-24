'''Original pause fixture's inactive-owner ports and actual pure policies.
Active classification/timeout producers are outside its default43 scenarios;
their unselected actions fail closed. Optional full cancel group is separate.
'''
from pathlib import Path
import re

def bind(c):
    source,args,function=c['source'],c['args'],c['function']
    root=args.source.resolve().parent
    classifier=(root/'sleep_resume_classification_native.inc').read_text('utf-8-sig')
    timeout=(root/'sleep_timeout_native.inc').read_text('utf-8-sig')
    input_source=(root/'input_host_shared_power.h').read_text('utf-8-sig')
    def type_body(text,name):
        masked=c['mask'](text);ms=list(re.finditer(r'\bstruct '+re.escape(name)+r'\s*\{',masked));assert len(ms)==1,(name,len(ms))
        a=ms[0].start();b=masked.index('{',a)+1;d=1
        while d:d+=(masked[b]=='{')-(masked[b]=='}');b+=1
        assert masked[b]==';';return text[a:b+1]
    def inline(name):
        masked=c['mask'](input_source);ms=list(re.finditer(r'\binline bool '+re.escape(name)+r'\([^;{}]*\) noexcept\s*\{',masked));assert len(ms)==1,(name,len(ms))
        a=ms[0].start();b=masked.index('{',a)+1;d=1
        while d:d+=(masked[b]=='{')-(masked[b]=='}');b+=1
        body=input_source[a:b]
        c['extracted'].append({'origin':'native/input_host_shared_power.h','name':name,'sha256':c['hashlib'].sha256(body.encode()).hexdigest(),'verbatim':True})
        return body
    prefix=r'''
#include "sleep_kernel_cycle.h"
static ymcc::sleeppower::KernelCycleObservations g_sgKernelCycles;

// This PID fixture models a lease on its one memory-only marker.
// The independent directory fixture owns file and peer-lease behavior.
namespace ymcc::sleepLegacyResume {
enum class Result { None, Cleaned, Pending };
struct ResumeLease {
 Result result=Result::Pending;
 ULONGLONG completedBirth=0;
 template<class Current> bool current(const Current& gate) const {
  return result==Result::None&&gate();
 }
};
}
template<class Current>
static ymcc::sleepLegacyResume::ResumeLease sgAcquireResumeFiles(
 const std::wstring& dir,DWORD pid,const Current& gate,ULONGLONG) {
 assert(dir==SG_SLEEP_LEASE_DIR&&pid==100);
 return {gate()?ymcc::sleepLegacyResume::Result::None:ymcc::sleepLegacyResume::Result::Pending,0};
}
static SgResumeResult sgResumeMarkedDirectoryUnlocked(const std::wstring& dir,bool force,
 json* rows,const std::function<bool()>& gate) {
 assert(rows==nullptr);
 if(gate&&!gate())return {};
 return sgResumeMarkedDirectoryUnlocked(dir,force);
}
static bool hardwareWriteAllowed();
static bool pauseUnselectedPolicyReached=false;
static unsigned pausePublicationCalls=0,pauseRevocationCalls=0;
static void publishSharedPowerLifecycle(const char*){++pausePublicationCalls;}
static void revokeSharedPowerMirrors() noexcept {++pauseRevocationCalls;}
static std::atomic<ULONGLONG> g_sgLatestUserSleepEntryFileTime{0},g_sgClosedSleepIntentFileTime{0},g_sgLastPowerButtonWakeEventFileTime{0};
static std::atomic<ULONGLONG> g_sgSleepTimeoutSuccessfulExit{0};
static ULONGLONG sgSleepTimeoutSuccessfulExitTime(){return g_sgSleepTimeoutSuccessfulExit.load();}
static std::atomic<unsigned long long> g_sgWakeClassificationBlockedGeneration{0};
static std::mutex g_sgWorkMx;
static std::atomic<bool> g_sgEntryHandoffPending{false},g_sgEntryHandoffReady{false};
static bool g_entryFailureExpanded=false;
static ULONGLONG g_entryFailureInterruptedAt=0,g_entryFailureNewOffFileTime=0;
static constexpr ULONGLONG SG_S0_ENTRY_FAILURE_WINDOW_MS=30000ULL;
static constexpr uintptr_t SG_RESUME_CLASSIFY_TIMER_ID=0xA213,SG_SLEEP_TIMEOUT_TIMER_ID=0xA214;
using UINT_PTR=uintptr_t;
static constexpr DWORD ERROR_SUCCESS=0;
static ULONGLONG g_sgSleepTimeoutGeneration=0,g_sgSleepTimeoutClassificationGeneration=0,g_sgSleepTimeoutClassificationOrigin=0;
static ymcc::sleeptimeout::Window g_sgSleepTimeoutWindow;
static bool sgSleepTimeoutCurrent(){pauseUnselectedPolicyReached=true;throw std::logic_error("inactive timeout producer reached");}
static bool sgSleepTimeoutClassificationPowerCurrent(){pauseUnselectedPolicyReached=true;throw std::logic_error("inactive timeout continuation reached");}
static void sgSleepTimeoutRetireClassification(const char*,ULONGLONG){pauseUnselectedPolicyReached=true;throw std::logic_error("inactive timeout retire reached");}
static UINT_PTR SetTimer(int,UINT_PTR,UINT_PTR,void*){pauseUnselectedPolicyReached=true;throw std::logic_error("inactive timeout timer reached");}
// Terminal Native snapshot only; input policy below is the original header.
// No Windows query, hardware permission or registration is created by this leaf.
static int __cdecl readSharedSystemPower(ymcc::sleeppower::SharedPowerV1* value,std::uint32_t bytes){
 if(!value||bytes<sizeof(*value)||g_exitRequested.load())return 0;
 *value={};value->generation=currentPowerGeneration();value->phase=static_cast<std::uint32_t>(g_powerLifecycle.load());
 value->flags=ymcc::sleeppower::kKnown|(hardwareWriteAllowed()?ymcc::sleeppower::kHardwareAllowed:0U);return 1;
}
'''
    for name,text in [('SgResumeClassificationState',classifier),('SgRetryAdmissionHandoff',classifier),('SgSleepTimeoutOriginalJob',timeout),('SgSleepTimeoutObservation',timeout)]:
        prefix+=type_body(text,name)+'\n'
    prefix+='static SgResumeClassificationState g_sgResumeClassification;\nstatic SgRetryAdmissionHandoff g_sgRetryAdmissionHandoff;\nstatic SgSleepTimeoutObservation g_sgSleepTimeoutObservation;\n'
    prefix+='namespace ymcc::inputHost::sharedPower {\n'+inline('inputAdmitted')+'\n'+inline('capture')+'\n}\n'
    prefix+=function('sgUserWakeCancelsSleepIntent')+'\n'
    prefix+=function('sgSleepTimeoutSuccessAfter',timeout,'native/sleep_timeout_native.inc')+'\n'
    for name in ['sgResumeClassificationOwned','sgResumeClassificationObserved','sgResumeClassificationHeld','sgResumeEntryObservationPresent','sgResumeEntryObservationHeld','sgCancelRetryAdmission','sgRevokeResumeEntryObservation','sharedSleepWakeClassified','sgCompleteSharedWakeClassification']:
        prefix+=function(name,classifier,'native/sleep_resume_classification_native.inc')+'\n'
    for name in ['sgSleepTimeoutObservationCurrent','sgSleepTimeoutObservationHeld','sgSleepTimeoutClassificationComplete']:
        prefix+=function(name,timeout,'native/sleep_timeout_native.inc')+'\n'
    prefix+=function('sgStopResumeClassificationWait',classifier,'native/sleep_resume_classification_native.inc')+'\n'
    purposes=list(re.finditer(r'^enum class SgDirectoryResumePurpose\s*\{[^{}]+\};',c['mask'](source),re.M))
    if purposes:
        assert len(purposes)==1
        body=source[purposes[0].start():purposes[0].end()]
        c['extracted'].append({'origin':'native/main.cpp','name':'SgDirectoryResumePurpose','sha256':c['hashlib'].sha256(body.encode()).hexdigest(),'verbatim':True})
        prefix+='\n'+body+'\n'
    return prefix

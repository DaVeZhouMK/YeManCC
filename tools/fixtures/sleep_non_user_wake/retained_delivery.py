"""Original fixture bindings for the actual retained lifecycle delivery owner."""
from pathlib import Path
import re,hashlib,json

NAMES=['sgCompletePauseWork','sgRetainedWorkDeliveryPendingLocked','sgPruneRetainedWorkDeliveryLocked','sgWorkDeliveryCovers','sgWorkDeliveryAlreadyQueuedLocked','sgRetainWorkDeliveryLocked','sgAppendWorkDeliveryLocked','sgTakeWorkDeliveryLocked','sgQueueReturnedPause','sgNotifyReturnedPauseQueued']

def parts(c,returned):
    source,mask,function=c['source'],c['mask'](c['source']),c['function']
    def structure(name):
        m=list(re.finditer(r'\bstruct '+re.escape(name)+r'\s*\{',mask));assert len(m)==1,name
        a=m[0].start();b=mask.index('{',a)+1;depth=1
        while depth:depth+=(mask[b]=='{')-(mask[b]=='}');b+=1
        assert mask[b]==';';return source[a:b+1]
    globals=[line for line in source.splitlines() if line.startswith('static std::array<SgRetainedWorkDelivery,') or line.startswith('static unsigned long long g_sgRetainedWorkOrder=')]
    assert len(globals)==2
    cells=structure('SgRetainedWorkDelivery')+'\n'+'\n'.join(globals)+'\n'
    helper='\n'.join(function(n,returned,'native/sleep_returned_pause_native.inc') for n in NAMES)+'\n'
    return structure('SgWorkItem'),cells,helper

def bind_pause(c,returned):
    function=c['function'];p=c['preamble']
    legacy='\n'.join(function(n,returned,'native/sleep_returned_pause_native.inc') for n in ['sgOrdinaryPauseCurrent','sgReturnedPauseCurrent','sgQueueReturnedPause'])+'\n'
    if 'struct SgRetainedWorkDelivery {' not in c['source']:return p,legacy
    item,cells,helpers=parts(c,returned);m=c['mask'](p);a=m.index('struct SgWorkItem {');b=m.index('{',a)+1;depth=1
    while depth:depth+=(m[b]=='{')-(m[b]=='}');b+=1
    assert m[b]==';';p=p[:a]+item+p[b+1:]
    p=p.replace('#include <atomic>','#include <atomic>\n#include <array>',1)
    anchor='std::deque<SgWorkItem> g_sgWorkQ;';assert p.count(anchor)==1;p=p.replace(anchor,anchor+'\n'+cells)
    # Preserve the existing memory fixture's positional seed data while binding
    # every named canceled-lease field required by the original queue owner.
    entry='struct RecoveryEntry {std::wstring dir;DWORD pid=0;ULONGLONG created=0,sleepOwnerGeneration=0;std::string markerText;bool done=false;};'
    expanded='struct RecoveryEntry {std::wstring dir;DWORD pid=0;ULONGLONG created=0,sleepOwnerGeneration=0;std::string markerText;bool done=false;bool fallback=false,acceptedOnce=false;std::wstring fallbackName;bool canceledLease=false;ULONGLONG canceledThrough=0;uint64_t canceledBoundarySequence=0;};\nusing SgWakeRecoveryEntry=RecoveryEntry;'
    assert p.count(entry)==1;p=p.replace(entry,expanded)
    header=(c['args'].source.parent/'wake_game_recovery.h').read_text(encoding='utf-8-sig');masked=c['mask'](header);a=masked.index('struct SgWakeRecoveryBatch {');b=masked.index('{',a)+1;depth=1
    while depth:depth+=(masked[b]=='{')-(masked[b]=='}');b+=1
    batch=header[a:b+1]+'\nstatic SgWakeRecoveryBatch g_sgWakeRecovery;'
    parked=re.findall(r'^static std::atomic<bool> g_sgWakeRecoveryParked\{false\};$',header,re.M);assert len(parked)<=1
    batch+='\n'+'\n'.join(parked)
    old='struct { ULONGLONG generation=0;std::vector<RecoveryEntry> entries; } g_sgWakeRecovery;';assert p.count(old)==1;p=p.replace(old,batch)
    if 'sgConfirmStrictWakeLeaseResume(' in header:
        entry_end='uint64_t canceledBoundarySequence=0;};'
        assert p.count(entry_end)==1
        original_wake='ULONGLONG confirmationOriginalWakeFileTime=0;' if 'confirmationOriginalWakeFileTime' in header else ''
        p=p.replace(entry_end,'uint64_t canceledBoundarySequence=0;bool strictConfirmation=false;uint64_t confirmationSleepSequence=0,confirmationSuspendSerial=0;\n'
                    +original_wake+
                    ' RecoveryEntry(std::wstring d,DWORD p,ULONGLONG c,ULONGLONG g,std::string t,bool end):dir(std::move(d)),pid(p),created(c),sleepOwnerGeneration(g),markerText(std::move(t)),done(end){}\n'
                    ' RecoveryEntry(std::wstring d,DWORD p,ULONGLONG c,std::string t):RecoveryEntry(std::move(d),p,c,0,std::move(t),false){}\n'
                    ' RecoveryEntry()=default;};')
    helper='\n'.join(function(n,returned,'native/sleep_returned_pause_native.inc') for n in ['sgOrdinaryPauseCurrent','sgReturnedPauseCurrent'])+'\n'+helpers
    queue=function('sgQueueCanceledSleepLeaseRecoveryUnlocked',header,'native/wake_game_recovery.h')
    cancel_helpers=['sgCanceledSleepRecoveryEntryCurrent']
    if 'sgCanceledSleepRecoveryBoundaryCurrent' in header:cancel_helpers.insert(0,'sgCanceledSleepRecoveryBoundaryCurrent')
    if 'sgWakeGameRecoveryHasWork(' in queue:
        cancel_helpers.insert(0,'sgWakeGameRecoveryHasWork')
    helper+='\n'+function('sgNotifyWork')+'\n'+'\n'.join(function(n,header,'native/wake_game_recovery.h') for n in cancel_helpers)+'\n'+queue+'\n'
    return p,helper

def bind_abort(c,p,returned,text):
    if 'struct SgRetainedWorkDelivery {' not in c['source']:return p
    _,cells,helpers=parts(c,returned)
    text('retained_delivery_cells.inc',cells)
    text('retained_delivery_helpers.inc',helpers)
    p=p.replace('#include <deque>','#include <deque>\n#include <array>',1)
    atomic='static std::atomic<unsigned long long> g_sgPauseWorkCompletedGeneration{0};';assert p.count(atomic)==1;p=p.replace(atomic,'')
    anchor='static std::deque<SgWorkItem> g_sgWorkQ;';assert p.count(anchor)==1;p=p.replace(anchor,anchor+'\n'+atomic+'\n#include "retained_delivery_cells.inc"')
    anchor='#include "sgQueueReturnedPause.inc"';assert p.count(anchor)==1;p=p.replace(anchor,'#include "retained_delivery_helpers.inc"')
    return p

def adapt_checks(c):
    if 'struct SgRetainedWorkDelivery {' not in c['source']:return c['preamble'],c['checks']
    p,checks=c['preamble'],c['checks']
    # Existing fixture observation of the first queued item; actual removal
    # always calls the original sgTakeWorkDeliveryLocked under its mutex.
    observer=r'''
static size_t fixtureWorkDeliveryCount(){return g_sgWorkQ.size()+static_cast<size_t>(std::count_if(g_sgRetainedWorkDelivery.begin(),g_sgRetainedWorkDelivery.end(),[](const auto& d){return d.order!=0;}));}
static SgWorkItem& fixtureWorkDeliveryFront(){SgRetainedWorkDelivery* first=nullptr;for(auto& d:g_sgRetainedWorkDelivery)if(d.order&&(!first||d.order<first->order))first=&d;if(first&&first->ahead==0)return first->item;if(g_sgWorkQ.empty())throw std::runtime_error("no fixture-observed queued work");return g_sgWorkQ.front();}
static bool fixtureTryWorkDeliveryPop(){std::lock_guard<std::mutex> lock(g_sgWorkMx);SgWorkItem item{};return sgTakeWorkDeliveryLocked(item);}
static void fixtureWorkDeliveryPop(){if(!fixtureTryWorkDeliveryPop())throw std::runtime_error("original delivery pop rejected fixture work");}
static void fixtureWorkDeliveryReset(){std::lock_guard<std::mutex> lock(g_sgWorkMx);g_sgWorkQ.clear();g_sgRetainedWorkDelivery={};g_sgRetainedWorkOrder=0;}
'''
    # Functions need the original helpers defined before this test observer.
    c['functions']+='\n'+observer+'\n'
    for a,b in [('g_sgWorkQ.size()','fixtureWorkDeliveryCount()'),('g_sgWorkQ.empty()','(fixtureWorkDeliveryCount()==0)'),('g_sgWorkQ.front()','fixtureWorkDeliveryFront()'),('g_sgWorkQ.pop_front()','fixtureWorkDeliveryPop()'),('g_sgWorkQ.clear()','fixtureWorkDeliveryReset()')]:checks=checks.replace(a,b)
    if c['args'].returned_pause_only and not c['args'].ordinary_pause_only:
        old='fixtureWorkDeliveryPop();\n   runProductionSuspendPause(scope.generation,&scope);'
        assert checks.count(old)==1
        checks=checks.replace(old,'if(scenario=="changed-generation"){assert(!fixtureTryWorkDeliveryPop());}\n   else {fixtureWorkDeliveryPop();runProductionSuspendPause(scope.generation,&scope);}')
    return p,checks

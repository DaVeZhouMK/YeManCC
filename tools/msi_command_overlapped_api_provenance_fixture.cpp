// Included after original/current verbatim extracts. Only this file supplies main().
#ifdef YMCC_FAULT_INJECT
#error Fixture must not compile production fault-inject branches
#endif
using original::MsiClawIoState;
namespace tests {
inline int checks=0,cases=0;
inline void check(bool ok,const char* expr,int line){++checks;if(!ok)throw std::runtime_error(std::string("CHECK line ")+std::to_string(line)+": "+expr);}
#define CHECK(x) ::tests::check(bool(x),#x,__LINE__)
using State=current::MsiClawIoState;
struct Before {
 using Diag=original::MsiClawHidDiag;
 static HANDLE open(Diag& d,bool mode){return mode?original::msiClawOpenModeCommand(d):original::msiClawOpenCommandHid(d);}
};
struct After {
 using Diag=current::MsiClawHidDiag;
 static HANDLE open(Diag& d,bool mode){return mode?current::msiClawOpenModeCommand(d):current::msiClawOpenCommandHid(d);}
};
inline void cleanup(){
 CHECK(!fake::op.active);
 original::msiClawIoSlotFreeLocked();current::msiClawIoSlotFreeLocked();
 for(auto& h:fake::resources)if(!h->closed)CloseHandle(h.get());
 fake::reset();
}
inline void run(const char* name,const std::function<void()>& body){
 cleanup();body();cleanup();++cases;std::cout<<"PASS\t"<<name<<"\n";
}
template<class Op> void selected(int wanted,bool mode=true){
 typename Op::Diag d;HANDLE h=Op::open(d,mode);CHECK(h!=INVALID_HANDLE_VALUE);CHECK(h->candidate==wanted);CHECK(d.deviceFound);CHECK(d.productId==fake::candidates[wanted].pid);CHECK(d.usagePage==fake::candidates[wanted].page);CHECK(d.usage==fake::candidates[wanted].usage);CHECK(d.firmware==fake::candidates[wanted].firmware);CHECK(d.outputReportByteLength==64);CHECK(fake::setDestroyed==1);CHECK(fake::openedCandidates.back()==wanted);CloseHandle(h);
}
template<class Fn> void bothSelectors(Fn test){test.template operator()<Before>();cleanup();test.template operator()<After>();}
inline void oneCandidate(){fake::candidates={fake::candidate()};}
inline std::array<BYTE,64> frame(){std::array<BYTE,64> b{};b[0]=15;b[3]=60;b[4]=36;b[5]=1;return b;}
inline current::MsiClawCommandOutcome mode(){return current::msiClawSwitchMode(1);}
inline void apiReceipt(const current::MsiClawWriteResult& r,bool returned,bool writeOk,DWORD writeError,bool queried,bool queryOk,DWORD queryError,const char* phase){
 CHECK(r.writeFileReturned==returned);CHECK(r.writeFileSucceeded==writeOk);CHECK(r.writeFileError==writeError);
 CHECK(r.completionQueryAttempted==queried);CHECK(r.completionQuerySucceeded==queryOk);CHECK(r.completionQueryError==queryError);
 CHECK(r.completionQueryPhase!=nullptr);CHECK(std::string(r.completionQueryPhase)==phase);
}
inline void apiDefaults(const current::MsiClawWriteResult& r){apiReceipt(r,false,false,ERROR_SUCCESS,false,false,ERROR_SUCCESS,"not-attempted");}
inline void apiCompleted(const current::MsiClawWriteResult& r,const char* phase){
 const bool immediate=fake::plan.start==fake::Immediate;
 apiReceipt(r,true,immediate,immediate?ERROR_SUCCESS:ERROR_IO_PENDING,true,true,ERROR_SUCCESS,phase);
}
inline void expectRetained(const current::MsiClawWriteResult& r){
 const bool immediate=fake::plan.start==fake::Immediate;
 if(r.state==State::IoDesignPending)apiReceipt(r,true,false,ERROR_IO_PENDING,false,false,ERROR_SUCCESS,"not-attempted");
 else apiReceipt(r,true,immediate,immediate?ERROR_SUCCESS:ERROR_IO_PENDING,true,false,ERROR_IO_INCOMPLETE,immediate?"immediate":(r.timedOut?"cancel-reap":"signaled"));
 CHECK(r.attempted);CHECK(!r.ok);CHECK(r.handleRetained);CHECK(r.state==State::Retired || r.state==State::IoDesignPending);
 auto& s=current::g_msiClawIoSlot;CHECK(s.pending);CHECK(s.handle==fake::op.handle);CHECK(s.event==fake::op.event);CHECK(&s.ov==fake::op.ov);CHECK(s.buffer.data()==fake::op.buffer);CHECK(s.pendingSize==64);CHECK(s.buffer==fake::op.snapshot);CHECK(!s.handle->closed);CHECK(!s.event->closed);CHECK(s.txSeq==r.txSeq);
}
inline void terminalReap(bool aborted=false,DWORD customError=0){
 auto h=fake::op.handle;auto event=fake::op.event;auto ov=fake::op.ov;
 fake::plan.results.push_back(customError?fake::Reply{FALSE,0,customError}:(aborted?fake::Reply{FALSE,0,ERROR_OPERATION_ABORTED}:fake::Reply{TRUE,64,0}));
 CHECK(current::msiClawIoSlotReapLocked());CHECK(h->closed);CHECK(event->closed);CHECK(!current::g_msiClawIoSlot.pending);CHECK(current::g_msiClawIoSlot.handle==INVALID_HANDLE_VALUE);CHECK(current::g_msiClawIoSlot.event==nullptr);CHECK(current::g_msiClawIoSlot.buffer.empty());CHECK(current::g_msiClawIoSlot.pendingSize==0);CHECK(current::g_msiClawIoSlot.ov.hEvent==nullptr);CHECK(fake::resultsHandles.back()==h);CHECK(fake::resultsOverlapped.back()==ov);CHECK(!fake::op.active);
}
inline void ordinaryCompleted(const current::MsiClawWriteResult& r,DWORD bytes){
 apiCompleted(r,fake::plan.start==fake::Immediate?"immediate":"signaled");
 CHECK(r.attempted);CHECK(r.written==bytes);CHECK(r.ok==(bytes==64));CHECK(r.state==(bytes==64?State::Completed:State::ShortWrite));CHECK(!r.handleRetained);CHECK(!r.timedOut);CHECK(!current::g_msiClawIoSlot.pending);CHECK(current::g_msiClawIoSlot.handle==INVALID_HANDLE_VALUE);CHECK(fake::op.handle->closed);CHECK(fake::writeCalls==1);CHECK(fake::nullCountCalls==1);CHECK(fake::nonNullCountCalls==0);CHECK(fake::cancelCalls==0);
}
}
int main(){
 using namespace tests;
 try {
 run("original-sync-mode-handle-blocks-before-timeout-branch",[]{
  oneCandidate();bool caught=false;
  try{(void)original::msiClawSwitchMode(1);}catch(const FakeContractFailure& e){caught=std::string(e.what())=="SYNC_HANDLE_BLOCKS_BEFORE_WAIT_BUDGET";}
  CHECK(caught);CHECK(fake::openFlags==std::vector<DWORD>({FILE_ATTRIBUTE_NORMAL}));CHECK(original::g_msiClawIoSlot.handle->flags==FILE_ATTRIBUTE_NORMAL);CHECK(fake::syncBoundaryHits==1);CHECK(fake::writeCalls==1);CHECK(fake::waitCalls==0);CHECK(fake::cancelCalls==0);CHECK(fake::resultCalls==0);CHECK(!fake::op.active);
 });
 run("fixed-mode-handle-overlapped-and-completion-payload",[]{
  oneCandidate();fake::plan.results={{TRUE,64,0}};auto out=mode();ordinaryCompleted(out.write,64);CHECK(out.diag.selectedOpenFlags==(FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OVERLAPPED));CHECK(fake::openFlags==std::vector<DWORD>({FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OVERLAPPED}));auto expected=frame();CHECK(fake::op.snapshot==std::vector<BYTE>(expected.begin(),expected.end()));CHECK(fake::waitCalls==0);CHECK(fake::resultCalls==1);
 });
 run("unchanged-default-callers-synchronous-writeframe",[]{
  bothSelectors([]<class Op>(){oneCandidate();typename Op::Diag d;auto h=Op::open(d,false);CHECK(h->flags==FILE_ATTRIBUTE_NORMAL);CHECK(fake::openFlags==std::vector<DWORD>({FILE_ATTRIBUTE_NORMAL}));auto b=frame();
   if constexpr(std::is_same_v<Op,Before>){auto r=original::msiClawWriteFrame(h,b.data(),b.size());CHECK(r.ok);CHECK(r.written==64);}else {CHECK(d.selectedOpenFlags==FILE_ATTRIBUTE_NORMAL);auto r=current::msiClawWriteFrame(h,b.data(),b.size());apiDefaults(r);CHECK(r.ok);CHECK(r.written==64);}
   CHECK(fake::resultCalls==0);CHECK(fake::waitCalls==0);CHECK(fake::nonNullCountCalls==1);CloseHandle(h);});
 });
 run("selector-first-1901-before-1902",[]{bothSelectors([]<class Op>(){fake::candidates={fake::candidate(L"first1901",0x1901,0xffa0,1),fake::candidate(L"second1902")};selected<Op>(0);CHECK(fake::openedCandidates==std::vector<int>({0}));});});
 run("selector-first-1902-before-1901",[]{bothSelectors([]<class Op>(){fake::candidates={fake::candidate(L"first1902"),fake::candidate(L"second1901",0x1901,0xffa0,1)};selected<Op>(0);CHECK(fake::openedCandidates==std::vector<int>({0}));});});
 run("selector-rejects-testing-wrong-vendor-and-usage",[]{bothSelectors([]<class Op>(){auto c=fake::candidate(L"foreign");c.vid=0x1234;fake::candidates={fake::candidate(L"1903",0x1903),fake::candidate(L"wrongUsage",0x1902,0xfff0,1),c,fake::candidate(L"correct")};selected<Op>(3);CHECK(fake::openedCandidates==std::vector<int>({0,1,2,3}));CHECK(fake::closed.size()==4);});});
 run("selector-open-denied-falls-through-without-reorder",[]{bothSelectors([]<class Op>(){auto c=fake::candidate(L"denied");c.openError=ERROR_ACCESS_DENIED;fake::candidates={c,fake::candidate(L"fallback",0x1901,0xffa0,1)};typename Op::Diag d;auto h=Op::open(d,true);CHECK(h->candidate==1);CHECK(d.openFailures==1);CHECK(d.lastError==ERROR_ACCESS_DENIED);CHECK(d.vendorMatches==2);CHECK(d.devicesScanned==2);CHECK(fake::openedCandidates==std::vector<int>({0,1}));CloseHandle(h);});});
 run("selector-attribute-preparsed-caps-failure-fallback",[]{bothSelectors([]<class Op>(){auto a=fake::candidate(L"attrs");a.attrs=false;auto b=fake::candidate(L"prep");b.prep=false;auto c=fake::candidate(L"caps");c.caps=false;fake::candidates={a,b,c,fake::candidate(L"valid")};selected<Op>(3);CHECK(fake::closed.size()==4);CHECK(fake::prepFreed==2);});});
 run("selector-exhaustion-is-complete-not-found",[]{bothSelectors([]<class Op>(){fake::candidates={fake::candidate(L"testing",0x1903)};typename Op::Diag d;auto h=Op::open(d,true);CHECK(h==INVALID_HANDLE_VALUE);CHECK(d.enumerationComplete);CHECK(d.enumerationError==0);CHECK(!d.deviceFound);CHECK(fake::closed.size()==1);CHECK(fake::setDestroyed==1);});});
 run("selector-class-error-fail-closed",[]{bothSelectors([]<class Op>(){fake::classError=ERROR_ACCESS_DENIED;typename Op::Diag d;CHECK(Op::open(d,true)==INVALID_HANDLE_VALUE);CHECK(d.enumerationError==ERROR_ACCESS_DENIED);CHECK(!d.enumerationComplete);CHECK(fake::openedCandidates.empty());CHECK(fake::setDestroyed==0);});});
 run("selector-enum-error-not-complete",[]{bothSelectors([]<class Op>(){fake::enumerationError=ERROR_GEN_FAILURE;typename Op::Diag d;CHECK(Op::open(d,true)==INVALID_HANDLE_VALUE);CHECK(d.enumerationError==ERROR_GEN_FAILURE);CHECK(!d.enumerationComplete);CHECK(fake::setDestroyed==1);});});
 run("selector-detail-size-and-fill-failure-then-fallback",[]{bothSelectors([]<class Op>(){auto a=fake::candidate(L"size");a.size=false;auto b=fake::candidate(L"detail");b.detail=false;fake::candidates={a,b,fake::candidate(L"valid")};typename Op::Diag d;auto h=Op::open(d,true);CHECK(h->candidate==2);CHECK(d.enumerationError==ERROR_GEN_FAILURE);CHECK(d.devicesScanned==2);CHECK(fake::openedCandidates==std::vector<int>({2}));CloseHandle(h);});});
 run("selector-vendor-path-case-and-open-failure-accounting",[]{bothSelectors([]<class Op>(){auto a=fake::candidate(L"foreign");a.path=L"\\\\?\\hid#vid_1234&foreign";a.openError=ERROR_ACCESS_DENIED;auto b=fake::candidate(L"upper");b.path=L"\\\\?\\HID#VID_0DB0&UPPER";b.openError=ERROR_ACCESS_DENIED;fake::candidates={a,b,fake::candidate(L"valid")};typename Op::Diag d;auto h=Op::open(d,true);CHECK(h->candidate==2);CHECK(d.vendorMatches==2);CHECK(d.openFailures==1);CHECK(fake::openedCandidates==std::vector<int>({0,1,2}));CHECK(!original::msiPathMentionsVendor(nullptr));CHECK(current::msiPathMentionsVendor(L"vID_0dB0"));CloseHandle(h);});});
 run("selector-trace-disabled-does-not-change-selection",[]{bothSelectors([]<class Op>(){oneCandidate();fake::trace=false;typename Op::Diag d;auto h=Op::open(d,true);CHECK(h->candidate==0);CHECK(d.deviceFound);CHECK(d.interfacePath.empty());CloseHandle(h);});});
 run("selector-explicit-open-flags-forwarded-only",[]{oneCandidate();current::MsiClawHidDiag d;auto h=current::msiClawOpenCommandHid(d,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH);CHECK(h->flags==(FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH));CHECK(d.selectedOpenFlags==h->flags);CHECK(!(h->flags&FILE_FLAG_OVERLAPPED));CloseHandle(h);});
 run("mode2-payload-and-shared-bounded-contract-unchanged",[]{oneCandidate();fake::plan.results={{TRUE,64,0}};auto out=current::msiClawSwitchMode(2);ordinaryCompleted(out.write,64);CHECK(fake::op.snapshot.size()==64);CHECK(fake::op.snapshot[0]==15);CHECK(fake::op.snapshot[3]==60);CHECK(fake::op.snapshot[4]==36);CHECK(fake::op.snapshot[5]==2);CHECK(fake::op.snapshot[6]==0);CHECK(std::count(fake::op.snapshot.begin()+7,fake::op.snapshot.end(),BYTE(0))==57);});
 run("immediate-completion-actual64-with-null-count",[]{oneCandidate();fake::plan.misleadingAsyncCount=0;fake::plan.results={{TRUE,64,0}};auto out=mode();ordinaryCompleted(out.write,64);CHECK(fake::resultCalls==1);CHECK(fake::waitCalls==0);});
 run("immediate-shortbytes-from-completion-not-writefile-count",[]{oneCandidate();fake::plan.misleadingAsyncCount=64;fake::plan.results={{TRUE,17,0}};auto out=mode();ordinaryCompleted(out.write,17);CHECK(out.write.lastError==0);});
 run("immediate-zero-completion-not-full-write",[]{oneCandidate();fake::plan.results={{TRUE,0,0}};auto out=mode();ordinaryCompleted(out.write,0);});
 run("original-immediate-count-could-claim-full-despite-short-completion",[]{oneCandidate();original::MsiClawHidDiag d;auto h=original::msiClawOpenCommandHid(d);h->flags|=FILE_FLAG_OVERLAPPED;fake::plan.strictNull=false;fake::plan.misleadingAsyncCount=64;fake::plan.results={{TRUE,17,0}};auto b=frame();auto r=original::msiClawCommandWriteBounded(h,b.data(),b.size());CHECK(r.ok);CHECK(r.written==64);CHECK(fake::resultCalls==0);CHECK(fake::nonNullCountCalls==1);CHECK(fake::plan.results.front().bytes==17);CloseHandle(h);});
 run("pending-signaled-completion64-single-write",[]{oneCandidate();fake::plan.start=fake::Pending;fake::plan.waits={WAIT_OBJECT_0};fake::plan.results={{TRUE,64,0}};auto out=mode();ordinaryCompleted(out.write,64);CHECK(out.write.writeFileError==ERROR_IO_PENDING);CHECK(out.write.lastError==ERROR_SUCCESS);CHECK(out.write.state!=State::StartFailed);CHECK(fake::waitBudgets==std::vector<DWORD>({2000}));CHECK(fake::resultCalls==1);});
 run("pending-signaled-short-completion",[]{oneCandidate();fake::plan.start=fake::Pending;fake::plan.waits={WAIT_OBJECT_0};fake::plan.results={{TRUE,7,0}};auto out=mode();ordinaryCompleted(out.write,7);CHECK(fake::waitCalls==1);});
 run("start-error433-propagates-no-retry",[]{oneCandidate();fake::plan.start=fake::StartError;fake::plan.startError=433;auto out=mode();auto r=out.write;apiReceipt(r,true,false,433,false,false,ERROR_SUCCESS,"not-attempted");CHECK(r.state==State::StartFailed);CHECK(r.lastError==433);CHECK(r.attempted);CHECK(!r.ok);CHECK(r.written==0);CHECK(!r.handleRetained);CHECK(fake::writeCalls==1);CHECK(fake::waitCalls==0);CHECK(fake::cancelCalls==0);CHECK(fake::resultCalls==0);CHECK(fake::op.handle->closed);CHECK(!current::g_msiClawIoSlot.pending);});
 run("immediate-terminal-completion-error433-not-retained",[]{oneCandidate();fake::plan.results={{FALSE,0,433}};auto out=mode();apiReceipt(out.write,true,true,ERROR_SUCCESS,true,false,433,"immediate");CHECK(out.write.state==State::StartFailed);CHECK(out.write.lastError==433);CHECK(!out.write.handleRetained);CHECK(fake::op.handle->closed);CHECK(fake::writeCalls==1);CHECK(fake::resultCalls==1);CHECK(fake::waitCalls==0);});
 run("signaled-terminal-completion-error433-not-retried",[]{oneCandidate();fake::plan.start=fake::Pending;fake::plan.waits={WAIT_OBJECT_0};fake::plan.results={{FALSE,0,433}};auto out=mode();apiReceipt(out.write,true,false,ERROR_IO_PENDING,true,false,433,"signaled");CHECK(out.write.state==State::StartFailed);CHECK(out.write.lastError==433);CHECK(!out.write.handleRetained);CHECK(fake::op.handle->closed);CHECK(fake::writeCalls==1);CHECK(fake::waitCalls==1);CHECK(fake::cancelCalls==0);});
 run("timeout-cancel-aborted-reaped-exact-budgets",[]{oneCandidate();fake::plan.start=fake::Pending;fake::plan.waits={WAIT_TIMEOUT,WAIT_OBJECT_0};fake::plan.results={{FALSE,0,ERROR_OPERATION_ABORTED}};auto out=mode();auto r=out.write;apiReceipt(r,true,false,ERROR_IO_PENDING,true,false,ERROR_OPERATION_ABORTED,"cancel-reap");CHECK(r.lastError==ERROR_SUCCESS);CHECK(r.state==State::TimeoutCancelledReaped);CHECK(r.timedOut);CHECK(r.cancelAttempted);CHECK(!r.ok);CHECK(!r.handleRetained);CHECK(fake::cancelCalls==1);CHECK(fake::cancelHandles[0]==fake::op.handle);CHECK(fake::cancelOverlapped[0]==fake::op.ov);CHECK(fake::waitBudgets==std::vector<DWORD>({2000,500}));CHECK(fake::op.handle->closed);});
 run("timeout-cancel-late64-completed-without-rewrite",[]{oneCandidate();fake::plan.start=fake::Pending;fake::plan.waits={WAIT_TIMEOUT,WAIT_OBJECT_0};fake::plan.results={{TRUE,64,0}};auto out=mode();apiCompleted(out.write,"cancel-reap");CHECK(out.write.lastError==ERROR_SUCCESS);CHECK(out.write.state==State::LateCompleted);CHECK(out.write.ok);CHECK(out.write.written==64);CHECK(out.write.timedOut);CHECK(out.write.cancelAttempted);CHECK(out.write.lateReaped);CHECK(!out.write.handleRetained);CHECK(fake::op.handle->closed);CHECK(fake::writeCalls==1);CHECK(fake::cancelCalls==1);});
 run("timeout-late-shortbytes-propagated",[]{oneCandidate();fake::plan.start=fake::Pending;fake::plan.waits={WAIT_TIMEOUT,WAIT_OBJECT_0};fake::plan.results={{TRUE,5,0}};auto out=mode();apiCompleted(out.write,"cancel-reap");CHECK(out.write.lastError==ERROR_SUCCESS);CHECK(out.write.state==State::ShortWrite);CHECK(!out.write.ok);CHECK(out.write.written==5);CHECK(out.write.lateReaped);CHECK(out.write.timedOut);CHECK(!out.write.handleRetained);CHECK(fake::op.handle->closed);});
 run("timeout-incomplete-retains-identical-resources-until-late-reap",[]{oneCandidate();fake::plan.start=fake::Pending;fake::plan.waits={WAIT_TIMEOUT,WAIT_TIMEOUT};fake::plan.results={{FALSE,0,ERROR_IO_INCOMPLETE}};auto out=mode();expectRetained(out.write);CHECK(out.write.lastError==ERROR_SUCCESS);CHECK(out.write.timedOut);CHECK(out.write.cancelAttempted);CHECK(fake::writeCalls==1);CHECK(fake::cancelCalls==1);CHECK(fake::waitBudgets==std::vector<DWORD>({2000,500}));terminalReap();});
 run("cancel-not-found-incomplete-still-retained",[]{oneCandidate();fake::plan.start=fake::Pending;fake::plan.cancelOk=false;fake::plan.waits={WAIT_TIMEOUT,WAIT_TIMEOUT};fake::plan.results={{FALSE,0,ERROR_IO_INCOMPLETE}};auto out=mode();expectRetained(out.write);CHECK(out.write.lastError==ERROR_NOT_FOUND);CHECK(out.write.cancelAttempted);CHECK(fake::cancelCalls==1);terminalReap(true);});
 run("retired-busy-rejects-new-write-does-not-reuse-buffer",[]{oneCandidate();fake::plan.start=fake::Pending;fake::plan.waits={WAIT_TIMEOUT,WAIT_TIMEOUT};fake::plan.results={{FALSE,0,ERROR_IO_INCOMPLETE}};auto first=mode();expectRetained(first.write);auto oldH=fake::op.handle,oldEvent=fake::op.event;auto oldOv=fake::op.ov;auto oldBuffer=fake::op.buffer;auto tx=first.write.txSeq;
  fake::plan.results={{FALSE,0,ERROR_IO_INCOMPLETE}};auto second=mode();apiDefaults(second.write);CHECK(second.write.state==State::SlotBusyRetired);CHECK(!second.write.attempted);CHECK(!second.write.handleRetained);CHECK(second.write.txSeq==tx);CHECK(fake::writeCalls==1);CHECK(fake::waitCalls==2);CHECK(fake::cancelCalls==1);CHECK(fake::op.handle==oldH);CHECK(fake::op.event==oldEvent);CHECK(fake::op.ov==oldOv);CHECK(fake::op.buffer==oldBuffer);CHECK(!oldH->closed);CHECK(!oldEvent->closed);CHECK(current::g_msiClawIoSlot.buffer==fake::op.snapshot);CHECK(fake::closed.back()!=oldH);terminalReap();
 });
 run("late-reap-old-operation-then-new-write-isolated",[]{oneCandidate();fake::plan.start=fake::Pending;fake::plan.waits={WAIT_TIMEOUT,WAIT_TIMEOUT};fake::plan.results={{FALSE,0,ERROR_IO_INCOMPLETE}};auto first=mode();expectRetained(first.write);auto oldH=fake::op.handle,oldEvent=fake::op.event;auto oldOv=fake::op.ov;
  fake::plan.start=fake::Immediate;fake::plan.results={{TRUE,64,0},{TRUE,64,0}};auto next=mode();apiCompleted(next.write,"immediate");CHECK(next.write.ok);CHECK(fake::writeCalls==2);CHECK(fake::resultsHandles[fake::resultsHandles.size()-2]==oldH);CHECK(fake::resultsOverlapped[fake::resultsOverlapped.size()-2]==oldOv);CHECK(oldH->closed);CHECK(oldEvent->closed);CHECK(fake::op.handle!=oldH);CHECK(fake::op.event!=oldEvent);CHECK(fake::op.handle->closed);CHECK(next.write.txSeq>first.write.txSeq);
 });
 run("immediate-io-incomplete-retains-active-slot",[]{oneCandidate();fake::plan.results={{FALSE,0,ERROR_IO_INCOMPLETE}};auto out=mode();expectRetained(out.write);CHECK(out.write.lastError==ERROR_IO_INCOMPLETE);CHECK(fake::waitCalls==0);CHECK(fake::cancelCalls==0);CHECK(fake::writeCalls==1);terminalReap();});
 run("signaled-wait-io-incomplete-retains-active-slot",[]{oneCandidate();fake::plan.start=fake::Pending;fake::plan.waits={WAIT_OBJECT_0};fake::plan.results={{FALSE,0,ERROR_IO_INCOMPLETE}};auto out=mode();expectRetained(out.write);CHECK(out.write.lastError==ERROR_IO_INCOMPLETE);CHECK(fake::waitBudgets==std::vector<DWORD>({2000}));CHECK(fake::cancelCalls==0);terminalReap(true);});
 run("original-signaled-incomplete-drops-live-ownership",[]{oneCandidate();original::MsiClawHidDiag d;auto h=original::msiClawOpenCommandHid(d);h->flags|=FILE_FLAG_OVERLAPPED;fake::plan.strictNull=false;fake::plan.start=fake::Pending;fake::plan.waits={WAIT_OBJECT_0};fake::plan.results={{FALSE,0,ERROR_IO_INCOMPLETE}};auto b=frame();auto r=original::msiClawCommandWriteBounded(h,b.data(),b.size());CHECK(r.state==original::MsiClawIoState::StartFailed);CHECK(r.lastError==ERROR_IO_INCOMPLETE);CHECK(!r.handleRetained);CHECK(!original::g_msiClawIoSlot.pending);CHECK(original::g_msiClawIoSlot.handle==INVALID_HANDLE_VALUE);CHECK(fake::op.active);bool caught=false;try{CloseHandle(h);}catch(const FakeContractFailure&e){caught=std::string(e.what())=="CLOSED_ACTIVE_DEVICE_OR_EVENT";}CHECK(caught);
  // Harness supplies a final fake completion solely to tear down this intentional negative reproduction.
  fake::plan.results={{TRUE,64,0}};DWORD bytes=0;CHECK(GetOverlappedResult(h,fake::op.ov,&bytes,FALSE));CloseHandle(h);
 });
 run("wait-failed-cancels-once-keeps-live-resources",[]{oneCandidate();fake::plan.start=fake::Pending;fake::plan.waits={WAIT_FAILED};auto out=mode();expectRetained(out.write);CHECK(out.write.state==State::IoDesignPending);CHECK(out.write.lastError==ERROR_INVALID_HANDLE);CHECK(out.write.cancelAttempted);CHECK(fake::cancelCalls==1);CHECK(fake::resultCalls==0);CHECK(fake::waitBudgets==std::vector<DWORD>({2000}));terminalReap();});
 run("missing-device-never-starts-io",[]{fake::candidates={fake::candidate(L"testing",0x1903)};auto out=mode();apiDefaults(out.write);CHECK(out.write.state==State::NoDevice);CHECK(!out.write.attempted);CHECK(out.diag.enumerationComplete);CHECK(fake::writeCalls==0);CHECK(fake::waitCalls==0);CHECK(fake::cancelCalls==0);CHECK(current::g_msiClawIoSlot.event==nullptr);});
 run("event-create-failure-no-write-and-caller-closes",[]{oneCandidate();fake::plan.eventOk=false;auto out=mode();apiDefaults(out.write);CHECK(out.write.state==State::IoDesignPending);CHECK(out.write.lastError==ERROR_ACCESS_DENIED);CHECK(out.write.attempted);CHECK(!out.write.handleRetained);CHECK(fake::writeCalls==0);CHECK(fake::closed.size()==1);CHECK(fake::closed.front()->kind==FakeResource::Device);CHECK(current::g_msiClawIoSlot.event==nullptr);});
 run("empty-slot-reap-has-no-completion-or-wait-call",[]{CHECK(current::msiClawIoSlotReapLocked());CHECK(fake::resultCalls==0);CHECK(fake::waitCalls==0);CHECK(fake::closed.empty());});
 run("immediate-shortwrite-does-not-copy-stale-last-error",[]{oneCandidate();SetLastError(433);fake::plan.results={{TRUE,23,0}};auto out=mode();apiCompleted(out.write,"immediate");CHECK(out.write.state==State::ShortWrite);CHECK(out.write.written==23);CHECK(out.write.lastError==0);CHECK(fake::writeCalls==1);});
 run("api-provenance-default-result-no-new-write-or-query",[]{current::MsiClawWriteResult r;apiDefaults(r);CHECK(!r.attempted);CHECK(fake::writeCalls==0);CHECK(fake::resultCalls==0);});
 run("timeout-cancel-query433-is-not-writefile433-aggregate-unchanged",[]{oneCandidate();fake::plan.start=fake::Pending;fake::plan.waits={WAIT_TIMEOUT,WAIT_OBJECT_0};fake::plan.results={{FALSE,0,433}};auto out=mode();auto r=out.write;
  apiReceipt(r,true,false,ERROR_IO_PENDING,true,false,433,"cancel-reap");CHECK(r.state==State::Retired);CHECK(r.lastError==ERROR_SUCCESS);CHECK(r.timedOut);CHECK(r.cancelAttempted);CHECK(r.handleRetained);CHECK(current::g_msiClawIoSlot.pending);CHECK(current::g_msiClawIoSlot.handle==fake::op.handle);CHECK(current::g_msiClawIoSlot.event==fake::op.event);CHECK(current::g_msiClawIoSlot.buffer.data()==fake::op.buffer);CHECK(!fake::op.handle->closed);CHECK(fake::writeCalls==1);CHECK(fake::cancelCalls==1);terminalReap(false,433);
 });
 run("cancel-not-found-late-success-preserves-legacy-aggregate-error",[]{oneCandidate();fake::plan.start=fake::Pending;fake::plan.cancelOk=false;fake::plan.waits={WAIT_TIMEOUT,WAIT_OBJECT_0};fake::plan.results={{TRUE,64,0}};auto out=mode();apiCompleted(out.write,"cancel-reap");CHECK(out.write.ok);CHECK(out.write.state==State::LateCompleted);CHECK(out.write.lastError==ERROR_NOT_FOUND);CHECK(out.write.completionQueryError==ERROR_SUCCESS);CHECK(out.write.written==64);CHECK(fake::writeCalls==1);CHECK(fake::cancelCalls==1);CHECK(fake::op.handle->closed);});
 run("prior-retired-reap-not-attributed-to-new-missing-device-receipt",[]{oneCandidate();fake::plan.start=fake::Pending;fake::plan.waits={WAIT_TIMEOUT,WAIT_TIMEOUT};fake::plan.results={{FALSE,0,ERROR_IO_INCOMPLETE}};auto old=mode();expectRetained(old.write);auto oldHandle=fake::op.handle,oldEvent=fake::op.event;auto oldOv=fake::op.ov;fake::candidates.clear();fake::plan.results={{TRUE,64,0}};auto out=mode();apiDefaults(out.write);CHECK(out.write.state==State::NoDevice);CHECK(!out.write.attempted);CHECK(fake::writeCalls==1);CHECK(fake::resultCalls==2);CHECK(fake::resultsHandles.back()==oldHandle);CHECK(fake::resultsOverlapped.back()==oldOv);CHECK(oldHandle->closed);CHECK(oldEvent->closed);CHECK(!current::g_msiClawIoSlot.pending);});
 std::cout<<"SUMMARY\t"<<cases<<"\tchecks="<<checks<<"\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL\tcase="<<(cases+1)<<"\t"<<e.what()<<"\n";return 1;}
}

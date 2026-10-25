// Own-process real Win32 pipe read cost; not the Host protocol or a real device.
// Keep original Runtime and kernel APIs. Peer scheduling is explicit input.
namespace latencyCost {
constexpr unsigned trials=9,calls=64,warmupCalls=8;
static unsigned long long word(FILETIME v){return(static_cast<unsigned long long>(v.dwHighDateTime)<<32)|v.dwLowDateTime;}
static unsigned long long threadCpu(){FILETIME c{},e{},k{},u{};if(!GetThreadTimes(GetCurrentThread(),&c,&e,&k,&u))throw std::runtime_error("latency-cost thread CPU unavailable");return word(k)+word(u);}
struct Peer {
 Pair& pair;std::vector<unsigned char> bytes;bool delayed;
 HANDLE start=nullptr;std::atomic<bool> stop{false};std::exception_ptr failure;std::thread worker;
 Peer(Pair& p,DWORD size,bool delay):pair(p),bytes(size,0x69),delayed(delay){
  if(!delayed)return;start=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!start)throw std::runtime_error("latency-cost peer event");
  try{worker=std::thread([this]{try{for(;;){const auto wait=WaitForSingleObject(start,5000);if(stop.load())return;if(wait!=WAIT_OBJECT_0)throw std::runtime_error("latency-cost private peer wait");Sleep(2);if(stop.load())return;
    if(peerIo(pair.client,true,bytes.data(),static_cast<DWORD>(bytes.size()))!=bytes.size())throw std::runtime_error("latency-cost peer short write");}}catch(...){failure=std::current_exception();}});}catch(...){CloseHandle(start);start=nullptr;throw;}
 }
 void request(){if(delayed){if(!SetEvent(start))throw std::runtime_error("latency-cost signal peer");}else if(peerIo(pair.client,true,bytes.data(),static_cast<DWORD>(bytes.size()))!=bytes.size())throw std::runtime_error("latency-cost preload failed");}
 void finish(){if(worker.joinable()){stop=true;SetEvent(start);worker.join();}if(start){CloseHandle(start);start=nullptr;}if(failure)std::rethrow_exception(failure);}
 ~Peer(){try{finish();}catch(...){}}
};
}
static void pipeLatencyCostCases(){
 using namespace latencyCost;
 for(unsigned trial=0;trial<trials;++trial)for(unsigned step=0;step<3;++step){
  const unsigned mode=(trial+step)%3;const char* name=mode==0?"prequeued-64":mode==1?"prequeued-4096":"delayed-peer-64";const DWORD bytes=mode==1?4096:64;
  Pair pair;pair.connect();Peer peer(pair,bytes,mode==2);std::vector<unsigned char> buffer(transport::kMessageBytes,0xA7);
  for(unsigned warm=0;warm<warmupCalls;++warm){peer.request();auto r=pair.owner->run(pair.server,transport::Kind::Read,buffer.data(),static_cast<DWORD>(buffer.size()),1000);if(!r.ok||r.transferred!=bytes)throw std::runtime_error("latency-cost warmup failed");}
  LARGE_INTEGER frequency{};if(!QueryPerformanceFrequency(&frequency)||frequency.QuadPart<=0)throw std::runtime_error("latency-cost frequency");
  std::uint64_t elapsedTicks=0,cycles=0;unsigned pending=0,inlineCompleted=0,waits=0;bool valid=true;
  const auto cpuBefore=threadCpu();const auto before=pair.owner->snapshot();
  for(unsigned i=0;i<calls;++i){
   peer.request();LARGE_INTEGER start{},end{};unsigned long long c0=0,c1=0;
   if(!QueryPerformanceCounter(&start)||!QueryThreadCycleTime(GetCurrentThread(),&c0))throw std::runtime_error("latency-cost begin counters");
   const auto read=pair.owner->run(pair.server,transport::Kind::Read,buffer.data(),static_cast<DWORD>(buffer.size()),1000);
   if(!QueryThreadCycleTime(GetCurrentThread(),&c1)||!QueryPerformanceCounter(&end)||c1<c0||end.QuadPart<start.QuadPart)throw std::runtime_error("latency-cost end counters");
   elapsedTicks+=static_cast<std::uint64_t>(end.QuadPart-start.QuadPart);cycles+=c1-c0;
   pending+=read.initialError==ERROR_IO_PENDING;inlineCompleted+=read.apiReturnedTrue;waits+=read.initialError==ERROR_IO_PENDING&&read.firstWait==WAIT_OBJECT_0;
   valid=valid&&read.ok&&read.byteCountKnown&&read.transferred==bytes&&read.terminal&&!read.retired&&!read.cancelAttempted&&
    std::all_of(buffer.begin(),buffer.begin()+bytes,[](unsigned char value){return value==0x69;});
  }
  const auto after=pair.owner->snapshot();const auto cpuAfter=threadCpu();peer.finish();
  check(valid,"latency-cost every actual read preserves payload and terminal byte count");
  check(before.available&&after.available&&after.pending==0&&after.payloadBytes==0&&after.admitted-before.admitted==calls&&after.reaped-before.reaped==calls,"latency-cost every measured actual operation completes and reaps");
  check(elapsedTicks>0&&cycles>0&&cpuAfter>=cpuBefore&&pending+inlineCompleted==calls,"latency-cost counters and actual issue paths complete without filtering");
  cases.push_back({{"case","real-private-pipe-latency-cost"},{"mode",name},{"trial",trial},{"calls",calls},{"warmupCalls",warmupCalls},{"receiptBytes",bytes},
   {"qpcFrequency",frequency.QuadPart},{"sumReadWallTicks",elapsedTicks},{"sumReadWallNs",static_cast<double>(elapsedTicks)*1.0e9/static_cast<double>(frequency.QuadPart)},
   {"sumReadThreadCycles",cycles},{"readerBatchCpu100ns",cpuAfter-cpuBefore},{"ioPendingReads",pending},{"inlineReads",inlineCompleted},{"completedWaits",waits},
   {"actualRuntime",true},{"realWin32Pipe",true},{"sameProcessPeer",true},{"normalProductOrDevicesUsed",false},{"payloadVerified",valid},
   {"peerDelayInputMs",mode==2?2:0},{"cpuWindow","parent read loop including peer signaling/preload and validation; separate from summed read scope"},
   {"readWindow","Runtime.run(Read) with QPC and thread-cycle instrumentation; no Host envelope, SDK or device"}});
 }
}
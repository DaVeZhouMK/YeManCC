#include <iterator>

// Private, disabled outside the three explicit production-control selectors.
#include <cstdint>
static std::atomic<bool> pcActive{false};
struct PCResourceRecord {unsigned kind=0;std::uintptr_t handle=0;LONG result=0;DWORD error=0,thread=0;};
static PCResourceRecord pcRecords[32]{};
static std::atomic<unsigned> pcUsed{0};
static std::atomic<bool> pcOverflow{false},pcGateTimedOut{false};
static std::atomic<DWORD> pcWorkerTid{0};
static std::atomic<unsigned> pcWaitingPublishes{0},pcStoppedPublishes{0},pcRunOneCalls{0},pcStopCommitCalls{0};
static HANDLE pcEntered=nullptr,pcRelease=nullptr;
static void pcNote(unsigned kind,HANDLE h,LONG result,DWORD error)noexcept {
 if(!pcActive.load(std::memory_order_acquire))return;
 const auto i=pcUsed.fetch_add(1,std::memory_order_relaxed);
 if(i<std::size(pcRecords))pcRecords[i]={kind,reinterpret_cast<std::uintptr_t>(h),result,error,GetCurrentThreadId()};else pcOverflow=true;
}
static HANDLE WINAPI pcCreateEventW(LPSECURITY_ATTRIBUTES a,BOOL m,BOOL s,LPCWSTR n){const auto h=::CreateEventW(a,m,s,n);const auto e=GetLastError();pcNote(1,h,h?1:0,e);SetLastError(e);return h;}
static BOOL WINAPI pcCloseHandle(HANDLE h){const auto r=::CloseHandle(h);const auto e=GetLastError();pcNote(2,h,r,e);SetLastError(e);return r;}

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <process.h>
#include <cstdint>
#include <cerrno>
static volatile LONG sfArm=0,sfBeginCalls=0,sfRefusals=0;
static std::uintptr_t __cdecl sfBeginThread(void* security,unsigned stack,unsigned (__stdcall* task)(void*),void* value,unsigned flags,unsigned* tid){
 InterlockedIncrement(&sfBeginCalls);
 if(InterlockedExchange(&sfArm,0)){InterlockedIncrement(&sfRefusals);errno=EAGAIN;SetLastError(ERROR_NOT_ENOUGH_MEMORY);return 0;}
 return ::_beginthreadex(security,stack,task,value,flags,tid);
}
#define _beginthreadex sfBeginThread
#include <thread>
#undef _beginthreadex

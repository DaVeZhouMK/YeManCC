#pragma once
#include <windows.h>
#include <vector>
#include <string>
// API observation/failure injection only. Every noninjected call reaches Win32.
// No expiry/ownership decision is duplicated here; actual Carrier owns those.
namespace carrierproof {
struct Call {const char* api; HWND hwnd; UINT_PTR id; UINT delay; UINT_PTR result; bool injected;};
inline std::vector<Call> calls;
inline HWND tracked=nullptr;
inline UINT_PTR lease=0;
inline bool failNext=false;
inline UINT_PTR WINAPI setTimer(HWND hwnd,UINT_PTR id,UINT delay,TIMERPROC callback){
 const bool injected=hwnd==tracked&&id==lease&&failNext;
 if(injected)failNext=false;
 const UINT_PTR result=injected ? 0 : ::SetTimer(hwnd,id,delay,callback);
 if(hwnd==tracked&&id==lease)calls.push_back({"SetTimer",hwnd,id,delay,result,injected});
 return result;
}
inline BOOL WINAPI killTimer(HWND hwnd,UINT_PTR id){
 const BOOL result=::KillTimer(hwnd,id);
 if(hwnd==tracked&&id==lease)calls.push_back({"KillTimer",hwnd,id,0,static_cast<UINT_PTR>(result),false});
 return result;
}
inline void reset(HWND hwnd,UINT_PTR id){tracked=hwnd;lease=id;failNext=false;calls.clear();}
inline size_t count(const char* api){size_t n=0;for(const auto& call:calls)if(std::string(call.api)==api)++n;return n;}
}

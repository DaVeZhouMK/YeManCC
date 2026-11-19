// Actual API observations only. API results and last-error values are retained.
// Maps contain only acquisitions made by the compiled request/typed bodies.
#include <map>
#include <set>
static std::map<HANDLE,unsigned> nativeObservedHandles;
static std::map<void*,SIZE_T> nativeObservedViews;
static std::set<HWND> nativeObservedWindows;
static std::set<HANDLE> nativeBaselineHandles;
static std::set<void*> nativeBaselineViews;
static std::set<HWND> nativeBaselineWindows;
static unsigned nativeResourceErrors=0,nativeHandleAcquires=0,nativeHandleReleases=0;
static unsigned nativeViewAcquires=0,nativeViewReleases=0,nativeWindowAcquires=0,nativeWindowReleases=0;
static void nativeTrackHandle(HANDLE value,unsigned kind) noexcept {
    if(!value||value==INVALID_HANDLE_VALUE)return;
    try{if(!nativeObservedHandles.emplace(value,kind).second)++nativeResourceErrors;else ++nativeHandleAcquires;}
    catch(...){++nativeResourceErrors;}
}
static HANDLE WINAPI nativeObserveOpenProcess(DWORD access,BOOL inherit,DWORD pid){
    HANDLE result=::OpenProcess(access,inherit,pid);const DWORD error=GetLastError();nativeTrackHandle(result,1);SetLastError(error);return result;
}
static BOOL WINAPI nativeObserveOpenProcessToken(HANDLE process,DWORD access,PHANDLE token){
    const BOOL result=::OpenProcessToken(process,access,token);const DWORD error=GetLastError();if(result&&token)nativeTrackHandle(*token,2);SetLastError(error);return result;
}
static HANDLE WINAPI nativeObserveCreateFile(LPCWSTR name,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES sa,DWORD creation,DWORD flags,HANDLE tmpl){
    HANDLE result=::CreateFileW(name,access,share,sa,creation,flags,tmpl);const DWORD error=GetLastError();nativeTrackHandle(result,3);SetLastError(error);return result;
}
static HANDLE WINAPI nativeObserveCreatePipe(LPCWSTR name,DWORD open,DWORD mode,DWORD count,DWORD out,DWORD in,DWORD timeout,LPSECURITY_ATTRIBUTES sa){
    HANDLE result=::CreateNamedPipeW(name,open,mode,count,out,in,timeout,sa);const DWORD error=GetLastError();nativeTrackHandle(result,4);SetLastError(error);return result;
}
static HANDLE WINAPI nativeObserveOpenMapping(DWORD access,BOOL inherit,LPCWSTR name){
    HANDLE result=::OpenFileMappingW(access,inherit,name);const DWORD error=GetLastError();nativeTrackHandle(result,5);SetLastError(error);return result;
}
static HANDLE WINAPI nativeObserveCreateMapping(HANDLE file,LPSECURITY_ATTRIBUTES sa,DWORD protect,DWORD high,DWORD low,LPCWSTR name){
    HANDLE result=::CreateFileMappingW(file,sa,protect,high,low,name);const DWORD error=GetLastError();nativeTrackHandle(result,6);SetLastError(error);return result;
}
static HANDLE WINAPI nativeObserveOpenEvent(DWORD access,BOOL inherit,LPCWSTR name){
    HANDLE result=::OpenEventW(access,inherit,name);const DWORD error=GetLastError();nativeTrackHandle(result,7);SetLastError(error);return result;
}
static HANDLE WINAPI nativeObserveCreateEvent(LPSECURITY_ATTRIBUTES sa,BOOL manual,BOOL initial,LPCWSTR name){
    HANDLE result=::CreateEventW(sa,manual,initial,name);const DWORD error=GetLastError();nativeTrackHandle(result,8);SetLastError(error);return result;
}
static LPVOID WINAPI nativeObserveMap(HANDLE mapping,DWORD access,DWORD high,DWORD low,SIZE_T bytes){
    LPVOID result=::MapViewOfFile(mapping,access,high,low,bytes);const DWORD error=GetLastError();
    if(result)try{if(!nativeObservedViews.emplace(result,bytes).second)++nativeResourceErrors;else ++nativeViewAcquires;}catch(...){++nativeResourceErrors;}
    SetLastError(error);return result;
}
static BOOL WINAPI nativeObserveUnmap(LPCVOID value){
    const BOOL result=::UnmapViewOfFile(value);const DWORD error=GetLastError();
    if(nativeObservedViews.contains(const_cast<void*>(value))){if(result){nativeObservedViews.erase(const_cast<void*>(value));++nativeViewReleases;}else ++nativeResourceErrors;}
    SetLastError(error);return result;
}
static BOOL WINAPI nativeObserveClose(HANDLE value){
    const BOOL result=::CloseHandle(value);const DWORD error=GetLastError();
    if(nativeObservedHandles.contains(value)){if(result){nativeObservedHandles.erase(value);++nativeHandleReleases;}else ++nativeResourceErrors;}
    SetLastError(error);return result;
}
static HWND WINAPI nativeObserveCreateWindow(DWORD ex,LPCWSTR cls,LPCWSTR name,DWORD style,int x,int y,int width,int height,HWND parent,HMENU menu,HINSTANCE instance,LPVOID param){
    HWND result=::CreateWindowExW(ex,cls,name,style,x,y,width,height,parent,menu,instance,param);const DWORD error=GetLastError();
    if(result)try{if(!nativeObservedWindows.insert(result).second)++nativeResourceErrors;else ++nativeWindowAcquires;}catch(...){++nativeResourceErrors;}
    SetLastError(error);return result;
}
static BOOL WINAPI nativeObserveDestroyWindow(HWND value){
    const BOOL result=::DestroyWindow(value);const DWORD error=GetLastError();
    if(nativeObservedWindows.contains(value)){if(result){nativeObservedWindows.erase(value);++nativeWindowReleases;}else ++nativeResourceErrors;}
    SetLastError(error);return result;
}
static void nativeObserveResourceBaseline(){
    nativeBaselineHandles.clear();for(const auto& row:nativeObservedHandles)nativeBaselineHandles.insert(row.first);
    nativeBaselineViews.clear();for(const auto& row:nativeObservedViews)nativeBaselineViews.insert(row.first);
    nativeBaselineWindows=nativeObservedWindows;
}
static bool nativeObservedResourcesReturned(){
    if(nativeResourceErrors||nativeObservedHandles.size()!=nativeBaselineHandles.size()||nativeObservedViews.size()!=nativeBaselineViews.size()||nativeObservedWindows!=nativeBaselineWindows)return false;
    for(const auto& row:nativeObservedHandles)if(!nativeBaselineHandles.contains(row.first))return false;
    for(const auto& row:nativeObservedViews)if(!nativeBaselineViews.contains(row.first))return false;
    return true;
}

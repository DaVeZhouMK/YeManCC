#pragma once
// The original media Worker owns all parsing, checks, files and transactions.
// These opt-in WinHTTP leaves never open a socket or make a DNS request.
#include <algorithm>
#include <atomic>
#include <string_view>
#include <cstring>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace mediaTransferFixture {
struct Handle { int kind; std::wstring path; size_t cursor=0; };
inline std::mutex mutex;
inline std::vector<Handle*> handles;
inline std::atomic<bool> enabled{false};
inline std::atomic<unsigned> opens{0},requests{0},metadataReads{0},assetReads{0},unsupported{0};
inline std::string metadata;
inline std::function<void()> beforeFirstAssetChunk;
inline Handle* find(HINTERNET value) {
    std::lock_guard lock(mutex);
    const auto it=std::find(handles.begin(),handles.end(),reinterpret_cast<Handle*>(value));
    return it==handles.end()?nullptr:*it;
}
inline HINTERNET make(int kind,std::wstring path={}) {
    auto* value=new Handle{kind,std::move(path)};
    std::lock_guard lock(mutex);handles.push_back(value);return reinterpret_cast<HINTERNET>(value);
}
inline size_t liveHandles(){std::lock_guard lock(mutex);return handles.size();}
inline void begin(std::string body,std::function<void()> action) {
    if(enabled.load()||liveHandles())throw std::runtime_error("fixture transport already owned");
    opens=0;requests=0;metadataReads=0;assetReads=0;unsupported=0;
    metadata=std::move(body);beforeFirstAssetChunk=std::move(action);enabled=true;
}
inline void end(){enabled=false;beforeFirstAssetChunk={};metadata.clear();}
inline HINTERNET WINAPI open(LPCWSTR agent,DWORD access,LPCWSTR proxy,LPCWSTR bypass,DWORD flags) {
    if(!enabled.load())return ::deniedNetwork(agent,access,proxy,bypass,flags);
    ++opens;return make(1);
}
inline HINTERNET WINAPI connect(HINTERNET session,LPCWSTR host,INTERNET_PORT port,DWORD reserved) {
    if(!enabled.load())return ::WinHttpConnect(session,host,port,reserved);
    const auto* value=find(session);
    if(!value||value->kind!=1||!host||port!=443){++unsupported;SetLastError(ERROR_INVALID_PARAMETER);return nullptr;}
    const std::wstring name(host);
    if(name!=L"store.steampowered.com"&&name!=L"steamstore-a.akamaihd.net"&&name!=L"cdn.akamai.steamstatic.com"){
        ++unsupported;SetLastError(ERROR_ACCESS_DENIED);return nullptr;
    }
    return make(2,name);
}
inline HINTERNET WINAPI openRequest(HINTERNET connection,LPCWSTR verb,LPCWSTR object,LPCWSTR,LPCWSTR,LPCWSTR const*,DWORD flags) {
    if(!enabled.load()||!find(connection)||!verb||std::wstring_view(verb)!=L"GET"||!object||!(flags&WINHTTP_FLAG_SECURE)){
        ++unsupported;SetLastError(ERROR_INVALID_PARAMETER);return nullptr;
    }
    const std::wstring target(object);
    if(!target.starts_with(L"/api/appdetails?")&&target!=L"/steam/apps/714/owned.jpg"){
        ++unsupported;SetLastError(ERROR_ACCESS_DENIED);return nullptr;
    }
    ++requests;return make(3,target);
}
inline BOOL WINAPI close(HINTERNET handle) {
    std::lock_guard lock(mutex);
    const auto it=std::find(handles.begin(),handles.end(),reinterpret_cast<Handle*>(handle));
    if(it==handles.end())return ::WinHttpCloseHandle(handle);
    delete *it;handles.erase(it);return TRUE;
}
inline BOOL WINAPI timeouts(HINTERNET handle,int,int,int,int){return find(handle)?TRUE:FALSE;}
inline BOOL WINAPI option(HINTERNET handle,DWORD,LPVOID,DWORD){return find(handle)?TRUE:FALSE;}
inline BOOL WINAPI headers(HINTERNET handle,LPCWSTR,DWORD,DWORD){return find(handle)?TRUE:FALSE;}
inline BOOL WINAPI send(HINTERNET handle,LPCWSTR,DWORD,LPVOID,DWORD,DWORD,DWORD_PTR){return find(handle)?TRUE:FALSE;}
inline BOOL WINAPI receive(HINTERNET handle,LPVOID){return find(handle)?TRUE:FALSE;}
inline BOOL WINAPI query(HINTERNET handle,DWORD field,LPCWSTR,LPVOID output,LPDWORD size,LPDWORD) {
    const auto* value=find(handle);
    if(!value||value->kind!=3||!size){++unsupported;SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
    const auto base=field&0xffff;
    if(base==WINHTTP_QUERY_STATUS_CODE&&(field&WINHTTP_QUERY_FLAG_NUMBER)){
        if(!output||*size<sizeof(DWORD)){*size=sizeof(DWORD);SetLastError(ERROR_INSUFFICIENT_BUFFER);return FALSE;}
        *static_cast<DWORD*>(output)=200;*size=sizeof(DWORD);return TRUE;
    }
    std::wstring text;
    if(base==WINHTTP_QUERY_CONTENT_LENGTH)text=std::to_wstring(value->path.starts_with(L"/api/")?metadata.size():65536);
    else if(base==WINHTTP_QUERY_ETAG)text=L"\"owned-active-transfer\"";
    else if(base==WINHTTP_QUERY_DATE)text=L"Sat, 14 Nov 2026 14:00:00 GMT";
    else {SetLastError(ERROR_WINHTTP_HEADER_NOT_FOUND);return FALSE;}
    const auto bytes=static_cast<DWORD>((text.size()+1)*sizeof(wchar_t));
    if(!output||*size<bytes){*size=bytes;SetLastError(ERROR_INSUFFICIENT_BUFFER);return FALSE;}
    std::memcpy(output,text.c_str(),bytes);*size=bytes-sizeof(wchar_t);return TRUE;
}
inline BOOL WINAPI read(HINTERNET handle,LPVOID output,DWORD capacity,LPDWORD readBytes) {
    auto* value=find(handle);
    if(!value||value->kind!=3||!output||!readBytes){++unsupported;SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
    *readBytes=0;
    if(value->path.starts_with(L"/api/")){
        ++metadataReads;const auto count=(std::min)(size_t(capacity),metadata.size()-value->cursor);
        std::memcpy(output,metadata.data()+value->cursor,count);value->cursor+=count;*readBytes=static_cast<DWORD>(count);return TRUE;
    }
    ++assetReads;
    if(value->cursor)return TRUE;
    if(beforeFirstAssetChunk)beforeFirstAssetChunk();
    static constexpr char bytes[]="owned-inflight-chunk";
    const auto count=(std::min)(size_t(capacity),sizeof(bytes)-1);
    std::memcpy(output,bytes,count);value->cursor=count;*readBytes=static_cast<DWORD>(count);return TRUE;
}
} // namespace mediaTransferFixture

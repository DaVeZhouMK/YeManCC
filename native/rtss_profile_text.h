#pragma once
#include <windows.h>
#include <cstdint>
#include <atomic>
#include <cwctype>
#include <string>
#include <utility>

// Explicit RTSS layout transactions only. Profile APIs accept UTF-16LE, but
// misread a UTF-8 BOM / UTF-16BE header and may append a second Settings section.
// Preserve the decoded document while making only those unambiguous encodings
// interoperable. The first raw preimage is a fixed, bounded backup, never a
// source of restore authority. No sampler, discovery, timer or retry loop.
namespace ymcc::rtssprofiletext {
inline constexpr std::size_t maxBytes=1U<<20;
inline constexpr std::size_t profileWriteHeadroom=256; // bounded Layout/section growth by the profile API
struct Handle {HANDLE value=INVALID_HANDLE_VALUE;explicit Handle(HANDLE v):value(v){}~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}Handle(const Handle&)=delete;};
struct Status {bool ok=true,changed=false,backupCreated=false;DWORD error=0;const char* code="ok";};
inline Status fail(const char*code,DWORD error=ERROR_INVALID_DATA){return {false,false,false,error,code};}
struct Snapshot {bool missing=false;std::string bytes;};
inline Status read(const std::wstring&path,Snapshot&out){
    Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_SEQUENTIAL_SCAN,nullptr));
    if(file.value==INVALID_HANDLE_VALUE){const DWORD e=GetLastError();if(e==ERROR_FILE_NOT_FOUND||e==ERROR_PATH_NOT_FOUND){out.missing=true;return {};}return fail("RTSS_CONFIG_READ_FAILED",e);}
    BY_HANDLE_FILE_INFORMATION before{};
    if(GetFileType(file.value)!=FILE_TYPE_DISK)return fail("RTSS_CONFIG_NOT_REGULAR_FILE");
    if(!GetFileInformationByHandle(file.value,&before))return fail("RTSS_CONFIG_READ_FAILED",GetLastError());
    if(before.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))return fail("RTSS_CONFIG_NOT_REGULAR_FILE");
    const auto size=(std::uint64_t(before.nFileSizeHigh)<<32)|before.nFileSizeLow;
    if(size>maxBytes)return fail("RTSS_CONFIG_TOO_LARGE",ERROR_FILE_TOO_LARGE);
    out.bytes.assign(static_cast<std::size_t>(size),'\0');DWORD got=0;
    if(!out.bytes.empty()){
        if(!ReadFile(file.value,out.bytes.data(),static_cast<DWORD>(out.bytes.size()),&got,nullptr))return fail("RTSS_CONFIG_READ_FAILED",GetLastError());
        if(got!=out.bytes.size())return fail("RTSS_CONFIG_READ_INCOMPLETE",ERROR_READ_FAULT);
    }
    char extra=0;if(!ReadFile(file.value,&extra,1,&got,nullptr))return fail("RTSS_CONFIG_READ_FAILED",GetLastError());
    if(got)return fail("RTSS_CONFIG_CHANGED");
    BY_HANDLE_FILE_INFORMATION after{};
    if(!GetFileInformationByHandle(file.value,&after)||before.nFileSizeHigh!=after.nFileSizeHigh||before.nFileSizeLow!=after.nFileSizeLow||
        CompareFileTime(&before.ftLastWriteTime,&after.ftLastWriteTime)!=0)return fail("RTSS_CONFIG_CHANGED");
    return {};
}
enum class Encoding {Unmarked,Utf8Bom,Utf16LE,Utf16BE};
inline Encoding encoding(const std::string&raw){
    if(raw.compare(0,3,"\xef\xbb\xbf")==0)return Encoding::Utf8Bom;
    if(raw.compare(0,2,"\xff\xfe")==0)return Encoding::Utf16LE;
    if(raw.compare(0,2,"\xfe\xff")==0)return Encoding::Utf16BE;
    return Encoding::Unmarked;
}
inline bool validWide(const std::wstring&text){
    for(std::size_t i=0;i<text.size();++i){const unsigned v=text[i];if(!v)return false;
        if(v>=0xd800&&v<=0xdbff){if(++i==text.size()||text[i]<0xdc00||text[i]>0xdfff)return false;}
        else if(v>=0xdc00&&v<=0xdfff)return false;
    }return true;
}
inline Status decode(const std::string&raw,std::wstring&text){
    const auto kind=encoding(raw);
    if(kind==Encoding::Utf8Bom){
        const auto n=static_cast<int>(raw.size()-3);if(!n)return {};
        const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,raw.data()+3,n,nullptr,0);
        if(!count)return fail("RTSS_CONFIG_ENCODING_INVALID",ERROR_NO_UNICODE_TRANSLATION);
        text.resize(count);if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,raw.data()+3,n,text.data(),count)!=count)return fail("RTSS_CONFIG_ENCODING_INVALID",ERROR_NO_UNICODE_TRANSLATION);
    }else if(kind==Encoding::Utf16LE||kind==Encoding::Utf16BE){
        if((raw.size()-2)%2)return fail("RTSS_CONFIG_ENCODING_INVALID");
        text.reserve((raw.size()-2)/2);
        for(std::size_t i=2;i<raw.size();i+=2){const unsigned a=static_cast<unsigned char>(raw[i]),b=static_cast<unsigned char>(raw[i+1]);text.push_back(static_cast<wchar_t>(kind==Encoding::Utf16LE?a|(b<<8):(a<<8)|b));}
    }else{
        // Section/key/layout names are ASCII. Keep unmarked non-ASCII bytes
        // opaque: guessing UTF-8 here could change an ANSI user's settings.
        text.reserve(raw.size());for(unsigned char c:raw)text.push_back(c);
    }
    if(!validWide(text))return fail("RTSS_CONFIG_ENCODING_INVALID");return {};
}
inline std::string utf16le(const std::wstring&text){
    std::string bytes="\xff\xfe";bytes.reserve(2+text.size()*2);
    for(unsigned c:text){bytes.push_back(static_cast<char>(c&255));bytes.push_back(static_cast<char>(c>>8));}return bytes;
}
inline Status saveFirstBackup(const std::wstring&path,const std::string&bytes,bool&created){
    static std::atomic<unsigned long long> sequence{0};
    const auto temporary=path+L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(++sequence);
    HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr);
    if(file==INVALID_HANDLE_VALUE)return fail("RTSS_CONFIG_BACKUP_FAILED",GetLastError());
    DWORD got=0,error=ERROR_SUCCESS;
    if(!bytes.empty()){
        if(!WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&got,nullptr))error=GetLastError();
        else if(got!=bytes.size())error=ERROR_WRITE_FAULT;
    }
    if(error==ERROR_SUCCESS&&!FlushFileBuffers(file))error=GetLastError();
    if(!CloseHandle(file)&&error==ERROR_SUCCESS)error=GetLastError();
    if(error!=ERROR_SUCCESS){DeleteFileW(temporary.c_str());return fail("RTSS_CONFIG_BACKUP_FAILED",error);}
    if(MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH)){created=true;return {};}
    error=GetLastError();DeleteFileW(temporary.c_str());
    if(error==ERROR_FILE_EXISTS||error==ERROR_ALREADY_EXISTS)return {}; // retain the first preimage
    return fail("RTSS_CONFIG_BACKUP_FAILED",error);
}
template<class Commit> Status normalize(const std::wstring&path,Commit&&commit){
    Snapshot original;auto result=read(path,original);if(!result.ok||original.missing)return result;
    if(original.bytes.size()>maxBytes-profileWriteHeadroom)return fail("RTSS_CONFIG_TOO_LARGE",ERROR_FILE_TOO_LARGE);
    std::wstring text;result=decode(original.bytes,text);if(!result.ok)return result;
    const auto kind=encoding(original.bytes);if(kind!=Encoding::Utf8Bom&&kind!=Encoding::Utf16BE)return {};
    if(text.size()>(maxBytes-profileWriteHeadroom-2)/2)return fail("RTSS_CONFIG_TOO_LARGE",ERROR_FILE_TOO_LARGE);
    const auto converted=utf16le(text);std::wstring roundtrip;result=decode(converted,roundtrip);
    if(!result.ok||text!=roundtrip)return fail("RTSS_CONFIG_ENCODING_ROUNDTRIP_FAILED");
    const auto backup=path+L".ymcc-encoding-original.bak";bool created=false;
    result=saveFirstBackup(backup,original.bytes,created);if(!result.ok)return result;
    Snapshot saved;result=read(backup,saved);if(!result.ok||saved.missing)return fail("RTSS_CONFIG_BACKUP_FAILED",result.error);
    if(created&&saved.bytes!=original.bytes)return fail("RTSS_CONFIG_BACKUP_VERIFY_FAILED");
    Snapshot current;result=read(path,current);if(!result.ok)return result;
    if(current.missing||current.bytes!=original.bytes)return fail("RTSS_CONFIG_CHANGED");
    // The main's existing unique-temp atomic writer is reused. This observed
    // preimage check is not a cross-process compare-and-swap guarantee.
    if(!commit(path,converted))return fail("RTSS_CONFIG_WRITE_FAILED",GetLastError());
    Snapshot verified;result=read(path,verified);if(!result.ok)return result;
    if(verified.missing||verified.bytes!=converted)return fail("RTSS_CONFIG_VERIFY_FAILED");
    result.changed=true;result.backupCreated=created;return result;
}
inline std::wstring trim(std::wstring value){const auto space=[](wchar_t c){return c==0xfeff||iswspace(c)!=0;};std::size_t first=0,last=value.size();while(first<last&&space(value[first]))++first;while(last>first&&space(value[last-1]))--last;return value.substr(first,last-first);}
inline bool equalAscii(const std::wstring&left,const wchar_t*right){std::wstring a=left,b=right;for(auto&c:a)if(c>=L'A'&&c<=L'Z')c+=L'a'-L'A';for(auto&c:b)if(c>=L'A'&&c<=L'Z')c+=L'a'-L'A';return a==b;}
inline Status verifyLayout(const std::wstring&path,const std::string&layout){
    Snapshot snapshot;auto result=read(path,snapshot);if(!result.ok)return result;if(snapshot.missing)return fail("RTSS_CONFIG_LAYOUT_MISSING",ERROR_FILE_NOT_FOUND);
    std::wstring text;result=decode(snapshot.bytes,text);if(!result.ok)return result;
    bool settings=false;std::size_t at=0;const std::wstring wanted(layout.begin(),layout.end());
    while(at<=text.size()){
        const auto end=text.find(L'\n',at);auto line=trim(text.substr(at,end==std::wstring::npos?text.size()-at:end-at));
        if(line.size()>=2&&line.front()==L'['&&line.back()==L']')settings=equalAscii(line.substr(1,line.size()-2),L"Settings");
        else if(settings){const auto eq=line.find(L'=');if(eq!=std::wstring::npos&&equalAscii(trim(line.substr(0,eq)),L"Layout"))return trim(line.substr(eq+1))==wanted?Status{}:fail("RTSS_CONFIG_LAYOUT_MISMATCH");}
        if(end==std::wstring::npos)break;at=end+1;
    }return fail("RTSS_CONFIG_LAYOUT_MISSING");
}
}

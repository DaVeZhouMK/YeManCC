#pragma once
#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace ymcc::logexport {
struct ArchiveCommandPlan {
    bool ok=false;
    std::wstring command,inputPath;
    std::string reason;
    std::string mode="not-planned";
    DWORD error=ERROR_SUCCESS;
    unsigned fileLists=0,listEntries=0,wideArgumentEntries=0;
};
// Windows bsdtar's -T reader consumes the active ANSI code page. Never replace
// unrepresentable names: those entries keep the existing wide argv path.
inline bool archiveListBytes(const std::wstring& text,std::string& bytes) {
    if(text.size()>static_cast<size_t>((std::numeric_limits<int>::max)()))return false;
    const UINT cp=GetACP();BOOL replaced=FALSE;
    const DWORD flags=cp==CP_UTF8?WC_ERR_INVALID_CHARS:WC_NO_BEST_FIT_CHARS;
    const int size=WideCharToMultiByte(cp,flags,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,cp==CP_UTF8?nullptr:&replaced);
    if(size<=0||replaced)return false;
    bytes.resize(static_cast<size_t>(size));replaced=FALSE;
    if(WideCharToMultiByte(cp,flags,text.data(),static_cast<int>(text.size()),bytes.data(),size,nullptr,cp==CP_UTF8?nullptr:&replaced)!=size||replaced)return false;
    const int wideSize=MultiByteToWideChar(cp,MB_ERR_INVALID_CHARS,bytes.data(),size,nullptr,0);
    if(wideSize<=0)return false;
    std::wstring roundtrip(static_cast<size_t>(wideSize),L'\0');
    return MultiByteToWideChar(cp,MB_ERR_INVALID_CHARS,bytes.data(),size,roundtrip.data(),wideSize)==wideSize&&roundtrip==text;
}
inline bool archiveListSafeText(const std::wstring& text) {
    return text.find_first_of(L"\r\n\0",0,3)==std::wstring::npos&&text.find(L'"')==std::wstring::npos;
}
// A stable partition keeps every same-name occurrence in source order. The
// archive/index sequence is packaging order, never an event chronology. A single
// -T must precede bare wide names because bsdtar stops parsing -T after them.
inline bool archiveNameListEncodable(const std::pair<std::wstring,std::wstring>& entry) {
    std::string bytes;return archiveListBytes(entry.second,bytes);
}
inline void orderArchiveEntries(std::vector<std::pair<std::wstring,std::wstring>>& entries) {
    std::stable_partition(entries.begin(),entries.end(),archiveNameListEncodable);
}
inline ArchiveCommandPlan buildArchiveCommand(const std::wstring& output,const std::wstring& staging,
        const std::vector<std::pair<std::wstring,std::wstring>>& entries) {
    namespace fs=std::filesystem;
    ArchiveCommandPlan plan;
    const auto quote=[](const std::wstring& value){return L"\""+value+L"\"";};
    if(!archiveListSafeText(output)||!archiveListSafeText(staging)){plan.reason="archive-invalid-path";plan.error=ERROR_INVALID_NAME;return plan;}
    const std::wstring base=L"\"C:\\Windows\\System32\\tar.exe\" --options zip:hdrcharset=UTF-8 --format=zip -c -f -";
    const fs::path root=fs::path(staging).lexically_normal();fs::path current=root;
    auto ordered=entries;orderArchiveEntries(ordered);
    std::wstring names;std::string bytes;bool firstWide=true;
    plan.command=base+L" -T -";
    for(const auto& entry:ordered) {
        const auto directory=fs::path(entry.first).lexically_normal();const auto fromRoot=directory.lexically_relative(root);
        if(fromRoot.empty()||fromRoot.is_absolute()||*fromRoot.begin()==L".."||!archiveListSafeText(entry.first)||
                !archiveListSafeText(entry.second)||entry.second.empty()||entry.second.front()==L'-'||entry.second.front()==L'@'||
                fs::path(entry.second).has_parent_path()||entry.second==L"."||entry.second==L"..") {plan.reason="archive-list-invalid-entry";plan.error=ERROR_INVALID_NAME;return plan;}
        auto relative=directory.lexically_relative(current).generic_wstring();if(relative.empty())relative=L".";
        if(archiveNameListEncodable(entry)) {
            names+=L"-C\n"+relative+L"\n"+fs::path(entry.second).generic_wstring()+L"\n";++plan.listEntries;
        } else {
            // Let the list position the first wide name. Further -C operands
            // are handled by bsdtar's create loop, without a second -T segment.
            if(firstWide){names+=L"-C\n"+relative+L"\n";firstWide=false;}
            else plan.command+=L" -C "+quote(relative);
            plan.command+=L" "+quote(entry.second);++plan.wideArgumentEntries;
        }
        current=directory;
    }
    if(!archiveListBytes(names,bytes)){plan.reason="archive-list-path-unrepresentable";plan.error=ERROR_NO_UNICODE_TRANSLATION;return plan;}
    if(plan.command.size()+1>32767){plan.reason="archive-command-too-long";plan.error=ERROR_FILENAME_EXCED_RANGE;return plan;}
    plan.inputPath=(root/L"archive-inputs.txt").wstring();
    HANDLE file=CreateFileW(plan.inputPath.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE){plan.reason="archive-list-create-failed";plan.error=GetLastError();return plan;}
    size_t offset=0;bool ok=true;DWORD error=ERROR_SUCCESS;
    while(offset<bytes.size()) {
        const DWORD chunk=static_cast<DWORD>((std::min)(bytes.size()-offset,static_cast<size_t>(1024*1024)));DWORD written=0;
        if(!WriteFile(file,bytes.data()+offset,chunk,&written,nullptr)||written!=chunk){ok=false;error=GetLastError();if(!error)error=ERROR_WRITE_FAULT;break;}
        offset+=written;
    }
    if(!CloseHandle(file)&&ok){ok=false;error=GetLastError();}
    if(!ok){plan.reason="archive-list-write-failed";plan.error=error;return plan;}
    plan.fileLists=1;plan.mode=plan.wideArgumentEntries?"ansi-file-list-and-wide-relative-argv":"ansi-file-list-stdin";
    plan.ok=true;plan.reason="ok";return plan;
}
} // namespace ymcc::logexport

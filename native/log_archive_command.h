#pragma once
#include <windows.h>
#include <wincrypt.h>
#include <algorithm>
#include <filesystem>
#include <iterator>
#include <limits>
#include <string>
#include <utility>
#include <vector>
#pragma comment(lib,"crypt32.lib")

namespace ymcc::logexport {
enum class ZipCompression { tarDefault, level5 };

struct ArchiveCommandPlan {
    bool ok=false,unicodeZip=false;
    std::wstring command,inputPath;
    std::string reason;
    std::string mode="not-planned";
    DWORD error=ERROR_SUCCESS;
    unsigned fileLists=0,listEntries=0,wideArgumentEntries=0;
};
inline bool archiveListBytesForCodePage(const std::wstring& text,std::string& bytes,UINT cp) {
    if(text.size()>static_cast<size_t>((std::numeric_limits<int>::max)()))return false;
    BOOL replaced=FALSE;
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
inline bool archiveListBytes(const std::wstring& text,std::string& bytes) {
    return archiveListBytesForCodePage(text,bytes,GetACP());
}
inline bool archiveListSafeText(const std::wstring& text) {
    return text.find_first_of(L"\r\n\0",0,3)==std::wstring::npos&&text.find(L'"')==std::wstring::npos;
}
// Keep existing member order. Supplementary characters also go through the
// Unicode writer on machines whose ANSI code page happens to be UTF-8.
inline bool archiveNameListEncodable(const std::wstring& name) {
    const auto cp=GetACP();
    if(cp==CP_UTF8&&std::any_of(name.begin(),name.end(),[](wchar_t ch){return 0xd800<=ch&&ch<=0xdfff;}))return false;
    std::string bytes;return archiveListBytesForCodePage(name,bytes,cp);
}
inline size_t orderArchiveEntries(std::vector<std::pair<std::wstring,std::wstring>>& entries) {
    const auto split=std::stable_partition(entries.begin(),entries.end(),[](const auto& entry){return archiveNameListEncodable(entry.second);});
    return static_cast<size_t>(split-entries.begin());
}
inline bool archiveMemberValid(const std::wstring& name) {
    return archiveListSafeText(name)&&!name.empty()&&name.front()!=L'-'&&name.front()!=L'@'&&
        !std::filesystem::path(name).has_parent_path()&&name!=L"."&&name!=L"..";
}
inline bool writeArchiveInput(ArchiveCommandPlan& plan,const std::wstring& path,const std::string& bytes) {
    plan.inputPath=path;
    HANDLE file=CreateFileW(plan.inputPath.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE){plan.reason="archive-list-create-failed";plan.error=GetLastError();return false;}
    size_t offset=0;bool ok=true;DWORD error=ERROR_SUCCESS;
    while(offset<bytes.size()) {
        const DWORD chunk=static_cast<DWORD>((std::min)(bytes.size()-offset,static_cast<size_t>(1024*1024)));DWORD written=0;
        if(!WriteFile(file,bytes.data()+offset,chunk,&written,nullptr)||written!=chunk){ok=false;error=GetLastError();if(!error)error=ERROR_WRITE_FAULT;break;}
        offset+=written;
    }
    if(!CloseHandle(file)&&ok){ok=false;error=GetLastError();}
    if(!ok){plan.reason="archive-list-write-failed";plan.error=error;return false;}
    plan.fileLists=1;return true;
}

// The script is fixed. All filenames travel as UTF-8 data over the inherited
// plan handle, and the child opens only the attempt's private CREATE_NEW ZIP.
inline constexpr wchar_t unicodeZipScript[]=LR"PS($ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';try{
Add-Type -AssemblyName System.IO.Compression,System.IO.Compression.FileSystem;
$utf8=[Text.UTF8Encoding]::new($false,$true);
$reader=[IO.StreamReader]::new([Console]::OpenStandardInput(),$utf8,$false);
$output=[IO.File]::Open($reader.ReadLine(),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read);
try{
$zip=[IO.Compression.ZipArchive]::new($output,[IO.Compression.ZipArchiveMode]::Create,$true,[Text.Encoding]::UTF8);
try{while($null -ne ($source=$reader.ReadLine())){
$member=$reader.ReadLine();if([string]::IsNullOrEmpty($member)){throw 'invalid-list';}
if($member.EndsWith('/')){[void]$zip.CreateEntry($member);continue;}
[void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,$source,$member,[IO.Compression.CompressionLevel]::Optimal);
}}finally{$zip.Dispose();}
}finally{$output.Dispose();}
}catch{[Console]::Error.WriteLine($_.Exception.GetBaseException().HResult);exit 1;};exit 0)PS";
inline std::wstring unicodeZipNativePath(const std::filesystem::path& path) {
    const auto& text=path.native();
    if(text.rfind(L"\\\\?\\",0)==0)return text;
    if(text.rfind(L"\\\\",0)==0)return L"\\\\?\\UNC\\"+text.substr(2);
    return L"\\\\?\\"+text;
}
inline ArchiveCommandPlan buildUnicodeZipCommand(const std::wstring& output,const std::wstring& staging,
        const std::vector<std::pair<std::wstring,std::wstring>>& ordered) {
    namespace fs=std::filesystem;
    ArchiveCommandPlan plan;plan.unicodeZip=true;plan.mode="unicode-zip-utf8-list";
    const auto root=fs::path(staging).lexically_normal();
    const auto target=fs::path(output).lexically_normal();
    const auto invalid=[&](DWORD error){plan.reason="unicode-zip-invalid-entry";plan.error=error;return plan;};
    if(!root.is_absolute()||target.parent_path()!=root||!archiveListSafeText(output)||!archiveListSafeText(staging))return invalid(ERROR_INVALID_NAME);
    std::wstring list=unicodeZipNativePath(target)+L'\n';
    auto append=[&](const fs::path& path,const fs::path& member,DWORD attributes) {
        const auto name=member.generic_wstring();
        if(attributes==INVALID_FILE_ATTRIBUTES||attributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DEVICE)||
            !archiveListSafeText(path.native())||!archiveListSafeText(name))return false;
        list+=unicodeZipNativePath(path)+L'\n'+name+((attributes&FILE_ATTRIBUTE_DIRECTORY)?L"/\n":L"\n");
        ++plan.listEntries;return true;
    };
    for(const auto& [directory,name]:ordered) {
        const auto dir=fs::path(directory).lexically_normal();const auto relative=dir.lexically_relative(root);
        if(relative.empty()||relative.is_absolute()||*relative.begin()==L".."||!archiveMemberValid(name))return invalid(ERROR_INVALID_NAME);
        const auto source=dir/name;
        const DWORD attributes=GetFileAttributesW(unicodeZipNativePath(source).c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES)return invalid(GetLastError());
        if(!append(source,name,attributes))return invalid(ERROR_INVALID_NAME);
        if(!(attributes&FILE_ATTRIBUTE_DIRECTORY))continue;
        std::error_code ec;
        for(fs::recursive_directory_iterator it(unicodeZipNativePath(source),ec),end;!ec&&it!=end;it.increment(ec)) {
            const auto childAttributes=GetFileAttributesW(it->path().c_str());
            if(childAttributes==INVALID_FILE_ATTRIBUTES)return invalid(GetLastError());
            const auto child=it->path().lexically_relative(unicodeZipNativePath(source));
            if(child.empty()||child.is_absolute()||*child.begin()==L".."||!append(it->path(),fs::path(name)/child,childAttributes))return invalid(ERROR_INVALID_NAME);
        }
        if(ec)return invalid(static_cast<DWORD>(ec.value()));
    }
    std::string bytes;
    if(!archiveListBytesForCodePage(list,bytes,CP_UTF8))return invalid(ERROR_NO_UNICODE_TRANSLATION);
    wchar_t system[MAX_PATH+1]{};
    const auto systemUnits=GetSystemDirectoryW(system,static_cast<UINT>(std::size(system)));
    if(!systemUnits||systemUnits>=std::size(system)) {
        plan.reason="unicode-zip-command-unavailable";plan.error=systemUnits?ERROR_INSUFFICIENT_BUFFER:GetLastError();return plan;
    }
    const auto* script=reinterpret_cast<const BYTE*>(unicodeZipScript);
    constexpr DWORD scriptBytes=sizeof(unicodeZipScript)-sizeof(wchar_t);
    constexpr DWORD flags=CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF;DWORD units=0;
    if(!CryptBinaryToStringW(script,scriptBytes,flags,nullptr,&units)) {
        plan.reason="unicode-zip-command-unavailable";plan.error=GetLastError();return plan;
    }
    std::wstring encoded(units,L'\0');
    if(!CryptBinaryToStringW(script,scriptBytes,flags,encoded.data(),&units)) {
        plan.reason="unicode-zip-command-unavailable";plan.error=GetLastError();return plan;
    }
    encoded.resize(units);
    plan.command=L"\""+std::wstring(system,systemUnits)+L"\\WindowsPowerShell\\v1.0\\powershell.exe\" -NoLogo -NoProfile -NonInteractive -EncodedCommand "+encoded;
    if(!writeArchiveInput(plan,unicodeZipNativePath(root/L"archive-inputs.txt"),bytes))return plan;
    plan.ok=true;plan.reason="ok";return plan;
}
inline ArchiveCommandPlan buildOrderedArchiveCommand(const std::wstring& output,const std::wstring& staging,
        const std::vector<std::pair<std::wstring,std::wstring>>& ordered,size_t listEntryCount,
        ZipCompression compression=ZipCompression::tarDefault,bool forceUnicodeZip=false) {
    namespace fs=std::filesystem;
    ArchiveCommandPlan plan;
    if(!archiveListSafeText(output)||!archiveListSafeText(staging)){plan.reason="archive-invalid-path";plan.error=ERROR_INVALID_NAME;return plan;}
    if(listEntryCount>ordered.size()){plan.reason="archive-list-invalid-entry";plan.error=ERROR_INVALID_NAME;return plan;}
    if(forceUnicodeZip||listEntryCount<ordered.size())return buildUnicodeZipCommand(output,staging,ordered);
    std::wstring base=L"\"C:\\Windows\\System32\\tar.exe\" --options zip:hdrcharset=UTF-8";
    if(compression==ZipCompression::level5)base+=L",zip:compression-level=5";
    base+=L" --format=zip -c -f -";
    const fs::path root=fs::path(staging).lexically_normal();fs::path current=root;
    std::wstring names;std::string bytes;
    plan.command=base+L" -T -";
    for(const auto& entry:ordered) {
        const auto directory=fs::path(entry.first).lexically_normal();const auto fromRoot=directory.lexically_relative(root);
        if(fromRoot.empty()||fromRoot.is_absolute()||*fromRoot.begin()==L".."||!archiveListSafeText(entry.first)||
                !archiveMemberValid(entry.second)) {plan.reason="archive-list-invalid-entry";plan.error=ERROR_INVALID_NAME;return plan;}
        auto relative=directory.lexically_relative(current).generic_wstring();if(relative.empty())relative=L".";
        names+=L"-C\n"+relative+L"\n"+fs::path(entry.second).generic_wstring()+L"\n";++plan.listEntries;
        current=directory;
    }
    if(!archiveListBytes(names,bytes)){plan.reason="archive-list-path-unrepresentable";plan.error=ERROR_NO_UNICODE_TRANSLATION;return plan;}
    if(plan.command.size()+1>32767){plan.reason="archive-command-too-long";plan.error=ERROR_FILENAME_EXCED_RANGE;return plan;}
    if(!writeArchiveInput(plan,(root/L"archive-inputs.txt").wstring(),bytes))return plan;
    plan.mode="ansi-file-list-stdin";
    plan.ok=true;plan.reason="ok";return plan;
}
inline ArchiveCommandPlan buildArchiveCommand(const std::wstring& output,const std::wstring& staging,
        const std::vector<std::pair<std::wstring,std::wstring>>& entries,
        ZipCompression compression=ZipCompression::tarDefault) {
    auto ordered=entries;
    const auto listEntryCount=orderArchiveEntries(ordered);
    return buildOrderedArchiveCommand(output,staging,ordered,listEntryCount,compression);
}
} // namespace ymcc::logexport

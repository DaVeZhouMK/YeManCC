#pragma once
// Win32 file boundary: actual main WriteAtomic and JSON Store run unchanged.
// Only evidence-private files are touched. This does not prove Win32 atomicity.
#include <functional>
#include <memory>
namespace fspath=std::filesystem;
using HANDLE=void*;
static void* const INVALID_HANDLE_VALUE=reinterpret_cast<void*>(static_cast<std::intptr_t>(-1));
static constexpr DWORD GENERIC_WRITE=0x40000000,CREATE_NEW=1,FILE_ATTRIBUTE_NORMAL=0x80,
    FILE_FLAG_WRITE_THROUGH=0x80000000,MOVEFILE_REPLACE_EXISTING=1,MOVEFILE_WRITE_THROUGH=8;
struct CalibrationFile { std::filesystem::path path;std::ofstream stream; };
static std::map<void*,std::unique_ptr<CalibrationFile>> calibrationFiles;
static unsigned fileFault=0;
static std::vector<json> calibrationEvents;
static std::function<void()> afterReplaceHook;
static void calibrationBoundaryReset(){calibrationFiles.clear();fileFault=0;calibrationEvents.clear();afterReplaceHook={};}
static void gamepadSerialPostEvent(const char* event,const json& value){calibrationEvents.push_back({{"event",event},{"value",value}});}
static DWORD GetCurrentProcessId(){return 4242;}
static HANDLE CreateFileW(const wchar_t* path,DWORD access,DWORD share,void* security,DWORD disposition,DWORD flags,void* templ){
    const std::filesystem::path p(path);CHECK(p.parent_path()==calibrationPath.parent_path());
    CHECK(access==GENERIC_WRITE&&share==0&&security==nullptr&&disposition==CREATE_NEW&&
        flags==(FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH)&&templ==nullptr);
    effects.push_back({{"case",activeCase},{"event","file-create"}});if(fileFault==1)return INVALID_HANDLE_VALUE;
    auto f=std::make_unique<CalibrationFile>();f->path=p;f->stream.open(p,std::ios::binary|std::ios::trunc);
    if(!f->stream)return INVALID_HANDLE_VALUE;void* h=f.get();calibrationFiles.emplace(h,std::move(f));return h;}
static BOOL WriteFile(HANDLE h,const void* data,DWORD size,DWORD* written,void*){
    const auto at=calibrationFiles.find(h);CHECK(at!=calibrationFiles.end());if(at==calibrationFiles.end())return FALSE;
    if(fileFault==2){*written=0;return FALSE;}*written=fileFault==3&&size?size-1:size;
    at->second->stream.write(static_cast<const char*>(data),*written);return at->second->stream.good()?1:0;}
static BOOL FlushFileBuffers(HANDLE h){const auto at=calibrationFiles.find(h);CHECK(at!=calibrationFiles.end());
    if(at==calibrationFiles.end())return FALSE;at->second->stream.flush();return fileFault==4?FALSE:1;}
static BOOL CloseHandle(HANDLE h){const auto at=calibrationFiles.find(h);CHECK(at!=calibrationFiles.end());
    if(at==calibrationFiles.end())return FALSE;at->second->stream.close();calibrationFiles.erase(at);return fileFault==5?FALSE:1;}
static BOOL MoveFileExW(const wchar_t* from,const wchar_t* to,DWORD flags){
    const std::filesystem::path source(from),target(to);CHECK(source.parent_path()==calibrationPath.parent_path()&&target==calibrationPath);
    CHECK(flags==(MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH));effects.push_back({{"case",activeCase},{"event","file-replace-attempt"}});
    if(fileFault==6)return FALSE;std::ifstream in(source,std::ios::binary);std::string content((std::istreambuf_iterator<char>(in)),{});in.close();
    std::ofstream out(target,std::ios::binary|std::ios::trunc);out<<content;out.close();if(!out)return FALSE;
    std::error_code ec;std::filesystem::remove(source,ec);effects.push_back({{"case",activeCase},{"event","file-replaced"}});
    if(afterReplaceHook)afterReplaceHook();return 1;}
static BOOL DeleteFileW(const wchar_t* path){const std::filesystem::path p(path);CHECK(p.parent_path()==calibrationPath.parent_path());
    std::error_code ec;const bool removed=std::filesystem::remove(p,ec);return removed&&!ec?1:0;}
static void controllerResetCalibration(void* h){if(handle(h))bridge_reset(h);}
static float controllerGetConfidence(void* h){return handle(h)?bridge_confidence(h):0;}
static void controllerGetOffset(void* h,float* x,float* y,float* z){if(handle(h))bridge_get_offset(h,x,y,z);}
static auto g_gmResetCal=&controllerResetCalibration;
static auto g_gmGetConf=&controllerGetConfidence;
static auto g_gmGetOffset=&controllerGetOffset;

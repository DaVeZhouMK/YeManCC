#include "json.hpp"
#include "wake_game_resume.h"
int originalProbe(int argc,char** argv){if(argc!=2)return 2;const std::string mode=argv[1];if(mode=="unknown"){const auto threads=WakeGameResume::queryThreads(100);assert(!threads.known&&threads.total==0&&threads.suspended==0);assert(YmccSleepBoundary::dynamicUnknown==1);std::cout<<"YMCC_SLEEP_QUERY_UNKNOWN_NO_OS_ENUMERATION\n";return 0;}if(mode=="long-path"){auto base=YmccSleepBoundary::root;const auto targetChars=240;std::filesystem::path folder=base/std::wstring(targetChars-base.wstring().size()-1,L'x');std::filesystem::create_directories(folder);const auto diagnosis=folder/L"diagnosis.json",feedback=folder/L"operator-feedback.json";auto h=CreateFileW(diagnosis.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);assert(h!=INVALID_HANDLE_VALUE);CloseHandle(h);h=CreateFileW(feedback.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);const auto error=h==INVALID_HANDLE_VALUE?GetLastError():0;if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);std::cout<<"LONG_PATH_OBSERVATION "<<diagnosis.wstring().size()<<" "<<feedback.wstring().size()<<" "<<error<<"\n";assert(error!=0);return 0;}if(mode=="snapshot"){CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);}if(mode=="thread"){OpenThread(0,FALSE,100);}if(mode=="process"){OpenProcess(0,FALSE,100);}if(mode=="resume"){ResumeThread(nullptr);}if(mode=="power"){SetSuspendState(FALSE,FALSE,FALSE);}if(mode=="network"){WinHttpOpen(nullptr,0,nullptr,nullptr,0);}return 3;}
int main(int argc,char** argv){
 if(argc!=2)return 2;
 const std::string_view mode=argv[1];
 if(!mode.starts_with("ancestor-"))return originalProbe(argc,argv);
 const auto parent=YmccSleepBoundary::root.parent_path();
 const auto target=mode=="ancestor-sibling"?parent/L"outside-probe-root":parent;
 const DWORD access=mode=="ancestor-write"?FILE_WRITE_ATTRIBUTES:FILE_READ_ATTRIBUTES;
 const DWORD flags=FILE_FLAG_BACKUP_SEMANTICS|(mode=="ancestor-follow"?0:FILE_FLAG_OPEN_REPARSE_POINT);
 const DWORD how=mode=="ancestor-create"?OPEN_ALWAYS:OPEN_EXISTING;
 const auto handle=CreateFileW(target.c_str(),access,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,how,flags,nullptr);
 if(mode!="ancestor-open"||handle==INVALID_HANDLE_VALUE)return 4;
 BY_HANDLE_FILE_INFORMATION info{};
 assert(GetFileInformationByHandle(handle,&info)&&(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT));
 assert(CloseHandle(handle));
 return 0;
}

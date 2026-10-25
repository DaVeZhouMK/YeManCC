#pragma once
#include "shared_game_media_shortcut.h"
#include "process_efficiency.h"
namespace ymcc::media::downloads {
// Only an exact EXE in the active account's current shortcut file may start
// automatic identification. Shortcut GameIDs and display titles are not IDs.
inline bool shortcutCurrent(const Json& target) {
  try {
    DWORD account=0,type=0,size=sizeof account;
    if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Valve\\Steam\\ActiveProcess",L"ActiveUser",RRF_RT_REG_DWORD,&type,&account,&size)!=ERROR_SUCCESS||!account||std::to_string(account)!=stringField(target,"steamAccount"))return false;
    wchar_t steamPath[32768]{};size=sizeof steamPath;
    if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Valve\\Steam",L"SteamPath",RRF_RT_REG_SZ,nullptr,steamPath,&size)!=ERROR_SUCCESS)return false;
    const auto file=fs::path(steamPath)/L"userdata"/std::to_wstring(account)/L"config"/L"shortcuts.vdf";
    const auto row=shortcutIdentity(file,stringField(target,"shortcutAppId"));
    size=sizeof account;DWORD after=0;
    return !row.empty()&&normalized(stringField(row,"executable"))==normalized(stringField(target,"executable"))&&
      RegGetValueW(HKEY_CURRENT_USER,L"Software\\Valve\\Steam\\ActiveProcess",L"ActiveUser",RRF_RT_REG_DWORD,nullptr,&after,&size)==ERROR_SUCCESS&&after==account;
  }catch(...){return false;}
}
inline fs::path identityAppRoot(){wchar_t path[32768]{};const auto n=GetModuleFileNameW(nullptr,path,32768);if(!n||n>=32768)throw std::runtime_error("media-identity-worker-unavailable");return fs::path(path).parent_path();}
inline std::wstring identityQuote(const std::wstring& text){std::wstring out=L"\"";size_t slashes=0;for(auto c:text){if(c==L'\\'){++slashes;continue;}out.append(slashes*(c==L'"'?2:1)+(c==L'"'?1:0),L'\\');out+=c;slashes=0;}out.append(slashes*2,L'\\');return out+L'"';}
struct IdentityPorts {
  fs::path appRoot;
  std::function<bool(const Json&)> current=shortcutCurrent;
  uint64_t timeoutMs=90000;
};
inline Json identifyShortcut(const fs::path& root,const Json& target,const IdentityPorts& ports,const std::function<void()>& heartbeat){
  heartbeat();if(!ports.current(target))throw std::runtime_error("media-shortcut-no-longer-current");
  Store store(root);const auto exe=stringField(target,"executable");auto cached=store.identity(exe);
  if(cached.value("verified",false))return cached;
  const auto helper=(ports.appRoot.empty()?identityAppRoot():ports.appRoot)/L"SteamArtworkLab.exe";
  if(!fs::is_regular_file(helper)||!fs::is_regular_file(fs::path(wide(exe))))throw std::runtime_error("media-identity-worker-unavailable");
  const auto output=root/L"cache"/L"jobs"/L"game-media-identity"/wide(randomId());fs::create_directories(output);
  struct Cleanup{fs::path path;~Cleanup(){std::error_code ec;fs::remove_all(path,ec);}} cleanup{output};
  auto arguments=identityWorkerArguments(exe,output);arguments[1]="--steam-comprehensive-identity-only";auto command=identityQuote(helper.wstring());for(const auto& arg:arguments)command+=L" "+identityQuote(wide(arg.get<std::string>()));
  std::vector<wchar_t> line(command.begin(),command.end());line.push_back(0);
  HANDLE job=CreateJobObjectW(nullptr,nullptr);if(!job)throw std::runtime_error("media-identity-launch-failed");
  struct Job{HANDLE h;~Job(){CloseHandle(h);}} closeJob{job};JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if(!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof limits))throw std::runtime_error("media-identity-launch-failed");
  STARTUPINFOW startup{sizeof startup};startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;PROCESS_INFORMATION child{};
  if(!ymcc::processEfficiency::createProcess(helper.c_str(),line.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|CREATE_SUSPENDED|IDLE_PRIORITY_CLASS,nullptr,helper.parent_path().c_str(),&startup,&child))throw std::runtime_error("media-identity-launch-failed");
  struct Child{PROCESS_INFORMATION p;~Child(){if(WaitForSingleObject(p.hProcess,0)==WAIT_TIMEOUT){TerminateProcess(p.hProcess,16);WaitForSingleObject(p.hProcess,2000);}CloseHandle(p.hThread);CloseHandle(p.hProcess);}} closeChild{child};
  if(!AssignProcessToJobObject(job,child.hProcess))throw std::runtime_error("media-identity-launch-failed");
  const auto tick=GetTickCount64();const auto started=now();auto membershipAt=tick;
  if(ResumeThread(child.hThread)==DWORD(-1))throw std::runtime_error("media-identity-launch-failed");
  for(;;){const auto state=WaitForSingleObject(child.hProcess,DWORD((std::min)(uint64_t{1000},ports.timeoutMs)));
    heartbeat();if(state==WAIT_OBJECT_0||GetTickCount64()-membershipAt>=5000){membershipAt=GetTickCount64();if(!ports.current(target))throw std::runtime_error("media-shortcut-no-longer-current");}
    if(state==WAIT_OBJECT_0)break;if(state!=WAIT_TIMEOUT)throw std::runtime_error("media-identity-launch-failed");
    if(GetTickCount64()-tick>=ports.timeoutMs)throw std::runtime_error("media-identity-timeout");
  }
  DWORD exit=1;if(!GetExitCodeProcess(child.hProcess,&exit)||exit)throw std::runtime_error("media-identity-unconfirmed");
  std::error_code ec;size_t examined=0;
  for(fs::recursive_directory_iterator it(output,fs::directory_options::skip_permission_denied,ec),end;!ec&&it!=end&&examined<64;it.increment(ec),++examined){
    if(it.depth()>2)it.disable_recursion_pending();if(it->is_symlink(ec)||!it->is_regular_file(ec)||it->path().filename()!=L"manifest.json")continue;
    heartbeat();if(!ports.current(target))throw std::runtime_error("media-shortcut-no-longer-current");
    cached=store.rememberIdentity(exe,it->path(),started);if(cached.value("verified",false))return cached;
  }
  throw std::runtime_error("media-identity-unconfirmed");
}
} // namespace ymcc::media::downloads

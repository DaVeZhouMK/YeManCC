#pragma once
#include <tlhelp32.h>
namespace ymcc::sleepsteam {
// Query-only, original-instance-bound process evidence. A loaded renderer/API
// module does not prove that its hooks are healthy or a game consumed input.
static Json integrityRid(HANDLE process) {
    HANDLE token=nullptr;if(!OpenProcessToken(process,TOKEN_QUERY,&token))return nullptr;
    DWORD bytes=0;GetTokenInformation(token,TokenIntegrityLevel,nullptr,0,&bytes);
    Json result=nullptr;
    if(bytes>=sizeof(TOKEN_MANDATORY_LABEL)&&bytes<=65536){
        std::vector<unsigned char> buffer(bytes);
        if(GetTokenInformation(token,TokenIntegrityLevel,buffer.data(),bytes,&bytes)){
            const auto label=reinterpret_cast<TOKEN_MANDATORY_LABEL*>(buffer.data());
            if(IsValidSid(label->Label.Sid)){const auto count=GetSidSubAuthorityCount(label->Label.Sid);if(count&&*count)result=*GetSidSubAuthority(label->Label.Sid,*count-1);}
        }
    }
    CloseHandle(token);return result;
}
static Json boundIntegrityRid(DWORD pid,ULONGLONG created) {
    if(!pid||!created)return nullptr;
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);if(!process)return nullptr;
    Json result=nullptr;
    if(nativeProcessCreatedFromHandle(process)==created&&WaitForSingleObject(process,0)==WAIT_TIMEOUT)result=integrityRid(process);
    if(nativeProcessCreatedFromHandle(process)!=created||WaitForSingleObject(process,0)!=WAIT_TIMEOUT)result=nullptr;
    CloseHandle(process);return result;
}
static Json processInputProbe(DWORD pid,ULONGLONG created,const std::wstring& steamRoot) {
    Json result={{"available",false},{"reason","original-process-unavailable"},{"moduleCount",0},{"moduleTruncated",false},
        {"steamOverlayModulePresent",nullptr},{"overlayFromCurrentSteamRoot",nullptr},{"inputLibrariesObserved",Json::array()},
        {"integrityRid",nullptr},{"hookHealthVerified",false},{"gameInputVerified",false}};
    if(!pid||!created)return result;
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);
    if(!process)return result;
    const auto alive=[process,created]{return nativeProcessCreatedFromHandle(process)==created&&WaitForSingleObject(process,0)==WAIT_TIMEOUT;};
    if(!alive()){CloseHandle(process);return result;}
    result["integrityRid"]=integrityRid(process);
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);
    if(snapshot==INVALID_HANDLE_VALUE){result["reason"]="game-module-query-unavailable";result["queryError"]=GetLastError();CloseHandle(process);return result;}
    MODULEENTRY32W module{};module.dwSize=sizeof(module);bool present=false,rootMatches=false;std::set<std::string> libraries;
    bool more=Module32FirstW(snapshot,&module)!=FALSE;unsigned count=0;
    const std::set<std::string> allowed={"xinput1_1.dll","xinput1_2.dll","xinput1_3.dll","xinput1_4.dll","xinput9_1_0.dll","dinput.dll","dinput8.dll","gameinput.dll","windows.gaming.input.dll","sdl2.dll","sdl3.dll"};
    const auto root=pathKey(W2U(steamRoot))+"\\";
    while(more&&count<512){
        ++count;const auto name=pathKey(W2U(module.szModule));
        if(name=="gameoverlayrenderer.dll"||name=="gameoverlayrenderer64.dll"){
            present=true;rootMatches=rootMatches||(!steamRoot.empty()&&pathKey(W2U(module.szExePath)).rfind(root,0)==0);
        }
        if(allowed.count(name))libraries.insert(name);
        module.dwSize=sizeof(module);more=Module32NextW(snapshot,&module)!=FALSE;
    }
    const DWORD error=more?ERROR_SUCCESS:GetLastError();CloseHandle(snapshot);
    if(!alive()){CloseHandle(process);result["reason"]="original-process-changed-during-query";result["integrityRid"]=nullptr;return result;}
    CloseHandle(process);
    if(!count||(error!=ERROR_SUCCESS&&error!=ERROR_NO_MORE_FILES)){result["reason"]="game-module-query-incomplete";result["queryError"]=error;return result;}
    result["available"]=true;result["reason"]="query-only-no-input-health-proof";result["moduleCount"]=count;result["moduleTruncated"]=more;
    result["steamOverlayModulePresent"]=present?Json(true):more?Json(nullptr):Json(false);result["overlayFromCurrentSteamRoot"]=present?Json(rootMatches):Json(nullptr);
    for(const auto& name:libraries)result["inputLibrariesObserved"].push_back(name);
    return result;
}
}

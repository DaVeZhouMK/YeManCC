#include <windows.h>
#include <winhttp.h>
#include <atomic>
static std::atomic<unsigned> shortcutLogNetworkCalls{0};
static HINTERNET WINAPI shortcutLogNoHttp(LPCWSTR,DWORD,LPCWSTR,LPCWSTR,DWORD){++shortcutLogNetworkCalls;SetLastError(ERROR_ACCESS_DENIED);return nullptr;}
#define WinHttpOpen shortcutLogNoHttp
#pragma warning(push)
#pragma warning(disable:4100 4189) // Existing unchanged media header-only bodies.
#include "shared_game_media_shortcut.h"
#pragma warning(pop)
#include "decky_sidebar_diagnostics.h"
#undef WinHttpOpen
#include <iostream>
#include <limits>
using Json=nlohmann::json;namespace fs=std::filesystem;
static Json checks=Json::array();
static void check(const char* name,bool pass){checks.push_back(Json{{"name",name},{"pass",pass}});}
static void put(const fs::path& file,const std::string& bytes){std::ofstream out(file,std::ios::binary);out.write(bytes.data(),bytes.size());if(!out)throw std::runtime_error("owned-fixture-write-failed");}
static Json observed(const fs::path& file,uint32_t account=17){auto value=ymcc::media::observeShortcutInventory(file,account,[account]{return account;});value["evidence"]["readAttempted"]=true;return value;}
static Json projected(const Json& value){return ymcc::deckydiag::record("wallpaper-inventory",Json{{"ok",value["available"]},{"reason",value["reason"]},{"shortcutRead",value["evidence"]}});}
int wmain(int argc,wchar_t** argv){try{
 if(argc!=3||std::wstring_view(argv[2])!=L"--shortcut-read")return 41;
 const fs::path root(argv[1]);if(fs::exists(root))return 42;fs::create_directories(root);
 const auto file=root/L"PRIVATE-shortcuts.vdf";const std::string empty("\0shortcuts\0\10\10",13);put(file,empty);const auto normal=observed(file);const auto normalView=projected(normal);
 check("original-private-empty-shortcuts-read-remains-successful",normal["available"]==true&&normal["rows"].empty()&&normalView["ok"]==true);
 check("original-read-evidence-survives-actual-diagnostic-projection",normalView.contains("shortcutRead"));
#ifdef YMCC_DECKY_SHORTCUT_LOG_OLD_ONLY
 check("owned-projection-does-not-call-network",shortcutLogNetworkCalls.load()==0);
#else
 DWORD before=0,after=0;GetProcessHandleCount(GetCurrentProcess(),&before);const auto& facts=normalView.at("shortcutRead");
 check("normal-original-read-facts-are-typed-and-file-bytes-exact",facts["sourceReadOnly"]==true&&facts["readAttempted"]==true&&facts["filePresent"]==true&&facts["regularFile"]==true&&facts["fileBytes"]==13&&facts["fileSizeErrorCode"]==0);
 check("source-path-account-and-arbitrary-values-are-omitted",normalView.dump().find("PRIVATE")==std::string::npos&&!facts.contains("file")&&!facts.contains("account"));
 const auto missing=observed(root/L"absent.vdf");const auto missingView=projected(missing);check("real-missing-file-keeps-original-error-and-not-a-fabricated-read",missing["available"]==false&&missingView["shortcutRead"]["filePresent"]==false&&missingView["shortcutRead"]["fileStatusErrorCode"]==missing["evidence"]["fileStatusErrorCode"]&&!missingView["shortcutRead"].contains("fileBytes"));
 const auto directory=observed(root);const auto directoryView=projected(directory);check("real-nonregular-file-keeps-original-classification",directory["reason"]=="shortcut-file-not-regular"&&directoryView["shortcutRead"]["regularFile"]==false&&!directoryView["shortcutRead"].contains("fileBytes"));
 put(file,"bad");const auto invalid=observed(file);const auto invalidView=projected(invalid);check("real-malformed-vdf-retains-finite-original-read-tag",invalid["available"]==false&&invalidView["shortcutRead"]["readErrorTag"]==invalid["evidence"]["readErrorTag"]);
 put(file,empty);HANDLE held=CreateFileW(file.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(held==INVALID_HANDLE_VALUE)throw std::runtime_error("owned-sharing-port-failed");const auto denied=observed(file);const auto deniedView=projected(denied);const bool released=CloseHandle(held)!=FALSE;
 check("real-read-refusal-keeps-separate-after-failure-probe-result",released&&denied["reason"]=="shortcut-file-read-failed"&&deniedView["shortcutRead"]["afterFailureReadProbe"]==true&&deniedView["shortcutRead"]["afterFailureReadErrorCode"]==ERROR_SHARING_VIOLATION&&deniedView["shortcutRead"]["readErrorTag"]=="media-shortcuts-unavailable");
 const auto noAccount=observed(root/L"never-read.vdf",0);const auto noAccountView=projected(noAccount);check("helper-invocation-is-not-proof-of-an-open-or-status-query",noAccount["reason"]=="waiting-steam-account"&&noAccountView["shortcutRead"]["readAttempted"]==true&&!noAccountView["shortcutRead"].contains("filePresent"));
 const auto original=denied.dump();projected(denied);check("projection-does-not-mutate-original-read-result",denied.dump()==original);
 const auto maximum=(std::numeric_limits<unsigned long long>::max)();const auto numbers=ymcc::deckydiag::record("wallpaper-inventory",Json{{"shortcutRead",{{"fileBytes",maximum},{"fileStatusErrorCode",-2147483647ll-1},{"fileSizeErrorCode",2147483647},{"afterFailureReadErrorCode",0xffffffffull}}}});check("unsigned-bytes-and-separate-signed-filesystem-errors-stay-exact",numbers["shortcutRead"]["fileBytes"]==maximum&&numbers["shortcutRead"]["fileStatusErrorCode"]==(-2147483647ll-1)&&numbers["shortcutRead"]["afterFailureReadErrorCode"]==0xffffffffull);
 const auto bad=ymcc::deckydiag::record("wallpaper-inventory",Json{{"shortcutRead",{{"readAttempted",1},{"fileBytes",-1},{"fileStatusErrorCode",maximum},{"afterFailureReadErrorCode",-1},{"readErrorTag","media-shortcut-PRIVATE"},{"file","PRIVATE"},{"account","PRIVATE"}}}});check("bad-types-overflows-and-private-tags-do-not-become-facts",!bad.contains("shortcutRead"));
 bool optional=true;for(const auto& raw:std::vector<Json>{nullptr,true,17,"PRIVATE",Json::array(),Json{{"file","PRIVATE"}}})optional&=!ymcc::deckydiag::record("wallpaper-inventory",Json{{"shortcutRead",raw}}).contains("shortcutRead");check("legacy-null-and-empty-allowlist-stay-unobserved",optional);
 check("unrelated-events-and-action-receipts-do-not-widen",!ymcc::deckydiag::record("runtime",Json{{"shortcutRead",normal["evidence"]}}).contains("shortcutRead")&&!ymcc::deckydiag::record("wallpaper-prepare",Json{{"shortcutRead",normal["evidence"]}}).contains("shortcutRead"));
 check("projection-does-not-certify-filesystem-category-or-path",facts["filesystemErrorCategoryKnown"]==false&&facts["pathsAndAccountsOmitted"]==true&&facts["reportedFields"].get<unsigned>()<=10);
 check("owned-projection-does-not-call-network",shortcutLogNetworkCalls.load()==0);
 GetProcessHandleCount(GetCurrentProcess(),&after);check("owned-handles-return",before==after);
#endif
 unsigned failed=0;for(const auto& row:checks)failed+=!row["pass"].get<bool>();Json result={{"checks",checks},{"failures",failed},{"networkCalls",shortcutLogNetworkCalls.load()}};
#ifndef YMCC_DECKY_SHORTCUT_LOG_OLD_ONLY
 result["normal"]=normalView;result["missing"]=missingView;result["directory"]=directoryView;result["invalid"]=invalidView;result["denied"]=deniedView;result["noAccount"]=noAccountView;result["handlesBefore"]=before;result["handlesAfter"]=after;
#endif
 std::cout<<result.dump(2)<<std::endl;return failed?2:0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 4;}}

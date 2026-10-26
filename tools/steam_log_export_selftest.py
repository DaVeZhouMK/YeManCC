"""Offline Steam export gate: compile production collector/copier/archive helper.
Only fixture files and Windows tar are exercised. Never starts YMCC/Steam,
reads/writes live registry values, or touches installed user logs.
"""
from __future__ import annotations
import argparse, hashlib, json, pathlib, subprocess, sys, zipfile, datetime, collections
from log_frequency_selftest import ROOT, extract
sys.stdout.reconfigure(encoding="utf-8")

PRELUDE = r"""
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <atomic>
#include <iostream>
#include <string>
#include <vector>
#include "json.hpp"
#include "steam_log_export.h"
#include "log_export_evidence.h"
#include "log_ai_index.h"
#include "process_efficiency.h"
#include <wincrypt.h>
#include <functional>
#pragma comment(lib,"user32.lib")
using json = nlohmann::json;
namespace fspath = std::filesystem;
static std::string W2U(const std::wstring& value) {
    if(value.empty())return {};
    const int n=WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    std::string text(n,'\0');WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),text.data(),n,nullptr,nullptr);return text;
}
struct RunOut {
    std::string out;DWORD exitCode=0;bool ran=false;
    const char* completion="not-started";DWORD win32Error=ERROR_SUCCESS;
};
/* PRODUCTION_CAPTURE_FUNCTION */
struct TarInvocation {
    std::wstring command,workingDirectory;DWORD budget=0;
    bool stdinProvided=false,stdoutProvided=false,inputCaptured=false,inputRestored=false;
    bool stdinInheritable=false,stdoutInheritable=false,callerHandlesPreserved=false;
    std::string input;
};
static size_t largestTarCommand=0;static DWORD lastTarCreateError=0;
static unsigned tarCalls=0,failTarChildren=0;static bool forceTarLaunchFailure=false;
static std::vector<TarInvocation> tarInvocations;
static RunOut runCapture(const std::wstring& command,DWORD budget=10000,
        const std::function<bool()>& canceled={},HANDLE stdinHandle=nullptr,
        HANDLE stdoutHandle=nullptr,const wchar_t* workingDirectory=nullptr) {
    ++tarCalls;RunOut result;largestTarCommand=(std::max)(largestTarCommand,command.size());
    TarInvocation call;call.command=command;call.budget=budget;
    if(workingDirectory)call.workingDirectory=workingDirectory;
    call.stdinProvided=stdinHandle&&stdinHandle!=INVALID_HANDLE_VALUE;
    call.stdoutProvided=stdoutHandle&&stdoutHandle!=INVALID_HANDLE_VALUE;
    DWORD flags=0;
    call.stdinInheritable=call.stdinProvided&&GetHandleInformation(stdinHandle,&flags)&&(flags&HANDLE_FLAG_INHERIT);
    flags=0;call.stdoutInheritable=call.stdoutProvided&&GetHandleInformation(stdoutHandle,&flags)&&(flags&HANDLE_FLAG_INHERIT);
    if(call.stdinProvided) {
        LARGE_INTEGER size{},offset{},zero{};
        if(GetFileSizeEx(stdinHandle,&size)&&size.QuadPart>=0&&size.QuadPart<=4*1024*1024&&
                SetFilePointerEx(stdinHandle,zero,&offset,FILE_CURRENT)) {
            call.input.resize(static_cast<size_t>(size.QuadPart));DWORD read=0;
            call.inputCaptured=SetFilePointerEx(stdinHandle,zero,nullptr,FILE_BEGIN)&&
                ReadFile(stdinHandle,call.input.data(),static_cast<DWORD>(call.input.size()),&read,nullptr)&&read==call.input.size();
            call.inputRestored=SetFilePointerEx(stdinHandle,offset,nullptr,FILE_BEGIN)!=FALSE;
        }
    }
    tarInvocations.push_back(std::move(call));
    const auto finish=[&]() {
        DWORD inputFlags=0,outputFlags=0;
        tarInvocations.back().callerHandlesPreserved=
            (!stdinHandle||GetHandleInformation(stdinHandle,&inputFlags))&&
            (!stdoutHandle||GetHandleInformation(stdoutHandle,&outputFlags));
        return result;
    };
    if(canceled&&canceled()){result.exitCode=ERROR_CANCELLED;result.completion="canceled-before-start";return finish();}
    if(forceTarLaunchFailure){result.win32Error=ERROR_FILENAME_EXCED_RANGE;result.completion="setup-create-process";return finish();}
    if(failTarChildren){--failTarChildren;result.ran=true;result.exitCode=17;result.completion="child-exit";return finish();}
    result=productionRunCapture(command,budget,canceled,stdinHandle,stdoutHandle,workingDirectory);
    if(!result.ran)lastTarCreateError=result.win32Error;
    return finish();
}
static json checks=json::array();
static void check(const char* name,bool ok){checks.push_back({{"name",name},{"pass",ok}});}
static void put(const fspath::path& path,const std::string& text){fspath::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);out<<text;}
"""
_HASH_SOURCE=(ROOT/'native/main.cpp').read_text(encoding='utf-8-sig')
def capture_source(text):
    start=text.index('static RunOut runCapture(')
    return text[start:text.index('// 日志导出不能因为',start)].rstrip()
_CAPTURE_SOURCE=capture_source(_HASH_SOURCE)
PRELUDE=PRELUDE.replace('/* PRODUCTION_CAPTURE_FUNCTION */',_CAPTURE_SOURCE.replace('static RunOut runCapture(', 'static RunOut productionRunCapture(',1))
PRELUDE += '\n'+extract(_HASH_SOURCE,'static std::string sha256FileCancellable(const std::wstring& path, const std::function<bool()>& cancelled) {')+'\n'+extract(_HASH_SOURCE,'static std::string sha256File(const std::wstring& path) {')

TESTS = r"""
int wmain(int argc,wchar_t** argv) {
    if(argc!=2)return 2;const fspath::path root(argv[1]);fspath::create_directories(root);
    using namespace ymcc::logexport;
    const auto active=root/L"运行中的 Steam";const auto old=root/L"old-install";
    put(active/L"steam.exe","");put(old/L"steam.exe","");put(old/L"logs"/L"old.txt","do-not-export");
    put(active/L"logs"/L"app.log","steam-app\n");
    put(active/L"logs"/L"controller.previous.txt","rotated-full\n");
    put(active/L"logs"/L"nested"/L"中文 诊断.bin","raw-bytes\n");
    put(active/L"logs"/L"live.txt","live-at-copy\n");
    put(active/L"logs"/L"locked.txt","locked-content\n");
    put(active/L"logs"/L"-leading-name.txt","leading\n");
    put(active/L"logs"/L"empty.txt","");
    auto collection=collectSteamLogs(std::vector<SteamRootCandidate>{{root/L"missing","invalid"},{active,"running-process-image",42},{old,"hkcu-steam-path"}});
    check("running-root-before-installation-and-all-extensions-collected",collection.root==active && collection.source=="running-process-image" && collection.files.size()==7);
    const auto stopped=collectSteamLogs(std::vector<SteamRootCandidate>{{old,"hklm-install-32"}});
    check("stopped-steam-install-directory-fallback",stopped.root==old && stopped.files.size()==1);
    const auto noLogs=root/L"current-no-logs";put(noLogs/L"steam.exe","");
    const auto missing=collectSteamLogs(std::vector<SteamRootCandidate>{{noLogs,"running-active-process",43},{old,"hkcu-steam-path"}});
    check("missing-current-logs-never-falls-back-to-stale-install",missing.root==noLogs && missing.status=="logs-directory-missing" && missing.files.empty());
    const auto emptyRoot=root/L"empty-install";put(emptyRoot/L"steam.exe","");fspath::create_directories(emptyRoot/L"logs");
    const auto empty=collectSteamLogs(std::vector<SteamRootCandidate>{{emptyRoot,"default-install-directory"}});
    check("empty-directory-explicit",empty.status=="logs-directory-empty");
    check("no-installation-explicit",collectSteamLogs(std::vector<SteamRootCandidate>{}).status=="not-installed-or-unavailable");
    const auto source=root/L"input.txt";const auto target=root/L"snapshot.txt";put(source,"source-before\n");DWORD error=0;
    HANDLE writable=CreateFileW(source.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    check("shared-active-writer-snapshot-succeeds",writable!=INVALID_HANDLE_VALUE && snapshotSharedLog(source,target,error));
    if(writable!=INVALID_HANDLE_VALUE)CloseHandle(writable);
    put(source,"source-after\n");std::ifstream copied(target);std::string copyText;std::getline(copied,copyText);copied.close();
    check("snapshot-independent-of-subsequent-writes",copyText=="source-before");
    check("existing-destination-never-overwritten",!snapshotSharedLog(source,target,error) && error==ERROR_FILE_EXISTS);
    std::ifstream original(source);std::string originalText;std::getline(original,originalText);original.close();
    check("copy-never-modifies-original",originalText=="source-after");
    HANDLE live=CreateFileW((active/L"logs"/L"live.txt").c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    HANDLE locked=CreateFileW((active/L"logs"/L"locked.txt").c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    put(root/L"app.log","ymcc-app\n");put(root/L"manifest.txt","fixture-manifest\n");
    const auto packed=createTarArchiveWithRetry((root/L"export.zip").wstring(),{(root/L"app.log").wstring()},root.wstring(),L"manifest.txt",&collection);
    check("running-steam-locked-file-does-not-discard-ymcc-export",live!=INVALID_HANDLE_VALUE && locked!=INVALID_HANDLE_VALUE && packed.ok && packed.steamCopied==6 && packed.steamSkipped==1 && packed.steamStatus=="snapshot-partial");
    if(live!=INVALID_HANDLE_VALUE)CloseHandle(live);if(locked!=INVALID_HANDLE_VALUE)CloseHandle(locked);
    const auto packedAll=createTarArchiveWithRetry((root/L"export-all.zip").wstring(),{(root/L"app.log").wstring()},root.wstring(),L"manifest.txt",&collection);
    check("unlocked-steam-complete-snapshot",packedAll.ok && packedAll.steamCopied==7 && packedAll.steamSkipped==0);
    const auto optional=createTarArchiveWithRetry((root/L"not-installed.zip").wstring(),{(root/L"app.log").wstring()},root.wstring(),L"manifest.txt",&missing);
    check("steam-absent-still-exports-ymcc-with-explicit-receipt",optional.ok && optional.steamCopied==0 && optional.steamStatus=="logs-directory-missing");
    const auto many=root/L"many-files";put(many/L"steam.exe","");
    for(int i=0;i<400;++i)put(many/L"logs"/(std::wstring(90,L'x')+std::to_wstring(i)+L".txt"),"full\n");
    const auto manyLogs=collectSteamLogs(std::vector<SteamRootCandidate>{{many,"fixture-many-files"}});
    const auto manyPacked=createTarArchiveWithRetry((root/L"many.zip").wstring(),{(root/L"app.log").wstring()},root.wstring(),L"manifest.txt",&manyLogs);
    check("steam-many-long-names-one-tree-entry-no-command-line-overflow",manyPacked.ok && manyPacked.steamCopied==400 && manyPacked.steamSkipped==0);
    // Python prepares valid, realistically named volumes; use the same native
    // collector/copier/archive execution for >270 entries and mixed Unicode.
    std::vector<std::wstring>manyYmcc;
    bool costNamesValid=true;
    for(const auto& entry:fspath::directory_iterator(root/L"ymcc-cost-archives")) {
        costNamesValid=costNamesValid&&hostCostArchiveEvidenceName(W2U(entry.path().filename().wstring()));
        manyYmcc.push_back(entry.path().wstring());
    }
    std::sort(manyYmcc.begin(),manyYmcc.end());const auto costVolumes=manyYmcc.size();
    check("more-than-270-real-cost-volume-names",costVolumes==360&&costNamesValid);
    // Even the old CLI with shortest relative staging dirs exceeds the limit.
    std::wstring legacyCommand=L"\"C:\\Windows\\System32\\tar.exe\" -a -c -f \"fixture-only-never-created.zip\"";
    for(size_t i=0;i<costVolumes;++i)legacyCommand+=L" -C \"entry-"+std::to_wstring(i)+L"\" \""+fspath::path(manyYmcc[i]).filename().wstring()+L"\"";
    check("legacy-shortest-wide-command-exceeds-createprocess-limit",legacyCommand.size()+1>32767);
    STARTUPINFOW legacyStartup{};legacyStartup.cb=sizeof(legacyStartup);PROCESS_INFORMATION legacyProcess{};
    std::vector<wchar_t> legacyBuffer(legacyCommand.begin(),legacyCommand.end());legacyBuffer.push_back(0);
    // Never resume this fixture-only control even if an unexpected OS accepts it.
    const BOOL legacyLaunched=CreateProcessW(L"C:\\Windows\\System32\\tar.exe",legacyBuffer.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,nullptr,&legacyStartup,&legacyProcess);
    const DWORD legacyCreateError=legacyLaunched?ERROR_SUCCESS:GetLastError();
    if(legacyLaunched){TerminateProcess(legacyProcess.hProcess,1);WaitForSingleObject(legacyProcess.hProcess,5000);CloseHandle(legacyProcess.hThread);CloseHandle(legacyProcess.hProcess);}
    check("legacy-oversize-createprocess-fails-with-exact-206",!legacyLaunched&&legacyCreateError==ERROR_FILENAME_EXCED_RANGE);
    const auto costChinese=root/L"中文 成本 源"/L"成本 摘要.log",costEmoji=root/L"中文 成本 源"/L"frame-\U0001F3AE.log";
    put(costChinese,"chinese-original\n");put(costEmoji,"emoji-original\n");
    manyYmcc.push_back(costChinese.wstring());manyYmcc.push_back(costEmoji.wstring());
    put(root/L"sleep-facts.log","{\"event\":\"sleep-test\"}\n");manyYmcc.push_back((root/L"sleep-facts.log").wstring());manyYmcc.push_back((root/L"app.log").wstring());
    const auto manyYmccPacked=createTarArchiveWithRetry((root/L"many-ymcc.zip").wstring(),manyYmcc,root.wstring(),L"manifest.txt",&missing);
    check("many-ymcc-retained-cost-archives-no-command-line-overflow",manyYmccPacked.ok);
    check("large-mixed-export-uses-list-and-only-small-wide-fallback",manyYmccPacked.ok&&manyYmccPacked.listEntries>=costVolumes&&manyYmccPacked.wideArgumentEntries<=2&&manyYmccPacked.commandCharacters+1<=32767);
    const auto& manyCall=tarInvocations.back();
    check("large-export-stdin-list-really-inherited-and-position-preserved",manyCall.stdinProvided&&manyCall.stdinInheritable&&manyCall.inputCaptured&&manyCall.inputRestored&&manyCall.callerHandlesPreserved&&manyCall.command.find(L" -T -")!=std::wstring::npos);
    check("large-export-stdout-zip-handle-and-wide-working-directory",manyCall.stdoutProvided&&manyCall.stdoutInheritable&&!manyCall.workingDirectory.empty()&&manyCall.command.find(L"-f -")!=std::wstring::npos);
    const int wideSize=MultiByteToWideChar(GetACP(),MB_ERR_INVALID_CHARS,manyCall.input.data(),static_cast<int>(manyCall.input.size()),nullptr,0);
    std::wstring decoded(wideSize>0?wideSize:0,L'\0');
    const bool decodedOk=wideSize>0&&MultiByteToWideChar(GetACP(),MB_ERR_INVALID_CHARS,manyCall.input.data(),static_cast<int>(manyCall.input.size()),decoded.data(),wideSize)==wideSize;
    bool namesInList=decodedOk,relativeDirectories=decodedOk;size_t cursor=0;
    for(size_t i=0;i<costVolumes;++i)namesInList=namesInList&&decoded.find(L"\n"+fspath::path(manyYmcc[i]).filename().wstring()+L"\n")!=std::wstring::npos;
    while((cursor=decoded.find(L"-C\n",cursor))!=std::wstring::npos) {
        cursor+=3;const auto stop=decoded.find(L'\n',cursor);if(stop==std::wstring::npos){relativeDirectories=false;break;}
        const auto directory=decoded.substr(cursor,stop-cursor);
        relativeDirectories=relativeDirectories&&!directory.empty()&&fspath::path(directory).is_relative()&&directory.find(L':')==std::wstring::npos&&
            std::all_of(directory.begin(),directory.end(),[](wchar_t c){return c>0&&c<128;});cursor=stop+1;
    }
    check("list-contains-every-cost-basename-with-ascii-relative-c-directories",namesInList&&relativeDirectories);
    std::string encodedChinese,encodedEmoji;
    const bool chineseEncodable=archiveListBytes(costChinese.filename().wstring(),encodedChinese);
    const bool emojiEncodable=archiveListBytes(costEmoji.filename().wstring(),encodedEmoji);
    const auto expectedWide=static_cast<unsigned>(!chineseEncodable)+static_cast<unsigned>(!emojiEncodable);
    check("only-non-acp-members-take-wide-arguments",manyYmccPacked.ok&&manyYmccPacked.wideArgumentEntries==expectedWide);
    check("successful-completion-retains-child-exit",manyYmccPacked.ok&&std::string(manyYmccPacked.completion)=="child-exit"&&manyYmccPacked.win32Error==ERROR_SUCCESS);
    const auto wideOutput=root/L"中文目录";fspath::create_directories(wideOutput);
    const auto wideFirst=root/L"first"/L"同名.log",wideMiddle=root/L"middle"/L"\U0001F3AE.log",wideLast=root/L"last"/L"同名.log";
    put(wideFirst,"first-duplicate");put(wideMiddle,"wide-argv");put(wideLast,"last-duplicate");
    const auto widePacked=createTarArchiveWithRetry((wideOutput/L"wide.zip").wstring(),{wideFirst.wstring(),wideMiddle.wstring(),wideLast.wstring()});
    check("list-and-wide-argv-preserve-unicode-output-and-ordered-duplicate-entries",widePacked.ok);
    // Keep archive order and names across an ACP list / wide Unicode argument /
    // next list boundary, including duplicate basenames and a Unicode ZIP path.
    const auto mixedSource=root/L"mixed-sources";
    const auto first=mixedSource/L"first"/L"same.log", middle=mixedSource/L"middle"/L"frame-\U0001F600.log", last=mixedSource/L"last"/L"same.log";
    put(first,"first-duplicate\n");put(middle,"unicode-frame\n");put(last,"last-duplicate\n");
    const auto mixedZip=root/L"中文 导出 \U0001F600"/L"mixed-中文.zip";fspath::create_directories(mixedZip.parent_path());
    const auto mixedPacked=createTarArchiveWithRetry(mixedZip.wstring(),{first.wstring(),middle.wstring(),last.wstring()},root.wstring(),L"manifest.txt",&missing);
    check("unicode-output-and-mixed-list-argv-order-exported",mixedPacked.ok);
    check("bounded-command-even-with-many-cost-volumes",largestTarCommand+1<=32767);
    const auto launchFailed=root/L"launch-failed.zip";put(launchFailed,"preserve-on-launch-failure");
    forceTarLaunchFailure=true;
    const auto launch=createTarArchiveWithRetry(launchFailed.wstring(),{(root/L"app.log").wstring()});
    forceTarLaunchFailure=false;
    std::ifstream oldLaunch(launchFailed);std::string oldLaunchText;std::getline(oldLaunch,oldLaunchText);oldLaunch.close();
    check("launch-error-retained-after-bounded-retry",!launch.ok&&launch.attempts==2&&launch.reason=="tar-launch-failed"&&launch.win32Error==ERROR_FILENAME_EXCED_RANGE&&std::string(launch.completion)=="setup-create-process");
    const auto childFailed=root/L"child-failed.zip";put(childFailed,"preserve-on-child-failure");failTarChildren=2;
    const unsigned childBefore=tarCalls;
    const auto childFailure=createTarArchiveWithRetry(childFailed.wstring(),{(root/L"app.log").wstring()});
    std::ifstream oldChild(childFailed);std::string oldChildText;std::getline(oldChild,oldChildText);oldChild.close();
    check("two-child-failures-retain-raw-code-completion-and-old-final",!childFailure.ok&&childFailure.attempts==2&&tarCalls==childBefore+2&&childFailure.reason=="tar-exit-nonzero"&&childFailure.win32Error==17&&std::string(childFailure.completion)=="child-exit"&&oldChildText=="preserve-on-child-failure");
    failTarChildren=1;const unsigned recoverBefore=tarCalls;
    const auto recovered=createTarArchiveWithRetry((root/L"recovered.zip").wstring(),{(root/L"app.log").wstring()});
    check("native-child-failure-retries-once-then-promotes",recovered.ok&&recovered.attempts==2&&tarCalls==recoverBefore+2&&std::string(recovered.completion)=="child-exit"&&recovered.win32Error==ERROR_SUCCESS);
    const std::vector<std::wstring> unsafeNames={L"-C",L"-option.log",L"@inputs.lst",L"bad\rname.log",L"bad\nname.log",std::wstring(L"bad\0name.log",12)};
    for(size_t i=0;i<unsafeNames.size();++i) {
        const auto planRoot=root/L"plan-controls"/std::to_wstring(i);fspath::create_directories(planRoot/L"entry-0");
        const auto invalid=buildArchiveCommand((root/L"control-never-created.zip").wstring(),planRoot.wstring(),{{(planRoot/L"entry-0").wstring(),unsafeNames[i]}});
        check(("unsafe-top-level-member-rejected-"+std::to_string(i)).c_str(),!invalid.ok&&invalid.error==ERROR_INVALID_NAME);
    }
    check("every-tar-attempt-keeps-120s-budget",std::all_of(tarInvocations.begin(),tarInvocations.end(),[](const TarInvocation& call){return call.budget==120000;}));
    check("launch-failure-keeps-previous-final-archive",oldLaunchText=="preserve-on-launch-failure");
    std::string large(9u<<20,'\0');unsigned random=0x79a2135u;
    for(auto& byte:large){random^=random<<13;random^=random>>17;random^=random<<5;byte=static_cast<char>(random);}
    const auto largeSource=root/L"large.bin";put(largeSource,large);
    const auto largePacked=createTarArchiveWithRetry((root/L"large.zip").wstring(),{largeSource.wstring()});
    check("production-capture-streams-zip-beyond-8MiB",largePacked.ok&&fspath::file_size(root/L"large.zip")>(8u<<20));
    const auto previous=root/L"previous.zip";put(previous,"retained-old-archive");const unsigned before=tarCalls;
    const auto failed=createTarArchiveWithRetry(previous.wstring(),{(root/L"does-not-exist.log").wstring()});
    std::ifstream retained(previous);std::string retainedText;std::getline(retained,retainedText);retained.close();
    check("mandatory-snapshot-failure-two-attempts-preserves-final",!failed.ok && failed.attempts==2 && retainedText=="retained-old-archive" && tarCalls==before);
    bool cleanup=true;for(const auto& entry:fspath::directory_iterator(root))if(entry.path().filename().wstring().find(L".staging-")!=std::wstring::npos || entry.path().filename().wstring().find(L".attempt-")!=std::wstring::npos)cleanup=false;
    check("only-temporary-staging-cleaned",cleanup && fspath::exists(active/L"logs"/L"locked.txt"));
    const auto outside=root/L"outside";put(outside/L"secret.txt","not-a-steam-log");
    const bool link=CreateSymbolicLinkW((active/L"logs"/L"outside-link").c_str(),outside.c_str(),SYMBOLIC_LINK_FLAG_DIRECTORY|0x2)!=FALSE;
    if(link){const auto linked=collectSteamLogs(std::vector<SteamRootCandidate>{{active,"fixture"}});check("directory-reparse-point-not-followed",linked.files.size()==7 && linked.issues.size()==1);}
    json report={{"legacyMinimumCommandCharacters",legacyCommand.size()},{"legacyCreateProcessError",legacyCreateError},{"costVolumeCount",costVolumes},{"localACP",GetACP()},{"chineseListEncodable",chineseEncodable},{"emojiListEncodable",emojiEncodable},{"largeExportListEntries",manyYmccPacked.listEntries},{"largeExportWideArgumentEntries",manyYmccPacked.wideArgumentEntries},{"largeExportCommandCharacters",manyYmccPacked.commandCharacters},{"largestTarCommandCharacters",largestTarCommand},{"lastTarCreateError",lastTarCreateError},{"checks",checks},{"failures",0},{"fixtureRoot",W2U(root.wstring())},{"reparsePointFixtureAvailable",link},{"scope","REAL_COLLECTOR_COPIER_ARCHIVE_HELPER_FIXTURE_ONLY_NO_PRODUCT_OR_REGISTRY"}};
    for(const auto& c:checks)if(!c["pass"].get<bool>())report["failures"]=report["failures"].get<int>()+1;
    std::cout<<report.dump(2)<<std::endl;return report["failures"].get<int>()?1:0;
}
"""

def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir',type=pathlib.Path,default=ROOT.parents[2]/'Build/Validation/SteamLogExport')
    args=parser.parse_args();out=args.output_dir.resolve();out.mkdir(parents=True,exist_ok=True)
    run=out/('run-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f'));run.mkdir()
    source=(ROOT/'native/main.cpp').read_bytes();text=source.decode('utf-8-sig').replace('\r\n','\n')
    start=text.index('static constexpr unsigned int LOG_ARCHIVE_MAX_ATTEMPTS')
    end=text.index('// 进程内实时检测 SMT',start)
    helper=text[start:end].replace('#include "sleep_export_diagnostics_native.inc"','// Unrelated sleep IPC is outside this archive consumer.')
    header=ROOT/'native/steam_log_export.h';bound_header=header.read_bytes()
    ai_headers={n:(ROOT/'native'/n).read_bytes() for n in ('log_ai_index.h','log_export_evidence.h')}
    archive_header=ROOT/'native/log_archive_command.h';bound_archive_header=archive_header.read_bytes()
    process_header=ROOT/'native/process_efficiency.h';bound_process_header=process_header.read_bytes()
    cpp=run/'harness.cpp';exe=run/'harness.exe';cpp.write_text(PRELUDE+'\n'+helper+'\n'+TESTS,encoding='utf-8')
    vcvars=pathlib.Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat')
    command=f'call "{vcvars}" >nul && cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 "{cpp}" /I"{ROOT / "deps/json"}" /I"{ROOT / "native"}" /Fo"{run / "harness.obj"}" /Fe"{exe}" /link advapi32.lib'
    batch=run/'build.cmd'
    env_script='import {calibrationToolchainPreamble} from "./tools/fixtures/hc_reviewed_calibration/toolchain.mjs";process.stdout.write(calibrationToolchainPreamble(process.argv[1]));'
    env_run=subprocess.run([str(ROOT.parents[2]/'Environment/Toolchains/nodejs/node-v22.14.0-win-x64/node.exe'),'--input-type=module','-e',env_script,str(vcvars)],cwd=ROOT,capture_output=True,text=True,encoding='utf-8',check=True)
    compile_command=command.split(' && ',1)[1]
    batch.write_text(env_run.stdout+compile_command+'\r\nexit /b %errorlevel%\r\n',encoding='utf-8')
    compiled=subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=run,capture_output=True,text=True,errors='replace')
    (run/'compile.log').write_text(compiled.stdout+compiled.stderr,encoding='utf-8')
    if compiled.returncode:print(compiled.stdout+compiled.stderr);return compiled.returncode
    fixture=run/'fixtures 中文 空格';cost_dir=fixture/'ymcc-cost-archives';cost_dir.mkdir(parents=True)
    original_cost_hashes={}
    for i in range(360):
        payload=(json.dumps({'event':'fixture-cost','index':i},separators=(',',':'))+'\n').encode('utf-8')
        digest=hashlib.sha256(payload).hexdigest()
        name=f'input-host-cost.log.archive.{639285400000000000+i:019d}.{digest[:24]}.zip'
        marker={'schema':'ymcc-log-volume/1','volumeId':name,'payloadEntry':'input-host-cost.log','sha256':digest,'sourceBytes':len(payload)}
        with zipfile.ZipFile(cost_dir/name,'w',compression=zipfile.ZIP_STORED) as volume:
            for member,data in (('BEGIN.json',dict(marker,record='file_begin')),('input-host-cost.log',payload),('END.json',dict(marker,record='file_end',completePayloadRetained=True))):
                info=zipfile.ZipInfo(member,date_time=(2026,10,26,0,0,0))
                volume.writestr(info,data if isinstance(data,bytes) else (json.dumps(data,separators=(',',':'))+'\n').encode('utf-8'))
        original_cost_hashes[name]=hashlib.sha256((cost_dir/name).read_bytes()).hexdigest()
    tested=subprocess.run([str(exe),str(fixture)],cwd=run,capture_output=True,encoding='utf-8',errors='replace')
    (run/'result.json').write_text(tested.stdout,encoding='utf-8')
    if not tested.stdout:print(tested.stderr);return tested.returncode or 1
    report=json.loads(tested.stdout)
    def check(name: str,ok: bool):
        report['checks'].append({'name':name,'pass':bool(ok)})
        if not ok:report['failures']+=1
    report['originalCostVolumeSha256']=original_cost_hashes
    with zipfile.ZipFile(fixture/'export.zip') as z:
        check('zip-full-tree-unicode-rotations-and-zero-length-preserved',z.read('steam/logs/nested/中文 诊断.bin')==b'raw-bytes\n' and z.read('steam/logs/controller.previous.txt')==b'rotated-full\n' and z.read('steam/logs/empty.txt')==b'')
        check('same-basename-ymcc-and-steam-do-not-collide',z.read('app.log')==b'ymcc-app\n' and z.read('steam/logs/app.log')==b'steam-app\n')
        receipt=json.loads(z.read('steam/steam-log-export.json'))
        check('locked-file-explicit-receipt-not-in-zip',receipt['copied']==6 and receipt['skipped']==1 and 'steam/logs/locked.txt' not in z.namelist() and any(not f['copied'] and f['win32Error'] for f in receipt['files']))
        check('no-unrelated-install-or-outside-directory-content',not any('old.txt' in name or 'secret.txt' in name for name in z.namelist()))
    with zipfile.ZipFile(fixture/'export-all.zip') as z:
        check('unlocked-originals-preserved-full-export',z.read('steam/logs/locked.txt')==b'locked-content\n')
        check('nested-steam-leading-dash-basename-retained',z.read('steam/logs/-leading-name.txt')==b'leading\n')
    current=(ROOT/'native/main.cpp').read_text(encoding='utf-8-sig')
    check('bound-archive-helper-stable',current[current.index('static constexpr unsigned int LOG_ARCHIVE_MAX_ATTEMPTS'):current.index('// 进程内实时检测 SMT',current.index('static constexpr unsigned int LOG_ARCHIVE_MAX_ATTEMPTS'))].replace('#include "sleep_export_diagnostics_native.inc"','// Unrelated sleep IPC is outside this archive consumer.')==helper)
    check('archive-extraction-has-no-sleep-globals', '#include "sleep_export_diagnostics_native.inc"' not in helper)
    check('collector-header-stable',header.read_bytes()==bound_header)
    check('production-capture-function-stable',capture_source(current)==_CAPTURE_SOURCE)
    check('process-launch-header-stable',process_header.read_bytes()==bound_process_header)
    report['captureFunctionSha256']=hashlib.sha256(_CAPTURE_SOURCE.encode('utf-8')).hexdigest()
    report['processLaunchHeaderSha256']=hashlib.sha256(bound_process_header).hexdigest()
    report['productionCaptureFunctionExecuted']=True
    check('archive-command-header-stable',archive_header.read_bytes()==bound_archive_header)
    report['archiveCommandHeaderSha256']=hashlib.sha256(bound_archive_header).hexdigest()
    report['tarExeSha256']=hashlib.sha256(pathlib.Path('C:/Windows/System32/tar.exe').read_bytes()).hexdigest()
    handler=extract(current,'ipc_on("logs.exportAll", [](const json& exportArgs) -> json {')
    check('production-export-wired-to-shared-snapshot', 'collectSteamLogs(g_steamSettingsObserver.sessionSnapshot().pid)' in handler and 'createTarArchiveWithRetry(zipPath, files, dataDir, manifestName, &steamLogs)' in handler)
    from ai_log_reader import iter_ai_records
    for archive_name in ('export.zip','export-all.zip','not-installed.zip','many.zip','many-ymcc.zip',pathlib.Path('中文 导出 😀')/'mixed-中文.zip'):
        records=list(iter_ai_records(fixture/archive_name,index_only=True))
        check('ai-reader-consumes-real-native-export:'+str(archive_name),records[-1]['coverageComplete'])
    check('ai-index-and-evidence-headers-stable',all((ROOT/'native'/n).read_bytes()==b for n,b in ai_headers.items()))
    report['aiHeaderSha256']={n:hashlib.sha256(b).hexdigest() for n,b in ai_headers.items()}
    report['sourceSha256']=hashlib.sha256(source).hexdigest();report['headerSha256']=hashlib.sha256(bound_header).hexdigest()
    many_zip=fixture/'many-ymcc.zip'
    check('many-ymcc-final-zip-created',many_zip.is_file())
    if many_zip.is_file():
        with zipfile.ZipFile(many_zip) as z:
            expected={f.name:f.read_bytes() for f in cost_dir.iterdir()}
            names=z.namelist()
            check('all-200-cost-archives-preserved-byte-for-byte',len(expected)>=200 and all(names.count(n)==1 and z.read(n)==b for n,b in expected.items()))
            check('all-360-cost-volumes-original-bytes-sha256-and-count',len(expected)==360 and all(names.count(n)==1 and z.read(n)==b and hashlib.sha256(z.read(n)).hexdigest()==original_cost_hashes[n] for n,b in expected.items()))
            check('cost-volume-originals-not-modified',all(hashlib.sha256(b).hexdigest()==original_cost_hashes[n] for n,b in expected.items()))
            report['archivedCostVolumeSha256']={n:hashlib.sha256(z.read(n)).hexdigest() for n in expected}
            check('large-export-unicode-source-and-output-paths',any(ord(c)>127 for c in str(cost_dir)) and ' ' in str(cost_dir) and any(ord(c)>127 for c in str(many_zip.parent)))
            check('large-export-chinese-and-emoji-original-bytes',z.read('成本 摘要.log')==(fixture/'中文 成本 源'/'成本 摘要.log').read_bytes()==b'chinese-original\n' and z.read('frame-🎮.log')==(fixture/'中文 成本 源'/'frame-🎮.log').read_bytes()==b'emoji-original\n')
            check('mandatory-sleep-and-app-retained-with-large-export',z.read('sleep-facts.log')==b'{"event":"sleep-test"}\n' and z.read('app.log')==b'ymcc-app\n')
            check('large-export-original-manifest-retained',z.read('manifest.txt')==(fixture/'manifest.txt').read_bytes()==b'fixture-manifest\n')
            snapshot=json.loads(z.read('log-export-snapshot.json'))
            receipts={row['entry']:row for row in snapshot['files']}
            check('snapshot-receipt-retains-all-mandatory-originals',all(receipts[n]['originalBytesRetained'] and receipts[n]['bytes']==len(b) for n,b in expected.items()) and snapshot['snapshotInspectionFailures']==0)
    wide_zip=fixture/'中文目录'/'wide.zip'
    check('unicode-wide-output-archive-created',wide_zip.is_file())
    if wide_zip.is_file():
        with zipfile.ZipFile(wide_zip) as z:
            names=[entry.filename for entry in z.infolist()]
            contents=[z.read(entry) for entry in z.infolist() if entry.filename in ['同名.log','🎮.log']]
            check('unicode-and-wide-fallback-entry-names-preserved',collections.Counter(names)==collections.Counter(['同名.log','🎮.log','同名.log','log-export-snapshot.json']))
            check('duplicate-entry-order-and-contents-preserved',[z.read(i) for i in z.infolist() if i.filename=='同名.log']==[b'first-duplicate',b'last-duplicate'] and z.read('🎮.log')==b'wide-argv')
    mixed_zip=fixture/'中文 导出 😀'/'mixed-中文.zip'
    check('unicode-mixed-final-archive-created',mixed_zip.is_file())
    if mixed_zip.is_file():
        with zipfile.ZipFile(mixed_zip) as z:
            infos=[i for i in z.infolist() if i.filename in ('same.log','frame-😀.log')]
            check('duplicate-names-and-unicode-order-preserved',collections.Counter(i.filename for i in infos)==collections.Counter(['same.log','frame-😀.log','same.log']) and [z.read(i) for i in infos if i.filename=='same.log']==[b'first-duplicate\n',b'last-duplicate\n'] and z.read('frame-😀.log')==b'unicode-frame\n')
    for archive in sorted(fixture.rglob('*.zip')):
        if archive.parent==cost_dir or archive.name in ('launch-failed.zip','child-failed.zip','previous.zip'):
            continue
        with zipfile.ZipFile(archive) as z:
            check('no-staging-or-input-list-leak:'+str(archive.relative_to(fixture)),all(not n.startswith('./') and not any(part.startswith(('entry-','archive-inputs','tar-inputs')) or '.staging-' in part or '.attempt-' in part for part in pathlib.PurePosixPath(n).parts) for n in z.namelist()))
    large_zip=fixture/'large.zip'
    check('large-archive-created',large_zip.is_file())
    if large_zip.is_file():
        with zipfile.ZipFile(large_zip) as z:
            check('large-zip-payload-preserved-byte-for-byte',z.read('large.bin')==(fixture/'large.bin').read_bytes())
    report['installedApplicationsOrHardwareExecuted']=False
    report['productionOrHardwareExecuted']=False;report['runDirectory']=str(run)
    (out/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k not in ('originalCostVolumeSha256','archivedCostVolumeSha256')},ensure_ascii=False,indent=2))
    return 1 if report['failures'] else tested.returncode
if __name__=='__main__':raise SystemExit(main())

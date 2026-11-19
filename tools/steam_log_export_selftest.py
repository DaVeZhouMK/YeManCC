"""Offline Steam export gate: compile production collector/copier/archive helper.
Only fixture files and Windows tar are exercised. Never starts YMCC/Steam,
reads/writes live registry values, or touches installed user logs.
"""
from __future__ import annotations
import argparse, hashlib, json, pathlib, re, subprocess, sys, zipfile, datetime, collections
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
#include "sleep_export_diagnostics.h"
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
    ymcc::sleepexport::CaptureFinalization captureFinalization;
};
#if defined(YMCC_CAPTURE_FINALIZATION_ONLY)
#include "log_capture_finalization_ports.inc"
#endif
/* PRODUCTION_CAPTURE_FUNCTION */
#if defined(YMCC_CAPTURE_FINALIZATION_ONLY)
#undef TerminateJobObject
#undef WaitForSingleObject
#undef QueryInformationJobObject
#endif
struct TarInvocation {
    std::wstring command,workingDirectory;DWORD budget=0;
    bool stdinProvided=false,stdoutProvided=false,inputCaptured=false,inputRestored=false;
    bool stdinInheritable=false,stdoutInheritable=false,callerHandlesPreserved=false;
    std::string input;
};
static size_t largestTarCommand=0;static DWORD lastTarCreateError=0;
static unsigned tarCalls=0,failTarChildren=0;static DWORD forcedArchiveExitCode=17;static bool forceTarLaunchFailure=false;
static std::wstring forcedArchiveChildCommand;
static std::vector<TarInvocation> tarInvocations;
static std::function<void(const wchar_t*)> snapshotAttemptObserver;
static std::function<void(HANDLE,const wchar_t*)> tarCompletionObserver;
static RunOut runCapture(const std::wstring& command,DWORD budget=10000,
        const std::function<bool()>& canceled={},HANDLE stdinHandle=nullptr,
        HANDLE stdoutHandle=nullptr,const wchar_t* workingDirectory=nullptr) {
    if(snapshotAttemptObserver)snapshotAttemptObserver(workingDirectory);
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
    if(failTarChildren){
        --failTarChildren;
        if(!forcedArchiveChildCommand.empty())result=productionRunCapture(forcedArchiveChildCommand,budget,canceled,stdinHandle,stdoutHandle,workingDirectory);
        else {result.ran=true;result.exitCode=forcedArchiveExitCode;result.completion="child-exit";result.captureFinalization.entered=true;}
        return finish();
    }
    result=productionRunCapture(command,budget,canceled,stdinHandle,stdoutHandle,workingDirectory);
    if(tarCompletionObserver)tarCompletionObserver(stdoutHandle,workingDirectory);
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
    put(root/L"model-adaptation.log",json({{"schema","ymcc.model-adaptation/1"},{"event","model-adaptation-unique"},{"firstSample",{{"mappingFound",false}}}}).dump()+"\n");
    put(root/L"virtual-adaptation.log",json({{"event","virtual-adaptation-witness"},{"firstSample",{{"hostSdkDeliveryProven",false}}}}).dump()+"\n");
    const auto packedAll=createTarArchiveWithRetry((root/L"export-all.zip").wstring(),{(root/L"app.log").wstring(),(root/L"model-adaptation.log").wstring(),(root/L"virtual-adaptation.log").wstring()},root.wstring(),L"manifest.txt",&collection);
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
    check("large-mixed-export-uses-the-unicode-list-without-wide-argv",manyYmccPacked.ok&&manyYmccPacked.listEntries>=costVolumes&&manyYmccPacked.wideArgumentEntries==0&&manyYmccPacked.commandCharacters+1<=32767);
    const auto& manyCall=tarInvocations.back();
    check("large-export-stdin-list-really-inherited-and-position-preserved",manyCall.stdinProvided&&manyCall.stdinInheritable&&manyCall.inputCaptured&&manyCall.inputRestored&&manyCall.callerHandlesPreserved);
    check("unicode-child-creates-its-private-zip-without-a-parent-output-handle-or-workdir",!manyCall.stdoutProvided&&manyCall.workingDirectory.empty()&&manyCall.command.find(L" -EncodedCommand ")!=std::wstring::npos);
    const int wideSize=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,manyCall.input.data(),static_cast<int>(manyCall.input.size()),nullptr,0);
    std::wstring decoded(wideSize>0?wideSize:0,L'\0');
    const bool decodedOk=wideSize>0&&MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,manyCall.input.data(),static_cast<int>(manyCall.input.size()),decoded.data(),wideSize)==wideSize;
    bool namesInList=decodedOk;
    for(size_t i=0;i<costVolumes;++i)namesInList=namesInList&&decoded.find(L"\n"+fspath::path(manyYmcc[i]).filename().wstring()+L"\n")!=std::wstring::npos;
    check("unicode-list-contains-all-cost-basenames-and-exact-non-ascii-names",namesInList&&
        decoded.find(L"\n"+costChinese.filename().wstring()+L"\n")!=std::wstring::npos&&
        decoded.find(L"\n"+costEmoji.filename().wstring()+L"\n")!=std::wstring::npos);
    std::string encodedChinese,encodedEmoji;
    const bool chineseEncodable=archiveListBytes(costChinese.filename().wstring(),encodedChinese);
    const bool emojiEncodable=archiveListBytes(costEmoji.filename().wstring(),encodedEmoji);
    check("top-level-emoji-selects-unicode-writer-on-first-attempt",manyYmccPacked.ok&&manyYmccPacked.attempts==1&&manyYmccPacked.archiveCommandMode=="unicode-zip-utf8-list");
    check("successful-completion-retains-child-exit",manyYmccPacked.ok&&std::string(manyYmccPacked.completion)=="child-exit"&&manyYmccPacked.win32Error==ERROR_SUCCESS);
    const auto wideOutput=root/L"中文目录";fspath::create_directories(wideOutput);
    const auto wideFirst=root/L"first"/L"同名.log",wideMiddle=root/L"middle"/L"\U0001F3AE.log",wideLast=root/L"last"/L"同名.log";
    put(wideFirst,"first-duplicate");put(wideMiddle,"wide-argv");put(wideLast,"last-duplicate");
    const auto widePacked=createTarArchiveWithRetry((wideOutput/L"wide.zip").wstring(),{wideFirst.wstring(),wideMiddle.wstring(),wideLast.wstring()});
    check("unicode-writer-preserves-output-name-and-ordered-duplicate-entries",widePacked.ok&&widePacked.attempts==1);
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
    const auto retrySteam=collectSteamLogs({{root/L"运行中的 Steam", "retry-control"}});
    const auto nested=root/L"运行中的 Steam"/L"logs"/L"frame-\U0001F3AE.log";put(nested,"nested-unicode\n");
    auto nestedSteam=retrySteam;nestedSteam.files.push_back(nested);
    const auto nestedPacked=createTarArchiveWithRetry((root/L"unicode-nested.zip").wstring(),{(root/L"app.log").wstring()},L"",L"",&nestedSteam);
    check("nested-steam-emoji-selects-unicode-writer-without-a-tar-attempt",nestedPacked.ok&&nestedPacked.attempts==1&&
        nestedPacked.archiveCommandMode=="unicode-zip-utf8-list");
    failTarChildren=1;forcedArchiveExitCode=0xc0000005u;const auto crashBefore=tarCalls;
    const auto unicodeRetry=createTarArchiveWithRetry((root/L"unicode-retry.zip").wstring(),{(root/L"app.log").wstring()});
    forcedArchiveExitCode=17;
    check("tar-exception-preserves-raw-first-error-and-uses-one-unicode-retry",unicodeRetry.ok&&unicodeRetry.attempts==2&&tarCalls==crashBefore+2&&
        unicodeRetry.previousAttemptError==0xc0000005u&&std::string(unicodeRetry.previousAttemptReason)=="tar-exit-nonzero"&&unicodeRetry.archiveCommandMode=="unicode-zip-utf8-list");
    const auto unicodeCollision=root/L"unicode-existing.zip";put(unicodeCollision,"keep-old-zip");
    const auto collision=createTarArchiveWithRetry(unicodeCollision.wstring(),{wideMiddle.wstring()});
    std::ifstream prior(unicodeCollision,std::ios::binary);std::string priorBytes;std::getline(prior,priorBytes);
    check("unicode-writer-keeps-existing-final-output-and-does-not-retry-the-move",!collision.ok&&collision.attempts==1&&
        collision.reason=="promote-failed"&&priorBytes=="keep-old-zip");
    const std::vector<std::wstring> unsafeNames={L"-C",L"-option.log",L"@inputs.lst",L"bad\rname.log",L"bad\nname.log",std::wstring(L"bad\0name.log",12)};
    for(size_t i=0;i<unsafeNames.size();++i) {
        const auto planRoot=root/L"plan-controls"/std::to_wstring(i);fspath::create_directories(planRoot/L"entry-0");
        const auto invalid=buildArchiveCommand((root/L"control-never-created.zip").wstring(),planRoot.wstring(),{{(planRoot/L"entry-0").wstring(),unsafeNames[i]}});
        check(("unsafe-top-level-member-rejected-"+std::to_string(i)).c_str(),!invalid.ok&&invalid.error==ERROR_INVALID_NAME);
    }
    check("every-tar-attempt-has-five-minute-budget",LOG_ARCHIVE_ATTEMPT_TIMEOUT_MS==300000&&std::all_of(tarInvocations.begin(),tarInvocations.end(),[](const TarInvocation& call){return call.budget==LOG_ARCHIVE_ATTEMPT_TIMEOUT_MS;}));
    check("launch-failure-keeps-previous-final-archive",oldLaunchText=="preserve-on-launch-failure");
    std::string large(22u<<20,'\0');unsigned random=0x79a2135u;
    for(auto& byte:large){random^=random<<13;random^=random>>17;random^=random<<5;byte=static_cast<char>(random);}
    const auto largeSource=root/L"large.bin";put(largeSource,large);
    const auto largePacked=createTarArchiveWithRetry((root/L"large.zip").wstring(),{largeSource.wstring()});
    check("production-capture-streams-zip-beyond-20MB-without-total-size-cap",largePacked.ok&&fspath::file_size(root/L"large.zip")>20'000'000ULL);
    const auto costActiveRoot=root/L"single-cost-active";fspath::create_directories(costActiveRoot);
    const auto costActive=costActiveRoot/L"input-host-cost.log";
    const std::string costRow="{\"kind\":\"cost-window\",\"payload\":\""+std::string(4096,'x')+"\"}\n";
    const std::string costLast="{\"kind\":\"cost-window\",\"newest\":true}\n";
    {std::ofstream out(costActive,std::ios::binary);while(static_cast<unsigned long long>(out.tellp())+costRow.size()+costLast.size()<=20'000'000ULL)out<<costRow;out<<costLast;}
    const auto costActivePacked=createTarArchiveWithRetry((root/L"single-cost-active.zip").wstring(),{costActive.wstring()},root.wstring(),L"manifest.txt",&missing);
    check("single-near-20MB-active-cost-file-copies-and-exports",costActivePacked.ok&&fspath::file_size(costActive)<=20'000'000ULL&&fspath::file_size(costActive)>19'990'000ULL);
    const auto previous=root/L"previous.zip";put(previous,"retained-old-archive");const unsigned before=tarCalls;
    const auto failed=createTarArchiveWithRetry(previous.wstring(),{(root/L"does-not-exist.log").wstring()});
    std::ifstream retained(previous);std::string retainedText;std::getline(retained,retainedText);retained.close();
    check("mandatory-snapshot-failure-two-attempts-preserves-final",!failed.ok && failed.attempts==2 && retainedText=="retained-old-archive" && tarCalls==before);
    const auto mandatoryEvidence=archiveSnapshotCopyFailureEvidence(failed);
    check("mandatory-copy-failure-retains-both-original-attempts",mandatoryEvidence["failureObservations"]==2&&mandatoryEvidence["retainedFailures"]==2&&mandatoryEvidence["retainedObservedFailuresComplete"].get<bool>()&&mandatoryEvidence["failures"][0]["attempt"]==1&&mandatoryEvidence["failures"][1]["attempt"]==2);
    check("mandatory-copy-failure-retains-basename-and-original-open-stage",mandatoryEvidence["failures"][0]["entry"]=="does-not-exist.log"&&mandatoryEvidence["failures"][0]["copyObservation"]["stage"]=="source-open"&&mandatoryEvidence["failures"][0]["copyObservation"]["sourceBytesAtOpen"].is_null()&&mandatoryEvidence["failures"][0]["win32Error"]==ERROR_FILE_NOT_FOUND);

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


SHARED_FACT_TESTS = r"""
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include "steam_session_observer.h"
#include "steam_information_client.h"
#include "steam_account_facts.h"
#include "process_inventory.h"
#include <functional>
#include <memory>
namespace fs=std::filesystem;
using Json=nlohmann::json;
static std::map<std::wstring,std::pair<std::wstring,DWORD>> registry;
static unsigned registryReads=0,environmentReads=0;
static std::map<std::wstring,unsigned> registryKeyReads;
static std::wstring environmentRoot;
static std::wstring regKey(HKEY hive,const wchar_t* key,const wchar_t* name){return std::to_wstring(reinterpret_cast<ULONG_PTR>(hive))+L"|"+key+L"|"+name;}
static void reg(const wchar_t* name,const std::wstring& value,DWORD error=0){registry[regKey(HKEY_CURRENT_USER,L"Software\\Valve\\Steam",name)]={value,error};}
static LSTATUS fixtureRegGetValueW(HKEY hive,LPCWSTR key,LPCWSTR name,DWORD flags,LPDWORD type,PVOID data,LPDWORD bytes){
    ++registryReads;++registryKeyReads[regKey(hive,key,name)];const auto at=registry.find(regKey(hive,key,name));
    if(at==registry.end())return ERROR_FILE_NOT_FOUND;
    if(at->second.second)return at->second.second;
    const auto& value=at->second.first;const auto required=static_cast<DWORD>((value.size()+1)*sizeof(wchar_t));
    if(*bytes<required){*bytes=required;return ERROR_MORE_DATA;}
    memcpy(data,value.c_str(),required);*bytes=required;if(type)*type=REG_SZ;return ERROR_SUCCESS;
}
static DWORD fixtureGetEnvironmentVariableW(LPCWSTR,LPWSTR data,DWORD size){++environmentReads;if(environmentRoot.size()+1>size)return static_cast<DWORD>(environmentRoot.size()+1);wcscpy_s(data,size,environmentRoot.c_str());return static_cast<DWORD>(environmentRoot.size());}
#define RegGetValueW fixtureRegGetValueW
#define GetEnvironmentVariableW fixtureGetEnvironmentVariableW
#include "steam_information_producer.h"
#undef GetEnvironmentVariableW
#undef RegGetValueW
#include "steam_log_export.h"
static Json checks=Json::array();
static void check(const char* name,bool ok){checks.push_back({{"name",name},{"pass",ok}});}
static void put(const fs::path& path,const std::string& text){fs::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);out<<text;}
static std::string u8(const fs::path& path){return ymcc::steaminfo::utf8(path.wstring());}
int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 2;const fs::path root(argv[1]);if(fs::exists(root))return 3;fs::create_directories(root);
    using namespace ymcc::steaminfo;using namespace ymcc::logexport;
    environmentRoot=(root/L"uninstalled-default").wstring();
    const auto old=root/L"old-install",active=root/L"运行 Steam 🎮",next=root/L"迁移后的 Steam",noLogs=root/L"current-no-logs";
    for(const auto& p:{old,active,next,noLogs})put(p/L"steam.exe","");
    put(old/L"logs"/L"old.txt","stale-do-not-export");put(active/L"logs"/L"nested"/L"中文.bin","actual\n");put(active/L"logs"/L"empty.txt","");put(next/L"logs"/L"new.txt","new\n");
    auto stale=std::make_shared<ymcc::steamsession::IdentitySnapshot>();stale->registryRoot=old.wstring();stale->rootReadError=ERROR_SUCCESS;
    reg(L"SteamPath",active.wstring());auto candidates=installationCandidates(stale);
    check("fresh-installation-over-stale-observer",candidates.front().path==u8(active));
    check("seven-installations-wire-bound",candidates.size()==7);
    auto table=std::make_shared<ymcc::processInventory::Snapshot>();table->status=ymcc::processInventory::Status::Complete;
    ymcc::steamsession::Observer observer;unsigned uiCalls=0;
    auto facts=collect(Installations|Presence,observer,[&](const auto&){++uiCalls;return Json::object();},table);
    check("metadata-without-ui-session-read",uiCalls==0&&facts.identityRevision==0&&facts.root.empty()&&facts.presence==0);
    check("facts-seven-entry-roundtrip",decode(encode(facts),Installations|Presence).has_value());
    auto stopped=collectSteamLogs(facts);
    check("offline-fresh-unicode-install-collected",stopped.root==active&&stopped.files.size()==2&&stopped.source=="hkcu-steam-path");
    reg(L"SteamPath",next.wstring());auto moved=collect(Installations|Presence,observer,{},table);
    check("second-manual-request-fresh-after-install-move",collectSteamLogs(moved).root==next&&moved.installations.front().path==u8(next));
    auto running=moved;running.presence=1;running.processPid=42;running.processBirth=100;running.processPath=u8(active/L"steam.exe");
    check("running-process-image-precedes-offline-registry",collectSteamLogs(running).root==active&&collectSteamLogs(running).source=="running-shared-process-image");
    running.processPath=u8(noLogs/L"steam.exe");auto absent=collectSteamLogs(running);
    check("missing-running-logs-never-falls-back",absent.root==noLogs&&absent.status=="logs-directory-missing"&&absent.files.empty());
    registry.clear();reg(L"SteamExe",L"\""+(active/L"steam.exe").wstring()+L"\"");auto legacy=installationCandidates(stale);
    check("steamexe-only-legacy-installation",legacy.front().source=="hkcu-steam-exe"&&legacy.front().path==u8(active)&&legacy.size()==7);
    Facts legacyFacts=facts;legacyFacts.installations=legacy;check("steamexe-only-offline-export",collectSteamLogs(legacyFacts).root==active);
    reg(L"SteamPath",L"");check("empty-steampath-uses-steamexe",installationCandidates(stale).front().path==u8(active));
    reg(L"SteamPath",L"",ERROR_ACCESS_DENIED);check("unreadable-steampath-uses-steamexe",installationCandidates(stale).front().path==u8(active));
    reg(L"SteamExe",(active/L"other.exe").wstring());const auto denied=installationCandidates(stale);
    check("wrong-executable-rejected-original-read-error-retained",denied.front().path.empty()&&denied.front().error==ERROR_ACCESS_DENIED&&denied.front().source=="hkcu-steam-path");
    reg(L"SteamPath",L"\""+active.wstring()+L"\"");check("quoted-steampath-normalized",installationCandidates(stale).front().path==u8(active));

    struct MachineSlot {const wchar_t* key;size_t index;const char* installSource;const char* pathSource;};
    for(const auto& slot:std::vector<MachineSlot>{{L"Software\\WOW6432Node\\Valve\\Steam",2,"hklm32-install-path","hklm32-steam-path"},
            {L"Software\\Valve\\Steam",3,"hklm-install-path","hklm-steam-path"}}){
        const auto checkSlot=[&](const char* suffix,bool ok){const auto name=std::string(slot.pathSource)+"/"+suffix;check(name.c_str(),ok);};
        const auto machineReg=[&](const wchar_t* name,const std::wstring& value,DWORD error=ERROR_SUCCESS){registry[regKey(HKEY_LOCAL_MACHINE,slot.key,name)]={value,error};};
        registry.clear();registryKeyReads.clear();machineReg(L"SteamPath",L"\""+active.wstring()+L"\"");
        const auto onlyPath=installationCandidates(stale);
        checkSlot("steampath-only-original-slot",onlyPath.size()==7&&onlyPath[slot.index].path==u8(active)&&onlyPath[slot.index].source==slot.pathSource&&onlyPath[slot.index].error==ERROR_SUCCESS);
        Facts machineFacts;machineFacts.available=true;machineFacts.requested=Installations|Presence;machineFacts.presence=0;machineFacts.installations=onlyPath;
        checkSlot("same-seven-slot-wire-roundtrip",decode(encode(machineFacts),Installations|Presence).has_value());
        const auto offline=collectSteamLogs(machineFacts);
        checkSlot("offline-original-collector-consumes-fallback",offline.root==active&&offline.source==slot.pathSource&&offline.files.size()==2);
        const auto activeReadKey=regKey(HKEY_CURRENT_USER,L"Software\\Valve\\Steam\\ActiveProcess",L"pid");
        const auto activeReads=registryKeyReads[activeReadKey];
        const auto metadata=collect(Installations,observer,[&](const auto&){++uiCalls;return Json::object();});
        checkSlot("mask-one-no-session-presence-work",metadata.requested==Installations&&metadata.presence==-1&&metadata.identityRevision==0&&uiCalls==0&&registryKeyReads[activeReadKey]==activeReads&&metadata.installations[slot.index].path==u8(active));
        machineReg(L"InstallPath",next.wstring());machineReg(L"SteamPath",L"",ERROR_ACCESS_DENIED);
        const auto reads=registryKeyReads[regKey(HKEY_LOCAL_MACHINE,slot.key,L"SteamPath")];
        const auto preferred=installationCandidates(stale);
        checkSlot("installpath-priority-no-fallback-read",preferred[slot.index].path==u8(next)&&preferred[slot.index].source==slot.installSource&&preferred[slot.index].error==ERROR_SUCCESS&&registryKeyReads[regKey(HKEY_LOCAL_MACHINE,slot.key,L"SteamPath")]==reads);
        machineReg(L"InstallPath",L"");auto slashes=active.wstring();std::replace(slashes.begin(),slashes.end(),L'\\',L'/');machineReg(L"SteamPath",slashes);
        const auto empty=installationCandidates(stale);
        checkSlot("empty-installpath-uses-normalized-steampath",empty[slot.index].path==u8(active)&&empty[slot.index].source==slot.pathSource&&empty[slot.index].error==ERROR_SUCCESS);
        machineReg(L"InstallPath",L"",ERROR_ACCESS_DENIED);const auto unreadable=installationCandidates(stale);
        checkSlot("unreadable-installpath-uses-steampath",unreadable[slot.index].path==u8(active)&&unreadable[slot.index].source==slot.pathSource&&unreadable[slot.index].error==ERROR_SUCCESS);
        machineReg(L"SteamPath",L"",ERROR_FILE_NOT_FOUND);const auto deniedBoth=installationCandidates(stale);
        checkSlot("failed-fallback-keeps-installpath-error",deniedBoth[slot.index].path.empty()&&deniedBoth[slot.index].source==slot.installSource&&deniedBoth[slot.index].error==ERROR_ACCESS_DENIED);
        registry.erase(regKey(HKEY_LOCAL_MACHINE,slot.key,L"InstallPath"));machineReg(L"SteamPath",L"",ERROR_ACCESS_DENIED);const auto missingFirst=installationCandidates(stale);
        checkSlot("missing-installpath-keeps-original-missing-error",missingFirst[slot.index].source==slot.installSource&&missingFirst[slot.index].error==ERROR_FILE_NOT_FOUND);
        machineReg(L"InstallPath",L"");machineReg(L"SteamPath",L"");const auto emptyBoth=installationCandidates(stale);
        checkSlot("empty-fallback-keeps-original-empty-slot",emptyBoth[slot.index].path.empty()&&emptyBoth[slot.index].source==slot.installSource&&emptyBoth[slot.index].error==ERROR_SUCCESS);
    }
    registry.clear();reg(L"SteamPath",active.wstring());
    registry[regKey(HKEY_LOCAL_MACHINE,L"Software\\WOW6432Node\\Valve\\Steam",L"SteamPath")]={next.wstring(),ERROR_SUCCESS};
    registry[regKey(HKEY_LOCAL_MACHINE,L"Software\\Valve\\Steam",L"SteamPath")]={old.wstring(),ERROR_SUCCESS};
    const auto ordered=installationCandidates(stale);Facts orderedFacts=facts;orderedFacts.installations=ordered;
    check("hklm-fallback-keeps-hkcu-precedence-and-slot-order",ordered.size()==7&&ordered[2].path==u8(next)&&ordered[3].path==u8(old)&&collectSteamLogs(orderedFacts).root==active);
    registry.clear();reg(L"SteamPath",L"\""+active.wstring()+L"\"");
    auto unavailable=facts;unavailable.available=false;check("unavailable-facts-explicit",collectSteamLogs(unavailable).status=="steam-information-unavailable");
    unavailable=facts;unavailable.requested=Installations;check("missing-presence-scope-explicit",collectSteamLogs(unavailable).status=="steam-information-unavailable");
    auto invalid=facts;invalid.installations.push_back({u8(old),"excess",0});check("oversize-installation-list-rejected",collectSteamLogs(invalid).status=="steam-information-invalid");
    invalid=facts;invalid.presence=2;check("invalid-presence-rejected",collectSteamLogs(invalid).status=="steam-information-invalid");
    invalid=facts;invalid.installations.front().path=std::string("x\0y",3);check("embedded-nul-path-rejected",collectSteamLogs(invalid).status=="steam-information-invalid");
    invalid=facts;invalid.installations.front().path=std::string("\xc0\xaf",2);check("invalid-utf8-path-rejected",collectSteamLogs(invalid).status=="steam-information-invalid");
    invalid=running;invalid.processBirth=0;check("process-without-birth-rejected",collectSteamLogs(invalid).status=="steam-information-invalid");
    invalid=running;invalid.processPath=u8(active/L"not-steam.exe");check("non-steam-process-path-rejected",collectSteamLogs(invalid).status=="steam-information-invalid");
    invalid=running;invalid.processPath="relative\\steam.exe";check("relative-process-path-rejected",collectSteamLogs(invalid).status=="steam-information-invalid");
    invalid=facts;invalid.processPid=55;check("contradictory-absent-process-rejected",collectSteamLogs(invalid).status=="steam-information-invalid");
    invalid=facts;invalid.installations={{"relative","relative",0},{u8(active),"valid",0}};check("relative-installation-skipped",collectSteamLogs(invalid).root==active);
    invalid=facts;invalid.installations={{u8(old),"denied",ERROR_ACCESS_DENIED},{u8(active),"valid",0}};check("installation-read-error-skipped",collectSteamLogs(invalid).root==active);
    const auto readsBefore=registryReads,envBefore=environmentReads;for(int i=0;i<8;++i)collectSteamLogs(facts);
    check("collector-zero-private-discovery",readsBefore==registryReads&&envBefore==environmentReads);
    const auto baseline=collectSteamLogs(std::vector<SteamRootCandidate>{{active,"fixture"},{old,"stale"}});
    check("original-vector-collector-intact",baseline.root==active&&baseline.files.size()==2);
    DWORD error=0;const auto original=active/L"logs"/L"nested"/L"中文.bin",snapshot=root/L"snapshot.bin";
    check("original-shared-snapshot-copier",snapshotSharedLog(original,snapshot,error)&&fs::file_size(snapshot)==7);
    check("original-create-new-no-overwrite",!snapshotSharedLog(original,snapshot,error)&&error==ERROR_FILE_EXISTS);
    check("original-size-cap-retained",!snapshotSharedLog(original,root/L"too-large.bin",error,1)&&error==ERROR_FILE_TOO_LARGE);
    unsigned failures=0;for(const auto& row:checks)if(!row.at("pass").get<bool>())++failures;
    std::cout<<Json({{"selection","shared-facts"},{"checks",checks},{"failures",failures},{"registryReads",registryReads},{"uiCalls",uiCalls},{"archiveExecutions",0},{"realSteamOperations",0}}).dump()<<std::endl;
    return failures?2:0;
}
"""

SHARED_FACT_ROOT_SELECTION_TESTS = r"""
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include "steam_log_export.h"
using Json=nlohmann::json;
namespace fs=std::filesystem;
static Json checks=Json::array(),observed=Json::object();
static void check(const char* name,bool ok){checks.push_back({{"name",name},{"pass",ok}});}
static void put(const fs::path& path,const std::string& text){fs::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);out<<text;}
static std::string u8(const fs::path& path){const auto value=path.wstring();const int n=WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);std::string result(n,'\0');WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),result.data(),n,nullptr,nullptr);return result;}
static std::string bytes(const fs::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
static int finish(bool control){unsigned failures=0;for(const auto& row:checks)if(!row.at("pass").get<bool>())++failures;
    std::cout<<Json({{"observed",observed},{"selection",control?"shared-facts-root-selection-old":"shared-facts-root-selection"},{"checks",checks},{"failures",failures},{"realSteamOperations",0},{"archiveExecutions",0}}).dump()<<std::endl;return failures?2:0;}
int wmain(int argc,wchar_t** argv){
    const bool control=argc==3&&std::wstring(argv[2])==L"old-control";
    if(argc!=2&&!control)return 2;const fs::path root(argv[1]);if(fs::exists(root))return 3;fs::create_directories(root);
    using namespace ymcc::steaminfo;using namespace ymcc::logexport;
    const auto old=root/L"old-install",active=root/L"运行中 Steam 🎮",absent=root/L"running-no-logs",missing=root/L"running-root-missing";
    put(old/L"steam.exe","");put(old/L"logs"/L"old.txt","stale-do-not-export");
    put(active/L"logs"/L"nested"/L"中文.bin","actual\n");put(active/L"logs"/L"empty.txt","");
    Facts facts;facts.available=true;facts.requested=Installations|Presence;facts.presence=0;facts.installations={{u8(old),"fixture-registry",0}};
    auto running=facts;running.presence=1;running.processPid=42;running.processBirth=100;running.processPath=u8(active/L"steam.exe");
    auto emptyPath=running;emptyPath.processPath.clear();const auto rejected=collectSteamLogs(emptyPath);
    check("running-missing-process-path-does-not-export-registry-logs",rejected.status=="steam-information-invalid"&&rejected.root.empty()&&rejected.files.empty());
    const auto selected=collectSteamLogs(running);
    observed={{"emptyPathStatus",rejected.status},{"emptyPathRoot",u8(rejected.root)},{"runningRoot",u8(selected.root)},{"runningSource",selected.source},{"runningFiles",selected.files.size()}};
    check("running-renamed-image-retains-current-root-logs",selected.root==active&&selected.source=="running-shared-process-image"&&selected.files.size()==2);
    if(control)return finish(true);
    auto noPid=emptyPath;noPid.processPid=0;
    check("running-empty-path-missing-pid-stays-invalid",collectSteamLogs(noPid).status=="steam-information-invalid");
    auto noBirth=emptyPath;noBirth.processBirth=0;
    check("running-empty-path-missing-birth-stays-invalid",collectSteamLogs(noBirth).status=="steam-information-invalid");
    check("running-root-does-not-probe-image-file-or-include-stale-member",!fs::exists(active/L"steam.exe")&&std::none_of(selected.files.begin(),selected.files.end(),[](const auto& file){return file.filename()==L"old.txt";})&&selected.issues.empty());
    fs::create_directories(active/L"steam.exe");const auto imageDirectory=collectSteamLogs(running);
    check("running-image-path-replacement-does-not-redirect-logs",imageDirectory.root==active&&imageDirectory.files==selected.files);
    running.processPath=u8(absent/L"steam.exe");const auto absentLogs=collectSteamLogs(running);
    check("running-root-missing-image-and-logs-never-falls-back",absentLogs.root==absent&&absentLogs.status=="logs-directory-missing"&&absentLogs.files.empty());
    running.processPath=u8(missing/L"steam.exe");const auto missingRoot=collectSteamLogs(running);
    check("running-missing-root-retains-selected-source",missingRoot.root==missing&&missingRoot.source=="running-shared-process-image"&&missingRoot.files.empty());
    auto malformed=running;malformed.processPath="relative/steam.exe";
    check("relative-running-path-still-rejected",collectSteamLogs(malformed).status=="steam-information-invalid");
    malformed=running;malformed.processPath=u8(active/L"other.exe");
    check("non-steam-running-image-still-rejected",collectSteamLogs(malformed).status=="steam-information-invalid");
    malformed=running;malformed.processPath=std::string("\xc0\xaf",2);
    check("malformed-running-utf8-still-rejected",collectSteamLogs(malformed).status=="steam-information-invalid");
    const auto offline=collectSteamLogs(facts);
    check("offline-installation-fallback-preserved",offline.root==old&&offline.source=="fixture-registry"&&offline.files.size()==1);
    auto unknown=facts;unknown.presence=-1;
    check("unknown-presence-keeps-explicit-installation-source",collectSteamLogs(unknown).root==old&&collectSteamLogs(unknown).source=="fixture-registry");
    const auto original=collectSteamLogs(std::vector<SteamRootCandidate>{{active,"not-an-installation"},{old,"fallback"}});
    check("original-vector-installation-validation-preserved",original.root==old&&original.source=="fallback");
    // The original regular-image path remains covered independently of the new
    // running-root selection. Use another path; do not replace fixture dirs.
    const auto regular=root/L"regular-running";put(regular/L"steam.exe","");put(regular/L"logs"/L"current.txt","current\n");
    running.processPath=u8(regular/L"steam.exe");
    check("original-running-image-precedes-registry",collectSteamLogs(running).root==regular&&collectSteamLogs(running).source=="running-shared-process-image");
    DWORD error=0;const auto source=active/L"logs"/L"nested"/L"中文.bin",snapshot=root/L"snapshot.bin";
    check("original-shared-snapshot-copier-retains-current-root-bytes",snapshotSharedLog(source,snapshot,error)&&bytes(snapshot)=="actual\n");
    check("original-create-new-no-overwrite",!snapshotSharedLog(source,snapshot,error)&&error==ERROR_FILE_EXISTS);
    check("original-size-cap-retained",!snapshotSharedLog(source,root/L"too-large.bin",error,1)&&error==ERROR_FILE_TOO_LARGE);
    return finish(false);
}

"""

SHARED_FACT_PRIORITY_TESTS = r"""
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include "steam_session_observer.h"
#include "steam_information_client.h"
#include "steam_client_peer_ownership.h"
#include "steam_account_facts.h"
#include "process_inventory.h"
#include <functional>
#include <memory>
namespace fs=std::filesystem;
using Json=nlohmann::json;
static std::map<std::wstring,std::pair<std::wstring,DWORD>> registry;
static unsigned registryReads=0,environmentReads=0;
static std::wstring environmentRoot;static DWORD activePid=0,activeError=0;
static unsigned activeRegistryReads=0;
namespace ymcc::steamsession {
struct FixtureSourceOwner {
    struct Read {
        explicit operator bool()const noexcept{return true;}
        bool current()const noexcept{return true;}
    };
};
struct FixtureObserver {
    std::shared_ptr<const IdentitySnapshot> held;
    unsigned identityReads=0,sessionReads=0,refreshes=0;
    auto identitySnapshot(){++identityReads;return held;}
    Session sessionSnapshot(){++sessionReads;return held?held->session:Session{};}
    bool sameIdentityEpoch(const std::shared_ptr<const IdentitySnapshot>& p){return p==held;}
    void requestIdentityRefresh(){++refreshes;}
    FixtureSourceOwner::Read sourceRead()const{return {};}
    bool sourceReadCurrent(const FixtureSourceOwner::Read& read)const{return read.current();}
};
}
namespace ymcc::steaminfo {
struct FixturePeer : Peer {
    static inline std::map<DWORD,std::pair<std::wstring,ULONGLONG>> fixtures;
    static inline std::vector<DWORD> attempted;
    bool load(DWORD p){attempted.push_back(p);const auto at=fixtures.find(p);if(at==fixtures.end())return false;pid=p;path=at->second.first;birth=at->second.second;return true;}
    bool live()const{return true;}bool sameUser(const FixturePeer&)const{return true;}
};
static unsigned legacyPackageChecks=0;
inline bool fixtureLegacyPackagePeer(const FixturePeer&){++legacyPackageChecks;return false;}
}
static std::wstring regKey(HKEY hive,const wchar_t* key,const wchar_t* name){return std::to_wstring(reinterpret_cast<ULONG_PTR>(hive))+L"|"+key+L"|"+name;}
static void reg(const wchar_t* name,const std::wstring& value,DWORD error=0){registry[regKey(HKEY_CURRENT_USER,L"Software\\Valve\\Steam",name)]={value,error};}
static LSTATUS fixtureRegGetValueW(HKEY hive,LPCWSTR key,LPCWSTR name,DWORD flags,LPDWORD type,PVOID data,LPDWORD bytes){
    ++registryReads;
    if(flags==RRF_RT_REG_DWORD){++activeRegistryReads;if(activeError)return activeError;if(*bytes<sizeof(DWORD))return ERROR_MORE_DATA;*static_cast<DWORD*>(data)=activePid;*bytes=sizeof(DWORD);return ERROR_SUCCESS;}
    const auto at=registry.find(regKey(hive,key,name));
    if(at==registry.end())return ERROR_FILE_NOT_FOUND;
    if(at->second.second)return at->second.second;
    const auto& value=at->second.first;const auto required=static_cast<DWORD>((value.size()+1)*sizeof(wchar_t));
    if(*bytes<required){*bytes=required;return ERROR_MORE_DATA;}
    memcpy(data,value.c_str(),required);*bytes=required;if(type)*type=REG_SZ;return ERROR_SUCCESS;
}
static DWORD fixtureGetEnvironmentVariableW(LPCWSTR,LPWSTR data,DWORD size){++environmentReads;if(environmentRoot.size()+1>size)return static_cast<DWORD>(environmentRoot.size()+1);wcscpy_s(data,size,environmentRoot.c_str());return static_cast<DWORD>(environmentRoot.size());}
#define RegGetValueW fixtureRegGetValueW
#define GetEnvironmentVariableW fixtureGetEnvironmentVariableW
#define Peer FixturePeer
#define Observer FixtureObserver
#define SourceOwner FixtureSourceOwner
#define gamingHomePackagePeer fixtureLegacyPackagePeer
#include "steam_information_producer.h"
#undef gamingHomePackagePeer
#undef SourceOwner
#undef Observer
#undef Peer
#undef GetEnvironmentVariableW
#undef RegGetValueW
#include "steam_log_export.h"
static Json checks=Json::array();
static void check(const char* name,bool ok){checks.push_back({{"name",name},{"pass",ok}});}
static void put(const fs::path& path,const std::string& text){fs::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);out<<text;}
static std::string u8(const fs::path& path){return ymcc::steaminfo::utf8(path.wstring());}
int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 3;using namespace ymcc::steaminfo;using namespace ymcc::logexport;
    const fs::path root(argv[1]),first=root/L"other",active=root/L"active",observed=root/L"observed";environmentRoot=root.wstring();
    for(const auto& p:{first,active,observed}){put(p/L"steam.exe","");put(p/L"logs"/L"app.log","fixture");}
    FixturePeer::fixtures={{11,{(first/L"steam.exe").wstring(),111}},{22,{(active/L"steam.exe").wstring(),222}},{33,{(observed/L"steam.exe").wstring(),333}}};
    ymcc::processInventory::Snapshot table;table.status=ymcc::processInventory::Status::Complete;table.rows={{11,L"steam.exe"},{22,L"steam.exe"},{33,L"steam.exe"}};
    ymcc::steamsession::Session session;session.pid=33;session.created=333;
    ymcc::steamsession::FixtureObserver observer;
    const auto publish=[&](DWORD registered,LSTATUS error){
        auto identity=std::make_shared<ymcc::steamsession::IdentitySnapshot>();
        identity->session=session;identity->registeredPid=registered;identity->registeredPidReadError=error;
        identity->presence=1;observer.held=identity;
    };
    const auto collectSelected=[&](Facts& facts){
        publish(activePid,activeError);
        FixturePeer::attempted.clear();
        facts=collect(Presence,observer,{},std::make_shared<ymcc::processInventory::Snapshot>(table));
        facts.requested|=Installations;
    };
    activePid=22;Facts facts;collectSelected(facts);
    check("registry-active-process-before-first-and-observer",facts.processPid==22&&collectSteamLogs(facts).root==active);
    check("registered-selection-never-probes-deferred-candidates",FixturePeer::attempted==std::vector<DWORD>{22});
    activeError=ERROR_FILE_NOT_FOUND;facts=Facts{};collectSelected(facts);
    check("observer-process-before-first-when-active-unavailable",facts.processPid==33&&facts.processBirth==333);
    session.created=999;facts=Facts{};collectSelected(facts);
    check("observer-pid-reuse-falls-back-to-qualified-image",facts.processPid==11);
    check("stale-observer-is-checked-before-the-original-fallback",FixturePeer::attempted==std::vector<DWORD>{33,11});
    session={};activeError=0;activePid=22;FixturePeer::fixtures.erase(22);facts=Facts{};collectSelected(facts);
    check("unreadable-active-process-falls-back",facts.processPid==11&&facts.presence==1);
    FixturePeer::fixtures.clear();facts=Facts{};collectSelected(facts);
    check("unreadable-running-images-never-prove-absence",facts.presence==1&&!facts.processPid&&facts.processPath.empty());
    check("unreadable-matches-keep-priority-and-original-fallback-order",FixturePeer::attempted==std::vector<DWORD>{22,11,33});
    table.rows.clear();facts=Facts{};collectSelected(facts);check("complete-empty-table-proves-absence",facts.presence==0);
    table.status=ymcc::processInventory::Status::Failed;facts=Facts{};collectSelected(facts);check("failed-table-keeps-unknown",facts.presence==-1);
    table.status=ymcc::processInventory::Status::Unavailable;facts=Facts{};collectSelected(facts);check("unavailable-table-keeps-unknown",facts.presence==-1);
    table.status=ymcc::processInventory::Status::Complete;
    table.rows={{11,L"steam.exe"},{22,L"steam.exe"},{33,L"steam.exe"}};
    FixturePeer::fixtures={{11,{(first/L"steam.exe").wstring(),111}},{22,{(active/L"steam.exe").wstring(),222}},{33,{(observed/L"steam.exe").wstring(),333}}};
    session.pid=33;session.created=333;publish(22,ERROR_SUCCESS);activePid=11;activeError=0;
    facts=collect(Presence,observer,{},std::make_shared<ymcc::processInventory::Snapshot>(table));
    check("published-registered-hint-wins-over-private-registry-successor",facts.processPid==22&&facts.processBirth==222);
    publish(22,ERROR_ACCESS_DENIED);activePid=22;
    facts=collect(Presence,observer,{},std::make_shared<ymcc::processInventory::Snapshot>(table));
    check("unreadable-published-hint-uses-same-public-session",facts.processPid==33&&facts.processBirth==333);
    observer.held.reset();
    facts=collect(Presence,observer,{},std::make_shared<ymcc::processInventory::Snapshot>(table));
    check("unknown-public-identity-keeps-fresh-qualified-candidate",facts.presence==1&&facts.processPid==11);
    publish(22,ERROR_SUCCESS);table.rows.clear();
    facts=collect(Presence,observer,{},std::make_shared<ymcc::processInventory::Snapshot>(table));
    check("retained-public-hint-never-overrides-fresh-complete-absence",facts.presence==0&&facts.processPid==0);
    check("presence-reads-one-public-publication-per-request",observer.identityReads==12);
    check("presence-never-rereads-private-registry-or-split-session",activeRegistryReads==0&&observer.sessionReads==0&&observer.refreshes==0);
    check("legacy-presence-never-enters-package-only-branch",legacyPackageChecks==0);
    table.rows={{10,L"other.exe"},{11,L"sTeAm.ExE"},{22,L"not-steam.exe"},{33,L"steam.exe"},{44,L"steam.exe"}};
    FixturePeer::fixtures={{10,{(first/L"steam.exe").wstring(),110}},{11,{(first/L"other.exe").wstring(),111}},
        {22,{(active/L"steam.exe").wstring(),222}},{33,{(observed/L"steam.exe").wstring(),333}},
        {44,{(active/L"steam.exe").wstring(),444}}};
    FixturePeer::attempted.clear();facts=Facts{};session.created=999;
    collectPresence(facts,table,session,22);
    check("only-steam-names-are-qualified-and-fallback-keeps-table-order",facts.presence==1&&facts.processPid==44&&
        FixturePeer::attempted==std::vector<DWORD>{33,11,44});
    FixturePeer::attempted.clear();facts=Facts{};facts.processPid=100;facts.processBirth=900;facts.processPath="prior";
    collectPresence(facts,table,session,22);
    check("existing-result-identity-is-not-replaced-or-reopened",facts.presence==1&&facts.processPid==100&&facts.processBirth==900&&
        facts.processPath=="prior"&&FixturePeer::attempted.empty());
    unsigned failures=0;for(const auto& row:checks)if(!row.at("pass").get<bool>())++failures;
    std::cout<<Json({{"selection","shared-facts-priority"},{"checks",checks},{"failures",failures},{"processIdentityQualification","fixture peer facts and terminal publisher; actual production collect/ordering"},{"realSteamOperations",0}}).dump()<<std::endl;return failures?2:0;
}
"""


SNAPSHOT_ERROR_TESTS = r"""
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
#include "steam_log_export.h"
#ifndef SNAPSHOT_OLD_CONTROL
#include "log_ai_index.h"
#endif
using json=nlohmann::json;
namespace fspath=std::filesystem;
static json checks=json::array();
static void check(const char* name,bool ok){checks.push_back({{"name",name},{"pass",ok}});}
static void put(const fspath::path& path,const std::string& text){fspath::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);out<<text;}
static std::string bytes(const fspath::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
namespace copy_port {
enum class Mode{normal,negativeSize,sizeFail,earlyEof,readFail,shortWrite,writeFail,outputCloseFail,shortReads,sourceGrowth,existingTarget};
struct State{Mode mode=Mode::normal;DWORD sentinel=ERROR_ACCESS_DENIED,apiError=ERROR_SHARING_VIOLATION;
    std::string source="snapshot",copied;std::size_t cursor=0;unsigned creates=0,sizes=0,reads=0,writes=0,inputCloses=0,outputCloses=0,deletes=0;
    bool sourceShareCorrect=true,destinationCreateNew=true,deletedOnlyDestination=true;};
static State state;
static HANDLE input(){return reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(11));}
static HANDLE output(){return reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(12));}
static void reset(Mode mode,DWORD sentinel=ERROR_ACCESS_DENIED,DWORD error=ERROR_SHARING_VIOLATION){state=State{};state.mode=mode;state.sentinel=sentinel;state.apiError=error;SetLastError(sentinel);}
static HANDLE WINAPI create(LPCWSTR,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES,DWORD disposition,DWORD,HANDLE){
    ++state.creates;SetLastError(state.sentinel);
    if(disposition==OPEN_EXISTING){state.sourceShareCorrect=access==GENERIC_READ&&share==(FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE);return input();}
    state.destinationCreateNew=disposition==CREATE_NEW&&access==GENERIC_WRITE&&share==0;
    if(state.mode==Mode::existingTarget){SetLastError(ERROR_FILE_EXISTS);return INVALID_HANDLE_VALUE;}
    return output();
}
static BOOL WINAPI size(HANDLE handle,PLARGE_INTEGER value){
    ++state.sizes;if(handle!=input())return FALSE;SetLastError(state.sentinel);
    if(state.mode==Mode::sizeFail){SetLastError(state.apiError);return FALSE;}
    value->QuadPart=state.mode==Mode::negativeSize?-1:static_cast<LONGLONG>(state.source.size());return TRUE;
}
static BOOL WINAPI read(HANDLE handle,LPVOID buffer,DWORD requested,LPDWORD amount,LPOVERLAPPED){
    ++state.reads;SetLastError(state.sentinel);if(handle!=input())return FALSE;*amount=0;
    if(state.mode==Mode::readFail){SetLastError(state.apiError);return FALSE;}
    if(state.mode==Mode::earlyEof)return TRUE;
    auto n=(std::min)(static_cast<std::size_t>(requested),state.source.size()-state.cursor);
    if(state.mode==Mode::shortReads)n=(std::min)(n,std::size_t{2});
    std::memcpy(buffer,state.source.data()+state.cursor,n);state.cursor+=n;*amount=static_cast<DWORD>(n);
    if(state.mode==Mode::sourceGrowth&&state.reads==1)state.source+="-later-growth";
    return TRUE;
}
static BOOL WINAPI write(HANDLE handle,LPCVOID buffer,DWORD requested,LPDWORD amount,LPOVERLAPPED){
    ++state.writes;SetLastError(state.sentinel);if(handle!=output())return FALSE;*amount=0;
    if(state.mode==Mode::writeFail){SetLastError(state.apiError);return FALSE;}
    *amount=state.mode==Mode::shortWrite?requested-1:requested;state.copied.append(static_cast<const char*>(buffer),*amount);return TRUE;
}
static BOOL WINAPI close(HANDLE handle){
    if(handle==input())++state.inputCloses;else if(handle==output())++state.outputCloses;else return FALSE;
    SetLastError(ERROR_INVALID_HANDLE);if(handle==output()&&state.mode==Mode::outputCloseFail){SetLastError(state.apiError);return FALSE;}return TRUE;
}
static BOOL WINAPI remove(LPCWSTR path){++state.deletes;state.deletedOnlyDestination=std::wstring(path).find(L"snapshot-destination")!=std::wstring::npos;SetLastError(ERROR_PATH_NOT_FOUND);return TRUE;}
}
namespace snapshot_fault {
namespace fs=std::filesystem;using ymcc::logexport::winPath;
#ifndef SNAPSHOT_OLD_CONTROL
using ymcc::logexport::SnapshotCopyEvidence;
using ymcc::logexport::SnapshotSourceSync;
#endif
#define CreateFileW copy_port::create
#define GetFileSizeEx copy_port::size
#define ReadFile copy_port::read
#define WriteFile copy_port::write
#define CloseHandle copy_port::close
#define DeleteFileW copy_port::remove
/* SNAPSHOT_SHARED_FUNCTION */
#undef DeleteFileW
#undef CloseHandle
#undef WriteFile
#undef ReadFile
#undef GetFileSizeEx
#undef CreateFileW
}
int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 3;const fspath::path root(argv[1]);fspath::create_directories(root);
    using namespace ymcc::logexport;using copy_port::Mode;
    const auto run=[](unsigned long long cap=0){DWORD error=0;const bool ok=snapshot_fault::snapshotSharedLog(L"C:/snapshot-source",L"C:/snapshot-destination",error,cap);return std::pair{ok,error};};
    for(const auto sentinel:{ERROR_ACCESS_DENIED,ERROR_FILE_NOT_FOUND,ERROR_SHARING_VIOLATION}){
        for(const auto mode:{Mode::negativeSize,Mode::earlyEof,Mode::shortWrite}){
            copy_port::reset(mode,sentinel);const auto [ok,error]=run();
            const char* stage=mode==Mode::negativeSize?"negative-size":mode==Mode::earlyEof?"early-eof":"short-write";
#ifdef SNAPSHOT_OLD_CONTROL
            const auto name=std::string("old-success-")+stage+"-misreports-stale-error-"+std::to_string(sentinel);
            check(name.c_str(),!ok&&error==sentinel);
#else
            const DWORD expected=mode==Mode::negativeSize?ERROR_INVALID_DATA:mode==Mode::earlyEof?ERROR_HANDLE_EOF:ERROR_WRITE_FAULT;
            const auto name=std::string("successful-")+stage+"-ignores-stale-error-"+std::to_string(sentinel);
            check(name.c_str(),!ok&&error==expected&&copy_port::state.inputCloses==1&&copy_port::state.outputCloses==(mode==Mode::negativeSize?0u:1u)&&copy_port::state.deletes==(mode==Mode::negativeSize?0u:1u)&&copy_port::state.deletedOnlyDestination);
#endif
        }
    }
#ifndef SNAPSHOT_OLD_CONTROL
    for(const auto mode:{Mode::sizeFail,Mode::readFail,Mode::writeFail}){
        for(const DWORD apiError:{DWORD{ERROR_ACCESS_DENIED},DWORD{ERROR_OPERATION_ABORTED},DWORD{ERROR_SUCCESS}}){
            copy_port::reset(mode,ERROR_FILE_NOT_FOUND,apiError);const auto [ok,error]=run();
            const DWORD expected=apiError?apiError:mode==Mode::sizeFail?ERROR_INVALID_DATA:mode==Mode::readFail?ERROR_HANDLE_EOF:ERROR_WRITE_FAULT;
            const auto name=std::string("failed-api-error-preserved-before-cleanup-")+std::to_string(static_cast<int>(mode))+"-"+std::to_string(apiError);
            check(name.c_str(),!ok&&error==expected&&copy_port::state.inputCloses==1&&copy_port::state.sourceShareCorrect&&copy_port::state.destinationCreateNew);
        }
    }
    copy_port::reset(Mode::normal,ERROR_ACCESS_DENIED);auto outcome=run();
    check("success-keeps-zero-error-without-inheriting-stale-cleanup-error",outcome.first&&outcome.second==ERROR_SUCCESS&&copy_port::state.copied=="snapshot"&&copy_port::state.deletes==0&&copy_port::state.inputCloses==1&&copy_port::state.outputCloses==1);
    copy_port::reset(Mode::normal);copy_port::state.source.clear();outcome=run();
    check("empty-snapshot-does-not-read-or-write-and-closes-both-handles",outcome.first&&outcome.second==0&&copy_port::state.reads==0&&copy_port::state.writes==0&&copy_port::state.inputCloses==1&&copy_port::state.outputCloses==1);
    copy_port::reset(Mode::shortReads);outcome=run();
    check("successful-short-reads-complete-original-observed-length",outcome.first&&outcome.second==0&&copy_port::state.copied=="snapshot"&&copy_port::state.reads==4);
    copy_port::reset(Mode::sourceGrowth);outcome=run();
    check("source-growth-after-size-is-not-an-unbounded-copy",outcome.first&&outcome.second==0&&copy_port::state.copied=="snapshot"&&copy_port::state.source=="snapshot-later-growth");
    copy_port::reset(Mode::normal);outcome=run(1);
    check("optional-size-cap-rejects-before-destination-create",!outcome.first&&outcome.second==ERROR_FILE_TOO_LARGE&&copy_port::state.creates==1&&copy_port::state.deletes==0);
    copy_port::reset(Mode::existingTarget);outcome=run();
    check("existing-destination-is-never-deleted-or-overwritten",!outcome.first&&outcome.second==ERROR_FILE_EXISTS&&copy_port::state.deletes==0&&copy_port::state.outputCloses==0&&copy_port::state.inputCloses==1);
    copy_port::reset(Mode::outputCloseFail,ERROR_FILE_NOT_FOUND,ERROR_WRITE_FAULT);outcome=run();
    check("output-close-error-remains-first-failure-through-input-close-delete",!outcome.first&&outcome.second==ERROR_WRITE_FAULT&&copy_port::state.deletes==1&&copy_port::state.inputCloses==1);
    const auto guide=aiReadingGuide();check("AI-guide-does-not-promote-copy-to-whole-source-or-durable-proof",guide["sharedSnapshotCopy"]["coverage"].get<std::string>().find("concurrent writes")!=std::string::npos);
        const auto source=root/L"input.txt";const auto target=root/L"snapshot.txt";put(source,"source-before\n");DWORD error=0;
    HANDLE writable=CreateFileW(source.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    check("shared-active-writer-snapshot-succeeds",writable!=INVALID_HANDLE_VALUE && snapshotSharedLog(source,target,error));
    if(writable!=INVALID_HANDLE_VALUE)CloseHandle(writable);
    put(source,"source-after\n");std::ifstream copied(target);std::string copyText;std::getline(copied,copyText);copied.close();
    check("snapshot-independent-of-subsequent-writes",copyText=="source-before");
    check("existing-destination-never-overwritten",!snapshotSharedLog(source,target,error) && error==ERROR_FILE_EXISTS);
    std::ifstream original(source);std::string originalText;std::getline(original,originalText);original.close();
    check("copy-never-modifies-original",originalText=="source-after");

    const auto emptySource=root/L"empty-source.txt",emptyTarget=root/L"empty-target.txt";put(emptySource,"");
    check("real-empty-source-copy-remains-valid",snapshotSharedLog(emptySource,emptyTarget,error)&&error==0&&fspath::file_size(emptyTarget)==0);
    check("real-optional-size-limit-keeps-source-and-no-output",!snapshotSharedLog(source,root/L"over-cap.txt",error,1)&&error==ERROR_FILE_TOO_LARGE&&!fspath::exists(root/L"over-cap.txt")&&bytes(source)=="source-after\n");
    const auto missing=root/L"missing.txt",missingTarget=root/L"missing-copy.txt";
    check("real-missing-source-does-not-create-output",!snapshotSharedLog(missing,missingTarget,error)&&error==ERROR_FILE_NOT_FOUND&&!fspath::exists(missingTarget));

    SnapshotCopyEvidence observation;
    auto observationJson=snapshotCopyEvidence(observation);
    check("copy-observation-not-started-size-is-null-not-zero",observationJson["stage"]=="not-started"&&observationJson["sourceBytesAtOpen"].is_null()&&!observationJson["sourceSizeKnown"].get<bool>()&&!observationJson["copyComplete"].get<bool>());
    const auto observedRun=[&](unsigned long long cap=0){DWORD code=0;const bool ok=snapshot_fault::snapshotSharedLog(L"C:/snapshot-source",L"C:/snapshot-destination",code,cap,&observation);return std::pair{ok,code};};
    bool captureOnly=false;
    const SnapshotSourceSync sync=[&](const auto& capture){
        capture();const auto& s=copy_port::state;
        captureOnly=s.creates==1&&s.sizes==1&&s.reads==0&&s.writes==0&&s.inputCloses==0;
        return true;
    };
    const auto syncedRun=[&]{DWORD code=0;const bool ok=snapshot_fault::snapshotSharedLog(L"C:/snapshot-source",L"C:/snapshot-destination",code,0,&observation,&sync);return std::pair{ok,code};};
    copy_port::reset(Mode::sourceGrowth);outcome=syncedRun();
    check("synchronized callback ends before destination open/read/write and pins the original length",outcome.first&&captureOnly&&copy_port::state.copied=="snapshot"&&copy_port::state.inputCloses==1&&snapshotCopyEvidence(observation)["monitorWriterBoundary"]=="captured-under-writer-lock");
    copy_port::reset(Mode::sizeFail);outcome=syncedRun();
    check("synchronized size failure releases source and preserves original error",!outcome.first&&outcome.second==ERROR_SHARING_VIOLATION&&captureOnly&&copy_port::state.inputCloses==1&&!observation.destinationCreated&&snapshotCopyEvidence(observation)["monitorWriterBoundary"]=="source-size-failed");
    observation.sourceSizeKnown=true;observation.sourceBytesAtOpen=999;observation.successfulReadBytes=999;observation.copyComplete=true;
    copy_port::reset(Mode::normal);outcome=observedRun();
    check("copy-observation-resets-and-retains-original-source-length",outcome.first&&outcome.second==0&&observation.sourceSizeKnown&&observation.sourceBytesAtOpen==8&&observation.successfulReadBytes==8&&observation.successfulWriteBytes==8&&observation.readCalls==1&&observation.writeCalls==1&&observation.destinationCreated&&observation.copyComplete&&std::string(observation.stage)=="copied");
    copy_port::reset(Mode::normal);copy_port::state.source.clear();outcome=observedRun();observationJson=snapshotCopyEvidence(observation);
    check("copy-observation-real-zero-length-does-not-become-unknown",outcome.first&&observationJson["sourceBytesAtOpen"]==0&&observation.sourceSizeKnown&&observation.readCalls==0&&observation.writeCalls==0&&observation.copyComplete);
    copy_port::reset(Mode::sourceGrowth);outcome=observedRun();
    check("copy-observation-source-growth-stays-opened-prefix-without-extra-probe",outcome.first&&observation.sourceBytesAtOpen==8&&observation.successfulReadBytes==8&&observation.successfulWriteBytes==8&&copy_port::state.copied=="snapshot"&&copy_port::state.source=="snapshot-later-growth"&&copy_port::state.sizes==1);
    copy_port::reset(Mode::shortReads);outcome=observedRun();
    check("copy-observation-short-read-progress-keeps-original-call-count",outcome.first&&observation.successfulReadBytes==8&&observation.successfulWriteBytes==8&&observation.readCalls==4&&observation.writeCalls==4&&copy_port::state.reads==4&&copy_port::state.writes==4);
    copy_port::reset(Mode::earlyEof);outcome=observedRun();
    check("copy-observation-early-eof-keeps-size-and-first-failure-stage",!outcome.first&&outcome.second==ERROR_HANDLE_EOF&&observation.sourceBytesAtOpen==8&&observation.successfulReadBytes==0&&observation.readCalls==1&&observation.writeCalls==0&&std::string(observation.stage)=="source-read"&&!observation.copyComplete&&copy_port::state.deletes==1);
    copy_port::reset(Mode::readFail);outcome=observedRun();
    check("copy-observation-read-failure-does-not-certify-reported-bytes",!outcome.first&&outcome.second==ERROR_SHARING_VIOLATION&&observation.successfulReadBytes==0&&observation.readCalls==1&&std::string(observation.stage)=="source-read");
    copy_port::reset(Mode::shortWrite);outcome=observedRun();
    check("copy-observation-successful-short-write-retains-partial-count",!outcome.first&&outcome.second==ERROR_WRITE_FAULT&&observation.successfulReadBytes==8&&observation.successfulWriteBytes==7&&observation.writeCalls==1&&std::string(observation.stage)=="destination-write"&&!observation.copyComplete);
    copy_port::reset(Mode::writeFail);outcome=observedRun();
    check("copy-observation-write-failure-keeps-first-error-and-unknown-progress",!outcome.first&&outcome.second==ERROR_SHARING_VIOLATION&&observation.successfulWriteBytes==0&&observation.successfulReadBytes==8&&std::string(observation.stage)=="destination-write");
    copy_port::reset(Mode::negativeSize);outcome=observedRun();
    check("copy-observation-invalid-size-remains-null-before-destination",!outcome.first&&outcome.second==ERROR_INVALID_DATA&&!observation.sourceSizeKnown&&!observation.destinationCreated&&snapshotCopyEvidence(observation)["sourceBytesAtOpen"].is_null()&&std::string(observation.stage)=="source-size");
    copy_port::reset(Mode::normal);outcome=observedRun(1);
    check("copy-observation-cap-retains-known-length-with-no-destination",!outcome.first&&outcome.second==ERROR_FILE_TOO_LARGE&&observation.sourceSizeKnown&&observation.sourceBytesAtOpen==8&&!observation.destinationCreated&&std::string(observation.stage)=="size-limit");
    copy_port::reset(Mode::existingTarget);outcome=observedRun();
    check("copy-observation-existing-target-retains-no-overwrite-stage",!outcome.first&&outcome.second==ERROR_FILE_EXISTS&&!observation.destinationCreated&&observation.sourceSizeKnown&&std::string(observation.stage)=="destination-open"&&copy_port::state.deletes==0);
    copy_port::reset(Mode::outputCloseFail,ERROR_FILE_NOT_FOUND,ERROR_WRITE_FAULT);outcome=observedRun();
    check("copy-observation-close-failure-does-not-promote-completed-bytes",!outcome.first&&outcome.second==ERROR_WRITE_FAULT&&observation.successfulWriteBytes==8&&!observation.copyComplete&&std::string(observation.stage)=="destination-close");
    bool sameOperations=true;
    for(const auto mode:{Mode::normal,Mode::negativeSize,Mode::sizeFail,Mode::earlyEof,Mode::readFail,Mode::shortWrite,Mode::writeFail,Mode::outputCloseFail,Mode::shortReads,Mode::sourceGrowth,Mode::existingTarget}){
        copy_port::reset(mode);const auto oldOutcome=run();const auto oldState=copy_port::state;
        copy_port::reset(mode);const auto newOutcome=observedRun();const auto& state=copy_port::state;
        sameOperations=sameOperations&&oldOutcome==newOutcome&&oldState.source==state.source&&oldState.copied==state.copied&&oldState.creates==state.creates&&oldState.sizes==state.sizes&&oldState.reads==state.reads&&oldState.writes==state.writes&&oldState.inputCloses==state.inputCloses&&oldState.outputCloses==state.outputCloses&&oldState.deletes==state.deletes;
    }
    check("copy-observation-optional-port-preserves-all-original-api-sequences",sameOperations);
    const auto observedSource=root/L"observed-source.txt",observedTarget=root/L"observed-target.txt";put(observedSource,"{}\n{");
    check("copy-observation-real-partial-json-tail-copied-byte-exact",snapshotSharedLog(observedSource,observedTarget,error,0,&observation)&&observation.sourceBytesAtOpen==4&&observation.successfulReadBytes==4&&observation.successfulWriteBytes==4&&bytes(observedSource)=="{}\n{"&&bytes(observedTarget)=="{}\n{"&&stagedFileEvidence(observedTarget,"native-lifecycle.log")["partialOrInvalidTrailingRecord"].get<bool>());
    observation.sourceSizeKnown=true;observation.sourceBytesAtOpen=(1ULL<<53)+1;observation.successfulReadBytes=(1ULL<<53)+1;observationJson=snapshotCopyEvidence(observation);
    check("copy-observation-json-keeps-exact-native-64bit-counts",observationJson["sourceBytesAtOpen"].dump()=="9007199254740993"&&observationJson["successfulReadBytes"].dump()=="9007199254740993"&&!observationJson["sourceStabilityProven"].get<bool>()&&!observationJson["durableWriteProven"].get<bool>()&&!observationJson["resourceReturnProven"].get<bool>());

#endif
    unsigned failures=0;for(const auto& row:checks)if(!row["pass"].get<bool>())++failures;
    std::cout<<json({{"selection","snapshot-errors"},{"checks",checks},{"failures",failures},{"realSteamOrProductExecuted",false},
        {"faultScope","exact snapshot function and deterministic Win32 transport ports;original copy assertions also use real private files"}}).dump(2)<<'\n';return failures?2:0;
}

"""

def snapshot_error_source() -> str:
    """Bind original production copier body to deterministic Win32 ports."""
    source=(ROOT/'native/steam_log_export.h').read_text(encoding='utf-8-sig')
    function=extract(source,'inline bool snapshotSharedLog(')
    return SNAPSHOT_ERROR_TESTS.replace('/* SNAPSHOT_SHARED_FUNCTION */',function,1)

def rotation_source(source=None, selection="rotation", archive_phase=None):
    """Original archive helper,only the disappearing discovered-alias boundary."""
    text=_HASH_SOURCE if source is None else source
    start=text.index('static constexpr unsigned int LOG_ARCHIVE_MAX_ATTEMPTS')
    end=text.index('// 进程内实时检测 SMT',start)
    helper=text[start:end].replace('#include "sleep_export_diagnostics_native.inc"','// Sleep adapter has its own original component scope.')
    if selection not in ('rotation','snapshot-context','attempt-files'):raise ValueError('unsupported archive fixture selection')
    name={'rotation':'steam_log_export_rotation.cpp','snapshot-context':'steam_log_export_snapshot_context.cpp','attempt-files':'steam_log_export_attempt_files.cpp'}[selection]
    tests=(ROOT/'tools/fixtures'/name).read_text(encoding='utf-8-sig')
    if selection=='rotation':
        tests=tests.replace('/* ACTUAL_DOMAIN_LOG_SOURCE_BOUNDARY */', '\n'.join(extract(text, signature) for signature in (
            'static int domainLogStreamIndex(', 'static bool domainLogExportSourceBoundary(')),1)
        for path, field, token in (
            ('virtualGamepadLogPath', 'virtualGamepadWriterBoundary', 'VIRTUAL_GAMEPAD'),
            ('focusTraceLogPath', 'focusTraceWriterBoundary', 'FOCUS_TRACE'),
        ):
            begin=text.index('[](const auto& capture){',text.index('{'+path+', '))
            end=text.index(', "'+field+'"',begin)
            tests=tests.replace('/* ACTUAL_'+token+'_SOURCE_BOUNDARY */',text[begin:end],1)
        capture_boundary='\n'.join(extract(text,signature) for signature in (
            'static bool inputCaptureFlushLockedForExport(', 'static bool inputCaptureExportSourceBoundary('))
        tests=tests.replace('/* ACTUAL_INPUT_CAPTURE_SOURCE_BOUNDARY */',capture_boundary,1)
        tests=tests.replace('/* ACTUAL_TDP_SOURCE_BOUNDARY */',extract(text,
            'static bool tdpObservationExportSourceBoundary('),1)
    if archive_phase is not None:
        if selection!='attempt-files' or archive_phase not in ('timing','timing-resource','index-work','snapshot-work','handle-close','cleanup','cleanup-returns','promotion','capture-finalization'):raise ValueError('unsupported original archive timing selection')
        terminal='        try{exportTerminal["archiveAttemptTiming"]=archiveAttemptTimingEvidence(packed);}catch(...){}'
        if text.count(terminal)!=1:raise ValueError('actual optional archive terminal block missing or ambiguous')
        tests=tests.replace('/* ACTUAL_ARCHIVE_TIMING_TERMINAL */',terminal,1)
        tests='#define YMCC_ARCHIVE_TIMING_ONLY 1\n'+tests
        if archive_phase=='index-work':tests='#define YMCC_ARCHIVE_INDEX_WORK_ONLY 1\n'+tests
        if archive_phase=='snapshot-work':tests='#define YMCC_ARCHIVE_SNAPSHOT_WORK_ONLY 1\n'+tests
        if archive_phase=='handle-close':tests='#define YMCC_ARCHIVE_HANDLE_CLOSE_ONLY 1\n'+tests
        if archive_phase=='cleanup':tests='#define YMCC_ARCHIVE_CLEANUP_ONLY 1\n'+tests
        if archive_phase=='cleanup-returns':tests='#define YMCC_ARCHIVE_CLEANUP_RETURN_ONLY 1\n'+tests
        if archive_phase=='promotion':tests='#define YMCC_ARCHIVE_PROMOTION_ONLY 1\n'+tests
        if archive_phase=='timing-resource':tests='#define YMCC_ARCHIVE_TIMING_RESOURCE_ONLY 1\n'+tests
    prefix='#define YMCC_CAPTURE_FINALIZATION_ONLY 1\n' if archive_phase=='capture-finalization' else ''
    ports='#include "log_export_promotion_ports.inc"\n' if archive_phase=='promotion' else '#include "log_export_snapshot_work_ports.inc"\n' if archive_phase=='snapshot-work' else ''
    unports='\n#undef MoveFileExW\n#undef CreateFileW\n' if archive_phase=='promotion' else '\n#undef snapshotSharedLog\n#undef stagedFileEvidence\n#undef CopyFileW\n' if archive_phase=='snapshot-work' else ''
    return prefix+PRELUDE+'\n'+ports+helper+unports+'\n'+tests


def run_shared_facts(output_dir: pathlib.Path, selection: str = 'shared-facts', archive_phase=None) -> int:
    """Explicit original-fixture projection/freshness scope; no archive workload."""
    import os
    out=output_dir.resolve();out.mkdir(parents=True,exist_ok=True)
    run=out/('shared-facts-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f'));run.mkdir()
    def save(name, value):
        with (run/name).open('xb') as handle:
            handle.write(value.encode('utf-8') if isinstance(value,str) else value);handle.flush();os.fsync(handle.fileno())
    cpp=run/'harness.cpp';exe=run/'harness.exe';save('harness.cpp',rotation_source(selection=selection,archive_phase=archive_phase) if selection in ('rotation','snapshot-context','attempt-files') else snapshot_error_source() if selection=='snapshot-errors' else SHARED_FACT_ROOT_SELECTION_TESTS if selection=='shared-facts-root-selection' else SHARED_FACT_PRIORITY_TESTS if selection=='shared-facts-priority' else SHARED_FACT_TESTS)
    vcvars=pathlib.Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
    env_script='import {calibrationToolchainPreamble} from "./tools/fixtures/hc_reviewed_calibration/toolchain.mjs";process.stdout.write(calibrationToolchainPreamble(process.argv[1]));'
    env_run=subprocess.run([str(ROOT.parents[2]/'Environment/Toolchains/nodejs/node-v22.14.0-win-x64/node.exe'),'--input-type=module','-e',env_script,str(vcvars)],cwd=ROOT,capture_output=True,text=True,encoding='utf-8',check=True)
    command=f'cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 "{cpp}" /I"{ROOT / "deps/json"}" /I"{ROOT / "native"}" /I"{ROOT / "tools/fixtures"}" /Fo"{run / "harness.obj"}" /Fe"{exe}" /link advapi32.lib user32.lib bcrypt.lib'
    save('build.cmd',env_run.stdout+command+'\r\nexit /b %errorlevel%\r\n')
    compiled=subprocess.run(['cmd.exe','/d','/c',str(run/'build.cmd')],cwd=run,capture_output=True,text=True,errors='replace',timeout=180)
    save('compile.log',compiled.stdout+compiled.stderr)
    if compiled.returncode:print(compiled.stdout+compiled.stderr);return compiled.returncode
    tested=subprocess.run([str(exe),str(run/'fixtures 中文 空格')],cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=45)
    save('result.json',tested.stdout);save('stderr.log',tested.stderr)
    print(tested.stdout)
    return tested.returncode

def run_staged_inspection_case(args):
    """Original snapshot-errors consumer,explicit inspector-only boundary."""
    import os,time
    out=args.output_dir.resolve();out.mkdir(parents=True,exist_ok=True)
    run=out/('staged-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f'));run.mkdir(exist_ok=False)
    def put(p,b):
        with p.open('xb') as f:f.write(b.encode('utf8') if isinstance(b,str) else b)
    def pin(p):
        b=p.read_bytes();return dict(path=str(p.resolve()),bytes=len(b),sha256=hashlib.sha256(b).hexdigest())
    names=['native/log_export_evidence.h','native/buffered_domain_log.h','native/log_jsonl_boundary.h','deps/json/json.hpp','tools/fixtures/steam_log_staged_inspection.cpp']
    files=[ROOT/name for name in names]+[pathlib.Path(__file__),ROOT/'tools/log_frequency_selftest.py',ROOT/'native/main.cpp']
    if args.staged_before_header:files.append(args.staged_before_header.resolve())
    bound={p:p.read_bytes() for p in files}
    for name in names:put(run/pathlib.Path(name).name,bound[ROOT/name])
    header=bound[ROOT/'native/log_export_evidence.h'].decode('utf-8-sig').replace('\r\n','\n')
    original=extract(header,'inline EvidenceJson stagedFileEvidence(')
    measured=original.replace('inline EvidenceJson stagedFileEvidence(','inline EvidenceJson stagedFileEvidenceMeasured(',1)
    probes=[('if(length)in.read(storage.get(),static_cast<std::streamsize>(length));',
             'stagedFixtureBeforeRead();if(length)in.read(storage.get(),static_cast<std::streamsize>(length));'),
            ('if(stagedJsonLineTime(text.data()+cursor,text.data()+cursor+count,timeStorage,time,timeField)){++valid;',
             'stagedFixturePoint(1);if(stagedJsonLineTime(text.data()+cursor,text.data()+cursor+count,timeStorage,time,timeField)){++valid;stagedFixturePoint(2);')]
    for before,after in probes:
        assert measured.count(before)==1;measured=measured.replace(before,after,1)
    reverse=measured
    for before,after in reversed(probes):reverse=reverse.replace(after,before,1)
    reverse=reverse.replace('inline EvidenceJson stagedFileEvidenceMeasured(','inline EvidenceJson stagedFileEvidence(',1);assert reverse==original
    put(run/'staged-observed.inc','namespace ymcc::logexport {\n'+measured+'\n}\n')
    bindings=dict(source=pin(ROOT/'native/log_export_evidence.h'),probesReverseExact=True,sourceBodySha256=hashlib.sha256(original.encode()).hexdigest(),
        measuredSha256=hashlib.sha256(measured.encode()).hexdigest(),probeScope='controlled truncate before read, or arm next real C++ allocation around parse;no product fault macros')
    if args.staged_before_header:
        old=extract(bound[args.staged_before_header.resolve()].decode('utf-8-sig').replace('\r\n','\n'),'inline EvidenceJson stagedFileEvidence(')
        put(run/'staged-before.inc','namespace ymcc::logexport {\n'+old.replace('inline EvidenceJson stagedFileEvidence(','inline EvidenceJson stagedFileEvidenceBefore(',1)+'\n}\n')
        bindings['oldSource']=pin(args.staged_before_header.resolve())
    put(run/'STAGED-BINDING.json',json.dumps(bindings,indent=2))
    recipe=json.loads((ROOT/'tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json').read_text(encoding='utf-8-sig'))['native']
    exe=run/'Staged.exe';cpp=run/'steam_log_staged_inspection.cpp'
    command=[recipe['compiler'],'/nologo','/std:c++20','/utf-8','/EHsc','/O2','/MT','/W4','/WX','/showIncludes','/sourceDependencies',str(run/'dependencies.json'),
        *(['/DYMCC_STAGED_BEFORE_CONTROL'] if args.staged_before_header else []),*['/I'+p for p in recipe['include']],'/I'+str(run),str(cpp),'/Fo'+str(run/'Staged.obj'),'/Fe'+str(exe),'/link','/VERBOSE:LIB','/INCREMENTAL:NO',*['/LIBPATH:'+p for p in recipe['lib']],'kernel32.lib','user32.lib','advapi32.lib']
    start=time.monotonic();compiled=subprocess.run(command,cwd=run,capture_output=True,timeout=100)
    put(run/'compile.stdout.raw',compiled.stdout);put(run/'compile.stderr.raw',compiled.stderr);put(run/'COMPILE.json',json.dumps(dict(arguments=command,exitCode=compiled.returncode)))
    if compiled.returncode:print(compiled.stdout.decode(errors='replace')+compiled.stderr.decode(errors='replace'));return compiled.returncode
    executed=subprocess.run([str(exe),str(run/'fixtures'),args.staged_inspection_case],cwd=run,capture_output=True,timeout=45)
    put(run/'native.stdout.raw',executed.stdout);put(run/'native.stderr.raw',executed.stderr);put(run/'NATIVE-EXIT.json',json.dumps(dict(exitCode=executed.returncode)))
    result=json.loads(executed.stdout) if executed.stdout.strip() else dict(checks=[],failures=None,nativeOutputState='empty-output;outcome-unproven')
    result.update(nativeExitCode=executed.returncode,runDirectory=str(run),executable=pin(exe),sourceStable=all(p.read_bytes()==b for p,b in bound.items()),
        boundInputs=[pin(p) for p in files],binding=bindings,elapsedMs=round((time.monotonic()-start)*1000,3),defaultParentRun=False,productOrDevicesExecuted=False)
    put(run/'result.json',json.dumps(result,ensure_ascii=False,indent=2));put(out/'report.json',json.dumps(result,ensure_ascii=False,indent=2))
    print(json.dumps(dict(runDirectory=str(run),exitCode=executed.returncode,checks=len(result['checks']),failures=result['failures'])))
    return executed.returncode if result['sourceStable'] else 4


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir',type=pathlib.Path,default=ROOT.parents[2]/'Build/Validation/SteamLogExport')
    parser.add_argument('--case',choices=['shared-facts','shared-facts-priority','shared-facts-root-selection','snapshot-errors','rotation','snapshot-context','attempt-files'],default=None)
    parser.add_argument('--staged-inspection-case',choices=['control','faults','format'],help='Only original staged inspector classification;no copier/archive workload')
    parser.add_argument('--staged-before-header',type=pathlib.Path,help='Explicit previous staged inspector for normal-format parity')
    parser.add_argument('--archive-phase',choices=['timing','timing-resource','index-work','snapshot-work','handle-close','cleanup','cleanup-returns','promotion','capture-finalization'],help='Only original attempt-files timing or resource continuation')
    args=parser.parse_args()
    if args.archive_phase and (args.case!='attempt-files' or args.staged_inspection_case or args.staged_before_header):parser.error('archive timing requires exclusive attempt-files')
    if args.staged_inspection_case:
        if args.case!='snapshot-errors':parser.error('staged inspection requires snapshot-errors')
        if args.staged_inspection_case=='format' and not args.staged_before_header:parser.error('format inspection requires explicit old staged header')
        if args.staged_before_header and args.staged_inspection_case!='format':parser.error('old staged header is only for format parity')
        return run_staged_inspection_case(args)
    if args.staged_before_header:parser.error('old staged header requires explicit format inspection')
    if args.case:return run_shared_facts(args.output_dir,args.case,args.archive_phase)
    out=args.output_dir.resolve();out.mkdir(parents=True,exist_ok=True)
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
        check('virtual-first-witness-exported-byte-identical',z.read('virtual-adaptation.log')==(fixture/'virtual-adaptation.log').read_bytes())
        check('adaptation-first-witness-exported-byte-identical',z.read('model-adaptation.log')==(fixture/'model-adaptation.log').read_bytes())
        check('unlocked-originals-preserved-full-export',z.read('steam/logs/locked.txt')==b'locked-content\n')
        check('nested-steam-leading-dash-basename-retained',z.read('steam/logs/-leading-name.txt')==b'leading\n')
    with zipfile.ZipFile(fixture/'single-cost-active.zip') as z:
        original=(fixture/'single-cost-active/input-host-cost.log').read_bytes()
        check('single-20MB-cost-file-export-preserves-all-newest-bytes',z.read('input-host-cost.log')==original and b'"newest":true' in original.splitlines()[-1])
        check('single-cost-export-produces-no-nested-volumes',not any(n.endswith('.zip') or n.endswith('.pending') for n in z.namelist()))
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
    check('production-export-wired-to-shared-snapshot', 'ymcc::steaminfo::collect(' in handler and 'ymcc::steaminfo::Installations | ymcc::steaminfo::Presence, g_steamSettingsObserver, {}' in handler and re.search(r'createTarArchiveWithRetry\(zipPath, files, dataDir, manifestName, &steamLogs, &exportOperation, &writerSources,\s*&steamTailPaths, ymcc::logexport::ZipCompression::level5\)', handler) is not None)
    from ai_log_reader import iter_ai_records
    for archive_name in ('export.zip','export-all.zip','not-installed.zip','many.zip','many-ymcc.zip','single-cost-active.zip',pathlib.Path('中文 导出 😀')/'mixed-中文.zip'):
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
        if archive.parent==cost_dir or archive.name in ('launch-failed.zip','child-failed.zip','previous.zip','unicode-existing.zip'):
            continue
        with zipfile.ZipFile(archive) as z:
            check('all-members-pass-crc:'+str(archive.relative_to(fixture)),z.testzip() is None)
            check('no-staging-or-input-list-leak:'+str(archive.relative_to(fixture)),all(not n.startswith('./') and not any(part.startswith(('entry-','archive-inputs','tar-inputs')) or '.staging-' in part or '.attempt-' in part for part in pathlib.PurePosixPath(n).parts) for n in z.namelist()))
    large_zip=fixture/'large.zip'
    retry_zip=fixture/'unicode-retry.zip'
    check('unicode-retry-final-archive-created',retry_zip.is_file())
    if retry_zip.is_file():
        with zipfile.ZipFile(retry_zip) as z:
            check('unicode-retry-retains-member-bytes-and-crc',z.testzip() is None and z.read('app.log')==(fixture/'app.log').read_bytes())
    nested_zip=fixture/'unicode-nested.zip'
    check('unicode-nested-final-archive-created',nested_zip.is_file())
    if nested_zip.is_file():
        with zipfile.ZipFile(nested_zip) as z:
            check('unicode-nested-member-retains-name-bytes-and-crc',z.testzip() is None and z.read('steam/logs/frame-🎮.log')==b'nested-unicode\n')
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

"""Offline Steam export gate: compile production collector/copier/archive helper.
Only fixture files and Windows tar are exercised. Never starts YMCC/Steam,
reads/writes live registry values, or touches installed user logs.
"""
from __future__ import annotations
import argparse, hashlib, json, pathlib, subprocess, sys, zipfile, datetime
from log_frequency_selftest import ROOT, extract

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
struct RunOut { bool ran=false; DWORD exitCode=1; };
static unsigned tarCalls=0;
static RunOut runCapture(const std::wstring& command,DWORD budget) {
    ++tarCalls;RunOut result;
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION pi{};
    std::vector<wchar_t> mutableCommand(command.begin(),command.end());mutableCommand.push_back(0);
    if(!CreateProcessW(nullptr,mutableCommand.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&pi))return result;
    result.ran=true;
    if(WaitForSingleObject(pi.hProcess,budget)!=WAIT_OBJECT_0) {TerminateProcess(pi.hProcess,1);WaitForSingleObject(pi.hProcess,5000);}
    GetExitCodeProcess(pi.hProcess,&result.exitCode);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);return result;
}
static json checks=json::array();
static void check(const char* name,bool ok){checks.push_back({{"name",name},{"pass",ok}});}
static void put(const fspath::path& path,const std::string& text){fspath::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);out<<text;}
"""
_HASH_SOURCE=(ROOT/'native/main.cpp').read_text(encoding='utf-8-sig')
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
    const auto previous=root/L"previous.zip";put(previous,"retained-old-archive");const unsigned before=tarCalls;
    const auto failed=createTarArchiveWithRetry(previous.wstring(),{(root/L"does-not-exist.log").wstring()});
    std::ifstream retained(previous);std::string retainedText;std::getline(retained,retainedText);retained.close();
    check("mandatory-snapshot-failure-two-attempts-preserves-final",!failed.ok && failed.attempts==2 && retainedText=="retained-old-archive" && tarCalls==before);
    bool cleanup=true;for(const auto& entry:fspath::directory_iterator(root))if(entry.path().filename().wstring().find(L".staging-")!=std::wstring::npos || entry.path().filename().wstring().find(L".attempt-")!=std::wstring::npos)cleanup=false;
    check("only-temporary-staging-cleaned",cleanup && fspath::exists(active/L"logs"/L"locked.txt"));
    const auto outside=root/L"outside";put(outside/L"secret.txt","not-a-steam-log");
    const bool link=CreateSymbolicLinkW((active/L"logs"/L"outside-link").c_str(),outside.c_str(),SYMBOLIC_LINK_FLAG_DIRECTORY|0x2)!=FALSE;
    if(link){const auto linked=collectSteamLogs(std::vector<SteamRootCandidate>{{active,"fixture"}});check("directory-reparse-point-not-followed",linked.files.size()==7 && linked.issues.size()==1);}
    json report={{"checks",checks},{"failures",0},{"fixtureRoot",W2U(root.wstring())},{"reparsePointFixtureAvailable",link},{"scope","REAL_COLLECTOR_COPIER_ARCHIVE_HELPER_FIXTURE_ONLY_NO_PRODUCT_OR_REGISTRY"}};
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
    helper=text[start:end]
    cpp=run/'harness.cpp';exe=run/'harness.exe';cpp.write_text(PRELUDE+'\n'+helper+'\n'+TESTS,encoding='utf-8')
    vcvars=pathlib.Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat')
    command=f'call "{vcvars}" >nul && cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 "{cpp}" /I"{ROOT / "deps/json"}" /I"{ROOT / "native"}" /Fo"{run / "harness.obj"}" /Fe"{exe}" /link advapi32.lib'
    batch=run/'build.cmd';batch.write_text('@echo off\n'+command+'\n',encoding='utf-8')
    compiled=subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=run,capture_output=True,text=True,errors='replace')
    (run/'compile.log').write_text(compiled.stdout+compiled.stderr,encoding='utf-8')
    if compiled.returncode:print(compiled.stdout+compiled.stderr);return compiled.returncode
    header=ROOT/'native/steam_log_export.h';bound_header=header.read_bytes()
    ai_headers={n:(ROOT/'native'/n).read_bytes() for n in ('log_ai_index.h','log_export_evidence.h')}
    tested=subprocess.run([str(exe),str(run/'fixtures')],cwd=run,capture_output=True,encoding='utf-8',errors='replace')
    (run/'result.json').write_text(tested.stdout,encoding='utf-8')
    if not tested.stdout:print(tested.stderr);return tested.returncode or 1
    report=json.loads(tested.stdout)
    def check(name: str,ok: bool):
        report['checks'].append({'name':name,'pass':bool(ok)})
        if not ok:report['failures']+=1
    fixture=run/'fixtures'
    with zipfile.ZipFile(fixture/'export.zip') as z:
        check('zip-full-tree-unicode-rotations-and-zero-length-preserved',z.read('steam/logs/nested/中文 诊断.bin')==b'raw-bytes\n' and z.read('steam/logs/controller.previous.txt')==b'rotated-full\n' and z.read('steam/logs/empty.txt')==b'')
        check('same-basename-ymcc-and-steam-do-not-collide',z.read('app.log')==b'ymcc-app\n' and z.read('steam/logs/app.log')==b'steam-app\n')
        receipt=json.loads(z.read('steam/steam-log-export.json'))
        check('locked-file-explicit-receipt-not-in-zip',receipt['copied']==6 and receipt['skipped']==1 and 'steam/logs/locked.txt' not in z.namelist() and any(not f['copied'] and f['win32Error'] for f in receipt['files']))
        check('no-unrelated-install-or-outside-directory-content',not any('old.txt' in name or 'secret.txt' in name for name in z.namelist()))
    with zipfile.ZipFile(fixture/'export-all.zip') as z:check('unlocked-originals-preserved-full-export',z.read('steam/logs/locked.txt')==b'locked-content\n')
    current=(ROOT/'native/main.cpp').read_text(encoding='utf-8-sig')
    check('bound-archive-helper-stable',current[current.index('static constexpr unsigned int LOG_ARCHIVE_MAX_ATTEMPTS'):current.index('// 进程内实时检测 SMT',current.index('static constexpr unsigned int LOG_ARCHIVE_MAX_ATTEMPTS'))]==helper)
    check('collector-header-stable',header.read_bytes()==bound_header)
    handler=extract(current,'ipc_on("logs.exportAll", [](const json& exportArgs) -> json {')
    check('production-export-wired-to-shared-snapshot', 'collectSteamLogs(g_steamSettingsObserver.sessionSnapshot().pid)' in handler and 'createTarArchiveWithRetry(zipPath, files, dataDir, manifestName, &steamLogs)' in handler)
    from ai_log_reader import iter_ai_records
    for archive_name in ('export.zip','export-all.zip','not-installed.zip','many.zip'):
        records=list(iter_ai_records(fixture/archive_name,index_only=True))
        check('ai-reader-consumes-real-native-export:'+archive_name,records[-1]['coverageComplete'])
    check('ai-index-and-evidence-headers-stable',all((ROOT/'native'/n).read_bytes()==b for n,b in ai_headers.items()))
    report['aiHeaderSha256']={n:hashlib.sha256(b).hexdigest() for n,b in ai_headers.items()}
    report['sourceSha256']=hashlib.sha256(source).hexdigest();report['headerSha256']=hashlib.sha256(bound_header).hexdigest()
    report['productionOrHardwareExecuted']=False;report['runDirectory']=str(run)
    (out/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False,indent=2))
    return 1 if report['failures'] else tested.returncode
if __name__=='__main__':raise SystemExit(main())

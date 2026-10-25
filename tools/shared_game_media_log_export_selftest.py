"""Exercise passive shared-media snapshots through the production archive helper."""
import argparse,datetime,hashlib,json,pathlib,subprocess,zipfile
from steam_log_export_selftest import PRELUDE
from log_frequency_selftest import ROOT,extract
TEST=r'''
#include "shared_game_media_diagnostics.h"
int wmain(int argc,wchar_t**argv){if(argc!=2)return 2;const fspath::path root(argv[1]);fspath::create_directories(root);const auto data=root/L"media";put(data/L"config"/L"game-media-downloads.json","{\"enabled\":true,\"jobs\":{}}\n");put(data/L"config"/L"game-media-download-progress.json","{\"phase\":\"paused\",\"resourcePauseReason\":\"cpu-busy\",\"systemCpuPct\":37.5}\n");put(data/L"config"/L"game-media.json","{\"entries\":{},\"revision\":7}\n");put(data/L"state"/L"last-opti-client-supplement.json","{\"status\":\"synced\",\"cost\":{\"cpuMs\":15.625,\"elapsedMs\":55}}\n");put(data/L"artwork"/L"video"/L"do-not-export.mp4","video-body");put(data/L"auth.json","never export unrelated credentials");ymcc::media::diagnostics::rememberRoot(data);
 const auto copied=ymcc::media::diagnostics::exportMetadata(root/L"normal");std::vector<std::wstring>files;for(const auto&file:copied.files)files.push_back(file.wstring());put(root/L"normal"/L"log-export-availability.json",json({{"sharedGameMedia",copied.receipt}}).dump());files.push_back((root/L"normal"/L"log-export-availability.json").wstring());const auto normal=createTarArchiveWithRetry((root/L"media.zip").wstring(),files);check("production-archive-contains-media-snapshot",normal.ok);
 HANDLE locked=CreateFileW((data/L"config"/L"game-media-download-progress.json").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);check("isolated-progress-lock-admitted",locked!=INVALID_HANDLE_VALUE);const auto partial=ymcc::media::diagnostics::exportMetadata(root/L"partial");if(locked!=INVALID_HANDLE_VALUE)CloseHandle(locked);files.clear();for(const auto&file:partial.files)files.push_back(file.wstring());put(root/L"partial"/L"log-export-availability.json",json({{"sharedGameMedia",partial.receipt}}).dump());files.push_back((root/L"partial"/L"log-export-availability.json").wstring());const auto skipped=createTarArchiveWithRetry((root/L"partial.zip").wstring(),files);check("optional-locked-media-file-does-not-break-export",skipped.ok);
 unsigned failures=0;for(const auto&item:checks)if(!item["pass"].get<bool>())++failures;std::cout<<json({{"ok",failures==0},{"checks",checks},{"failures",failures}}).dump()<<std::endl;return failures?3:0;}
'''
def main():
 p=argparse.ArgumentParser();p.add_argument('--output-dir',type=pathlib.Path,required=True);a=p.parse_args();out=a.output_dir.resolve();out.mkdir(parents=True,exist_ok=False)
 text=(ROOT/'native/main.cpp').read_text(encoding='utf-8-sig');start=text.index('static constexpr unsigned int LOG_ARCHIVE_MAX_ATTEMPTS');end=text.index('// 进程内实时检测 SMT',start);helper=text[start:end];cpp=out/'harness.cpp';cpp.write_text(PRELUDE+'\n'+helper+'\n'+TEST,encoding='utf-8');exe=out/'harness.exe';batch=out/'build.cmd';vc=r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat';batch.write_text('@echo off\nset "PATH=%SystemRoot%\\System32;%PATH%"\ncall "'+vc+'" >nul && cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 "'+str(cpp)+'" /I"'+str(ROOT/'native')+'" /I"'+str(ROOT/'deps/json')+'" /Fo"'+str(out/'harness.obj')+'" /Fe"'+str(exe)+'" /link advapi32.lib shell32.lib user32.lib\n',encoding='utf-8')
 built=subprocess.run(['cmd.exe','/d','/c',str(batch)],capture_output=True,text=True,errors='replace');(out/'build.log').write_text(built.stdout+built.stderr,encoding='utf-8');
 if built.returncode:print(built.stdout+built.stderr);return built.returncode
 ran=subprocess.run([str(exe),str(out/'fixtures')],capture_output=True,text=True,errors='replace');(out/'native.log').write_text(ran.stdout+ran.stderr,encoding='utf-8');
 if ran.returncode:print(ran.stdout+ran.stderr);return ran.returncode
 result=json.loads(ran.stdout)
 def check(label,ok):result['checks'].append({'name':label,'pass':bool(ok)});result['failures']+=not ok
 with zipfile.ZipFile(out/'fixtures/media.zip') as z:
  for n in ['game-media-downloads.json','game-media-download-progress.json','game-media.json']:
   check(n+' exact source snapshot packed',z.read(n)==(out/'fixtures/media/config'/n).read_bytes())
  check('OPT performance receipt exact source snapshot packed',z.read('last-opti-client-supplement.json')==(out/'fixtures/media/state/last-opti-client-supplement.json').read_bytes())
  check('OPT cost kept separate from wallpaper CPU metrics',json.loads(z.read('last-opti-client-supplement.json'))['cost']['cpuMs']==15.625)
  check('pause reason and CPU value survive actual ZIP',json.loads(z.read('game-media-download-progress.json'))['resourcePauseReason']=='cpu-busy' and json.loads(z.read('game-media-download-progress.json'))['systemCpuPct']==37.5)
  check('no videos or unrelated credentials packed',not any(n.endswith(('.mp4','auth.json')) for n in z.namelist()))
 with zipfile.ZipFile(out/'fixtures/partial.zip') as z:
  check('locked state omitted without stale substitution','game-media-download-progress.json' not in z.namelist())
  states=json.loads(z.read('log-export-availability.json'))['sharedGameMedia']['files'];check('locked state has explicit export error',any(s['name']=='game-media-download-progress.json' and not s['copied'] and s.get('win32Error') for s in states))
 handler=extract(text,'ipc_on("logs.exportAll", [](const json& exportArgs) -> json {');accessor=extract(text,'static ymcc::media::Store& sharedGameMediaStore() {')
 check('actual export handler uses passive metadata helper','diagnostics::exportMetadata(fspath::path(dataDir))' in handler and 'for(const auto& path:mediaExport.files)addFile(path.wstring())' in handler)
 check('actual store remembers its already-selected root','diagnostics::rememberRoot(root)' in accessor)
 check('export hook does not start or select a media root','sharedGameMediaStore()' not in handler and 'defaultDataRoot(' not in handler)
 result['ok']=result['failures']==0;result['scope']='actual passive media exporter, shared snapshot copier and production archive helper; locked-file fixture and ZIP bytes; no live application/cache/Steam actions';result['helperSha256']=hashlib.sha256(helper.encode()).hexdigest();(out/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps({'ok':result['ok'],'checks':len(result['checks']),'failures':result['failures']}));return 0 if result['ok'] else 3
if __name__=='__main__':raise SystemExit(main())

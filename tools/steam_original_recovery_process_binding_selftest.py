from pathlib import Path
import argparse,subprocess,json,hashlib,os,datetime

def main():
 a=argparse.ArgumentParser();a.add_argument('--output',type=Path,required=True);args=a.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
 repo=Path(__file__).resolve().parent.parent;inputs=['native/sleep_steam_tracking.h','native/sleep_steam_capture.h','native/steam_game_link_runtime.h','tools/fixtures/steam_original_recovery_valve_guard/process-binding-cases.cpp.in','deps/json/json.hpp']
 frozen={p:(repo/p).read_bytes()for p in inputs};snapshot=Path('G:/YeManCC-Work/Testing/YMCC/Evidence/RogRecoveryFailure-20261023-133711/live/gameprocess-binding-before-fault.txt')
 tools=json.loads((repo/'tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json').read_text(encoding='utf-8-sig'))['native'];env={k.upper():v for k,v in os.environ.items()};env['PATH']=';'.join([str(Path(tools['compiler']).parent),'C:/Windows/System32','C:/Windows']);env['INCLUDE']=';'.join(tools['include']);env['LIB']=';'.join(tools['lib'])
 for k in ['CL','_CL_','LINK','_LINK_']:env[k]=''
 (out/'json.hpp').write_bytes(frozen['deps/json/json.hpp']);cpp=frozen[inputs[3]].decode('utf-8-sig')
 variants=[('current',None,None,None)]
 variants += [('process-mode-uses-ui-remove','native/sleep_steam_tracking.h','scope==TrackingScope::RunningListAndProcess','(scope==TrackingScope::RunningListAndProcess||scope==TrackingScope::ProcessLifetime)'),('inventory-process-mode-uses-ui-remove','native/sleep_steam_capture.h','scope==TrackingScope::RunningListAndProcess','(scope==TrackingScope::RunningListAndProcess||scope==TrackingScope::ProcessLifetime)'),('explicit-drop-ignored','native/sleep_steam_tracking.h','if(drop){if(live.erase(id))removed=true;continue;}','if(drop){continue;}'),('client-reset-ignored','native/sleep_steam_tracking.h','if(text.rfind("Client version:",0)==0){live.clear();removed=false;continue;}','if(text.rfind("Client version:",0)==0){continue;}'),('wrong-path-allowed','native/sleep_steam_tracking.h','pathKey(commandExe(action.substr(end+marker.size())))!=pathKey(exe)','false'),('wrong-pid-allowed','native/sleep_steam_tracking.h','candidate!=pid','false')]
 expected={'process-mode-uses-ui-remove':('process-lifetime-retains-exact-launch','process-launch-lost-on-ui-remove'),'inventory-process-mode-uses-ui-remove':('process-lifetime-retains-exact-launch','process-inventory-lost-on-ui-remove'),'explicit-drop-ignored':('explicit-pid-drop-still-revokes','explicit-drop-ignored'),'client-reset-ignored':('new-steam-client-clears-old-association','client-reset-ignored'),'wrong-path-allowed':('wrong-original-path-refused','wrong-path-accepted'),'wrong-pid-allowed':('wrong-original-pid-refused','wrong-pid-accepted')}
 rows=[]
 for name,target,old,new in variants:
  d=out/name;d.mkdir()
  for header in ['native/sleep_steam_tracking.h','native/sleep_steam_capture.h']:
   text=frozen[header].decode('utf-8-sig').replace('\r\n','\n')
   if target==header:
    if text.count(old)!=1:raise RuntimeError('mutation ambiguous '+name)
    text=text.replace(old,new)
   (d/Path(header).name).write_text(text,encoding='utf8')
  src=d/'fixture.cpp';src.write_text(cpp,encoding='utf8');exe=d/'fixture.exe'
  built=subprocess.run([tools['compiler'],'/nologo','/std:c++20','/utf-8','/EHsc','/MT','/O1','/W4','/WX','/I'+str(d),'/I'+str(out),str(src),'/Fo'+str(d/'fixture.obj'),'/Fe'+str(exe)],env=env,capture_output=True,timeout=60)
  (d/'compile.log').write_bytes(built.stdout+built.stderr);row={'name':name,'compileExit':built.returncode}
  if built.returncode==0:
   run=subprocess.run([str(exe),str(snapshot)],capture_output=True,env=env,timeout=10);(d/'run.log').write_bytes(run.stdout+run.stderr);detail=json.loads(run.stdout.decode('utf-8-sig'));(d/'result.json').write_text(json.dumps(detail,indent=2),encoding='utf8');row.update({'runExit':run.returncode,'tests':detail['tests'],'checks':detail['checks'],'passed':detail['passed']})
   if name=='current':row['detected']=run.returncode==0 and detail['passed']
   else:
    case,why=expected[name];row['expectedFailure']={'case':case,'reason':why};row['detected']=run.returncode==1 and any(c['name']==case and c.get('reason')==why and not c['passed']for c in detail['cases'])
  rows.append(row)
 runtime=frozen['native/steam_game_link_runtime.h'].decode('utf-8-sig');start=runtime.index('static TrackerRead tracking(');end=runtime.index('\nstatic ',start+1);body=runtime[start:end]
 result={'schema':'ymcc.original-process-log-binding-regression.v1','at':datetime.datetime.now().astimezone().isoformat(),'runs':rows,'realSnapshot':{'path':str(snapshot),'sha256':hashlib.sha256(snapshot.read_bytes()).hexdigest()},'runtimeUsesExplicitProcessScopeTwice':body.count('TrackingScope::ProcessLifetime')==2,'sourcePins':[{'path':p,'sha256':hashlib.sha256(b).hexdigest()}for p,b in frozen.items()],'sourceDrift':[p for p,b in frozen.items()if(repo/p).read_bytes()!=b],'realSteamActions':0,'sleepActions':0}
 result['passed']=all(r.get('compileExit')==0 and r.get('detected')for r in rows)and not result['sourceDrift'] and result['runtimeUsesExplicitProcessScopeTwice'];(out/'result.json').write_text(json.dumps(result,indent=2),encoding='utf8');print(json.dumps(result));return 0 if result['passed']else 1
if __name__=='__main__':raise SystemExit(main())

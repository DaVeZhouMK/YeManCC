"""Reproduce the normal lighting copy boundary; never launch YMCC/HC/device IO."""
from pathlib import Path, PurePosixPath
import argparse, datetime, hashlib, json, os, shutil, stat, subprocess, sys, time, zipfile

def sha(p):
    h=hashlib.sha256()
    with Path(p).open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''): h.update(b)
    return h.hexdigest()

def read(p): return json.loads(Path(p).read_text(encoding='utf-8-sig'))
def write(p,v):
    p=Path(p);p.parent.mkdir(parents=True,exist_ok=True)
    p.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def need(ok,code):
    if not ok: raise RuntimeError(code)
def scoped(root,relative):
    root=Path(root).resolve();p=PurePosixPath(relative)
    need(relative and not p.is_absolute() and '\\' not in relative and ':' not in relative and all(x not in ('','.','..') for x in relative.split('/')),'UNSAFE_FIXTURE_PATH')
    result=root.joinpath(*p.parts)
    need(result.resolve().is_relative_to(root),'FIXTURE_PATH_ESCAPE')
    return result

def extract(archive,destination,contract):
    need(sha(archive)==contract['zip']['sha256'] and archive.stat().st_size==contract['zip']['bytes'],'FIXTURE_ZIP_IDENTITY')
    with zipfile.ZipFile(archive) as z:
        entries=z.infolist();need(len(entries)==contract['zip']['members']<=2048,'FIXTURE_COUNT')
        names=set();total=0
        for e in entries:
            scoped(destination,e.filename)
            need(not e.is_dir() and len(e.filename)<=512 and e.filename.casefold() not in names,'FIXTURE_DUPLICATE_OR_DIRECTORY')
            names.add(e.filename.casefold());mode=(e.external_attr>>16)&0xf000
            need(mode!=stat.S_IFLNK and (e.external_attr&0x400)==0,'FIXTURE_REPARSE')
            need(0<=e.file_size<=134217728,'FIXTURE_MEMBER_BOUND');total+=e.file_size
            need(total<=536870912,'FIXTURE_TOTAL_BOUND')
        need(total==contract['zip']['uncompressedBytes'],'FIXTURE_TOTAL_IDENTITY')
        destination.mkdir(exist_ok=False)
        for e in entries:
            p=scoped(destination,e.filename);p.parent.mkdir(parents=True,exist_ok=True)
            with z.open(e) as src,p.open('xb') as dst:shutil.copyfileobj(src,dst)
    ip=destination/contract['index']['name'];need(sha(ip)==contract['index']['sha256'],'FIXTURE_INDEX_IDENTITY')
    index=read(ip);need(len(index['members'])+1==len(entries),'FIXTURE_INDEX_COVERAGE')
    for row in index['members']:
        p=scoped(destination,row['path']);need(p.stat().st_size==row['bytes'] and sha(p)==row['sha256'],'FIXTURE_MEMBER_BYTES')
    return index

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--out',required=True);ap.add_argument('--ps7',required=True);ap.add_argument('--ps51',required=True)
    a=ap.parse_args();repo=Path(__file__).resolve().parents[4];out=Path(a.out).resolve()
    need(not out.exists(),'FRESH_EVIDENCE_REQUIRED')
    need(out.drive.casefold()=='d:' and not out.is_relative_to(repo),'D_ONLY_EVIDENCE')
    for p in [out,*out.parents]:need(not p.is_symlink() and not p.is_junction(),'OUTPUT_REPARSE')
    out.mkdir(parents=True);processes=[];steps=[];started=datetime.datetime.now(datetime.timezone.utc).isoformat()
    fixed=repo/'tools/ymcc_sim/global/lighting-component-continuity/v4';contract=read(fixed/'fixture.contract.json')
    relatives=['tools/deploy-installed.ps1','tools/package-release.ps1','tools/build-workspace.ps1','tools/lighting-build-contract.ps1','tools/release-export-identity.ps1','tools/ymcc_sim/global/lighting-installed-preflight/run.py','tools/ymcc_sim/global/lighting-installed-preflight/Invoke-CopyLane.ps1','tools/ymcc_sim/global/lighting-installed-preflight/README.md','tools/ymcc_sim/global/lighting-component-continuity/v4/Archive-IO.ps1','tools/ymcc_sim/global/lighting-component-continuity/v4/fixture.contract.json']+[r['path'] for r in contract['frozenTools']]
    source={r:sha(repo/r) for r in relatives}
    for row in contract['frozenTools']:need(source[row['path']]==row['sha256'],'FROZEN_TOOL_DRIFT')
    write(out/'source-inputs.json',source)
    def execute(name,engine,args,expect_success=True):
        log=out/'processes';log.mkdir(exist_ok=True)
        stdout=log/(name+'.stdout');stderr=log/(name+'.stderr');command=[str(engine),'-NoLogo','-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass',*map(str,args)]
        with stdout.open('wb') as so,stderr.open('wb') as se:
            proc=subprocess.Popen(command,stdout=so,stderr=se,creationflags=0x08000000,cwd=repo)
            row={'name':name,'pid':proc.pid,'command':command,'startedUtc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'stdout':str(stdout),'stderr':str(stderr)};processes.append(row);write(out/'live-processes.json',processes)
            code=proc.wait();row.update(exitCode=code,naturalExit=True,finishedUtc=datetime.datetime.now(datetime.timezone.utc).isoformat());write(out/'live-processes.json',processes)
        need((code==0)==expect_success,'UNEXPECTED_PROCESS_EXIT:'+name+':'+str(code));return row
    try:
        fixture=out/'fixture';index=extract(fixed/'fixture.zip',fixture,contract);tuple_=contract['fixedTuple']
        member_map={r['path']:r for r in index['members']}
        def restore(root):
            rows=[r for r in index['members'] if r['path'].startswith('fixture-before/')]
            need(len(rows)==105,'INSTALLATION_FIXTURE_SET')
            for row in rows:
                rel=row['path'][len('fixture-before/'):];p=scoped(root,rel);p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(fixture/row['path'],p);need(sha(p)==row['sha256'],'RESTORED_PREIMAGE_DRIFT')
        def inventory(root):return {p.relative_to(root).as_posix():sha(p) for p in sorted(root.rglob('*')) if p.is_file()}
        old_native='legacy/deps/lightsetter-admission/candidate7b-v1/fixture/before/YeManCC/YeManCC.exe'
        old_native_sha=member_map[old_native]['sha256'];need(old_native_sha=='9e9e31d88aad4fa537e1453e1b15fd9c5c12f28fdf854849f0faa7e8ebd8f50a','OLD_NATIVE_IDENTITY')
        for engine_name,engine in [('ps7',a.ps7),('ps51',a.ps51)]:
            case=out/('copy-'+engine_name);install=case/'installation';restore(install);shutil.copyfile(fixture/old_native,install/'YeManCC/YeManCC.exe')
            source_pc=case/'source/PowerControl';payload=source_pc/'light-setter';payload.mkdir(parents=True)
            for p in (fixture/'v4/bundle/runtime').iterdir():shutil.copyfile(p,payload/p.name)
            before=inventory(install)
            execute('original-copy-'+engine_name,engine,['-File',Path(__file__).with_name('Invoke-CopyLane.ps1'),'-DeploySource',repo/'tools/deploy-installed.ps1','-SourcePowerControl',source_pc,'-InstalledRoot',install,'-BackupRoot',case/'backup','-EvidenceDir',case,'-ExpectedHelperSha256',tuple_['helper'],'-ExpectedNativeSha256',old_native_sha])
            raw=read(case/'result.json');after=inventory(install);changed=[k for k in before if before[k]!=after[k]]
            need(raw['status']=='GAP_REPRODUCED' and raw['wrongTupleCopied'] and set(before)==set(after) and changed==['PowerControl/light-setter/YeManLightSetter.dll'],'COPY_SUBLANE_EXACT_CHANGE_SET')
            steps.append({'name':'original-copy-'+engine_name,'status':'GAP_REPRODUCED','helperChanged':True,'nativeUnchanged':True,'changedFiles':changed,'preservedFiles':len(before)-len(changed),'receipt':{'path':str(case/'result.json'),'sha256':sha(case/'result.json')}})
        for engine_name,engine in [('ps7',a.ps7),('ps51',a.ps51)]:
            case=out/('locked-copy-'+engine_name);install=case/'installation';restore(install);shutil.copyfile(fixture/old_native,install/'YeManCC/YeManCC.exe')
            source_pc=case/'source/PowerControl';payload=source_pc/'light-setter';payload.mkdir(parents=True)
            for p in (fixture/'v4/bundle/runtime').iterdir():shutil.copyfile(p,payload/p.name)
            before=inventory(install)
            execute('locked-copy-'+engine_name,engine,['-File',Path(__file__).with_name('Invoke-CopyLane.ps1'),'-DeploySource',repo/'tools/deploy-installed.ps1','-SourcePowerControl',source_pc,'-InstalledRoot',install,'-BackupRoot',case/'backup','-EvidenceDir',case,'-ExpectedHelperSha256',tuple_['helper'],'-ExpectedNativeSha256',old_native_sha,'-Mode','LockedSource'])
            raw=read(case/'result.json');backup_helper=case/'backup/PowerControl/light-setter/YeManLightSetter.dll'
            need(raw['status']=='GAP_REPRODUCED' and raw['copyFailedWithoutLocalRollback'] and sha(backup_helper)==before['PowerControl/light-setter/YeManLightSetter.dll'],'COPY_FAILURE_NOT_REPRODUCED')
            steps.append({'name':'locked-copy-'+engine_name,'status':'GAP_REPRODUCED','oldPayloadMovedBeforeCopyFailure':True,'oldHelperAvailableAtBackup':True,'localAutomaticRollback':False,'error':raw['copyError'],'receipt':{'path':str(case/'result.json'),'sha256':sha(case/'result.json')}})
        install=out/'controlled-connector/installation';restore(install)
        args=['-ProjectRoot',index['originalProjectRoot'],'-HelperCandidateRoot',index['originalHelperCandidate'],'-HelperWorkspaceRoot',index['originalHelperWorkspace'],'-HelperSnapshotRoot',str(fixture),'-NativeWorkspaceRoot',index['originalNativeWorkspace'],'-InstallRoot',str(install),'-OutputRoot',str(out/'delivery'),'-InventoryTemplatePath',str(fixture/'legacy/deps/lightsetter-admission/candidate7b-v1/proofs/original-plan.json')]
        args_path=out/'generator-arguments.json';write(args_path,args)
        execute('actual-plan-generator',a.ps7,['-File',fixed/'Archive-IO.ps1','-CaseRoot',out,'-FixtureRoot',fixture,'-Phase','helper','-Tool',repo/'tools/hc-device-runtime/New-LightingBuildDelivery.ps1','-ArgumentsPath',args_path,'-TracePath',out/'archive-io.json'])
        plan=out/'delivery/plan.json';need(read(plan)['kind']=='lighting-build-v4','ACTUAL_GENERATED_PLAN_KIND')
        shutil.copyfile(fixture/old_native,install/'YeManCC/YeManCC.exe');before=inventory(install)
        execute('actual-connector-wrong-native',a.ps7,['-File',repo/'tools/hc-device-runtime/connect-installed-lighting.ps1','-PlanPath',plan],False)
        raw_path=out/'delivery/connection/verification.json';raw=read(raw_path);after=inventory(install)
        need(raw['status']=='FAIL' and not raw['passed'] and raw['changedFiles']==[] and before==after and not raw['rollbackPerformed'],'CONTROLLED_CONNECTOR_DID_NOT_REFUSE_BEFORE_WRITES')
        need('native' in raw['error'].casefold() or 'tuple' in raw['error'].casefold(),'EXPECTED_NATIVE_TUPLE_REFUSAL')
        steps.append({'name':'actual-connector-wrong-native','status':'REFUSED_BEFORE_WRITES','error':raw['error'],'changedFiles':[],'preservedFiles':len(before),'receipt':{'path':str(raw_path),'sha256':sha(raw_path)}})
        drift=[r for r,h in source.items() if sha(repo/r)!=h];need(not drift,'SOURCE_CHANGED_DURING_PROBE')
        result={'schemaVersion':1,'status':'PASS_BOUNDARY_REPRODUCTION','passed':True,'startedUtc':started,'finishedUtc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'steps':steps,'processes':processes,'sourceHashes':source,'sourceChanges':drift,'fixtureZipSha256':contract['zip']['sha256'],'fixtureMembers':contract['zip']['members'],'oldNativeSha256':old_native_sha,'newHelperSha256':tuple_['helper'],'matchingNativeSha256':tuple_['native'],'gap':'Normal lighting copy sub-lane performs no explicit candidate/matching-native admission and has no local rollback when actual source file locking breaks its copy after moving the old payload. Existing controlled connector rejects native mismatch before changes.','fullDeployExecuted':False,'fullDeployBypassProved':False,'existingWholeExportGuardsRetained':True,'productBugFixed':False,'productSourceModified':False,'productApplicationExecuted':False,'hardwareIo':False,'actualCWritten':False,'actualGCanonicalWritten':False,'goalComplete':False}
        write(out/'result.json',result);print(json.dumps({'status':result['status'],'steps':len(steps),'processes':len(processes),'scope':'copy sub-lane plus controlled connector negative; not full deploy acceptance'},ensure_ascii=False));return 0
    except Exception as e:
        write(out/'result.json',{'schemaVersion':1,'status':'FAIL','error':str(e),'steps':steps,'processes':processes,'sourceHashes':source,'fullDeployExecuted':False,'productApplicationExecuted':False,'actualCWritten':False,'actualGCanonicalWritten':False,'goalComplete':False});raise
if __name__=='__main__':sys.exit(main())

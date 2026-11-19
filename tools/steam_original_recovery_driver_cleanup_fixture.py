"""Private proposed companion for the original publisher generator; prepare only."""
from pathlib import Path
import datetime
import hashlib
import json
import os
import re
import time

CASE = 'driver-cleanup-diagnostic-unwind'
HERE = Path(__file__).resolve().parent
BOUNDARIES = ['first-diagnostic-attached', 'cleanup-diagnostic-attached', 'management-handle-debt',
              'unknown-control-debt', 'cleanup-disarm-confirmed-diagnostic', 'detach-confirmed-diagnostic',
              'idle-diagnostic-no-debt', 'retained-image-no-active-debt',
              'normal-disarm-confirmed-diagnostic', 'wait-failed-is-not-exit', 'cleanup-nonstd-unwind-last-guard']

def pin(path):
    path = Path(path).resolve(); raw = path.read_bytes()
    return dict(path=str(path), bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest())

def save_bytes(path, raw):
    path = Path(path); path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('xb') as f:
        f.write(raw); f.flush(); os.fsync(f.fileno())

def save(path, obj):
    save_bytes(path, (json.dumps(obj, ensure_ascii=False, indent=2)+'\n').encode())

def within(path, root):
    return path == root or root in path.parents

class Instrument:
    def __init__(self, raw):
        self.original=raw; self.text=raw.decode('utf-8-sig'); self.edits=[]
    def replace(self, before, after, purpose):
        # Normalize only the requested anchor to the input's own EOL.
        eol='\r\n' if '\r\n' in self.text else '\n'
        before=before.replace('\r\n','\n').replace('\n',eol)
        after=after.replace('\r\n','\n').replace('\n',eol)
        if self.text.count(before)!=1: raise RuntimeError((purpose,self.text.count(before)))
        self.text=self.text.replace(before,after,1)
        self.edits.append(dict(before=before,after=after,purpose=purpose))
    def finish(self):
        reverse=self.text
        for e in reversed(self.edits):
            if reverse.count(e['after'])!=1: raise RuntimeError('nonunique inverse: '+e['purpose'])
            reverse=reverse.replace(e['after'],e['before'],1)
        if reverse.encode()!=self.original: raise RuntimeError('whole-file inverse differs')
        # Independent forward application starts again from the exact original.
        forward=self.original.decode()
        for e in self.edits:
            if forward.count(e['before'])!=1: raise RuntimeError('nonunique forward')
            forward=forward.replace(e['before'],e['after'],1)
        if forward!=self.text: raise RuntimeError('whole-file forward differs')
        return self.text.encode(),dict(hunks=self.edits,wholeForwardExact=True,wholeInverseExact=True,
            originalBytes=len(self.original),originalSha256=hashlib.sha256(self.original).hexdigest())

def driver_instrument(raw, variant):
    p=Instrument(raw)
    # Derive the normal Disarm statements from THIS complete driver. Only this
    # fixed statement slice is invoked by the entry hook; controlCall itself,
    # raw packet verification, original catches and final guard remain real.
    normalized=p.text.replace('\r\n','\n')
    start=normalized.index('    need(controlCall(tr::Op::Disarm) == 0, "trial-disarm-after-restore");')
    end=normalized.index('    out["statusAfterDisarm"] = statusJson(packet);', start)
    normal_raw=normalized[start:end]
    normal_diagnostic='    out["statusAfterDisarm"] = statusJson(packet);'
    hook='''
#if defined(YMCC_STEAM_DRIVER_CLEANUP_TESTING)
    ::driver_cleanup_fixture::inject(process,packetMemory,pathMemory,packet,
        managementThread,pid,attached,initialBreakpointSeen,preparedTrial,
        controlUnknown,loaded);
    if (::driver_cleanup_fixture::normalDisarm()) {
'''+normal_raw+'''
      ::driver_cleanup_fixture::diagnostic(5);
'''+normal_diagnostic+'''
    }
    ::driver_cleanup_fixture::failNative();
#endif
'''
    before='  try {\n    nativeFailureBoundary([&] {'
    p.replace(before,before+hook,'Entry state injection; normal raw Disarm slice copied exactly from this source; preconstructed exception')
    p.replace('  } catch (const std::exception &e) {',
        '  } catch (const std::exception &e) {\n#if defined(YMCC_STEAM_DRIVER_CLEANUP_TESTING)\n    ::driver_cleanup_fixture::diagnostic(1);\n#endif',
        'One-shot real new failure before unchanged initial JSON diagnostic')
    p.replace('    } catch (const std::exception &cleanup) {',
        '    } catch (const std::exception &cleanup) {\n#if defined(YMCC_STEAM_DRIVER_CLEANUP_TESTING)\n      ::driver_cleanup_fixture::diagnostic(2);\n#endif',
        'One-shot real new failure before unchanged secondary cleanup JSON diagnostic')
    # Select the catch's statement, not either normal-success projection.
    statement='        try { out["disarmConfirmed"] = true; } catch (...) {}' if variant=='candidate' else '        out["disarmConfirmed"] = true;'
    p.replace(statement,'#if defined(YMCC_STEAM_DRIVER_CLEANUP_TESTING)\n        ::driver_cleanup_fixture::diagnostic(3);\n#endif\n'+statement,
        'Fail actual confirmation JSON allocation after original cleanup control and raw success')
    statement='        try { out["errorDetach"] = true; } catch (...) {}' if variant=='candidate' else '        out["errorDetach"] = true;'
    p.replace(statement,'#if defined(YMCC_STEAM_DRIVER_CLEANUP_TESTING)\n        ::driver_cleanup_fixture::diagnostic(4);\n#endif\n'+statement,
        'Fail actual JSON projection after original detach and debugger confirmation')
    if variant=='candidate':
        p.replace('        revocationExports.pending()) {\n      ownerHeld(process.h, sessionNonce, out, pendingNow);',
            '        revocationExports.pending()) {\n#if defined(YMCC_STEAM_DRIVER_CLEANUP_TESTING)\n      ::driver_cleanup_fixture::lastGuardEntry();\n#endif\n      ownerHeld(process.h, sessionNonce, out, pendingNow);',
            'POD counter only inside the original active-debt final guard path; not an explicit catch ownerHeld count')
        p.replace('    ~ActiveOwnerReturn() noexcept { retain(); }',
            '    ~ActiveOwnerReturn() noexcept {\n#if !defined(YMCC_STEAM_DRIVER_CLEANUP_NO_LAST_GUARD)\n      retain();\n#endif\n    }',
            'Guard-only necessary control removes only original retain action when explicit NO_LAST_GUARD macro is defined')
    result,proof=p.finish()
    proof['normalRawSlice'] = dict(text=normal_raw,sourceOffsetNormalized=start,
        sha256=hashlib.sha256(normal_raw.encode()).hexdigest(),statementSource='same full driver',
        limitation='Entry hook models already-reached normal control state; original normal qualification/debug-event prefix is compiled but not executed')
    return result,proof

def engine_instrument(raw):
    p=Instrument(raw)
    before='class QueryActor {'
    query='''
#if defined(YMCC_STEAM_DRIVER_CLEANUP_TESTING)
#define ISteamClient FixtureQueryClient
#define compat driver_cleanup_fixture::QueryQualification
#undef LoadLibraryExW
#define LoadLibraryExW fixtureQueryLoad
#define GetProcAddress fixtureQueryProc
#define FreeLibrary fixtureQueryFree
#endif
'''
    p.replace(before,query+before,'Bind original memory-provider SDK/qualification leaves around complete unchanged QueryActor class')
    after='''
#if defined(YMCC_STEAM_DRIVER_CLEANUP_TESTING)
#undef ISteamClient
#undef compat
#undef LoadLibraryExW
#define LoadLibraryExW fixtureLoad
#undef GetProcAddress
#undef FreeLibrary
#endif
'''
    before='struct SavedThread{'
    p.replace(before,after+before,'End actor-only provider bindings; whole class body remains byte exact')
    before='void ownerHeld(HANDLE process,const std::string& nonce,J& out,bool pending) noexcept {'
    p.replace(before,before+'\n#if defined(YMCC_STEAM_DRIVER_CLEANUP_TESTING)\n  ::driver_cleanup_fixture::ownerEntry(process);\n#endif',
        'Observe entry only; retain original nonallocating exact-handle ownerHeld loop')
    return p.finish()

def prepare(args, repo):
    if args.scope!='publication-revocation' or args.case!=CASE or not args.prepare_only:
        raise RuntimeError('driver cleanup branch is explicit prepare-only; business execution requires later owned producer authorization')
    if len(args.source_root)!=1: raise RuntimeError('one exact r4 source overlay required')
    overlay=args.source_root[0].resolve(); repo=repo.resolve(); root=args.evidence.resolve()
    project=repo.parents[2]
    evidence_base=(project/'Testing/YMCC/Evidence').resolve()
    workspace_base=(project/'Testing/YMCC/Workspace').resolve()
    output_base=(args.driver_cleanup_workspace or root/'generated').resolve()
    if not within(root,evidence_base) or root.exists():
        raise RuntimeError('fresh explicit core Evidence subdirectory required')
    if not any(within(output_base,base) for base in (evidence_base,workspace_base)):
        raise RuntimeError('generated workspace must stay in core Evidence/Workspace')
    root.mkdir(parents=True)
    summary=dict(status='PREPARING',sourceReady=False,compilerInvoked=False,businessExecuted=False,
                 defaultCasesExecuted=0,oldGroupsExecuted=0,atEpochMs=int(time.time()*1000))
    try:
        closure={}; original={}; edges=[]
        def snapshot(relative):
            relative=Path(os.path.normpath(str(relative))); key=relative.as_posix()
            if relative.is_absolute() or '..' in relative.parts: raise RuntimeError('include escaped repo: '+key)
            if key in closure:return
            source=next((base/relative for base in (overlay,repo) if (base/relative).is_file()),None)
            if source is None: raise RuntimeError('missing source: '+key)
            raw=source.read_bytes();destination=root/'inputs'/relative
            save_bytes(destination,raw);closure[key]=destination;original[str(source)]=raw
            if relative.suffix not in ('.cpp','.h','.hpp','.inc'):return
            for quoted in re.findall(r'^\s*#\s*include\s*"([^"\r\n]+)"',raw.decode('utf-8-sig'),re.M):
                options=[relative.parent/quoted,Path('native')/quoted,Path('deps/json')/quoted,
                         Path('native/steam_original_recovery_engine')/quoted,
                         Path('native/steam_original_recovery_engine/vendor')/quoted]
                dependency=next((Path(os.path.normpath(str(p))) for p in options
                    if any((base/p).is_file() for base in (overlay,repo))),None)
                if dependency is None:
                    # The supplied SDK contains this exact optional Steam2
                    # compatibility include, disabled in the fixed Win64 recipe.
                    # Do not silently waive any other unresolved dependency.
                    if key=='native/steam_original_recovery_engine/query-sdk/steamclientpublic.h' and quoted=='steamcommon.h' and '#if defined( INCLUDED_STEAM2_USERID_STRUCTS )' in raw.decode():
                        edges.append(dict(parent=key,include=quoted,dependency=None,
                            condition='INCLUDED_STEAM2_USERID_STRUCTS',status='INACTIVE_UNDEFINED_IN_FIXED_RECIPE'))
                        continue
                    if key=='native/steam_original_recovery_engine/query-sdk/steam_api_internal.h' and quoted=='../../clientdll/steam_api_callback_inspection.h' and '#ifdef STEAM_CALLBACK_INSPECTION_ENABLED' in raw.decode():
                        edges.append(dict(parent=key,include=quoted,dependency=None,
                            condition='STEAM_CALLBACK_INSPECTION_ENABLED',status='INACTIVE_UNDEFINED_IN_FIXED_RECIPE'))
                        continue
                    raise RuntimeError('unresolved quoted dependency: '+key+': '+quoted)
                edges.append(dict(parent=key,include=quoted,dependency=dependency.as_posix()))
                snapshot(dependency)
        snapshot(Path('native/steam_original_recovery_engine.cpp'))
        # Whole coordinator is needed by the outer composition, not a fixture model.
        snapshot(Path('native/steam_command_coordinator.h'))
        old_path=HERE/'fixtures/recovery_driver_original_r258.inc';old=old_path.read_bytes()
        if len(old)!=69453 or hashlib.sha256(old).hexdigest()!='0f739651f7505f47b57e71154ebd3ff6931ec92cc44240a0bcfc7157ec3a746a':raise RuntimeError('immutable original driver snapshot mismatch')
        save_bytes(root/'old-driver.original.inc',old)
        proofs={};generated=[]
        for variant,driver in [('old',old),('candidate',closure['native/steam_original_recovery_engine/driver_port.inc'].read_bytes())]:
            destination=output_base/root.name/variant
            if destination.exists():raise RuntimeError('generated epoch must be fresh')
            destination.mkdir(parents=True)
            for relative,source in closure.items():
                save_bytes(destination/relative,old if variant=='old' and relative=='native/steam_original_recovery_engine/driver_port.inc' else source.read_bytes())
            # Product-derived instrumented files are distinct includes, leaving
            # copied exact products available for independent reverse readback.
            d,dp=driver_instrument(driver,variant)
            en,ep=engine_instrument(closure['native/steam_original_recovery_engine.cpp'].read_bytes())
            # Exactly one include path change is itself explicitly reversible.
            q=Instrument(en)
            q.replace('#include "steam_original_recovery_engine/driver_port.inc"',
                '#include "steam_original_recovery_engine/driver-cleanup.instrumented.inc"','Bind instrumented complete driver in this one private TU')
            en,binding=q.finish()
            save_bytes(destination/'native/steam_original_recovery_engine/driver-cleanup.instrumented.inc',d)
            save_bytes(destination/'native/steam_original_recovery_engine.driver-cleanup.cpp',en)
            for fixture in ('driver-cleanup-leaves.h','driver-cleanup-cases.inc'):
                save_bytes(destination/fixture,(HERE/fixture).read_bytes())
            tu=b'#define YMCC_STEAM_DRIVER_CLEANUP_TESTING 1\n#include "driver-cleanup-leaves.h"\n#include "native/steam_original_recovery_engine.driver-cleanup.cpp"\n#include "driver-cleanup-cases.inc"\n'
            save_bytes(destination/'cases.cpp',tu)
            proofs[variant]=dict(driver=dp,engine=ep,engineIncludeBinding=binding,
                completeDriverPreserved=True,originalHMemExportsPreserved=True,
                originalCatchesAndOwnerHeldLoopPreserved=True,runTrialStub=False,
                oldEngineTestingMacroDefined=False)
            # Capture every generated file from the known write set, not an open tree scan.
            files=[destination/p for p in closure]+[destination/'native/steam_original_recovery_engine/driver-cleanup.instrumented.inc',
                destination/'native/steam_original_recovery_engine.driver-cleanup.cpp',destination/'cases.cpp',
                destination/'driver-cleanup-leaves.h',destination/'driver-cleanup-cases.inc']
            generated.append(dict(variant=variant,directory=str(destination),translationUnit=pin(destination/'cases.cpp'),
                files=[pin(p) for p in files]))
        drift=[p for p,b in original.items() if Path(p).read_bytes()!=b]
        if drift:raise RuntimeError('source drift after generation: '+repr(drift))
        save(root/'STRICT-FORWARD-REVERSE.json',proofs)
        save(root/'SOURCE-DEPENDENCIES.json',dict(inputs=[pin(p) for p in original],edges=edges,
            generated=generated,sourceDrift=drift,meaning='All quoted input edges snapshotted; actual compiler headers/libraries still NOT_RUN'))
        save(root/'EXPECTED-SELECTION.json',dict(scope=args.scope,case=CASE,caseCountPerProducer=1,
            boundaries=BOUNDARIES,defaultCasesExecuted=0,oldGroupsExecuted=0,
            oldNecessaryFailure=['first-diagnostic-attached','cleanup-diagnostic-attached','management-handle-debt',
                'unknown-control-debt','cleanup-disarm-confirmed-diagnostic','detach-confirmed-diagnostic',
                'normal-disarm-confirmed-diagnostic','wait-failed-is-not-exit','retained-image-no-active-debt'],
            controls=['idle-diagnostic-no-debt'],
            lastGuardBoundary='cleanup-nonstd-unwind-last-guard',
            lastGuardOnlyControl=dict(macro='YMCC_STEAM_DRIVER_CLEANUP_NO_LAST_GUARD=1',
                sourceVariant='candidate',selectedBoundaries=['cleanup-nonstd-unwind-last-guard'],
                normalCandidateMacro='ABSENT',oldDriverForThisNewBoundary='NOT_PLANNED',
                removedAction='ActiveOwnerReturn destructor retain() only; all r4 diagnostic/scalar fixes stay'),
            binaryAdmission='Missing/wrong scope/case/boundary return64 before creating events or threads',
            testVerdicts='old emits actual ok=false/exit1; teardown release does not rewrite pre-release assertion'))
        summary.update(status='PREPARED_NOT_COMPILED_NOT_EXECUTED',preparedAtEpochMs=int(time.time()*1000),
            scope=args.scope,selectedCase=CASE,candidateOverlay=str(overlay),
            candidateDriver=pin(closure['native/steam_original_recovery_engine/driver_port.inc']),oldDriver=pin(old_path),
            generated=generated,proof=pin(root/'STRICT-FORWARD-REVERSE.json'),dependencies=pin(root/'SOURCE-DEPENDENCIES.json'),
            selection=pin(root/'EXPECTED-SELECTION.json'),testsExecuted=0,preparer=pin(Path(__file__)),originalEntry=pin(Path(args._driver_entry)))
        save(root/'PREPARATION.json',summary)
        print(json.dumps({k:summary[k] for k in ('status','sourceReady','compilerInvoked','testsExecuted','selectedCase')},ensure_ascii=False))
        return 0
    except Exception as error:
        summary.update(status='PREPARATION_FAILED',error=repr(error),failedAtEpochMs=int(time.time()*1000))
        save(root/'PREPARATION-FAILURE.json',summary)
        raise

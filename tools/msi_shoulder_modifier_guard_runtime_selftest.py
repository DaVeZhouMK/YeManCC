#!/usr/bin/env python3
"""Extract exact current adapter/global/call-site source; build offline fixtures only."""
from pathlib import Path
import datetime, hashlib, importlib.util, json, os, re, subprocess, sys
sys.dont_write_bytecode = True
from virtual_sample_tree_perf_selftest import clean, match_brace
REPO=Path(__file__).resolve().parents[1]
OUT=REPO.parents[2]/"Build/Validation/MsiShoulderModifierGuardRuntime-20261017"
VC=Path("C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat")
def sha(data): return hashlib.sha256(data).hexdigest()
def save(path,value): path.write_text(json.dumps(value,indent=2)+"\n",encoding="utf-8")
def function(text,signature):
    # Default-argument constructors may contain braces. Locate the function's
    # balanced parameter list, then require an actual body (never a declaration).
    masked=clean(text); found=[]
    for match in re.finditer(re.escape(signature),masked):
        opening=masked.find("(",match.start()); depth=1; pos=opening+1
        while depth and pos<len(masked):
            if masked[pos]=="(": depth+=1
            elif masked[pos]==")": depth-=1
            pos+=1
        tail=pos
        while tail<len(masked) and masked[tail] not in "{;": tail+=1
        if tail<len(masked) and masked[tail]=="{":
            found.append(text[match.start():match_brace(text,tail)+1])
    if len(found)!=1: raise RuntimeError("Expected one actual body: "+signature+"; found "+str(len(found)))
    return found[0]
def extracts(text):
    functions=[function(text,"static void msiShoulderModifierPublish("),
               function(text,"static bool msiShoulderModifierSuppress(")]
    globals_=re.findall(r"^static std::atomic<[^\n]+> g_msiShoulderModifier[^\n]*;",text,re.MULTILINE)
    if len(globals_)!=7: raise RuntimeError("Unexpected adapter globals; re-audit required")
    callback=function(text,"static void inputCaptureOnGamepad(")
    call=re.findall(r'if\s*\(msiClawKnownBoardAuthority\(\)\)\s*msiShoulderModifierPublish\([\s\S]*?;',clean(callback))
    if len(call)!=1: raise RuntimeError("Expected one board-scoped publisher call")
    # clean() preserves literals/offsets, so recover exact original call bytes.
    at=clean(callback).index(call[0]); actual=callback[at:at+len(call[0])]
    anchors=["g_realStickTest.enabled","g_realStickTest.virtualTargetEnabled","focusPlan.steamDeckTarget",
             "focusPlan.decision!=FocusDecision::SkipHostUnavailable","live","inputSource==\"msi-dinput\"",
             "pad,focusPlan.virtualUiHostEpoch"]
    if not all(anchor in actual for anchor in anchors): raise RuntimeError("Publisher scope anchor drift")
    blocks={"globals":"\n".join(globals_),"publish":functions[0],"suppress":functions[1],"publisherCall":actual}
    # No emitter declaration/stub is supplied, and these bodies must not name one.
    if re.search(r"\b(SendInput|keybd_event|mouse_event|PostMessage|SendMessage)\w*\s*\(","\n".join(blocks.values())):
        raise RuntimeError("Input emission appeared in extracted source")
    return blocks,anchors

def main():
    run=OUT/("run-"+datetime.datetime.now().strftime("%Y%m%d-%H%M%S-%f")+"-"+str(os.getpid()));run.mkdir(parents=True)
    result={"pass":False,"run":str(run),"profiles":[],"actualOSInput":False}
    try:
        raw=(REPO/"native/main.cpp").read_bytes();blocks,anchors=extracts(raw.decode("utf-8-sig"))
        sources={name:(REPO/name).read_bytes() for name in ("tools/msi_shoulder_modifier_guard_runtime_selftest.cpp",
                 "native/msi_shoulder_modifier_guard.h","deps/json/json.hpp")}
        for name,data in sources.items(): (run/Path(name).name).write_bytes(data)
        (run/"runner.snapshot.py").write_bytes(Path(__file__).read_bytes())
        (run/"actual_adapter.inc").write_text("\n\n".join(blocks[name] for name in ("globals","publish","suppress"))+"\n",encoding="utf-8")
        (run/"actual_publisher_call.inc").write_text("static void actualPublisherCall(bool live,const XINPUT_GAMEPAD& pad,const std::string& inputSource,const FocusPlan& focusPlan){\n"+blocks["publisherCall"]+"\n}\n",encoding="utf-8")
        result["binding"]={"mainSha256Before":sha(raw),"extractHashes":{name:sha(block.encode()) for name,block in blocks.items()},
             "sourceHashes":{name:sha(data) for name,data in sources.items()},"staticAnchors":anchors,
             "limitation":"Publisher expression compiled in a fake factory wrapper; static anchors do not prove full callback/hook execution, concurrency or physical causation"}
        save(run/"source-binding.initial.json",result["binding"])
        for profile,flags in (("Release",["/O2","/DNDEBUG"]),("Checked",["/Od","/RTC1","/D_ITERATOR_DEBUG_LEVEL=0"])):
            exe=run/(profile+".exe");imports=run/(profile+"-imports.txt")
            args=["cl","/nologo","/std:c++20","/utf-8","/EHsc","/W4","/WX","/MT",*flags,
                  str(run/"msi_shoulder_modifier_guard_runtime_selftest.cpp"),"/I"+str(run),
                  "/Fo"+str(run/(profile+".obj")),"/Fe"+str(exe)]
            batch=run/(profile+".cmd")
            batch.write_text('@echo off\nset VSLANG=1033\ncall "'+str(VC)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(args)+'\nif errorlevel 1 exit /b %errorlevel%\ndumpbin /imports "'+str(exe)+'" > "'+str(imports)+'"\nexit /b %errorlevel%\n',encoding="utf-8")
            built=subprocess.run(["cmd.exe","/d","/c",str(batch)],cwd=run,capture_output=True,encoding="utf-8",errors="replace",timeout=60)
            (run/(profile+"-compile.log")).write_text(built.stdout+built.stderr,encoding="utf-8")
            if built.returncode: raise RuntimeError(profile+" build failed: "+built.stdout+built.stderr)
            imported=imports.read_text(encoding="utf-8",errors="replace").lower()
            if any(name in imported for name in ("user32.dll","win32u.dll","sendinput","keybd_event","getasynckeystate","setupapi.dll","hid.dll","advapi32.dll")):
                raise RuntimeError("Forbidden real input/device/service import")
            dlls=re.findall(r"^\s+([^\s]+\.dll)\s*$",imported,re.MULTILINE)
            if dlls!=["kernel32.dll"]: raise RuntimeError("Unexpected dependency (both profiles require /MT): "+repr(dlls))
            executed=subprocess.run([str(exe)],cwd=run,capture_output=True,encoding="utf-8",errors="replace",timeout=30)
            (run/(profile+"-result.json")).write_text(executed.stdout,encoding="utf-8")
            (run/(profile+"-stderr.txt")).write_text(executed.stderr,encoding="utf-8")
            if executed.returncode: raise RuntimeError(profile+" fixture failed: "+executed.stderr)
            observed=json.loads(executed.stdout)
            if not observed["pass"]: raise RuntimeError(profile+" reported failure")
            result["profiles"].append({"profile":profile,"runtime":"/MT","imports":dlls,"checks":observed["checks"],"cases":observed["cases"]})
            print(profile+": "+str(observed["cases"])+" cases, "+str(observed["checks"])+" checks",flush=True)
        now=(REPO/"native/main.cpp").read_bytes();current,_=extracts(now.decode("utf-8-sig"))
        if current!=blocks: raise RuntimeError("Extracted adapter or call scope changed while testing; rerun required")
        if any((REPO/name).read_bytes()!=data for name,data in sources.items()): raise RuntimeError("Fixture/header dependency changed while testing")
        result["binding"].update({"mainSha256After":sha(now),"wholeMainStable":raw==now,"testedBlocksStable":True})
        result["pass"]=True
    except Exception as error:
        result["error"]=str(error);print(str(error),file=sys.stderr)
    result["artifacts"]={p.name:sha(p.read_bytes()) for p in run.iterdir() if p.is_file()}
    save(run/"result.json",result);save(OUT/"latest.json",result)
    if result["pass"]:
        # Reuse the old runner unchanged. Its generic extractor can mistake a
        # default argument's {} for a body; adapt extraction, not assertions.
        spec=importlib.util.spec_from_file_location("retention_baseline",REPO/"tools/keyboard_modifier_decision_retention_selftest.py")
        baseline=importlib.util.module_from_spec(spec);spec.loader.exec_module(baseline)
        baseline.source_function=function
        try:
            baseline.main();result["retentionBaselinePassed"]=True
        except Exception as error:
            result["pass"]=False;result["retentionBaselinePassed"]=False;result["retentionError"]=str(error);print(str(error),file=sys.stderr)
        save(run/"result.json",result);save(OUT/"latest.json",result)
    print(json.dumps({"pass":result["pass"],"run":str(run)}),flush=True)
    return 0 if result["pass"] else 1
if __name__=="__main__": sys.exit(main())

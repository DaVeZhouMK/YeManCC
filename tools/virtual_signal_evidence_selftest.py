"""Actual native blend/observer/serializer, fake settings/sink/clock; no product/device process."""
from pathlib import Path
import argparse, datetime, hashlib, json, re, subprocess
from log_frequency_selftest import extract
ROOT=Path(__file__).resolve().parents[1]
VC=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
def sha(b):return hashlib.sha256(b).hexdigest()
def consumers(mainSource,ui,touch):
    calls=re.findall(r'(?m)^    inputHostNativeSignalEvidence\([^;]+;',mainSource)
    assert len(calls)==1,'exactly one actual observer call required'
    expected=('inputHostNativeSignalEvidence(effectiveContent,commonFrameReceipt,diagSubmitAttempted,accepted,'
        'semanticNeutral,nativeSignalWireTick,nativeSignalWireSample,nativeSignalRequestPower,barrierMask,'
        'pad,outputPad,blend,analogAdmitted);')
    assert re.sub(r'\s+','',calls[0])==expected,'actual submit composition wiring missing'
    serializer='namespace ymcc::screenpads {using Json=json;\n'+extract(touch,'struct Contact {')+';\n'+extract(touch,'struct Snapshot {')+';\n'+extract(touch,'inline Json snapshotJson(')+'\n}\n'
    serializer+=extract(mainSource,'static constexpr float inputHostNormalizeAxis(')+'\n'+extract(ui,'struct FocusFrameContent {')+';\n'+extract(ui,'static FocusFrameContent focusIsolationNeutralContent(')+'\n'+extract(ui,'static json focusIsolationAssembleFrame(')+'\n'
    blend='\n'.join(extract(mainSource,sig)+';' for sig in ('struct MotionContribution {','struct MergedVirtualPadFrame {'))+'\n'
    blend+='\n'.join(extract(mainSource,sig) for sig in ('static short inputHostClampShort(','static void inputHostApplyAxisModifiers('))+'\n'
    blend+=extract(ui,'struct FocusStickBlend {')+';\n'+extract(ui,'static FocusStickBlend focusIsolationBlendStick(')+'\n'
    # Execute the exact production blend-axis consumers and final neutral selection.
    # This bounded projection is not the complete Host submit function.
    start=mainSource.index('    const short lxShort = blend.lxShort;')
    aliases=mainSource[start:mainSource.index('    const float lx =',start)]
    start=mainSource.index('    FocusFrameContent frameContent;',start)
    axes=mainSource[start:mainSource.index('    frameContent.lt = lt;',start)]
    start=mainSource.index('    FocusFrameContent effectiveContent =',start)
    final=mainSource[start:mainSource.index(';',start)+1]
    projection='static FocusFrameContent fixtureComposedFrame(const XINPUT_GAMEPAD& outputPad, const FocusStickBlend& blend, bool semanticNeutral) {\n'+aliases+axes+final+'\n    return effectiveContent;\n}\n'
    guideStart=mainSource.index('    XINPUT_GAMEPAD outputPad = pad;')
    guideEnd=mainSource.index('    // G19/HC parity:',guideStart)
    guide='static XINPUT_GAMEPAD fixtureGuidePad(const XINPUT_GAMEPAD& pad,bool semanticNeutral) {\n'+mainSource[guideStart:guideEnd]+'\n    return outputPad;\n}\n'
    return {'actual-source-snapshot.inc':extract(mainSource,'struct InputCaptureGamepadSnapshot {')+';\n','actual-guide-assembly.inc':guide,'actual-serializer.inc':serializer,'actual-blend.inc':blend,
        'actual-frame-consumer.inc':projection,'actual-observer-call.inc':calls[0]+'\n',
        'actual-classifier.inc':extract(mainSource,'static bool nativeLifecycleRecordIsStreaming(')}
def main():
    p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--main',type=Path,default=ROOT/'native/main.cpp');a=p.parse_args()
    mainBytes=a.main.read_bytes();mainSource=mainBytes.decode('utf-8-sig').replace('\r\n','\n')
    assert '#include "virtual_signal_evidence_runtime.h"' in mainSource,'actual native runtime include missing'
    assert 'inputHostNativeSignalEvidence(effectiveContent,commonFrameReceipt,diagSubmitAttempted,accepted,' in mainSource,'actual submit wiring missing'
    assert 'pad,outputPad,blend,analogAdmitted);' in mainSource
    assert 'if (abnormal) {\n            // GP-926R5' in mainSource,'existing control branch changed'
    assert 'busRateLogState.observe(evidenceRateFlags)' in mainSource
    assert 'addRotated(dataDir + L"\\\\input-host-signal.log")' in mainSource
    files=['native/touch_haptic_dispatch.h','native/virtual_signal_evidence.h','native/virtual_signal_evidence_runtime.h','native/virtual_control_window_evidence.h','native/virtual_controller_ui_output_runtime.h','native/screen_touchpads.h','native/log_frequency_policy.h','native/log_snapshot_state.h','deps/json/json.hpp','tools/fixtures/native_signal_evidence_20261022/fixture.cpp','tools/virtual_signal_evidence_selftest.py','tools/log_frequency_selftest.py']
    original={name:(ROOT/name).read_bytes() for name in files}
    run=a.out.resolve()/('run-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f'));run.mkdir(parents=True)
    ui=original['native/virtual_controller_ui_output_runtime.h'].decode('utf-8-sig');touch=original['native/screen_touchpads.h'].decode('utf-8-sig')
    extracted=consumers(mainSource,ui,touch)
    for name,text in extracted.items():(run/name).write_text(text,encoding='utf-8')
    (run/'bound-main.cpp').write_bytes(mainBytes)
    for name in ('log_frequency_policy.h','log_snapshot_state.h'):(run/name).write_bytes(original['native/'+name])
    for source,target in [('native/touch_haptic_dispatch.h','touch_haptic_dispatch.h'),('native/virtual_signal_evidence.h','virtual_signal_evidence.h'),('native/virtual_signal_evidence_runtime.h','virtual_signal_evidence_runtime.h'),('native/virtual_control_window_evidence.h','virtual_control_window_evidence.h'),('deps/json/json.hpp','json.hpp'),('tools/fixtures/native_signal_evidence_20261022/fixture.cpp','fixture.cpp')]: (run/target).write_bytes(original[source])
    (run/'binding.json').write_text(json.dumps({'sourceHashes':{n:sha(b) for n,b in original.items()},'mainSource':str(a.main),'mainSha256':sha(mainBytes),'consumerHashes':{n:sha(t.encode('utf-8')) for n,t in extracted.items()},'actualRuntimeAdapter':True,'actualSerializerExtracted':True,'actualBlendAndAxisModifiersExtracted':True,'actualAxisConsumerAndNeutralSelectionExtracted':True,'actualObserverCallExtracted':True,'fakeSettingsClockAndSink':True,'hardwareIO':False},indent=2),encoding='utf-8')
    sdk=Path('C:/Program Files (x86)/Windows Kits/10')
    versions=sorted((d for d in (sdk/'Include').iterdir() if (d/'um/windows.h').exists() and (sdk/'Lib'/d.name/'ucrt/x64/ucrt.lib').exists()),key=lambda d:tuple(map(int,d.name.split('.'))))
    assert versions,'complete Windows SDK required'
    version=versions[-1].name
    includes=';'.join(str(sdk/'Include'/version/n) for n in ('um','shared','ucrt'))
    libs=';'.join(str(sdk/'Lib'/version/n/'x64') for n in ('um','ucrt'))
    sdk_env='set "INCLUDE='+includes+';%INCLUDE%"\r\nset "LIB='+libs+';%LIB%"\r\nset VSLANG=1033\r\n'
    profiles=[]
    for name,flags in [('Release','/O2 /DNDEBUG /MT'),('Checked','/Od /RTC1 /D_DEBUG /MTd')]:
        exe=run/(name+'.exe');script=run/(name+'.cmd');script.write_text('@echo off\r\ncall "'+str(VC)+'" >nul\r\nif errorlevel 1 exit /b %errorlevel%\r\n'+sdk_env+'cl /nologo /std:c++20 /utf-8 /EHsc /W4 /WX /permissive- '+flags+' "'+str(run/'fixture.cpp')+'" /I"'+str(run)+'" /Fo"'+str(run/(name+'.obj'))+'" /Fe"'+str(exe)+'"\r\nexit /b %errorlevel%\r\n',encoding='utf-8')
        c=subprocess.run(['cmd.exe','/d','/c',str(script)],cwd=run,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=90);(run/(name+'-build.log')).write_text(c.stdout+c.stderr,encoding='utf-8')
        if c.returncode:raise RuntimeError(name+' compile failed: '+(c.stdout+c.stderr)[-1600:])
        c=subprocess.run([str(exe)],cwd=run,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=30);(run/(name+'-result.json')).write_text(c.stdout,encoding='utf-8');result=json.loads(c.stdout)
        if c.returncode or not result.get('pass'):raise RuntimeError(name+' fixture failed: '+c.stdout)
        profiles.append({'profile':name,**result});print(json.dumps({k:v for k,v in profiles[-1].items() if k!='compositionWitnesses'}))
    assert all((ROOT/n).read_bytes()==b for n,b in original.items()),'fixture or dependency changed during run'
    finalMain=a.main.read_bytes()
    assert consumers(finalMain.decode('utf-8-sig').replace('\r\n','\n'),ui,touch)==extracted,'extracted main consumers changed during run'
    summary={'pass':True,'run':str(run),'profiles':profiles,'actualNativeWiringChecked':True,
        'mainSha256AtStart':sha(mainBytes),'mainSha256AtEnd':sha(finalMain),'mainUnchangedDuringRun':mainBytes==finalMain,
        'extractedMainConsumersUnchangedDuringRun':True,'realHardwareAcceptance':False,
        'wholeNativeTranslationUnitCompiled':False,'parentPersistentLifecycleAcceptance':'UNKNOWN'};(run/'RESULT.json').write_text(json.dumps(summary,indent=2),encoding='utf-8');(a.out/'LATEST.json').write_text(json.dumps(summary,indent=2),encoding='utf-8');print(json.dumps({'pass':True,'run':str(run)}))
if __name__=='__main__':main()


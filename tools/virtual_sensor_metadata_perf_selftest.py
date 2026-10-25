"""Actual-source metadata projections/provider buffer ownership, isolated matrix proof.

No production/device calls. Candidate mode validates before any Native patch.
"""
from pathlib import Path
import argparse,datetime,hashlib,json,subprocess,difflib
from virtual_sample_metadata_perf_selftest import matching_brace
ROOT=Path(__file__).resolve().parent.parent
SHA=lambda b:hashlib.sha256(b).hexdigest()
META_MARKER='struct InputCaptureSensorMeta {'
HELPERS='''struct InputCaptureLegacySensorFields {
    bool gyro = false;
    bool accel = false;
};
static InputCaptureLegacySensorFields inputCaptureLegacySensorFieldsSnapshot() {
    std::lock_guard<std::mutex> lock(g_sensorMetaMx);
    return {g_sensorMeta.gyroFamily == "sensorapi" && g_sensorMeta.gyroHcLegacyFields,
            g_sensorMeta.accelFamily == "sensorapi" && g_sensorMeta.accelHcLegacyFields};
}
static bool inputCaptureGyroPresentSnapshot() {
    std::lock_guard<std::mutex> lock(g_sensorMetaMx);
    return g_sensorMeta.gyroPresent;
}
'''
def block(text,marker):
 s=text.index(marker);op=text.index('{',s);end=matching_brace(text,op)+1
 if marker.startswith('struct '):assert text[end]==';';end+=1
 return text[s:end]
def proposed(text,projections_only=False):
 getter=block(text,'static InputCaptureSensorMeta inputCaptureSensorMetaSnapshot() {');assert text.count(getter)==1
 text=text.replace(getter,getter+'\n'+HELPERS.rstrip())
 matrix=block(text,'static void inputCaptureApplyHcMatrixCandidate(')
 new=matrix.replace('const auto meta=inputCaptureSensorMetaSnapshot();','const auto legacyFields=inputCaptureLegacySensorFieldsSnapshot();').replace('meta.gyroFamily=="sensorapi" && meta.gyroHcLegacyFields','legacyFields.gyro').replace('meta.accelFamily=="sensorapi" && meta.accelHcLegacyFields','legacyFields.accel')
 assert new!=matrix;text=text.replace(matrix,new)
 consumer='{"sensorPresent", inputCaptureSensorMetaSnapshot().gyroPresent}';assert text.count(consumer)==1;text=text.replace(consumer,'{"sensorPresent", inputCaptureGyroPresentSnapshot()}')
 if projections_only:return text
 provider=block(text,'static std::string inputCaptureSensorProviderKey(')
 new=provider.replace('''        if (classGuid != std::string::npos && classGuid > acpi + 5)
            return value.substr(acpi, classGuid - acpi);''','''        if (classGuid != std::string::npos && classGuid > acpi + 5) {
            value.erase(classGuid);
            value.erase(0, acpi);
            return value;
        }''')
 new=new.replace('''        if (second != std::string::npos && second > acpi) return value.substr(acpi, second);''','''        if (second != std::string::npos && second > acpi) {
            // Preserve the original substr(acpi, second) count, including its prefix case.
            value.erase(acpi + (std::min)(second, value.size() - acpi));
            value.erase(0, acpi);
            return value;
        }''')
 new=new.replace('''        if(classGuid!=std::string::npos && instanceDelimiter!=std::string::npos && instanceDelimiter<classGuid)
            return value.substr(hid,classGuid-hid);''','''        if(classGuid!=std::string::npos && instanceDelimiter!=std::string::npos && instanceDelimiter<classGuid) {
            value.erase(classGuid);
            value.erase(0, hid);
            return value;
        }''')
 assert new!=provider;text=text.replace(provider,new);return text

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--before',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--candidate',action='store_true');p.add_argument('--projections-only',action='store_true');a=p.parse_args();out=a.output/(('candidate-' if a.candidate else 'final-')+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir(parents=True)
 before=a.before.read_text(encoding='utf-8-sig');current=proposed(before,a.projections_only) if a.candidate else (ROOT/'native/main.cpp').read_text(encoding='utf-8-sig')
 markers=[META_MARKER,'static InputCaptureSensorMeta inputCaptureSensorMetaSnapshot() {','static void inputCaptureApplyHcMatrixCandidate(','static std::string inputCaptureSensorProviderKey(']
 expected=proposed(before,a.projections_only);checks={m:block(expected,m)==block(current,m) for m in markers};checks['projected-helpers']=HELPERS.rstrip() in current;checks['same-ui-getter-consumer']='{"sensorPresent", inputCaptureGyroPresentSnapshot()}' in current
 assert all(checks.values()),checks
 generated=''
 for name,text in [('before',before),('current',current)]:
  generated+='namespace '+name+' {\n'+block(text,META_MARKER)+'\nstatic FakeMutex g_sensorMetaMx;\nstatic InputCaptureSensorMeta g_sensorMeta;\nstatic Machine g_machineIdentity;\nstatic Selector g_hcMotionSelector;\n'+block(text,markers[1])+'\n'
  if name=='current':generated+=HELPERS+'\n'
  generated+=block(text,markers[2])+'\n'+block(text,markers[3])+'\n}\n'
 generated=generated.replace('std::lock_guard<std::mutex>', 'std::lock_guard<FakeMutex>')
 for name,text in [('threadedBefore',before),('threadedCurrent',current)]:
  generated+='namespace '+name+' {\n'+block(text,META_MARKER)+'\nstatic std::mutex g_sensorMetaMx;\nstatic InputCaptureSensorMeta g_sensorMeta;\n'+block(text,markers[1])+'\n'
  if name=='threadedCurrent':generated+=HELPERS+'\n'
  generated+='}\n'
 (out/'metadata-extracted.h').write_text(generated,encoding='utf-8');(out/'fixture.cpp').write_bytes((ROOT/'tools/virtual_sensor_metadata_perf_selftest.cpp').read_bytes());(out/'candidate-main.cpp').write_text(current,encoding='utf-8');(out/'native-candidate.diff').write_text(''.join(difflib.unified_diff(before.splitlines(True),current.splitlines(True),fromfile='before/native',tofile='candidate/native')),encoding='utf-8')
 binding={'candidateOnly':a.candidate,'projectionsOnly':a.projections_only,'sourceCurrentSha256':SHA(current.encode()),'blocks':{m:SHA(block(current,m).encode()) for m in markers},'jsonHeaderSha256':SHA((ROOT/'deps/json/json.hpp').read_bytes()),'msiCapabilityHeaderSha256':SHA((ROOT/'native/msi_motion_capabilities.h').read_bytes()),'harnessSha256':SHA((ROOT/'tools/virtual_sensor_metadata_perf_selftest.cpp').read_bytes()),'checks':checks,'fixtureMutexSeam':'Deterministic FakeMutex proves observation position and after-unlock snapshots. Concurrency and actual-mutex benchmark rows use std::mutex. No real sensor or controller executed.'}
 vc=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat');results=[]
 for name,opts in [('release',['/O2','/DNDEBUG','/MT']),('checked',['/Od','/RTC1','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd'])]:
  exe=out/(name+'.exe');cmd=['cl','/nologo','/std:c++20','/utf-8','/EHsc',*opts,str(out/'fixture.cpp'),'/I'+str(out),'/I'+str(ROOT/'deps/json'),'/I'+str(ROOT/'native'),'/Fo'+str(out/(name+'.obj')),'/Fe'+str(exe)]
  batch=out/(name+'-build.bat');batch.write_text('@echo off\nset VSLANG=1033\ncall "'+str(vc)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(cmd)+'\n',encoding='utf-8');ran=subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=out,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120);(out/(name+'-build.log')).write_text(ran.stdout+ran.stderr,encoding='utf-8')
  if ran.returncode:raise RuntimeError('Build failed: '+str(batch))
  ran=subprocess.run([str(exe)],cwd=out,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120);(out/(name+'-result.json')).write_text(ran.stdout,encoding='utf-8');(out/(name+'-stderr.txt')).write_text(ran.stderr,encoding='utf-8')
  if ran.returncode:raise RuntimeError('Fixture failed: '+ran.stderr)
  row=json.loads(ran.stdout);row['profile']=name;results.append(row)
 if not a.candidate:
  fresh=(ROOT/'native/main.cpp').read_text(encoding='utf-8-sig');assert all(block(fresh,m)==block(current,m) for m in markers);assert HELPERS.rstrip() in fresh
 assert SHA((ROOT/'tools/virtual_sensor_metadata_perf_selftest.cpp').read_bytes())==binding['harnessSha256']
 result={'pass':True,'run':str(out),'binding':binding,'profiles':results,'scope':'Actual getters/provider/matrix source, realMSI capability and legacy-axis helper; matrix function namedmapper and mutex are deterministic seams. Getter+caller-local string destruction costs only; actual-mutex rows are uncontended std::mutex. No full BUS/Host/whole-process measurement.'};(a.output/('candidate-result.json' if a.candidate else 'result.json')).write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(json.dumps({'pass':True,'candidateOnly':a.candidate,'profiles':results},indent=2))
if __name__=='__main__':main()

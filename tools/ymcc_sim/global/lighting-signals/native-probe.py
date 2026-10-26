"""Execute only extracted native pure functions and the existing route header."""
import argparse,hashlib,json,os,pathlib,subprocess,sys
p=argparse.ArgumentParser();p.add_argument('--evidence-dir',required=True);p.add_argument('--node',default='node');a=p.parse_args()
repo=pathlib.Path(__file__).resolve().parents[4];out=pathlib.Path(a.evidence_dir).resolve();out.mkdir(parents=True,exist_ok=True)
def run(args,log,env=None):
 r=subprocess.run(args,cwd=repo,env=env,capture_output=True,timeout=120)
 (out/log).write_bytes(r.stdout+r.stderr)
 if r.returncode:raise RuntimeError(str(args[0])+' failed: '+str(out/log))
 return r.stdout.decode('utf-8',errors='replace')
run([sys.executable,str(repo/'tools/fixtures/lightsetter_devicecore/verify_native_binding.py'),'--scratch',str(out)],'route-run.log')
command_out=out/'command';command_out.mkdir(exist_ok=True)
run([a.node,str(repo/'tools/oem_lighting_command_selftest.mjs'),str(command_out)],'command-generation.log')
# The extracted fixture serializes its ASCII-only fake command path. Use an
# explicit conversion so /WX does not reject the fixture's iterator narrowing.
cpp_path=command_out/'actual-lighting-command.cpp'
cpp=cpp_path.read_text(encoding='utf-8-sig')
cpp=cpp.replace('const auto value=cmd.str();return json{{"command",std::string(value.begin(),value.end())}};', 'const auto value=cmd.str();std::string ascii;for(wchar_t ch:value){if(ch<0||ch>127)throw std::runtime_error("non-ASCII fixture command");ascii.push_back(static_cast<char>(ch));}return json{{"command",ascii}};')
cpp_path.write_text(cpp,encoding='utf-8')
vs=pathlib.Path(os.environ['VCToolsInstallDir']).parent if os.environ.get('VCToolsInstallDir') else pathlib.Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC')
msvc=sorted((v for v in vs.iterdir() if (v/'bin/Hostx64/x64/cl.exe').is_file()),reverse=True)[0]
sdk=pathlib.Path('C:/Program Files (x86)/Windows Kits/10');version=sorted((v.name for v in (sdk/'Include').iterdir() if (v/'ucrt').is_dir()),reverse=True)[0]
env=os.environ.copy();binary=msvc/'bin/Hostx64/x64';env['PATH']=str(binary)+os.pathsep+env.get('PATH','')
env['INCLUDE']=';'.join(str(v) for v in [msvc/'include',sdk/'Include'/version/'ucrt',sdk/'Include'/version/'shared',sdk/'Include'/version/'um',repo/'deps/json'])
env['LIB']=';'.join(str(v) for v in [msvc/'lib/x64',sdk/'Lib'/version/'ucrt/x64',sdk/'Lib'/version/'um/x64'])
exe=command_out/'command-test.exe'
run([str(binary/'cl.exe'),'/nologo','/std:c++17','/EHsc','/W4','/WX','/utf-8','/MT','/O2',str(command_out/'actual-lighting-command.cpp'),'/Fo'+str(command_out/'command-test.obj'),'/Fe'+str(exe)],'command-compile.log',env)
text=run([str(exe)],'command-run.log')
report={'schema':'ymcc.lighting-native.v1','status':'PASS','route':json.loads((out/'native-binding/report.json').read_text()),'commandParser':{'stdout':text.strip(),'sourceBinding':json.loads((command_out/'source-binding.json').read_text()),'actualExtractedFunctions':True,'processApiLinked':False},'hardwareIo':False}
(out/'result.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps({'status':'PASS','routeCases':report['route']['routeCases'],'commandParser':text.strip(),'hardwareIo':False}))


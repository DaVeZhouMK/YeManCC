from pathlib import Path
import argparse,hashlib,json,subprocess,sys

def block(text,marker):
    start=text.index(marker)
    opening=text.index('{',start)
    depth=1;pos=opening+1
    while depth:
        if text[pos]=='{': depth+=1
        elif text[pos]=='}': depth-=1
        pos+=1
    return text[start:pos]

def main():
    parser=argparse.ArgumentParser(description='Exact Fan thread-attribution collector with fake read-only thread metadata; no CPU benchmark or hardware.')
    parser.add_argument('--program',required=True,type=Path)
    parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--mode',choices=['legacy','selected'],default='selected')
    args=parser.parse_args()
    program=args.program.resolve();out=args.output.resolve()
    if out.exists(): raise RuntimeError('Output must be new; existing evidence is never cleared.')
    out.mkdir(parents=True)
    raw=program.read_bytes();text=raw.decode('utf-8-sig')
    fragments=[block(text,'    private readonly struct ThreadCpuRow'),block(text,'    private (object[] HotThreads, object[] ThreadsAdded, int ThreadsRemovedCount)')]
    shape='    private static (object[] HotThreads, object[] ThreadsAdded) ShapeThreadCpuAttribution('
    if shape in text: fragments.append(block(text,shape))
    fields='    private readonly Dictionary<int, double> lastThreadCpuMs = new();\n    private HashSet<int> lastThreadIds = new();'
    if fields.splitlines()[0] not in text or fields.splitlines()[1] not in text: raise RuntimeError('Production accounting fields changed; review fixture bindings.')
    extracted='using System;\nusing System.Collections.Generic;\nusing System.Linq;\ninternal sealed partial class FanThreadChecks\n{\n'+fields+'\n'+'\n'.join(fragments)+'\n}\n'
    (out/'Collector.cs').write_text(extracted,encoding='utf-8')
    fixture=Path(__file__).with_suffix('.cs').read_bytes();(out/'Program.cs').write_bytes(fixture)
    (out/'ThreadChecks.csproj').write_text('<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><TreatWarningsAsErrors>true</TreatWarningsAsErrors></PropertyGroup></Project>',encoding='utf-8')
    manifest={'source':str(program),'sourceSha256':hashlib.sha256(raw).hexdigest(),'fixtureSha256':hashlib.sha256(fixture).hexdigest(),'fragmentSha256':[hashlib.sha256(x.encode()).hexdigest() for x in fragments],'mode':args.mode,'scope':'exact production row/collector/selection bodies; Process/ProcessThread are deterministic read-only substitutes; no runtime CPU, hardware, Host or sleep operation'}
    (out/'binding.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    with (out/'build.log').open('w',encoding='utf-8') as log:
        built=subprocess.run(['dotnet','build',str(out/'ThreadChecks.csproj'),'-c','Release','-o',str(out/'bin')],stdout=log,stderr=subprocess.STDOUT)
    if built.returncode:
        print((out/'build.log').read_text(encoding='utf-8'));return built.returncode
    with (out/'run.log').open('w',encoding='utf-8') as log:
        ran=subprocess.run(['dotnet',str(out/'bin/ThreadChecks.dll'),args.mode,str(out/'result.json')],stdout=log,stderr=subprocess.STDOUT)
    print((out/'run.log').read_text(encoding='utf-8'))
    return ran.returncode

if __name__=='__main__':
    try: sys.exit(main())
    except Exception as error: print(str(error),file=sys.stderr);sys.exit(1)

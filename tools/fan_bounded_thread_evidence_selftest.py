from pathlib import Path
import argparse,hashlib,json,shutil,subprocess,sys

def block(text,signature):
    start=text.index(signature);opened=text.index('{',start);depth=1;end=opened+1
    while depth:
        if text[end]=='{':depth+=1
        elif text[end]=='}':depth-=1
        end+=1
    return text[start:end]

def main():
    parser=argparse.ArgumentParser(description='Actual runtime diagnostics method with deterministic counters/thread-probe seam in a scratch-only Host build; no user CPU benchmark or hardware')
    parser.add_argument('--program',required=True,type=Path);parser.add_argument('--output',required=True,type=Path);parser.add_argument('--mode',choices=['legacy','bounded'],required=True);args=parser.parse_args()
    project=Path(__file__).resolve().parents[1];out=args.output.resolve();source=args.program.resolve()
    if out.exists():raise RuntimeError('Output must be new; no prior evidence will be cleared')
    out.mkdir(parents=True);hostroot=out/'host';hostroot.mkdir();origin=project/'FanLab/real-host'
    inputs=[]
    for p in origin.iterdir():
        if p.is_file() and p.suffix.lower() in ['.cs','.json']:shutil.copyfile(p,hostroot/p.name);inputs.append({'name':p.name,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
    raw=source.read_bytes();text=raw.decode('utf-8-sig');nl='\r\n' if '\r\n' in text else '\n'
    marker='internal sealed class FanHostEngine : IDisposable';start=text.index('{',text.index(marker))+1
    fields='''
    internal static long RuntimeThreadProbeTimestampForFixture = 1;
    internal static double RuntimeThreadProbeCpuMsForFixture = 0d;
    internal static int RuntimeThreadProbeCallsForFixture = 0;
    internal static int RuntimeThreadProbeDetailsForFixture = 0;
    internal static bool RuntimeThreadProbeThrowForFixture = false;
'''.replace('\n',nl)
    text=text[:start]+fields+text[start:]
    sample=block(text,'    internal void WriteRuntimeDiagnostics()')
    metered=sample.replace('var nowTimestamp = Stopwatch.GetTimestamp();','var nowTimestamp = RuntimeThreadProbeTimestampForFixture;').replace('var nowCpu = process.TotalProcessorTime;','var nowCpu = TimeSpan.FromMilliseconds(RuntimeThreadProbeCpuMsForFixture);').replace('var attributionTimestamp = Stopwatch.GetTimestamp();','var attributionTimestamp = RuntimeThreadProbeTimestampForFixture;')
    needle='                diagnostics.Write("host.runtime-sample", new';assert metered.count(needle)==1
    metered=metered.replace(needle,'                RuntimeThreadProbeDetailsForFixture++;'+nl+needle)
    text=text.replace(sample,metered)
    collector=block(text,'    private (object[] HotThreads, object[] ThreadsAdded, int ThreadsRemovedCount)')
    replacement='''    private (object[] HotThreads, object[] ThreadsAdded, int ThreadsRemovedCount) CollectThreadCpuAttribution(Process process)
    {
        RuntimeThreadProbeCallsForFixture++;
        if (RuntimeThreadProbeThrowForFixture)
        {
            RuntimeThreadProbeThrowForFixture = false;
            throw new IOException("fixture-thread-probe-unavailable");
        }
        return ([new { tid = 7701, cpuDeltaMs = 10d, cpuTotalMs = 20d, startedAt = "fixture", state = "Wait" }], [], 0);
    }'''.replace('\n',nl)
    text=text.replace(collector,replacement);(hostroot/'Program.cs').write_bytes(text.encode('utf-8'))
    xml=(origin/'YeManFanHost.csproj').read_text(encoding='utf-8-sig').replace('<AssemblyName>YeManFanHost</AssemblyName>','<AssemblyName>YeManFanHostEvidenceProbe</AssemblyName>').replace('..\\fan-backend\\YeManFanBackend.csproj',str(project/'FanLab/fan-backend/YeManFanBackend.csproj'))
    (hostroot/'ProbeHost.csproj').write_text(xml,encoding='utf-8')
    runner=out/'runner';runner.mkdir();shutil.copyfile(Path(__file__).with_suffix('.cs'),runner/'Program.cs')
    (runner/'Checks.csproj').write_text('<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0-windows10.0.19041.0</TargetFramework><UseWPF>true</UseWPF><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><TreatWarningsAsErrors>true</TreatWarningsAsErrors></PropertyGroup></Project>',encoding='utf-8')
    for name,proj,dest in [('host',hostroot/'ProbeHost.csproj',out/'host-bin'),('runner',runner/'Checks.csproj',out/'runner-bin')]:
        with (out/(name+'-build.log')).open('w',encoding='utf-8') as log:r=subprocess.run(['dotnet','build',str(proj),'-c','Release','--artifacts-path',str(out/(name+'-artifacts')),'-o',str(dest)],stdout=log,stderr=subprocess.STDOUT)
        if r.returncode:print((out/(name+'-build.log')).read_text(encoding='utf-8',errors='replace'));return r.returncode
    with (out/'run.log').open('w',encoding='utf-8') as log:r=subprocess.run(['dotnet',str(out/'runner-bin/Checks.dll'),str(out/'host-bin/YeManFanHostEvidenceProbe.dll'),str(out/'logs'),args.mode,str(out/'result.json')],stdout=log,stderr=subprocess.STDOUT)
    binding={'productProgramSha256':hashlib.sha256(raw).hexdigest(),'originalDiagnosticsMethodSha256':hashlib.sha256(sample.encode()).hexdigest(),'originalCollectorSha256':hashlib.sha256(collector.encode()).hexdigest(),'instrumentedProgramSha256':hashlib.sha256(text.encode()).hexdigest(),'hostInputs':inputs,'mode':args.mode,'scope':'Actual diagnostics scheduling, logger, maintenance finally and control state; process CPU/timestamps and thread collector replaced only in scratch; other process metadata getters read fixture process only; no real Host Main, HC, device, sleep or CPU performance timing','exit':r.returncode}
    (out/'binding.json').write_text(json.dumps(binding,ensure_ascii=False,indent=2),encoding='utf-8');print((out/'run.log').read_text(encoding='utf-8'));return r.returncode
if __name__=='__main__':
    try:sys.exit(main())
    except Exception as error:print(str(error),file=sys.stderr);sys.exit(1)

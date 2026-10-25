"""Prove both reference audit and compiler reject an introduced real consumer."""
from pathlib import Path
import argparse,json,os,shutil,subprocess

def save(p,v):p.write_text(json.dumps(v,indent=2)+'\n',encoding='utf-8')
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--attempt',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);ap.add_argument('--audit',type=Path,required=True);a=ap.parse_args();shutil.copytree(a.attempt/'frozen',a.out)
 (a.out/'NuGet.Config').write_text('<configuration><packageSources><clear /></packageSources></configuration>',encoding='utf-8')
 (a.out/'InputHost/VerbatimConsumerProbe.cs').write_text('internal static class VerbatimConsumerProbe { internal static object Create() => new YeManInputHost.HcVerbatim.ControllerState(); }',encoding='utf-8')
 env=dict(os.environ);env.update(DOTNET_CLI_TELEMETRY_OPTOUT='1',DOTNET_CLI_UI_LANGUAGE='en-US',DOTNET_SKIP_FIRST_TIME_EXPERIENCE='1');jobs=[]
 def run(name,cmd):
  with (a.out/(name+'.log')).open('w',encoding='utf-8') as log:
   proc=subprocess.Popen(cmd,cwd=a.out,env=env,stdout=log,stderr=subprocess.STDOUT);code=proc.wait()
  jobs.append({'name':name,'command':cmd,'pid':proc.pid,'exitCode':code,'terminal':True});save(a.out/'jobs.json',jobs);return code
 cmd=['dotnet','build','InputHost/YeManInputHost.csproj','-c','Release','--configfile','NuGet.Config','--nologo','-v','minimal','-p:NuGetAudit=false','-p:UseSharedCompilation=false','-nodeReuse:false','-p:IncludeSourceRevisionInInformationalVersion=false','-p:EnableSourceControlManagerQueries=false','-p:EnableSourceLink=false','-p:PathMap='+str(a.out)+'=/_/frozen']
 assert run('consumer-present-build',cmd)==0
 before=a.out/'InputHost/bin/Release/net10.0-windows/YeManInputHost.dll';candidate=a.attempt/'candidate/InputHost/bin/Release/net10.0-windows/YeManInputHost.dll'
 assert run('consumer-audit',['dotnet',str(a.audit),str(before),str(candidate),str(a.out/'audit-result.json')])!=0
 audit=json.loads((a.out/'audit-result.json').read_text(encoding='utf-8'));assert audit['incoming']
 for f in (a.out/'InputHost/HcVerbatim').glob('*.cs'):f.unlink()
 assert run('consumer-after-removal-build',cmd)!=0
 log=(a.out/'consumer-after-removal-build.log').read_text(encoding='utf-8');assert 'CS0234' in log and 'HcVerbatim' in log
 save(a.out/'result.json',{'pass':True,'auditRejectedRealConsumer':True,'compilerRejectedRealConsumerAfterRemoval':True,'productsDevicesRun':False,'jobs':jobs});print(json.dumps({'pass':True,'incomingRoots':len(audit['incoming'])}))
if __name__=='__main__':main()

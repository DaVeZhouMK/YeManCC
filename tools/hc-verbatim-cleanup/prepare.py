"""Freeze and compile InputHost twice offline; never execute the product."""
from pathlib import Path
import argparse,hashlib,json,os,re,shutil,subprocess,sys
REPO=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def save(p,o):p.write_text(json.dumps(o,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
def main():
 ap=argparse.ArgumentParser();ap.add_argument("--out",type=Path,required=True);ap.add_argument("--verbatim-preimage",type=Path);a=ap.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
 inputs=[]
 for source in sorted((REPO/"InputHost").rglob("*")):
  if source.is_file() and not any(x in {"bin","obj"} for x in source.relative_to(REPO/"InputHost").parts):
   target=out/"frozen"/source.relative_to(REPO);target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target);inputs.append({"path":str(source),"relative":source.relative_to(REPO).as_posix(),"bytes":source.stat().st_size,"sha256":sha(source)})
 for rel in ["native/ProcessEfficiency.cs","deps/hidmaestro/HIDMaestro.Core.dll"]+["deps/ymcc-shared-runtime/bin/"+x for x in ["Newtonsoft.Json.dll","Nefarius.Utilities.DeviceManagement.dll","Nefarius.Drivers.HidHide.dll","Nefarius.Vicius.Abstractions.dll","Microsoft.Extensions.DependencyInjection.Abstractions.dll","Microsoft.Extensions.Logging.Abstractions.dll"]]:
  source=REPO/rel;target=out/"frozen"/rel;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target);inputs.append({"path":str(source),"relative":rel,"bytes":source.stat().st_size,"sha256":sha(source)})
 if not list((out/"frozen/InputHost/HcVerbatim").glob("*.cs")):
  if a.verbatim_preimage is None:raise RuntimeError("Sources already retired: supply --verbatim-preimage pointing to archived original eight .cs files")
  retired=json.loads((REPO/"InputHost/HcVerbatim/REMOVED-SOURCES.json").read_text(encoding="utf-8"))["files"]
  for record in retired:
   source=a.verbatim_preimage/Path(record["relative"]).name
   if sha(source)!=record["sha256"]:raise RuntimeError("Original source hash mismatch: "+str(source))
   target=out/"frozen"/record["relative"];target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
   inputs.append({"path":str(source),"relative":record["relative"],"bytes":source.stat().st_size,"sha256":sha(source),"restoredFromPreimage":True})
 save(out/"frozen-inputs.json",inputs)
 config='<configuration><packageSources><clear /></packageSources></configuration>'
 env=dict(os.environ);env.update(DOTNET_CLI_TELEMETRY_OPTOUT="1",DOTNET_SKIP_FIRST_TIME_EXPERIENCE="1",DOTNET_CLI_UI_LANGUAGE="en-US",DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE="true")
 jobs=[]
 for name in ["baseline","candidate"]:
  root=out/name;shutil.copytree(out/"frozen",root);(root/"NuGet.Config").write_text(config,encoding="utf-8")
  if name=="candidate":
   # Only the eight frozen .cs inputs are removed. No project or other source edit.
   for f in (root/"InputHost/HcVerbatim").glob("*.cs"):f.unlink()
  cmd=["dotnet","build",str(root/"InputHost/YeManInputHost.csproj"),"-c","Release","--configfile",str(root/"NuGet.Config"),"--nologo","-v","minimal","-p:NuGetAudit=false","-p:UseSharedCompilation=false","-nodeReuse:false","-p:IncludeSourceRevisionInInformationalVersion=false","-p:EnableSourceControlManagerQueries=false","-p:EnableSourceLink=false","-p:PathMap="+str(root)+"=/_/frozen"]
  with (out/(name+"-build.log")).open("w",encoding="utf-8") as log:
   proc=subprocess.Popen(cmd,cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT);save(out/(name+"-running.json"),{"pid":proc.pid,"command":cmd});code=proc.wait()
  jobs.append({"name":name,"command":cmd,"pid":proc.pid,"exitCode":code,"terminal":True});save(out/"build-jobs.json",jobs)
  if code:raise RuntimeError(name+" build failed")
 save(out/"build-result.json",{"pass":True,"jobs":jobs,"productsExecuted":False,"networkSources":[],"removedFiles":[i for i in inputs if i["relative"].startswith("InputHost/HcVerbatim/") and i["relative"].endswith(".cs")]})
 print(json.dumps({"out":str(out),"pass":True}))
if __name__=="__main__":main()

#!/usr/bin/env python3
"""R25 source-extracted MessageCodec WriteAsync conservative bound candidate.
Standalone .NET framework fixture only, no Host Main/device SDK or installed app.
"""
from pathlib import Path
import argparse,datetime,difflib,hashlib,json,os,re,subprocess,sys
sys.dont_write_bytecode=True
from virtual_sample_tree_perf_selftest import source_function
REPO=Path(__file__).resolve().parents[1];OUTPUT=REPO.parents[2]/'Build/Validation/VirtualInputPerf-R25-20261015'
PLAN={'campaigns':2,'rounds':11,'minimumFasterRounds':10,'minimumPairedReduction':.05,'minimumRatioReduction':.05,'allocationsMustNotIncrease':True,'formalWritesPerMeasurement':24000,'fixedWarmupMs':1500,'tieredCompilation':'0','scenarios':['ASCII-frame-ACK','Unicode-frame-ACK','ASCII-initial-fast-bound','Unicode-grown-buffer'],'decision':'Every eligible write scenario must pass both Release/Debug and both campaigns; fallbacks require exact behavior parity; no relaxation.'}
sha=lambda x:hashlib.sha256(x.encode('utf-8') if isinstance(x,str) else x).hexdigest()
def write(p,x):
 p=Path(p).resolve();assert p.is_relative_to(OUTPUT.resolve());p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(x.encode('utf-8') if isinstance(x,str) else x)
def save(p,x):write(p,json.dumps(x,ensure_ascii=False,indent=2)+'\n')
def codec(src):return source_function(src,'private sealed class MessageCodec')
def transform(src):
 body=codec(src)
 a=body.index('            var required = Encoding.UTF8.GetByteCount(message);');b=body.index('            // NamedPipe message mode',a);old=body[a:b]
 new='''            int required;
            // Default UTF-8 needs at most three bytes per UTF-16 code unit,
            // including replacement for unpaired surrogates. Prove both bounds
            // before skipping the count pass; use the encoder's actual length.
            if (message is not null &&
                message.Length <= Math.Min(_writeBuffer.Length, MaxMessageBytes) / 3)
            {
                required = Encoding.UTF8.GetBytes(message, 0, message.Length, _writeBuffer, 0);
            }
            else
            {
                required = Encoding.UTF8.GetByteCount(message!);
                if (required > MaxMessageBytes) throw new InvalidDataException("response-too-large");
                if (_writeBuffer.Length < required) _writeBuffer = new byte[required];
                Encoding.UTF8.GetBytes(message!, 0, message!.Length, _writeBuffer, 0);
            }
'''
 if '\r\n' in old:new=new.replace('\n','\r\n')
 assert body.count(old)==1
 after=src.replace(old,new,1);assert after.replace(new,old,1)==src
 return after

def generated(src,candidate):
 limit=re.search(r'private const int MaxMessageBytes\s*=\s*([^;]+);',src).group(1)
 text='using System;\nusing System.IO;\nusing System.IO.Pipes;\nusing System.Text;\nusing System.Threading;\nusing System.Threading.Tasks;\ninternal static class GeneratedCodec { internal const int MaxMessageBytes = '+limit+'; }\n'
 for name,s,counted in [('BaselineCodec',src,False),('CandidateCodec',candidate,False),('CountedBaselineCodec',src,True),('CountedCandidateCodec',candidate,True)]:
  body=codec(s).replace('private sealed class MessageCodec','internal sealed class '+name,1)
  a=body.index('{')+1;body=body[:a]+'\n        private const int MaxMessageBytes = GeneratedCodec.MaxMessageBytes;\n        internal byte[] FixtureBuffer => _writeBuffer;\n'+body[a:]
  if counted:body=body.replace('Encoding.UTF8.','Utf8Calls.')
  text+=body+'\n'
 return text

def dotnet(run,args,stem,timeout=180):
 env=os.environ.copy();env.update(DOTNET_CLI_TELEMETRY_OPTOUT='1',DOTNET_NOLOGO='1',DOTNET_TieredCompilation='0')
 p=subprocess.run(['dotnet',*args],cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=timeout,env=env)
 write(run/(stem+'.stdout.log'),p.stdout);write(run/(stem+'.stderr.log'),p.stderr)
 return p

def invoke(run,profile,mode,campaign=None):
 args=[str(run/'bin'/profile/'net10.0-windows/CodecEncodeFixture.dll'),mode]
 if campaign is not None:args.append(str(campaign))
 stem=profile+'-'+mode+('' if campaign is None else '-'+str(campaign));p=dotnet(run,args,stem,300)
 if p.returncode and not(mode=='benchmark' and p.returncode==2):raise RuntimeError(stem+': '+p.stdout+p.stderr)
 result=json.loads(p.stdout);save(run/(stem+'.json'),result);return result

def build_verify(run,src,candidate):
 gen=generated(src,candidate);write(run/'GeneratedCodec.cs',gen);write(run/'ProgramFixture.cs',(REPO/'tools/inputhost_message_encode_perf_selftest.cs').read_bytes())
 project='''<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0-windows</TargetFramework><AssemblyName>CodecEncodeFixture</AssemblyName><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><PlatformTarget>x64</PlatformTarget><EnableDefaultCompileItems>false</EnableDefaultCompileItems></PropertyGroup><ItemGroup><Compile Include="ProgramFixture.cs"/><Compile Include="GeneratedCodec.cs"/></ItemGroup></Project>'''
 write(run/'CodecEncodeFixture.csproj',project);empty=run/'empty-nuget-source';empty.mkdir()
 p=dotnet(run,['restore','CodecEncodeFixture.csproj','--source',str(empty),'-p:NuGetAudit=false'],'restore')
 if p.returncode:raise RuntimeError('restore: '+p.stdout+p.stderr)
 results=[]
 for name in ['Release','Debug']:
  p=dotnet(run,['build','CodecEncodeFixture.csproj','-c',name,'--no-restore','--nologo'],'build-'+name)
  if p.returncode:raise RuntimeError('build '+name+' failed; see '+str(run/('build-'+name+'.stdout.log')))
  result=invoke(run,name,'verify');results.append({'profile':name,**result});print(name+': '+str(result['checks'])+' checks passed',flush=True)
 save(run/'correctness-result.json',{'pass':True,'profiles':results,'scope':'Actual extracted codec classes; write-only memory PipeStream substitutes; not OS message-boundary proof or installed Host/device execution'})
 return results

def prepare():
 run=OUTPUT/('encode-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+str(os.getpid()));run.mkdir(parents=True)
 raw=(REPO/'InputHost/Program.cs').read_bytes();src=raw.decode('utf-8-sig');candidate=transform(src);newraw=(b'\xef\xbb\xbf' if raw.startswith(b'\xef\xbb\xbf') else b'')+candidate.encode('utf-8')
 write(run/'baseline-Program.cs',raw);write(run/'candidate-Program.cs',newraw);write(run/'candidate-UNAPPLIED.patch',''.join(difflib.unified_diff(src.splitlines(True),candidate.splitlines(True),fromfile='a/InputHost/Program.cs',tofile='b/InputHost/Program.cs')))
 save(run/'acceptance-plan.json',PLAN);build_verify(run,src,candidate)
 assert raw==(REPO/'InputHost/Program.cs').read_bytes(),'production drift during proof'
 save(run/'source-binding.json',{'baselineProgramSha256':sha(raw),'candidateProgramSha256':sha(newraw),'baselineCodecSha256':sha(codec(src)),'candidateCodecSha256':sha(codec(candidate)),'baselineReadMethodSha256':sha(source_function(src,'public async Task<string?> ReadAsync(')),'readMethodUnchanged':source_function(src,'public async Task<string?> ReadAsync(')==source_function(candidate,'public async Task<string?> ReadAsync('),'outsideWriteBlockUnchanged':True,'productionEdited':False,'targetFramework':'net10.0-windows','fixtureSha256':sha((run/'ProgramFixture.cs').read_bytes()),'generatedSha256':sha((run/'GeneratedCodec.cs').read_bytes())})
 save(OUTPUT/'latest-prepare.json',{'pass':True,'run':str(run)});print(json.dumps({'pass':True,'run':str(run),'productionEdited':False}))

def benchmark(run):
 assert json.loads((run/'acceptance-plan.json').read_text())==PLAN
 binding=json.loads((run/'source-binding.json').read_text());assert sha((REPO/'InputHost/Program.cs').read_bytes())==binding['baselineProgramSha256']
 profiles=[]
 for name in ['Release','Debug']:
  for campaign in [1,2]:
   result=invoke(run,name,'benchmark',campaign);profiles.append({'profile':name,**result});print(name+' campaign '+str(campaign)+': '+str(sum(r['pass'] for r in result['rows']))+'/'+str(len(result['rows']))+' passed',flush=True)
 result={'candidateAccepted':all(p['pass'] for p in profiles),'profiles':profiles,'plan':PLAN,'productionEdited':False,'wholeProcessClaim':False};save(run/'benchmark-result.json',result);print(json.dumps({'candidateAccepted':result['candidateAccepted'],'run':str(run)}))

if __name__=='__main__':
 ap=argparse.ArgumentParser();ap.add_argument('--benchmark-run',type=Path);args=ap.parse_args();benchmark(args.benchmark_run) if args.benchmark_run else prepare()

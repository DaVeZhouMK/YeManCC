#!/usr/bin/env python3
"""R28 protocol optimization parity defect: actual C7 ACK scanner versus DOM.
Correctness repair only; malformed-ACK reproduction is not a field root-cause claim.
"""
from pathlib import Path
import argparse,datetime,difflib,hashlib,json,os,subprocess,sys
sys.dont_write_bytecode=True
from virtual_sample_tree_perf_selftest import source_function,match_brace
REPO=Path(__file__).resolve().parents[1];OUT=REPO.parents[2]/'Build/Validation/VirtualInputReceiptParser-R28-20261015'
VCVARS=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
PLAN={'variant':'cold-unicode-helper-and-initialized-number-locals','profiles':['Release','Checked'],'campaigns':2,'rounds':11,'hotAsciiMaximumMedianRatio':1.05,'unicodeEscapes':'Measured separately; extra mandatory surrogate validation is not called a CPU improvement','correctness':'candidate fast acceptance must imply actual current DOM acceptance and identical returned values','nonuintUnknownNumbers':'Conservative DOM fallback for negative/decimal/exponent/overflow uint; legacy processing remains unchanged','scope':'source-extracted scanner only; no actual pipe/Host or process CPU'}
sha=lambda b:hashlib.sha256(b.encode('utf-8') if isinstance(b,str) else b).hexdigest()
def write(p,x):
 p=Path(p).resolve();assert p.is_relative_to(OUT.resolve());p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(x.encode('utf-8') if isinstance(x,str) else x)
def save(p,x):write(p,json.dumps(x,ensure_ascii=False,indent=2)+'\n')
def inline_transform(src):
 scan=source_function(src,'inline bool scanString(')
 old='''                for (int i = 1; i <= 4; ++i) { if (!isHex(p[i])) return false; }
                p += 5;
                continue;'''
 new=r'''                const auto hexValue = [](char h) -> unsigned {
                    return h <= '9' ? unsigned(h - '0') :
                        (h <= 'F' ? unsigned(h - 'A' + 10) : unsigned(h - 'a' + 10));
                };
                unsigned codeUnit = 0;
                for (int i = 1; i <= 4; ++i) {
                    if (!isHex(p[i])) return false;
                    codeUnit = (codeUnit << 4) | hexValue(p[i]);
                }
                // JSON's Unicode escapes must encode paired UTF-16 surrogates,
                // matching the existing DOM parser even in ignored metadata.
                if (codeUnit >= 0xD800 && codeUnit <= 0xDBFF) {
                    if (end - p < 11 || p[5] != '\\' || p[6] != 'u') return false;
                    unsigned low = 0;
                    for (int i = 7; i <= 10; ++i) {
                        if (!isHex(p[i])) return false;
                        low = (low << 4) | hexValue(p[i]);
                    }
                    if (low < 0xDC00 || low > 0xDFFF) return false;
                    p += 11;
                } else {
                    if (codeUnit >= 0xDC00 && codeUnit <= 0xDFFF) return false;
                    p += 5;
                }
                continue;'''
 if '\r\n' in scan:old=old.replace('\n','\r\n');new=new.replace('\n','\r\n')
 assert scan.count(old)==1
 after=src.replace(scan,scan.replace(old,new,1),1)
 validate=source_function(after,'bool validateValue(')
 anchor='        if (!scanNumber(p, end, iu, u, nxt)) return false;'
 replacement='''        if (!scanNumber(p, end, iu, u, nxt)) return false;
        // Only bounded unsigned integers are proven representable here.
        // Other numeric metadata (including overflow) uses the original DOM
        // path, which owns floating range and conversion semantics.
        if (!iu) return false;'''
 if '\r\n' in validate:replacement=replacement.replace('\n','\r\n')
 assert validate.count(anchor)==1
 out=after.replace(validate,validate.replace(anchor,replacement,1),1)
 assert out.replace(replacement,anchor,1).replace(new,old,1)==src
 return out

def transform(src):
 after=inline_transform(src)
 scan=source_function(after,'inline bool scanString(')
 a=scan.index("            if (e == 'u') {");o=scan.index('{',a);e=match_brace(scan,o)+1
 block=scan[a:e];body=scan[o+1:e-1].strip('\r\n')
 import textwrap
 body=textwrap.dedent(body)
 assert body.rstrip().endswith('continue;')
 body=body.rstrip()[:-len('continue;')].rstrip()
 helper='__declspec(noinline) bool scanUnicodeEscape(const char*& p, const char* end) {\n'+textwrap.indent(body,'    ')+'\n    return true;\n}\n\n'
 replacement="            if (e == 'u') {\n                if (!scanUnicodeEscape(p, end)) return false;\n                continue;\n            }"
 if '\r\n' in scan:
  helper=helper.replace('\r\n','\n').replace('\n','\r\n');replacement=replacement.replace('\n','\r\n')
 newscan=scan.replace(block,replacement,1)
 after=after.replace(scan,helper+newscan,1)
 validate=source_function(after,'bool validateValue(')
 old='bool iu; unsigned long long u; const char* nxt;'
 new='bool iu = false; unsigned long long u = 0; const char* nxt = nullptr;'
 assert validate.count(old)==1
 after=after.replace(validate,validate.replace(old,new,1),1)
 assert after.replace(helper+newscan,scan,1).replace(new,old,1)==inline_transform(src)
 return after

def core(src):
 a=src.index('struct InputHostFastReceipt {');b=src.index('namespace c7aReceiptFast {',a);e=src.index('// 诊断 parity（仅 tick-cost 门内调用）',b)
 return src[a:b],src[b:e]+'\n}\n'

def header(before,after):
 struct,b=core(before);_,a=core(after)
 verify=source_function(before,'void verifyAgainstDom(')
 start=verify.index('            const bool tupleMatches =');end=verify.index('        } catch (...) {',start)
 oracle=verify[start:end]
 out=struct+b.replace('namespace c7aReceiptFast {','namespace baselineAck {',1)+a.replace('namespace c7aReceiptFast {','namespace candidateAck {',1)
 out+='''struct DomResult { bool accepted=false,firstFrame=false; unsigned long long submitUs=0; std::string instance; };
static DomResult domGolden(const std::string& wire,const char* expectedStatus,const std::string& requestIdValue,unsigned long long sequenceValue) {
    bool domAccept=false,domFirstFrame=false;unsigned long long domSubmitUs=0;std::string domHostInstanceId;
    try {
        auto response=json::parse(wire,nullptr,false);
        if(!response.is_object())return {};
'''+oracle+'''
    } catch(...) { domAccept=false; }
    return {domAccept,domFirstFrame,domSubmitUs,domHostInstanceId};
}
'''
 return out

def compile_verify(run):
 results=[]
 for name,flags in [('Release',['/O2','/DNDEBUG','/MT']),('Checked',['/Od','/RTC1','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd'])]:
  cmd=['cl','/nologo','/std:c++20','/utf-8','/EHsc','/fp:precise',*flags,str(run/'fixture.cpp'),'/I'+str(run),'/Fo'+str(run/(name+'.obj')),'/Fe'+str(run/(name+'.exe'))]
  bat=run/(name+'-build.bat');write(bat,'@echo off\nset VSLANG=1033\ncall "'+str(VCVARS)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(cmd)+'\nexit /b %errorlevel%\n')
  p=subprocess.run(['cmd.exe','/d','/c',str(bat)],cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=180);write(run/(name+'-build.log'),p.stdout+p.stderr)
  if p.returncode:raise RuntimeError('compile failed '+name+': '+str(run/(name+'-build.log')))
  result=invoke(run,name,'verify');results.append({'profile':name,**result});print(name+': '+str(result['checks'])+' checks; old false accepts '+str(result['originalFalseAccepts']),flush=True)
 save(run/'correctness-result.json',{'pass':True,'profiles':results,'existingDOMBodyExtracted':True,'wholeProcessClaim':False,'fieldTriggerUnproven':True})

def invoke(run,name,mode,c=None):
 args=[str(run/(name+'.exe')),mode]
 if c is not None:args.append(str(c))
 p=subprocess.run(args,cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=300)
 stem=name+'-'+mode+('' if c is None else '-'+str(c));write(run/(stem+'.stdout.json'),p.stdout);write(run/(stem+'.stderr.log'),p.stderr)
 if p.returncode and not(mode=='cost' and p.returncode==2):raise RuntimeError(stem+': '+p.stderr)
 return json.loads(p.stdout)

def prepare():
 run=OUT/('contract-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+str(os.getpid()));run.mkdir(parents=True)
 raw=(REPO/'native/main.cpp').read_bytes();src=raw.decode('utf-8-sig');after=transform(src);newraw=(b'\xef\xbb\xbf' if raw.startswith(b'\xef\xbb\xbf') else b'')+after.encode('utf-8')
 write(run/'baseline-main.cpp',raw);write(run/'candidate-main.cpp',newraw);write(run/'candidate-UNAPPLIED.patch',''.join(difflib.unified_diff(src.splitlines(True),after.splitlines(True),fromfile='a/native/main.cpp',tofile='b/native/main.cpp')))
 write(run/'extracted.hpp',header(src,after));write(run/'fixture.cpp',(REPO/'tools/virtual_ack_parser_contract_selftest.cpp').read_bytes());write(run/'json.hpp',(REPO/'deps/json/json.hpp').read_bytes());save(run/'acceptance-plan.json',PLAN)
 compile_verify(run)
 assert raw==(REPO/'native/main.cpp').read_bytes(),'source drift during proof'
 save(run/'source-binding.json',{'baselineMainSha256':sha(raw),'candidateMainSha256':sha(newraw),'baselineScanStringSha256':sha(source_function(src,'inline bool scanString(')),'candidateScanStringSha256':sha(source_function(after,'inline bool scanString(')),'baselineValidateValueSha256':sha(source_function(src,'bool validateValue(')),'candidateValidateValueSha256':sha(source_function(after,'bool validateValue(')),'scannerCaptureAndCallerUnchanged':True,'onlyUnicodeEscapeAndUnknownNumberProofChanged':True,'productionEdited':False})
 save(OUT/'latest-prepare.json',{'pass':True,'run':str(run)});print(json.dumps({'pass':True,'run':str(run),'productionEdited':False}))

def cost(run):
 assert json.loads((run/'acceptance-plan.json').read_text())==PLAN
 binding=json.loads((run/'source-binding.json').read_text());assert sha((REPO/'native/main.cpp').read_bytes())==binding['baselineMainSha256']
 results=[]
 for profile in ['Release','Checked']:
  for c in [1,2]:
   r=invoke(run,profile,'cost',c);results.append({'profile':profile,**r});print(profile+' cost campaign '+str(c)+': '+str(r['pass']),flush=True)
 save(run/'cost-result.json',{'pass':all(r['pass'] for r in results),'profiles':results,'plan':PLAN,'correctnessRepairNotOptimizationClaim':True,'productionEdited':False})

def final(accepted):
 run=OUT/('current-source-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'));run.mkdir(parents=True)
 raw=(REPO/'native/main.cpp').read_bytes();base=(accepted/'baseline-main.cpp').read_bytes()
 assert raw==(accepted/'candidate-main.cpp').read_bytes() and raw.decode('utf-8-sig')==transform(base.decode('utf-8-sig'))
 write(run/'extracted.hpp',header(base.decode('utf-8-sig'),raw.decode('utf-8-sig')));write(run/'fixture.cpp',(REPO/'tools/virtual_ack_parser_contract_selftest.cpp').read_bytes());write(run/'json.hpp',(REPO/'deps/json/json.hpp').read_bytes())
 compile_verify(run);assert raw==(REPO/'native/main.cpp').read_bytes()
 save(run/'source-binding.json',{'currentSourceSha256':sha(raw),'acceptedSourceSha256':sha((accepted/'candidate-main.cpp').read_bytes()),'exactCandidateIdentity':True,'acceptedRun':str(accepted)});save(OUT/'latest-final.json',{'pass':True,'run':str(run)});print(json.dumps({'pass':True,'run':str(run)}))

if __name__=='__main__':
 ap=argparse.ArgumentParser();ap.add_argument('--cost-run',type=Path);ap.add_argument('--final-run',type=Path);a=ap.parse_args()
 if a.cost_run:cost(a.cost_run)
 elif a.final_run:final(a.final_run)
 else:prepare()

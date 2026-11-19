import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from raw_packet_owned import *
import re

def graph(out,text):
 d=load(out/'dependencies.json')['Data']
 need(not d.get('ImportedModules') and not d.get('ImportedHeaderUnits'),'UNDECLARED_MODULE')
 sources=unique_pins([pin(p) for p in [d['Source'],*d['Includes']]])
 libs=[]
 for line in text.splitlines():
  m=re.match(r'\s*(?:Searching|Found|Loaded)\s+(.+?\.lib)(?:\([^)]*\))?(?::)?\s*$',line,re.I)
  if m:
   p=Path(m.group(1).strip());need(p.is_absolute() and p.is_file(),'UNRESOLVED_LIBRARY:'+line);libs.append(pin(p))
 need(libs,'NO_ACTUAL_LIBRARY_GRAPH')
 return unique_pins([*sources,*libs]),sources,unique_pins(libs)

def main():
 begin=now();decl=load(sys.argv[3]);prepared=load(E/'PREPARED.json')
 verify(prepared['inputs']);verify(prepared['generated']);verify(decl['sources']);verify(decl['tools'])
 need('14.44.35207' in decl['compiler'] and '10.0.26100.0' in decl['environment']['INCLUDE'],'FIXED_TOOLCHAIN_REQUIRED')
 env=dict(decl['environment']);temp=E/'Temp';temp.mkdir();env['TEMP']=env['TMP']=str(temp)
 env['PYTHONDONTWRITEBYTECODE']='1'
 support=unique_pins([*[pin(p['path']) for p in prepared['inputs']],pin(E/'PREPARED.json'),pin(sys.argv[3]),pin(HELPER)])
 original=(E/'dispatcher.generated.h').read_bytes().decode('utf-8')
 # Only the smallest selected boundary witnesses run for each semantic negative.
 variants=[('Candidate-Release',['/O2','/DNDEBUG','/MD'],original,None,1),('Candidate-Checked',['/Od','/RTC1','/Zi','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MDd'],original,None,1),
 ('Neg-no-insufficient-retry',['/O2','/DNDEBUG','/MD'],original.replace('if (rawSizeFromHeader && rawReadResult == ~0u && rawReadError == ERROR_INSUFFICIENT_BUFFER)', 'if (!rawSizeFromHeader && rawReadResult == ~0u && rawReadError == ERROR_INSUFFICIENT_BUFFER)'),'raw-packet-insufficient-recovery',1),
 ('Neg-no-final-bounds',['/O2','/DNDEBUG','/MD'],original.replace('rawReadResult == dwSize && dwSize >= sizeof(RAWINPUTHEADER) && dwSize <= sizeof(g_riBuf)','rawReadResult == dwSize'),'raw-packet-final-bounds',1),
 ('Diag-off-compile-only',['/O2','/DNDEBUG','/MD'],original,None,0)]
 need(variants[2][2]!=original and variants[3][2]!=original,'MUTATION_ANCHOR_MISSING')
 phase=sys.argv[4] if len(sys.argv)>4 else 'normal-and-controls'
 need(phase in ['normal-and-controls','remaining-controls','final-bounds-suffix','mouse-payload-before','mouse-payload-admission','mouse-payload-controls'],'UNKNOWN_CONTINUATION_PHASE')
 if phase=='remaining-controls':variants=variants[2:]
 if phase=='final-bounds-suffix':variants=[(a,b,c,'raw-packet-final-bounds',d) for a,b,c,_,d in [variants[0],variants[1],variants[3]]]
 if phase=='mouse-payload-before':
  variants=[('Neg-before-mouse-no-payload',['/O2','/DNDEBUG','/MD'],original,'raw-mouse-diag-off',1)]
 if phase in ['mouse-payload-admission','mouse-payload-controls']:
  bad=original.replace('!g_inputEdgeEvents.enabled()', 'true')
  need(bad!=original,'MOUSE_GUARD_ANCHOR_MISSING')
  variants=[('Candidate-Mouse-Release',['/O2','/DNDEBUG','/MD'],original,'mouse-payload-all',1),
   ('Candidate-Mouse-Checked',['/Od','/RTC1','/Zi','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MDd'],original,'mouse-payload-all',1),
   ('Candidate-Mouse-Diag0',['/O2','/DNDEBUG','/MD'],original,'mouse-payload-all',0),
   ('Candidate-Mouse-Fault',['/O2','/DNDEBUG','/MD','/DYMCC_FAULT_INJECT'],original,'mouse-payload-all',1),
   ('Neg-mouse-diag-on',['/O2','/DNDEBUG','/MD'],bad,'raw-mouse-diag-on',1)]
 if phase=='mouse-payload-controls':variants=variants[3:]
 results=[]
 for index,(label,flags,source,selection,diag) in enumerate(variants):
  need(now()<deadline()-60000,'INSUFFICIENT_REMAINING_TIME')
  out=E/'B'/str(index);out.mkdir(parents=True)
  put(out/'dispatcher.generated.h',source)
  # Same selected formal harness bytes in every configuration.
  with (out/'raw_packet.cpp').open('xb') as f:f.write((E/'raw_packet.cpp').read_bytes())
  with (out/'input_edge_evidence.h').open('xb') as f:f.write((E/'input_edge_evidence.h').read_bytes())
  exe=out/'consumer.exe'
  args=['/nologo','/std:c++17','/EHsc','/utf-8','/W4','/WX','/permissive-','/fp:precise',*flags,'/DYMCC_ROG_FSE_DIAGNOSTICS='+str(diag),str(out/'raw_packet.cpp'),'/Fo'+str(out/'consumer.obj'),'/Fd'+str(out/'compiler.pdb'),'/Fe'+str(exe),'/sourceDependencies',str(out/'dependencies.json'),'/link','/INCREMENTAL:NO','/VERBOSE:LIB']
  need(max(len(str(out/p)) for p in ['raw_packet.cpp','consumer.exe','dependencies.json'])<260,'OUTPUT_PATH_LENGTH')
  sources=unique_pins([*decl['sources'],*support,pin(out/'raw_packet.cpp'),pin(out/'dispatcher.generated.h'),pin(out/'input_edge_evidence.h')]);tools=decl['tools']
  recipe=dict(program=decl['compiler'],arguments=args,environment=env,diagnostics=diag,selection=selection)
  put(out/'recipe.json',recipe);sources.append(pin(out/'recipe.json'))
  put(out/'before.json',dict(recordedEpochMs=now(),sources=sources,tools=tools,phase='BEFORE_COMPILER_START'))
  print(json.dumps(dict(stage='compile',label=label)),flush=True)
  stdout,stderr,terminal=owned(label+'-compile',decl['compiler'],args,env,sources,tools,180000,compile=True)
  actual,inc,libs=graph(out,stdout);before={key(p['path']):p for p in [*sources,*tools]}
  for p in actual:
   need(key(p['path']) in before,'ACTUAL_UNDECLARED_INPUT:'+p['path']);b=before[key(p['path'])];need(b['sha256']==p['sha256'] and b['bytes']==p['bytes'],'ACTUAL_INPUT_CHANGED')
  verify(sources);verify(tools)
  put(out/'after.json',dict(recordedEpochMs=now(),sources=verify(sources),tools=verify(tools),actualSourceIncludes=inc,actualLibraries=libs,unknownActual=[],binary=pin(exe),compileTerminal=pin(E/('Raw-'+label+'-compile')/'terminal.json')))
  row=dict(label=label,recipe=pin(out/'recipe.json'),before=pin(out/'before.json'),after=pin(out/'after.json'),binary=pin(exe),compileTerminal=pin(E/('Raw-'+label+'-compile')/'terminal.json'))
  if diag==0 and not phase.startswith('mouse-payload-'):row.update(status='COMPILED_ONLY',actualRuns=0,oldGroupNotRun=True)
  else:
   negative=label.startswith('Neg-')
   stdout,stderr,t=owned(label+'-run',exe,['--mouse-payload-admission'] if selection=='mouse-payload-all' else ['--case',selection] if selection else [],env,[*sources,pin(out/'after.json')],tools,30000,expected=1 if negative else 0)
   matches=re.findall(r'^CASE ([a-z-]+) checks=(\d+) failures=(\d+)\r?$',stdout,re.M)
   total=re.search(r'^SUMMARY cases=(\d+) checks=(\d+) failures=(\d+)\r?$',stdout,re.M);need(total,'SEMANTIC_SUMMARY_MISSING')
   names=[a[0] for a in matches];wanted=prepared['selectedMousePayloadCases'] if selection=='mouse-payload-all' else [selection] if selection else prepared['selectedOriginalParameterizedCases']
   need(names==wanted and int(total[1])==len(wanted),'EXACT_SELECTION_MISMATCH')
   need(sum(int(a[1]) for a in matches)==int(total[2]) and sum(int(a[2]) for a in matches)==int(total[3]),'SUMMARY_MISMATCH')
   if negative:
    need(int(total[3])>0 and all(line.startswith('FAIL '+selection+': ') for line in stderr.splitlines()),'NOT_NAMED_SEMANTIC_REJECTION')
   else:need(int(total[3])==0 and not stderr.strip(),'NORMAL_ASSERTIONS_FAILED')
   row.update(status='EXPECTED_NAMED_SEMANTIC_REJECTION' if negative else 'PASSED',actualRuns=1,cases=names,checks=int(total[2]),failures=int(total[3]),runTerminal=pin(E/('Raw-'+label+'-run')/'terminal.json'),stdout=pin(E/('Raw-'+label+'-run')/'stdout.raw'),stderr=pin(E/('Raw-'+label+'-run')/'stderr.raw'))
  put(out/'RESULT.json',row);results.append(row);print(json.dumps(dict(stage='complete',label=label,status=row['status'])),flush=True)
 jobs=[]
 for raw in sorted(E.glob('Raw-*')):
  t=load(raw/'terminal.json');jobs.append(dict(terminal=pin(raw/'terminal.json'),admission=pin(raw/'admission.json'),business=t['business'],job=t['job'],reason=t['reason'],toolCleanup=t.get('declaredToolCleanup')))
 expectedJobs=2*len(variants) if phase.startswith('mouse-payload-') else 5 if phase=='remaining-controls' else 6 if phase=='final-bounds-suffix' else 9
 need(len(jobs)==expectedJobs and all(j['job']['zeroObservedBeforeDispose'] and j['job']['activeProcessesFinal']==0 and j['job']['disposed'] for j in jobs),'ALL_SELECTED_ACTUAL_JOB_ZERO_REQUIRED')
 put(E/'RESULT.json',dict(status='PASSED_SCOPED_DISPATCHER',passed=True,phase=phase,results=results,jobs=jobs,actualCompiles=len(variants),actualRuns=sum(r.get('actualRuns',0) for r in results),old31NotRun=True,oldWholeMutantsNotRun=True,startedEpochMs=begin,finishedEpochMs=now(),deadlineEpochMs=deadline(),sourceReady=False,scope=prepared['scope']))

if __name__=='__main__':
 try:main()
 except Exception:
  put(E/'FAILURE.json',dict(status='STOP_FURTHER_STARTS',error=traceback.format_exc(),atEpochMs=now()))
  raise

"""Prepare only: actual complete native count helper and base classifier; no execution.

--reference-source requires an original complete main without the new edge. This
entry never compiles,launches the product,reads live logs or touches devices.
"""
from pathlib import Path
import argparse,hashlib,json,re
ROOT=Path(__file__).resolve().parents[1]

def definition(source,signature):
    assert source.count(signature)==1,(signature,source.count(signature))
    start=source.index(signature)
    masked=re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',lambda m:' '*len(m[0]),source,flags=re.S)
    # Match outer parameters first; default {} must not truncate the definition.
    opening=masked.index('(',start);depth=0;end=None
    for i in range(opening,len(masked)):
        if masked[i]=='(':depth+=1
        elif masked[i]==')':
            depth-=1
            if depth==0:end=i+1;break
    assert end is not None
    opening=masked.index('{',end);assert ';' not in masked[end:opening]
    depth=0
    for i in range(opening,len(masked)):
        if masked[i]=='{':depth+=1
        elif masked[i]=='}':
            depth-=1
            if depth==0:return source[start:i+1]
    raise ValueError('incomplete definition')

def parts(source):
    begin=source.index('struct InputChainCounters {');end=source.index('// Host-only receipt evidence:',begin)
    state=source[begin:end]
    assert state.count('g_inputChain;')==1 and 'kWakeChainRejectRowMax = 8;' in state
    return dict(STATE=state,COUNT=definition(source,'static void inputChainCountFrame('),BASE=definition(source,'static bool nativeLifecycleEventIsBase(const char* event) {'))

def fixture(source,reference=None):
    template=(ROOT/'tools/fixtures/logging_input_chain.cpp.in').read_text('utf-8-sig')
    for variant,text in [('CURRENT',source),('REFERENCE',reference if reference is not None else source)]:
        for name,value in parts(text).items():
            marker='// ACTUAL_'+variant+'_'+name;assert template.count(marker)==1;template=template.replace(marker,value,1)
    return template

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output-dir',type=Path,required=True);p.add_argument('--reference-source',type=Path,required=True);a=p.parse_args()
    raw=(ROOT/'native/main.cpp').read_bytes();source=raw.decode('utf-8-sig').replace('\r\n','\n');reference=a.reference_source.read_text('utf-8-sig') if a.reference_source else None
    if 'wake-input-chain-first-nonneutral' in parts(reference)['COUNT']:p.error('--reference-source must be an original source without the new count edge')
    cpp=fixture(source,reference);a.output_dir.mkdir(parents=True,exist_ok=False)
    with (a.output_dir/'input-chain.cpp').open('x',encoding='utf-8',newline='\n') as f:f.write(cpp)
    report=dict(status='PREPARED_ONLY',sourceSha256=hashlib.sha256(raw).hexdigest(),selectedDefinitions=['InputChainCounters+constants','inputChainCountFrame','nativeLifecycleEventIsBase'],referenceProvided=reference is not None,productExecution=False,defaultCompile=False)
    with (a.output_dir/'PREPARED.json').open('x',encoding='utf-8') as f:json.dump(report,f,indent=2);f.write('\n')
    print(json.dumps(report));return 0
if __name__=='__main__':raise SystemExit(main())

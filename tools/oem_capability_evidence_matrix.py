"""Join compiled native capability routes, committed HC source and exported log evidence.
Static declarations and callback overrides are source facts, never physical acceptance.
"""
import argparse, collections, hashlib, json, pathlib, re
EFFECTS={'SolidColor':'solid','Breathing':'breathing','Rainbow':'rainbow','Wave':'wave','Wheel':'wheel','Gradient':'gradient','Ambilight':'ambilight','LEDPreset':'preset'}
def digest(b): return hashlib.sha256(b).hexdigest()
def strip_comments(s):
    blank=lambda m: re.sub(r'[^\n]', ' ', m.group(0))
    return re.sub(r'//[^\n]*',blank,re.sub(r'/\*[\s\S]*?\*/',blank,s))
def ctor(text,name):
    clean=strip_comments(text)
    match=re.search(r'\b(?:public|protected|internal)\s+'+re.escape(name)+r'\s*\([^)]*\)\s*(?::[^\{]*)?\{',clean)
    if not match: return ''
    start=match.end();depth=1
    for i in range(start,len(clean)):
        if clean[i]=='{':depth+=1
        elif clean[i]=='}':
            depth-=1
            if depth==0:return clean[start:i]
    raise ValueError('unclosed constructor '+name)
def parse_hc(root):
    classes={}
    for f in sorted(root.rglob('*.cs')):
        data=f.read_bytes();text=data.decode('utf-8-sig');clean=strip_comments(text)
        matches=list(re.finditer(r'public\s+(?:(?:abstract|partial|sealed)\s+)*class\s+(\w+)(?:\s*:\s*([\w.]+))?',clean))
        for i,match in enumerate(matches):
            stop=matches[i+1].start() if i+1<len(matches) else len(text)
            segment=text[match.start():stop];clean_segment=clean[match.start():stop]
            name=match[1];body=ctor(segment,name)
            operations=[]
            for op in re.finditer(r'\b(Capabilities|DynamicLightingCapabilities)\s*(\|=|\+=|-=|=)\s*([^;]+);',body):
                domain='flags' if op[1]=='Capabilities' else 'effects'
                namespace='DeviceCapabilities' if domain=='flags' else 'LEDLevel'
                names=re.findall(namespace+r'\.(\w+)',op[3]);operations.append({'domain':domain,'op':op[2],'names':names,'expression':op[3].strip()})
            callbacks={}
            for method in ('SetLedStatus','SetLedBrightness','SetLedColor','SetLEDPreset'):
                m=re.search(r'public\s+(?:override\s+|virtual\s+)?bool\s+'+method+r'\s*\(',clean_segment)
                if m:
                    orig=re.search(r'\b'+method+r'\s*\(',segment)
                    callbacks[method]={'class':name,'path':str(f),'line':text[:match.start()].count('\n')+segment[:orig.start()].count('\n')+1 if orig else None,'baseStub':name=='IDevice'}
            chords=[]
            for ch in re.finditer(r'OEMChords\.Add\(new\s+\w*Chord\(([\s\S]*?)\)\)\s*;',body):
                args=ch[1];label=re.search(r'"([^"]+)"',args);flags=re.search(r'ButtonFlags\.(\w+)',args)
                silenced=re.search(r',\s*(true|false)\s*,\s*(?:ButtonFlags|flushInterval)',args)
                chords.append({'name':label[1] if label else '?','buttonFlag':flags[1] if flags else None,
                    'keys':list(dict.fromkeys(re.findall(r'KeyCode\.(\w+)',args))),
                    'silenced':silenced[1]=='true' if silenced else False,'class':name})
            namespace_matches=re.findall(r'\bnamespace\s+([\w.]+)\s*[;{]',clean[:match.start()]);namespace=namespace_matches[-1] if namespace_matches else ''
            aliases=dict(re.findall(r'\busing\s+(\w+)\s*=\s*([\w.]+)\s*;',clean[:match.start()]))
            declared_base=match[2];resolved_base=aliases.get(declared_base,declared_base) if declared_base else None
            classes[name]={'name':name,'namespace':namespace,'fullName':(namespace+'.' if namespace else '')+name,'baseDeclared':declared_base,'base':resolved_base.split('.')[-1] if resolved_base else None,'file':str(f),'sha256':digest(data),
                'operations':operations,'callbacks':callbacks,'chords':chords,'clearsChords':'OEMChords.Clear()' in body,
                'conditionalConstructor':bool(re.search(r'\bif\s*\(',body))}
    return classes

def effective(classes,name):
    chain=[];current=name;seen=set()
    while current in classes and current not in seen:
        seen.add(current);chain.insert(0,current);current=classes[current]['base']
    values={'flags':[],'effects':[]};callbacks={};chords=[]
    for node in chain:
        cls=classes[node]
        for op in cls['operations']:
            # Nonliteral expressions are disclosed; do not synthesize capabilities.
            domain=values[op['domain']]
            if op['op']=='=':domain.clear()
            for value in op['names']:
                if op['op']=='-=':
                    if value in domain:domain.remove(value)
                elif value not in domain:domain.append(value)
        callbacks.update(cls['callbacks'])
        if cls['clearsChords']:chords=[]
        chords+=cls['chords']
    return {'classChain':chain,'capabilityFlags':values['flags'],'effects':sorted(EFFECTS[e] for e in values['effects'] if e in EFFECTS),
        'callbacks':callbacks,'chords':chords,'sourceFiles':[{'path':classes[n]['file'],'sha256':classes[n]['sha256']} for n in chain],
        'interpretation':'constructor declaration analysis; data-dependent branches/method behavior need separate runtime evidence'}

def main():
    p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--hc-devices',required=True);p.add_argument('--logs',required=True);p.add_argument('--output',required=True);p.add_argument('--host-directions');p.add_argument('--source-reachability');p.add_argument('--runtime-lighting');a=p.parse_args()
    native=json.loads(pathlib.Path(a.native).read_text(encoding='utf-8'));classes=parse_hc(pathlib.Path(a.hc_devices));logs=json.loads((pathlib.Path(a.logs)/'log-audit-index.json').read_text(encoding='utf-8'))
    host_directions=json.loads(pathlib.Path(a.host_directions).read_text(encoding='utf-8')) if a.host_directions else []
    source_report=json.loads(pathlib.Path(a.source_reachability).read_text(encoding='utf-8')) if a.source_reachability else None
    source_lookup={(c['deviceClass'],k['keyId']):k for c in source_report['classes'] for k in c['keys']} if source_report else {}
    runtime_report=json.loads(pathlib.Path(a.runtime_lighting).read_text(encoding='utf-8')) if a.runtime_lighting else None
    runtime_lookup={c['deviceClass']:c for c in runtime_report['classes']} if runtime_report else {}
    unique={m['deviceClass']:m for m in native['models']};result=[];issues=[]
    for name,n in sorted(unique.items()):
        h=effective(classes,name.split('.')[-1]);light='DynamicLighting' in h['capabilityFlags'];second='DynamicLightingSecondLEDColor' in h['capabilityFlags'];brightness='DynamicLightingBrightness' in h['capabilityFlags']
        observed=[r for r in logs['results'] if any(i.get('deviceClass')==name for i in r['identities'])]
        per_key=[]
        for key in n['keys']:
            receipts=[]
            for archive in observed:
                log_path=pathlib.Path(a.logs)/(pathlib.Path(archive['archive']).stem+'.json')
                details=json.loads(log_path.read_text(encoding='utf-8'))
                observation=details.get('keySourceObservations',{}).get(key['keyId'])
                if observation:receipts.append({'archive':archive['archive'],**observation})
            per_key.append({'keyId':key['keyId'],'sourceStatus':'Partially observed' if receipts else 'Not proven',
                'receipts':receipts,'staticSourceReachability':source_lookup.get((name,key['keyId'])),
                'hostDirectionCodecs':[d for d in host_directions if d['model']==name and d['keyId']==key['keyId']],
                'actionDispatch':'Not proven','officialActionSuppression':'Not proven',
                'physicalLeftRight':'Not proven','externalConsumer':'Not proven'})
        conflicts=[]
        for k in n['keys']:
            for r in k['keyboardRules']:
                if k['backIndex']>0 and r['outputBackBits'] and r['outputBackBits']!=(1<<(k['backIndex']-1)):
                    conflicts.append({'keyId':k['keyId'],'catalogBackIndex':k['backIndex'],'outputBackBits':r['outputBackBits'],
                        'status':'Contradicted','scope':'catalog front-view side vs existing keyboard output bit; physical side not proven'})
        comparisons={'lightingDeclarationMatches':light==n['lightingDeclared'],
          'effectsMatch':not light or h['effects']==sorted(n['ledEffects']),
          'secondColorMatches':second==n['secondColorDeclared'],'hcBrightnessDeclared':brightness}
        unsupportedSecondExclusion=name.split('.')[-1] in ('AYANEOFlipDS','AYANEOFlip1SDS') and not light
        if unsupportedSecondExclusion:comparisons['acceptedSecondColorExclusionWhenNoLighting']=True
        mismatches=[]
        if not comparisons['lightingDeclarationMatches']:mismatches.append('lightingDeclarationMatches')
        if not comparisons['effectsMatch']:mismatches.append('effectsMatch')
        if not comparisons['secondColorMatches'] and not unsupportedSecondExclusion:mismatches.append('secondColorMatches')
        noops=[method for method,provider in h['callbacks'].items() if provider['baseStub'] and
            ((method=='SetLedBrightness' and brightness) or (method=='SetLedColor' and light))]
        row={'deviceClass':name,'family':n['family'],'identityCases':[m['identityCase'] for m in native['models'] if m['deviceClass']==name],
          'native':n,'hc':h,'comparison':comparisons,'logs':[{'path':r['archive'],'sha256':r['archiveSha256'],'nativeSha256':r['manifest'].get('nativeSha256'),'exportedAtLocal':r['manifest'].get('exportedAtLocal')} for r in observed],
          'directionConflicts':conflicts,'baseStubWithDeclaredCapability':noops,'perKeyEvidence':per_key,
          'closure':{'catalogResolver':'Proven',
            'staticSourceRoute':('Not proven' if not source_report else 'Contradicted' if any(not source_lookup.get((name,k['keyId']),{}).get('sourceCandidateDeclared',False) for k in n['keys']) else 'Proven'),
            'hcStaticComparison':('Not proven' if not h['classChain'] else 'Contradicted' if mismatches else 'Proven'),
            'realSourceEvents':'Partially observed' if any(k['receipts'] for k in per_key) else 'Not proven','physicalLeftRight':'Not proven',
            'physicalLighting':'Not proven' if light else 'Not applicable','physicalVibration':'Not proven',
            'vendorActionSuppression':'Not proven','complete':False}}
        if runtime_report:
            row['runtimeLightingBinding']=runtime_lookup.get(name)
        result.append(row)
        if conflicts or mismatches or noops:issues.append({'deviceClass':name,'directionConflicts':conflicts,'staticMismatches':mismatches,'baseStubWithDeclaredCapability':noops})
    report={'schema':1,'HCCommit':'06c0b9544db2b1f39abf9cd3796225ccfb096103','HCSource':'committed git objects; candidate working-copy modifications excluded',
      'runtimeLightingBindings':runtime_report,'sourceReachability':source_report,'hostDirectionalCodecCases':len(host_directions),'nativeModelCases':native['modelCases'],'uniqueDeviceClasses':len(unique),'logArchives':logs['archives'],
      'loggedDeviceClasses':sorted({i.get('deviceClass') for r in logs['results'] for i in r['identities']}),
      'classes':result,'issues':issues,'goalComplete':False,'hardwareIo':False}
    out=pathlib.Path(a.output);out.mkdir(parents=True,exist_ok=True);(out/'oem-capability-evidence-matrix.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({'modelCases':native['modelCases'],'uniqueClasses':len(unique),'issues':[{'class':i['deviceClass'],'directionConflicts':len(i['directionConflicts']),'staticMismatches':i['staticMismatches'],'baseStubWithDeclaredCapability':i['baseStubWithDeclaredCapability']} for i in issues]},ensure_ascii=False,indent=2))
if __name__=='__main__':main()
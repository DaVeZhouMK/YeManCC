"""Read ZIP exports as evidence only; no extraction, device API, settings write or subprocess.
Counts describe exported rows, not physical input/lighting/haptic verification.
"""
from __future__ import annotations
import argparse, collections, hashlib, json, pathlib, re, zipfile

INTEREST = re.compile(r"oem|backkey|rear|chord|feedback|vibrat|light|aura|setLed|claw-led|wmi|led-|gpd-back|virtual-pad-backbuttons|steamdeck-ui-output|domain-pad-truth|rog\.controller-remap|machine-identity|keyboard-shortcut", re.I)
EVENT_DETAIL = re.compile(r"virtual-pad-backbuttons|steamdeck-ui-output|domain-pad-truth|machine-identity|oem-key-catalog-query|oem-raw-keyboard-admit|backkey-edge|gpd-back|feedbackCaps|feedback-caps|lighting|vibration|gamepad-vibrate-request|setLed|msi-claw-led|led-|aura|light-setter|oem-shortcut-edge|oem-producer-edge|msi-frontkey|msi-wmi-key|msi-wmi-event|rog-oem-key$", re.I)
FILE_RE = re.compile(r"^(?:native-(?:lifecycle|detail)\.log(?:\.\d+)?|virtual-gamepad\.log(?:\.\d+)?|input-host-xbox-sdk\.log|app\.log|.*(?:light|feedback).*\.log)$", re.I)

def sha(data): return hashlib.sha256(data).hexdigest()
def compact(row):
    return {k:v for k,v in row.items() if not isinstance(v,(dict,list)) and len(str(v))<900}
def audit_zip(archive):
    result = {'archive':str(archive), 'archiveSha256':sha(archive.read_bytes()), 'archiveBytes':archive.stat().st_size,
      'events':{}, 'identities':[], 'capabilities':[], 'manifest':{}, 'files':[], 'details':[], 'keySourceObservations':{},
      'limits':{'physicalLeftRightProven':False,'physicalLightingProven':False,'physicalVibrationProven':False,
        'eventCountsArePhysicalPressCounts':False,'capsAreRuntimeEndpointObservationNotHardwareAcceptance':True}}
    counters=collections.Counter(); event_stats={}; details=[]
    with zipfile.ZipFile(archive) as z:
        names=z.namelist()
        for name in names:
            base=pathlib.PurePosixPath(name.replace('\\','/')).name
            if base == 'YeManCC-export-manifest.txt':
                text=z.read(name).decode('utf-8-sig','replace')
                for line in text.splitlines():
                    if '=' not in line: continue
                    key,value=line.split('=',1)
                    if key in ('exportedAtLocal','processId','nativeSha256','appVersion','hostSha256','hostDllSha256','hcAssemblySha256','hcAssembly','nativeExe','settingsFileSha256'):
                        result['manifest'][key]=value
                    if key in ('msiRearSourceObservationExport',):
                        try: result[key]=json.loads(value)
                        except ValueError: result[key]={'parseError':True}
                result['manifestSha256']=sha(z.read(name))
            elif base=='yeman-settings.json':
                try:
                    s=json.loads(z.read(name).decode('utf-8-sig'))
                    result['settingsAtExport']={'gamepadFeedback':s.get('gamepad',{}).get('feedback'),
                        'outputTarget':s.get('input',{}).get('outputTarget'),
                        'shortcutRules':s.get('input',{}).get('buttonMapping',{}).get('rules')}
                except (ValueError,AttributeError): result['settingsAtExport']={'parseError':True}
            if not FILE_RE.match(base): continue
            file_stats={'file':name,'jsonRows':0,'unparsedNonemptyRows':0}
            with z.open(name) as stream:
                for line_no,line in enumerate(stream,1):
                    if not line.strip(): continue
                    try: row=json.loads(line.decode('utf-8-sig'))
                    except (ValueError,UnicodeError): file_stats['unparsedNonemptyRows']+=1;continue
                    if not isinstance(row,dict): continue
                    file_stats['jsonRows']+=1
                    e=row.get('event',row.get('eventName',row.get('kind','')))
                    if not isinstance(e,str) or not INTEREST.search(e): continue
                    counters[e]+=1
                    receipt={'file':name,'line':line_no,**compact(row)}
                    stats=event_stats.setdefault(e,{'first':receipt,'last':receipt,'booleanCounts':{},'statusCounts':{},'total':0})
                    stats['last']=receipt;stats['total']+=1
                    for k,v in row.items():
                        if isinstance(v,bool):
                            count_key=k+'='+str(v).lower();stats['booleanCounts'][count_key]=stats['booleanCounts'].get(count_key,0)+1
                        elif k in ('state','status','reason','why','backend','source','button','transition','phase','sourceId') and isinstance(v,str):
                            key=k+'='+v;stats['statusCounts'][key]=stats['statusCounts'].get(key,0)+1
                    key_id=None
                    if e=='msi-wmi-key': key_id={41:'claw',88:'qs'}.get(row.get('code'))
                    elif e=='rog-oem-key': key_id={166:'cc',56:'ac',147:'lib'}.get(row.get('code'))
                    elif e=='msi-backkey-edge' and row.get('button') in ('m1','m2'): key_id=row['button']
                    elif e=='gpd-back-chord-match' and row.get('reason') in ('press','release'):
                        key_id={512:'l4',1024:'r4'}.get(row.get('baseKey'))
                    if key_id:
                        proof=result['keySourceObservations'].setdefault(key_id,{'rows':0,'first':receipt,'last':receipt,
                            'scope':'named native source/keyboard match receipt only; not physical direction, action or external-consumption proof'})
                        proof['rows']+=1;proof['last']=receipt
                    if e=='machine-identity': result['identities'].append(receipt)
                    if re.search(r'feedbackCaps|feedback-caps',e,re.I):
                        # Keep original nested proof metadata without interpreting it as acceptance.
                        result['capabilities'].append({'file':name,'line':line_no,**row})
                    if EVENT_DETAIL.search(e): details.append(receipt)
            result['files'].append(file_stats)
    result['events']=dict(sorted(counters.items()))
    result['eventStats']=event_stats
    result['detailRowsTotal']=len(details)
    result['details']=details if len(details)<=600 else details[:100]+details[-500:]
    result['detailRowsTruncated']=len(details)>600
    return result

def main():
    a=argparse.ArgumentParser();a.add_argument('--logs',required=True);a.add_argument('--output',required=True);args=a.parse_args()
    root=pathlib.Path(args.logs).resolve();out=pathlib.Path(args.output).resolve();out.mkdir(parents=True,exist_ok=True)
    results=[]
    for archive in sorted(root.rglob('*.zip')):
        report=audit_zip(archive);results.append(report)
        (out/(archive.stem+'.json')).write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
        classes=sorted({r.get('deviceClass','?') for r in report['identities']})
        print(json.dumps({'archive':archive.name,'classes':classes,'capRows':len(report['capabilities']),
            'events':len(report['events']),'detailRows':report['detailRowsTotal']},ensure_ascii=False),flush=True)
    index={'schema':1,'logRoot':str(root),'archives':len(results),'logsReadOnly':True,'hardwareIo':False,
      'scriptSha256':sha(pathlib.Path(__file__).read_bytes()),'results':[{'archive':r['archive'],
        'archiveSha256':r['archiveSha256'],'manifest':r['manifest'],'identities':r['identities'],
        'events':r['events'],'detailRowsTotal':r['detailRowsTotal'],'limits':r['limits']} for r in results]}
    (out/'log-audit-index.json').write_text(json.dumps(index,ensure_ascii=False,indent=2),encoding='utf-8')
if __name__=='__main__': main()
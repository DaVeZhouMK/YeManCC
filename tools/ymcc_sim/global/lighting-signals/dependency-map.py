"""Source-derived dependency/route inventory; never load a device runtime."""
import argparse,hashlib,json,pathlib,re,sys
p=argparse.ArgumentParser();p.add_argument('--output',required=True);a=p.parse_args()
repo=pathlib.Path(__file__).resolve().parents[4];sys.path.insert(0,str(repo/'tools'))
from oem_capability_evidence_matrix import parse_hc,effective,strip_comments
main=repo/'native/main.cpp';raw=main.read_text(encoding='utf-8-sig');text=raw
classes=parse_hc(repo/'deps/handheldcompanion-runtime/source/Devices')
def block(start,end):return strip_comments(text[text.index(start):text.index(end,text.index(start)+len(start))])
lighting=block('static const std::vector<std::string>& ymccDeviceClassLightingSet()', 'static const std::vector<std::string>& ymccDeviceClassSecondColorSet()')
models=list(dict.fromkeys(re.findall(r'"(HandheldCompanion\.Devices\.[\w.]+)"',lighting)))
effects_text=block('static std::vector<std::string> ymccDeviceClassLedEffects(', 'static bool ymccFamilySupportsRumble(')
effect_map={}
for m in re.finditer(r'if\s*\((.*?)\)\s*return\s*\{([^}]+)\};',effects_text,re.S):
 for cls in re.findall(r'"(HandheldCompanion\.Devices\.[\w.]+)"',m[1]):effect_map[cls]=re.findall(r'"(\w+)"',m[2])
ops=(repo/'native/oem_lighting_operations.h').read_text(encoding='utf-8-sig')
preset=re.findall(r'cls == "([^"]+)"',ops[ops.index('inline bool presetImplemented'):ops.index('inline bool presetDeclaredButUnavailable')])
no_brightness=re.findall(r'cls == "([^"]+)"',ops[ops.index('inline bool brightnessImplemented'):ops.index('inline bool validPreset')])
rows=[]
for name in models:
 short=name.rsplit('.',1)[-1];details=effective(classes,short) if short in classes else None
 native='rog-aura' if short in ['XboxROGAllyX','XboxROGAlly','ROGAlly','ROGAllyX'] else 'msi-claw-native-hid' if short in ['ClawA1M','ClawA2VM','ClawBZ2EM','ClawCG3EM'] else 'light-setter-hc-adapter'
 effects=[x for x in effect_map.get(name,[]) if x!='preset' or name in preset]
 routes={mode:('independent-device-core' if short=='OneXPlayerX1Mini' and mode in ['solid','rainbow'] else native) for mode in effects}
 line=next((n+1 for n,v in enumerate(raw.splitlines()) if '"'+name+'"' in v and n+1>=raw[:raw.index('static const std::vector<std::string>& ymccDeviceClassLightingSet()')].count('\n')+1),None)
 rows.append({'deviceClass':name,'runtimeSourceType':classes.get(short,{}).get('fullName'),'lightingTable':{'path':str(main),'line':line},'declaredEffects':effect_map.get(name,[]),'admittedEffects':effects,'effectRoutes':routes,'offRoute':native,'brightnessRoute':'independent-device-core' if short=='OneXPlayerX1Mini' else native,'brightnessSupported':name not in no_brightness,'presetImplemented':name in preset,'presetDeclaredButUnavailable':short=='OneXPlayerX1Mini','callbacks':details['callbacks'] if details else None,'sourceHashes':details['sourceFiles'] if details else [],'evidenceKind':'source-derived-fixture','runtimeFactorySelection':'UNKNOWN','physicalEffectVerified':False})
report={'schema':'ymcc.lighting-dependency-map.v1','evidenceKind':'source-derived-fixture','nativeMainSha256':hashlib.sha256(main.read_bytes()).hexdigest(),'modelCount':len(rows),'models':rows,'HCNamedStringsAreLoads':False,'hardwareIo':False,'historicalSender':{'evidence':'Pinned functions/source-lock/SOURCE-NOTICE retain sender lineage; pre-trim successful archived device receipt was not supplied','receiptStatus':'UNKNOWN'}}
path=pathlib.Path(a.output);path.parent.mkdir(parents=True,exist_ok=True);path.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8');print(json.dumps({'models':len(rows),'evidenceKind':'source-derived-fixture','hardwareIo':False}))


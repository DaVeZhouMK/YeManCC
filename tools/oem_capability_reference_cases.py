"""Add source-only capability queries from fixed HC factory and native lighting list.
These cases do not claim hardware identity selection or authorize device operations.
"""
import argparse,hashlib,json,pathlib,re
from oem_capability_evidence_matrix import parse_hc
p=argparse.ArgumentParser();p.add_argument('--hc-devices',required=True);p.add_argument('--native-matrix',required=True);p.add_argument('--main',required=True);p.add_argument('--output',required=True);a=p.parse_args()
hc=pathlib.Path(a.hc_devices);classes=parse_hc(hc);factory=(hc/'IDevice.cs').read_text(encoding='utf-8-sig');main=pathlib.Path(a.main).read_text(encoding='utf-8-sig');matrix=json.loads(pathlib.Path(a.native_matrix).read_text(encoding='utf-8-sig'))
start=factory.index('public static IDevice GetCurrent()');brace=factory.index('{',start);depth=1;i=brace+1
while depth:
 if factory[i]=='{':depth+=1
 elif factory[i]=='}':depth-=1
 i+=1
factoryClasses={x.split('.')[-1] for x in re.findall(r'new\s+([\w.]+)\s*\(',factory[brace:i]) if x.split('.')[-1] in classes}
lit=main[main.index('static const std::vector<std::string>& ymccDeviceClassLightingSet()'):];lit=lit[:lit.index('\n}')+2];lighting=set(re.findall(r'"HandheldCompanion.Devices.(\w+)"',lit));covered={m['deviceClass'].split('.')[-1] for m in matrix['models']}
def family(name):
 for prefix,value in [('AOKZOE','Aokzoe'),('AYANEO','Ayanero'),('GPD','Gpd'),('Loki','Ayn'),('Ayn','Ayn'),('OneX','OneXPlayer')]:
  if name.startswith(prefix):return value
 raise ValueError('New class requires reviewed family routing for capability query: '+name)
extra=[{'name':'HC-capability-only-'+name,'family':family(name),'cls':'HandheldCompanion.Devices.'+name,'caseKind':'reference-capability-query-only','physicalIdentitySelectionVerified':False,'referenceInHCFactory':name in factoryClasses,'inProductionLightingList':name in lighting} for name in sorted((factoryClasses|lighting)-covered-{'DefaultDevice'})]
output=pathlib.Path(a.output);output.parent.mkdir(parents=True,exist_ok=True);output.write_text(json.dumps(extra,indent=2),encoding='utf-8')
receipt={'schema':1,'HCFactoryTypes':sorted(factoryClasses),'factoryDeviceClassCount':len(factoryClasses),'excludedDefaultDevice':True,'nativeLightingClasses':sorted(lighting),'extraCases':len(extra),'identitySelectionVerified':False,'hardwareIo':False,'factorySourceSha256':hashlib.sha256((hc/'IDevice.cs').read_bytes()).hexdigest(),'mainSha256':hashlib.sha256(pathlib.Path(a.main).read_bytes()).hexdigest(),'scope':'prefix-to-family is a reviewed query fixture hint, not observed physical identity or HC factory execution'}
output.with_suffix('.receipt.json').write_text(json.dumps(receipt,indent=2),encoding='utf-8');print(json.dumps({'factoryTypes':len(factoryClasses),'lightingDeclarations':len(lighting),'extraCapabilityQueries':len(extra),'identitySelectionVerified':False,'hardwareIo':False}))

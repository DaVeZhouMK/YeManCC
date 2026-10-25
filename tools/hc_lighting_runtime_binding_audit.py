"""Bind compiled model capabilities to pinned source namespaces and actual DLL metadata.
No inspected assembly is loaded; metadata ownership is not physical readiness.
"""
import argparse,hashlib,json,pathlib
from oem_capability_evidence_matrix import parse_hc,effective
p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--hc-devices',required=True);p.add_argument('--output',required=True);p.add_argument('--metadata');a=p.parse_args()
root=pathlib.Path(a.output);root.mkdir(parents=True,exist_ok=True)
native=json.loads(pathlib.Path(a.native).read_text(encoding='utf-8'));source=parse_hc(pathlib.Path(a.hc_devices));models={m['deviceClass']:m for m in native['models']}
expected={name:source[name.split('.')[-1]]['fullName'] for name in models}
(root/'expected-runtime-type-map.json').write_text(json.dumps(expected,ensure_ascii=False,indent=2),encoding='utf-8')
if a.metadata:
 metadata=json.loads(pathlib.Path(a.metadata).read_text(encoding='utf-8'));lookup={row['deviceClass']:row for row in metadata['classes']};rows=[];issues=[]
 for name,model in sorted(models.items()):
  cls=source[name.split('.')[-1]];hc=effective(source,cls['name']);runtime=lookup.get(name);callbacks=[]
  backend='native-msi-hid' if model['family']=='MsiClaw' else 'native-rog-aura' if model['family'] in ['AsusRogAllyX','AsusRogAlly','AsusRogAllyClassic','AsusRogAllyXClassic'] else 'HC-LightSetter' if model['lightingDeclared'] else 'none'
  errors=[]
  if runtime is None:errors.append('no-runtime-type-metadata')
  elif runtime['runtimeTypeName']!=expected[name]:errors.append('source-runtime-full-name-mismatch')
  for method,argc in [('SetLedColor',4),('SetLedBrightness',1),('SetLedStatus',1),('SetLEDPreset',1)]:
   src=hc['callbacks'].get(method);actual=runtime['callbacks'].get(method) if runtime else None;expected_owner=source[src['class']]['fullName'] if src else None
   owner_match=actual is not None and actual['owner']==expected_owner
   signature_match=actual is not None and actual['parameterCount']==argc and actual['returnsBoolean'] and actual['isPublic']
   callbacks.append({'method':method,'sourceOwner':expected_owner,'runtimeOwner':actual['owner'] if actual else None,'ownerMatches':owner_match,'signatureShapeMatches':signature_match,'constantBooleanBody':actual['constantBooleanBody'] if actual else None})
   if model['lightingDeclared'] and method!='SetLEDPreset' and not(owner_match and signature_match):errors.append(method+'-source-runtime-contract-mismatch')
  row={'deviceClass':name,'runtimeTypeName':expected[name],'normalizedIdentityAlias':name!=expected[name],'lightingDeclared':model['lightingDeclared'],'backendCandidate':backend,'effects':model['ledEffects'],'callbacks':callbacks,'bindingIssues':errors,'hardwareReadyVerified':False,'physicalLightVerified':False}
  rows.append(row)
  if errors:issues.append({'deviceClass':name,'issues':errors})
 report={'schema':1,'HCCommit':'06c0b9544db2b1f39abf9cd3796225ccfb096103','HCAssemblySha256':metadata['assemblySha256'],'classes':rows,'classCount':len(rows),'lightingClassCount':sum(r['lightingDeclared'] for r in rows),'helperLightingClasses':sum(r['lightingDeclared'] and r['backendCandidate']=='HC-LightSetter' for r in rows),'normalizedAliases':sum(r['normalizedIdentityAlias'] for r in rows),'issues':issues,'helperLedLevelsMatchRuntime':metadata['helperLedLevelsMatchRuntime'],'hardwareIo':False,'HCAssemblyLoaded':False,'scope':'source namespace/type hierarchy, compiled native backend candidates, shipped DLL method ownership and signature shape; no factory selection, argument/transport execution or physical effects'}
 (root/'lighting-runtime-source-bindings.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
 print(json.dumps({k:report[k] for k in ['classCount','lightingClassCount','helperLightingClasses','normalizedAliases','helperLedLevelsMatchRuntime','issues']},ensure_ascii=False,indent=2))
else:print(json.dumps({'expectedRuntimeTypes':len(expected),'normalizedAliases':sum(k!=v for k,v in expected.items()),'mapWritten':True,'hardwareIo':False}))

# Read-only ZIP/name audit. No device APIs, processes, settings or repair actions.
import argparse,hashlib,json,pathlib,re,zipfile,collections

def steam_records(text):
 starts=list(re.finditer(r'\[([^\]]+)\]\s*Local Device Found[^\r\n]*',text))
 result=[]
 for index,start in enumerate(starts):
  block=text[start.start():starts[index+1].start()if index+1<len(starts)else len(text)]
  identity=re.search(r'\btype:\s*([0-9a-f]+)\s+([0-9a-f]+)',block,re.I)
  alias=re.search(r'\bpath:\s*(sdl://\d+)',block,re.I);product=re.search(r'(?:^|\n)(?:\[[^\]]+\][ \t]*)?[ \t]*Product:[ \t]*([^\r\n]*)',block)
  result.append({'line':text.count('\n',0,start.start())+1,'time':start[1],'vid':int(identity[1],16)if identity else None,'pid':int(identity[2],16)if identity else None,'alias':alias[1]if alias else None,'product':product[1]if product else None,'productKnown':bool(product),'blockBoundedByNextDiscovery':True})
 return result

def rotated_members(names,base):
 return sorted(n for n in names if n==base or re.fullmatch(re.escape(base)+r'\.(?:[0-9]+|prev)',n))

def units(text):
 if not isinstance(text,str):return None
 raw=text.encode('utf-16-le',errors='surrogatepass');return [int.from_bytes(raw[i:i+2],'little')for i in range(0,len(raw),2)]
def selftest():
 text='[2026-10-12 07:00:00] Local Device Found\n type: 0db0 1902 path: sdl://1\n[2026-10-12 07:00:00]   Product:      ؉\n[2026-10-12 07:00:01] Local Device Found\n type: 0db0 1902 path: sdl://1\n Product: ؉\n[2026-10-12 07:00:02] Local Device Found\n type: 0db0 1902 path: sdl://2\n no product\n[2026-10-12 07:00:03] Local Device Found unknown identity\n Product: unrelated\n'
 assert rotated_members(['virtual-gamepad.log','virtual-gamepad.log.1','virtual-gamepad.log.previous','native-lifecycle.log'],'virtual-gamepad.log')==['virtual-gamepad.log','virtual-gamepad.log.1']
 parsed=steam_records(text);assert len(parsed)==4 and parsed[2]['product']is None and parsed[3]['vid']is None and len({x['alias']for x in parsed[:2]})==1 and units(parsed[0]['product'])==[1545]
 return {'passed':True,'rowsNotSimultaneousDevices':True,'missingProductDoesNotBorrowNextEvenWhenNextIdentityIsUnknown':True,'aliasNotGuessedMI_COL':True}
def main():
 p=argparse.ArgumentParser();p.add_argument('--zip',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True);p.add_argument('--reference',type=pathlib.Path);a=p.parse_args();sha=hashlib.sha256(a.zip.read_bytes()).hexdigest();report={'schemaVersion':1,'scope':'USER_ZIP_BYTES_ONLY_NO_PRODUCT_OR_HARDWARE','zipPath':str(a.zip),'zipSha256':sha,'syntheticParserSelftest':selftest(),'metadata':[]}
 with zipfile.ZipFile(a.zip)as z:
  manifest=z.read('YeManCC-export-manifest.txt').decode('utf-8-sig');kv=dict(line.split('=',1)for line in manifest.splitlines()if '='in line)
  process=int(kv['processId']);native=[]
  nativeFiles=rotated_members(z.namelist(),'native-lifecycle.log')
  for member in nativeFiles:
   for line in z.read(member).decode('utf-8-sig',errors='replace').splitlines():
    try:r=json.loads(line)
    except:continue
    if r.get('pid')==process and r.get('time'):native.append(r)
  end=kv['exportedAtLocal'];boots=[r['time']for r in native if r.get('event')=='boot-single-instance-acquired'and r['time']<=end]
  begin=max(boots)if boots else min(r['time']for r in native)
  report.update(nativePid=process,nativeArtifactSha256=kv.get('nativeSha256'),nativeWindow=[begin,end],nativeWindowBasis='latest-current-PID-boot-or-left-censored-native-through-export',nativeWindowLeftCensored=not bool(boots),nativeFilesRead=nativeFiles,timeZone='Asia/Hong_Kong')
  metadataFiles=rotated_members(z.namelist(),'virtual-gamepad.log');report['metadataFilesRead']=metadataFiles
  metadataRows=[];metadataSeen=set();report['malformedMetadataRows']=0
  for member in metadataFiles:
   for lineNum,line in enumerate(z.read(member).decode('utf-8-sig',errors='replace').splitlines(),1):
    try:r=json.loads(line)
    except:report['malformedMetadataRows']+=1;continue
    if r.get('event')!='msi-dinput-enumeration':continue
    key=json.dumps(r,sort_keys=True,ensure_ascii=False)
    if key in metadataSeen:continue
    metadataSeen.add(key)
    metadataRows.append((member,lineNum,r))
  for member,lineNum,r in metadataRows:
   if r.get('nativePid',r.get('pid'))!=process or not(begin<=r.get('time','')<=end):continue
   candidates=[]
   for c in r.get('candidates',[]):
    di=c.get('productUtf16');hid=c.get('hidProductUtf16');candidates.append({'interfacePath':c.get('interfacePath'),'instanceGuid':c.get('instanceGuid'),'selectedMain':c.get('selectedMain'),'axes':c.get('axes'),'buttons':c.get('buttons'),'usagePage':c.get('hidUsagePage'),'usage':c.get('hidUsage'),'directInputRawUtf16':di,'hidRawUtf16':hid,'productTextMatchesRaw':units(c.get('product'))==di,'hidTextMatchesRaw':units(c.get('hidProductString'))==hid if c.get('hidProductStringKnown')else None,'namesRawEqual':di==hid if isinstance(di,list)and isinstance(hid,list)else None,'metadataHasSeparateApiTimes':c.get('hidProductObservation')is not None})
   report['metadata'].append({'file':member,'line':lineNum,'time':r.get('time'),'timeMeaning':r.get('timeFieldMeaning','log-emission-not-atomic-API-snapshot'),'nativePidKnown':isinstance(r.get('nativePid',r.get('pid')),int),'captureId':r.get('metadataCaptureId'),'coalesced':r.get('coalescedReceipts'),'dropped':r.get('droppedReceipts'),'candidates':candidates})
  steamName='steam/logs/controller.txt';steam=steam_records(z.read(steamName).decode('utf-8-sig',errors='replace'));b=begin.replace('T',' ')[:19];e=end.replace('T',' ')[:19]
  window=[r for r in steam if b<=r['time']<=e and r['vid']==0x0db0];report['steam']={'file':steamName,'windowLocal':[b,e],'groups':[]}
  for pid in sorted({r['pid']for r in window if r['pid']is not None}):
   rs=[r for r in window if r['pid']==pid];report['steam']['groups'].append({'vid':0x0db0,'pid':pid,'discoveryRows':len(rs),'sequentialUniqueAliases':len({r['alias']for r in rs if r['alias']}),'productNames':sorted({r['product']for r in rs if r['productKnown']}),'records':rs,'sameTimeDeviceCount':'not-observed','aliasToMI_COL':'not-observed'})
  report['archiveCRCFirstBadEntry']=z.testzip()
 if a.reference:
  ref=json.loads(a.reference.read_text(encoding='utf-8-sig'));refCandidates=ref.get('metadataReceipt',{}).get('candidates',[]);report['historicalComparison']={'referenceFile':str(a.reference),'referenceZipSha256':ref.get('zipSha256'),'referenceArtifactSha256':ref.get('nativeArtifactSha256'),'referenceNamePairs':list({json.dumps([c.get('directInputRawUtf16'),c.get('hidRawUtf16')])for c in refCandidates}),'currentNamePairs':list({json.dumps([c['directInputRawUtf16'],c['hidRawUtf16']])for r in report['metadata']for c in r['candidates']}),'sameExactCompiledArtifact':ref.get('nativeArtifactSha256','').lower()==report.get('nativeArtifactSha256','').lower(),'canAttributeChangeToFirmwareOrUSB':False}
 report['unknownBoundaries']={'USBDescriptorOrStringIndex':'not-observed-no-request-evidence','languageTableOrDriverCacheCause':'not-proven','SteamInternalNameAPI':'not-observed','SDLToMI_COLMapping':'not-observed','selectedMainFreshInputOrSteamDelivery':'not-proven'}
 a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(json.dumps({'metadataReceipts':len(report['metadata']),'steamGroups':[{k:v for k,v in x.items()if k!='records'}for x in report['steam']['groups']],'historicalComparison':report.get('historicalComparison'),'CRC':report['archiveCRCFirstBadEntry']},ensure_ascii=False,indent=2));return 0
if __name__=='__main__':raise SystemExit(main())

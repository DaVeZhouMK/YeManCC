import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
export const sha = bytes => createHash('sha256').update(bytes).digest('hex');
function endBrace(text, start) {
  let depth=0,mode='code';
  for(let i=start;i<text.length;++i){const c=text[i],n=text[i+1];
    if(mode==='line'){if(c==='\n')mode='code';continue;}
    if(mode==='block'){if(c==='*'&&n==='/'){mode='code';++i;}continue;}
    if(mode==='string'||mode==='char'){if(c==='\\'){++i;continue;}if(c===(mode==='string'?'"':"'"))mode='code';continue;}
    if(c==='/'&&n==='/'){mode='line';++i;continue;}if(c==='/'&&n==='*'){mode='block';++i;continue;}
    if(c==='"'){mode='string';continue;}if(c==="'"){mode='char';continue;}
    if(c==='{')++depth;if(c==='}'&&!--depth)return i+1;
  }throw Error('Unbalanced source braces');
}
export function extractSources(mainPath) {
  const bytes=fs.readFileSync(mainPath),text=bytes.toString('utf8').replace(/\r\n/g,'\n'),fragments=[];
  const record=(name,start,end,execution)=>{const body=text.slice(start,end);fragments.push({name,startLine:text.slice(0,start).split('\n').length,sha256:sha(body),execution,body});return body;};
  const fn=(name,execution='complete-function-executed')=>{
    const pattern=new RegExp('^static[^\\n]*\\b'+name+'\\s*\\(','gm');let m;
    while((m=pattern.exec(text))){const open=text.indexOf('{',m.index),semicolon=text.indexOf(';',m.index);if(semicolon>=0&&semicolon<open)continue;return record(name,m.index,endBrace(text,open),execution);}
    throw Error('Function unavailable: '+name);
  };
  const type=name=>{const pattern=new RegExp('^(?:struct|enum class) '+name+'\\b[^\\n]*','m'),m=pattern.exec(text);if(!m)throw Error('Type unavailable: '+name);const end=endBrace(text,text.indexOf('{',m.index));return record(name,m.index,text.indexOf(';',end)+1,'actual-type');};
  const decl=name=>{const pattern=new RegExp('^static[^\\n]*\\b'+name+'\\b[^\\n]*','m'),m=pattern.exec(text);if(!m)throw Error('Declaration unavailable: '+name);return record(name,m.index,text.indexOf(';',m.index)+1,'actual-declaration');};
  const base=[decl('kLegionBackPids'),decl('kLegionSPids'),fn('legionBackIsPid'),fn('legionSBackIsPid'),fn('inputCaptureLegionControllerMotionRoute'),type('HidHidePhysicalIdentity'),
    fn('legionMotionHardwareAdmitted'),fn('legionMotionCollectionAdmitted'),fn('hidHideUpper'),fn('hidHideDeviceInstanceId'),type('HidBaseWalk'),fn('hidHideNearestBaseInstance'),fn('hidHideIsVirtualIdentity'),type('HidResolveReject'),fn('hidHideResolveInterfaceIdentity')].join('\n\n');
  const cache=[type('HidResolveKind'),type('HidResolveCacheEntry'),...['g_hidResolveCacheMx','g_hidResolveCache','g_legionMotionResolveCache','g_hidResolveCacheGeneration','kHidResolveStableTtlMs','kHidResolveTransientTtlMs','kHidResolveAbsentTtlMs','kHidResolveRouteRefreshMs','kHidResolveCacheMaxEntries','g_hidResolveDiagMx','g_hidResolveDiagKeys'].map(decl),fn('hidHideResolvedGenerationStillCurrent'),fn('hidResolveTtlForKind'),fn('hidHideInvalidateRawInputIdentityCache'),fn('hidHideEvictOldestResolveEntryLocked')].join('\n\n');
  const raw=fn('hidHideResolveRawInputIdentity','partial-prefix-and-fill-executed;full-miss-enumeration-source-only');
  const miss=raw.indexOf('// ── 未命中');if(miss<0)throw Error('Raw cache prefix seam unavailable');
  const prefix=raw.slice(0,miss)+'// Fixture boundary: full miss enumeration NOT executed.\n    return false;\n}\n';
  const fillStart=raw.indexOf('    bool generationStable = false;'),fillEnd=raw.indexOf('    gpResolveCost.noteFillKind',fillStart);
  const cacheSelect=raw.match(/    auto& cache=allowLegionMotionCollection[^\n]+/)[0];
  if(fillStart<0||fillEnd<0)throw Error('Raw fill block seam unavailable');
  const fill=`static bool executeProductionCacheFill(bool allowLegionMotionCollection,std::uintptr_t cacheKey,HidResolveKind kind,const HidHidePhysicalIdentity& resolved,const std::string& wanted,ULONGLONG now,std::uint64_t genAtStart) {\n${cacheSelect}\n    const bool physical=kind==HidResolveKind::Physical;\n    const std::string route=physical?resolved.baseContainerDevicePath:std::string{};\n${raw.slice(fillStart,fillEnd)}\n    return generationStable;\n}\n`;
  const enumeration=fn('hidHideResolvePhysicalIdentities');
  const observe=fn('hidHideObserveRawInputDevice');
  const header=base+'\n\n'+cache+'\n\n'+prefix+'\n'+fill+'\n'+enumeration+'\nstatic std::mutex g_hidHidePhysicalIdentityMx;\nstatic HidHidePhysicalIdentity g_hidHidePhysicalIdentity;\n'+observe+'\n';
  return {mainPath,mainSha256AtRead:sha(bytes),fragments,header,raw,prefix,fill,scope:{fullInterface:true,fullAncestorWalk:true,fullEvictAndInvalidate:true,rawCacheHitPrefix:true,rawFillBlock:true,fullRawMissEnumeration:false,fullRawResolver:false,
    defaultCandidateEnumeration:true,defaultSelectedObserve:true,realHideTransaction:false,win32Boundaries:'memory stubs only',performanceAndLogging:'effect sinks, not production telemetry'}};
}

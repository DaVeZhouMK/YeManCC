// Pure reader of the existing Lossless Scaling list; never creates another store.
export interface LsProfile { title:string;path:string;enabled:boolean;start:number;end:number;block:string; }
export const normalizeLsPath=(value:string)=>value.trim().replace(/\//g,'\\').toLowerCase();
export const lsExeKey=(value:string)=>normalizeLsPath(value).split('\\').slice(-1)[0]||'';
function unescapeXml(value:string):string {
  return value.replace(/&#x([\da-f]+);|&#(\d+);|&(quot|apos|lt|gt|amp);/gi,(_,hex,dec,name)=>hex||dec
    ? String.fromCodePoint(parseInt(hex||dec,hex?16:10)) : ({quot:'"',apos:"'",lt:'<',gt:'>',amp:'&'} as Record<string,string>)[name.toLowerCase()]);
}
export function parseLsProfiles(xml:string):LsProfile[] {
  if(xml.length>2*1024*1024||/<!DOCTYPE|<!ENTITY/i.test(xml)||!/<Settings\b/.test(xml)||!/<\/Settings>\s*$/.test(xml))throw new Error('小黄鸭 Settings.xml 格式异常，未修改');
  if(typeof DOMParser!=='undefined'&&new DOMParser().parseFromString(xml,'application/xml').querySelector('parsererror'))throw new Error('小黄鸭 Settings.xml 无效，未修改');
  const list=/<GameProfiles\b[^>]*>([\s\S]*?)<\/GameProfiles>/.exec(xml);
  if(!list)throw new Error('小黄鸭 EXE 列表不存在，未修改');
  const offset=list.index+list[0].indexOf(list[1]),matches=[...list[1].matchAll(/<Profile\b[^>]*>[\s\S]*?<\/Profile>/g)];
  if(matches.length!==(list[1].match(/<Profile\b/g)||[]).length||matches.length!==(list[1].match(/<\/Profile>/g)||[]).length)throw new Error('小黄鸭 Profile 列表不完整，未修改');
  return matches.map(match=>{
    const block=match[0],path=/<Path>([\s\S]*?)<\/Path>/.exec(block)?.[1]||'',auto=[...block.matchAll(/<AutoScale>\s*(true|false)\s*<\/AutoScale>/g)];
    if(auto.length!==1)throw new Error('小黄鸭 AutoScale 字段不明确，未修改');
    return {title:unescapeXml(/<Title>([\s\S]*?)<\/Title>/.exec(block)?.[1]||''),path:unescapeXml(path),enabled:auto[0][1]==='true',start:offset+match.index!,end:offset+match.index!+block.length,block};
  });
}
export function matchLsProfile(profiles:LsProfile[],gamePath:string):LsProfile|null {
  const named=profiles.filter(profile=>profile.path&&/\.exe$/i.test(profile.path.trim()));
  const exact=named.filter(profile=>normalizeLsPath(profile.path)===normalizeLsPath(gamePath));
  if(exact.length>1)throw new Error('小黄鸭列表存在重复 EXE 路径，未选择方案');
  if(exact.length===1)return exact[0];
  const sameExe=named.filter(profile=>lsExeKey(profile.path)===lsExeKey(gamePath));
  if(sameExe.length>1)throw new Error('小黄鸭列表存在同名 EXE，须补齐准确路径');
  return sameExe[0]||null;
}

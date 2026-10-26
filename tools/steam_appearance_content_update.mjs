// Rebuild exact-content markers and manifest bytes after a reviewed CSS change.
// Explicit --write is required for mutations; default mode verifies every asset.
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';
const root=path.resolve(import.meta.dirname,'..'),pack=path.join(root,'PowerControl/steam-appearance');
const argv=process.argv.slice(2);if(argv.some(x=>!['--write'].includes(x)))throw Error('Only --write is accepted');const write=argv.includes('--write');
const manifestFile=path.join(pack,'manifest.json'),manifest=JSON.parse(fs.readFileSync(manifestFile,'utf8'));
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');const tail=/\/\* YMCC content receipt: exact bundled file revision\. \*\/\n:root \{ --ymcc-content-[0-9a-f]{16}: 1; \}\n$/;
const seen=new Set(),updates=[];let cssCount=0;
for(const item of manifest.files){
 if(typeof item.path!=='string'||item.path.includes('\\')||item.path.split('/').some(x=>!x||x==='.'||x==='..')||path.isAbsolute(item.path)||seen.has(item.path))throw Error('Invalid/repeated asset path');seen.add(item.path);
 const file=path.join(pack,'themes',item.path),bytes=fs.readFileSync(file);let after=bytes;
 if(item.path.endsWith('.css')){
  ++cssCount;const current=bytes.toString('utf8');if(current.charCodeAt(0)===0xfeff)throw Error('CSS must be UTF8 without BOM: '+item.path);
  let body=current.replace(tail,'');if(/--ymcc-content-[a-f0-9]{16}\s*:/.test(body))throw Error('Unexpected repeated content marker: '+item.path);if(!body.endsWith('\n'))body+='\n';
  const property='--ymcc-content-'+hash(Buffer.from(body)).slice(0,16);after=Buffer.from(body+'/* YMCC content receipt: exact bundled file revision. */\n:root { '+property+': 1; }\n');
  if(item.contentProperty!==property||!bytes.equals(after)){if(!write)throw Error('Stale content marker: '+item.path);item.contentProperty=property;updates.push(item.path);fs.writeFileSync(file,after);}
 }
 const digest=hash(after);if(item.sha256!==digest||item.bytes!==after.length){if(!write)throw Error('Stale asset hash/size: '+item.path);Object.assign(item,{sha256:digest,bytes:after.length});if(!updates.includes(item.path))updates.push(item.path)}
}
if(cssCount!==43)throw Error('Expected exactly 43 bundled CSS assets');if(manifest.renderContentProtocol!==1){if(!write)throw Error('Missing content protocol');manifest.renderContentProtocol=1;}
if(write)fs.writeFileSync(manifestFile,JSON.stringify(manifest,null,2)+'\n');console.log(JSON.stringify({ok:true,cssFiles:cssCount,assets:seen.size,mode:write?'write':'verify',updated:updates},null,2));

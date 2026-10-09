// Cached official SDK is now vendored in the mainline. No package-manager install or runtime filesystem IO.
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';
const root=path.resolve(import.meta.dirname,'..'),sdk=path.join(root,'decky-plugin/vendor/decky-api-1.1.3');
export function deckyApiPlugins(){
 const pin=JSON.parse(fs.readFileSync(path.join(root,'decky-plugin/vendor/DECKY-API-LOCK.json'),'utf8').replace(/^\uFEFF/,''));
 if(pin.name!=='@decky/api'||pin.version!=='1.1.3'||pin.archiveSha256!=='273227cd80104fd5a9307328b798c299abaa174111e36808128e616fdabeb807')throw Error('Pinned public SDK metadata mismatch');
 for(const item of pin.files){const content=fs.readFileSync(path.join(sdk,item.relative));if(content.length!==item.bytes||crypto.createHash('sha256').update(content).digest('hex')!==item.sha256)throw Error('Vendored SDK source/license drift');}
 const metadata=JSON.parse(fs.readFileSync(path.join(sdk,'package.json'),'utf8'));
 if(metadata.name!=='@decky/api'||metadata.version!=='1.1.3')throw Error('Unexpected vendored SDK package');
 const manifest=JSON.parse(fs.readFileSync(path.join(root,'decky-plugin/plugin.json'),'utf8'));
 if(manifest.name!=='ymcc-sidebar')throw Error('Unexpected loader plugin name');
 return [{name:'ymcc-pinned-decky-api',setup(build){
  build.onResolve({filter:/^@decky\/api$/},()=>({path:path.join(sdk,'dist/index.js')}));
  build.onResolve({filter:/^@decky\/manifest$/},()=>({path:'ymcc-manifest',namespace:'ymcc-build-manifest'}));
  build.onLoad({filter:/.*/,namespace:'ymcc-build-manifest'},()=>({contents:JSON.stringify({name:manifest.name}),loader:'json'}));
 }}];
}

/** Build the optional gyro 3D runtime against a verified local Three.js npm package. */
import fs from 'node:fs';import path from 'node:path';import {createRequire}from'node:module';
const root=path.resolve(import.meta.dirname,'..'),req=createRequire(path.join(root,'package.json')),{build}=req('esbuild');
const three=process.argv[2];if(!three||JSON.parse(fs.readFileSync(path.join(three,'package.json'))).version!=='0.186.1')throw Error('Expected Three.js 0.186.1 package');
const out=path.join(root,'public/gyro-simulation');fs.mkdirSync(out,{recursive:true});
await build({entryPoints:[path.join(root,'tools/gyro-runtime/range3d.js')],outfile:path.join(out,'range3d.js'),bundle:true,minify:true,format:'esm',target:'es2020',alias:{'three/addons':path.resolve(three,'examples/jsm'),three:path.resolve(three,'build/three.module.js')},legalComments:'eof',banner:{js:'/*! Three.js 0.186.1 — MIT License. See three-LICENSE.txt. YMCC range renderer; no Kitbash engine source included. */'}});
fs.copyFileSync(path.join(three,'LICENSE'),path.join(out,'three-LICENSE.txt'));
const files=fs.readdirSync(out,{recursive:true}).filter(f=>f!=='size-manifest.json'&&fs.statSync(path.join(out,f)).isFile()).map(f=>({file:f,bytes:fs.statSync(path.join(out,f)).size})),bytes=files.reduce((s,f)=>s+f.bytes,0);if(bytes>2500000)throw Error('Optional 3D size budget exceeded: '+bytes);fs.writeFileSync(path.join(out,'size-manifest.json'),JSON.stringify({budget:2500000,bytes,files},null,2));console.log('Optional 3D runtime + assets: '+bytes+' bytes');

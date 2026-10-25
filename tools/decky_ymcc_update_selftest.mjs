// Execute the shipped row selector and API admission helper, without any backend installation.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {createRequire} from 'node:module';
const root=path.resolve(import.meta.dirname,'..'),req=createRequire(path.join(root,'package.json')),{build}=req('esbuild'),cases=[];
const result=await build({entryPoints:[path.join(root,'decky-plugin/src/deckyLoaderUpdate.tsx')],bundle:true,write:false,metafile:true,platform:'node',format:'esm',jsxFactory:'SP_REACT.createElement',jsxFragment:'SP_REACT.Fragment',charset:'utf8'});
globalThis.SP_REACT={createElement:(type,props,...children)=>({type,props,children})};
const {LoaderUpdateRow,YmccPackageUpdateRow}=await import('data:text/javascript;base64,'+Buffer.from(result.outputFiles[0].contents).toString('base64'));
const helper=await build({entryPoints:[path.join(root,'decky-plugin/src/deckyLoaderConsumer.ts')],bundle:true,write:false,platform:'node',format:'esm'}),{supportsOfficialUpdaterConsumer:accept}=await import('data:text/javascript;base64,'+Buffer.from(helper.outputFiles[0].contents).toString('base64'));
const check=(name,fn)=>{fn();cases.push(name);};
check('actual product row always selects the single YMCC package updater',()=>{const client={};const value=LoaderUpdateRow({client,host:{navigator:{platform:'Win32'}}});assert.equal(value.type,YmccPackageUpdateRow);assert.equal(value.props.client,client);});
for(const platform of ['Linux x86_64','Linux aarch64',''])check('platform '+platform+' cannot select an independent Decky controller',()=>assert.equal(LoaderUpdateRow({host:{navigator:{platform}}}).type,YmccPackageUpdateRow));
check('official pinned version is reviewed by consumer API adapter',()=>assert(accept(JSON.parse(fs.readFileSync(path.join(root,'decky-plugin/vendor/decky-loader.lock.json'),'utf8')).version)));
check('normal stable 3.x consumer updates do not require a new literal',()=>{for(const v of ['v3.3.0','3.4.0','v3.99.0'])assert(accept(v));});
check('unreviewed major, prerelease, malformed and old APIs are rejected',()=>{for(const v of ['v4.0.0','v3.3.0-pre1','v3.2.10','v03.3.0',null,{},'x'.repeat(100)])assert(!accept(v));});
check('production row bundle contains no independent upstream update route or release fetch',()=>{const js=result.outputFiles[0].text;for(const text of ['updater/do_update','updater/check_for_updates','api.github.com','NativeDeckyUpdater'])assert(!js.includes(text));});
check('production row consumes the existing YMCC authenticated update client',()=>{assert(Object.keys(result.metafile.inputs).some(p=>p.endsWith('deckyUpdaterClient.ts')));assert(result.outputFiles[0].text.includes('Decky 随 YMCC 一起更新'));});
const out=path.resolve(process.argv[2]||process.env.YMCC_DECKY_ARTIFACT_ROOT||'work/decky-ymcc-only');fs.mkdirSync(out,{recursive:true});fs.writeFileSync(path.join(out,'YMCC-ONLY-UPDATE.json'),JSON.stringify({passed:true,cases:cases.length,results:cases,installations:0,realSteam:false},null,2));console.log('YMCC-only Decky update tests passed: '+cases.length);

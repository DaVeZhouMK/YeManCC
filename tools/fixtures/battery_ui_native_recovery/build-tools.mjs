import fs from 'node:fs';
import path from 'node:path';
import {createRequire} from 'node:module';
import {createHash} from 'node:crypto';
import {resolveSnapshotNodeTools,validateBoundNodeDeclaration} from './snapshot-node-binding.mjs';
const need=(value,code)=>{if(!value)throw Error(code);};
const hash=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex');
export function loadUiBuildTools(cfg){
 need(hash(cfg.artifactSnapshot)===cfg.artifactSnapshotSha256,'BATTERY_UI_NODE_ARTIFACT_SNAPSHOT_CHANGED');
 const rows=JSON.parse(fs.readFileSync(cfg.artifactSnapshot,'utf8')).files;
 const pins=new Map(rows.map(row=>[path.resolve(row.path).toLowerCase(),row.sha256]));
 const pinned=p=>{const file=fs.realpathSync(p);need(pins.get(path.resolve(file).toLowerCase())===hash(file),'BATTERY_UI_NODE_ENTRY_UNDECLARED:'+file);return file;};
 if(cfg.nodeToolBinding){
  const authority=resolveSnapshotNodeTools(cfg.nodeToolBinding.snapshotRoot,cfg.nodeToolBinding);validateBoundNodeDeclaration(rows,authority);
  need(cfg.packageRepo===authority.packageRepo&&cfg.nodeModulesPath===authority.nodeModulesPath&&cfg.esbuildBinary===authority.esbuildBinary,'BATTERY_UI_NODE_BUILD_CONFIG_CHANGED');
  process.env.ESBUILD_BINARY_PATH=pinned(authority.esbuildBinary);
 }
 const req=createRequire(path.join(cfg.packageRepo,'package.json')),runtimeReq=createRequire(path.join(cfg.browserModuleRoot,'_battery-ui-consumer.cjs'));
 const entries=['esbuild','vue/compiler-sfc','vue'].map(name=>({name,path:pinned(req.resolve(name))}));
 entries.push({name:'playwright',path:pinned(runtimeReq.resolve('playwright'))});
 const {build}=req('esbuild'),{parse,compileScript,compileStyle}=req('vue/compiler-sfc'),{chromium}=runtimeReq('playwright');
 return {req,runtimeReq,build,parse,compileScript,compileStyle,chromium,entries,nodePaths:[cfg.nodeModulesPath??path.join(cfg.packageRepo,'node_modules')]};
}

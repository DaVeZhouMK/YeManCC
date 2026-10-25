import fs from 'node:fs';
import path from 'node:path';
import { sha256, readJson, inside } from './common.mjs';
import {createRequire} from 'node:module';
export const config = readJson(process.env.YMCC_MONITOR_AUDIT_CONFIG);
export const trace=[];
const loaded=new Map();
const virtual=[];
const ts=createRequire(path.join(config.repoRoot,'package.json'))('typescript');
const injectedImports=new Set(['./api','./ipc','./monitorAppliedTdp','./monitorData','./monitorGameLogo','./monitorHardwareMetadata','./monitorNoticeHost','./gamePolicyTarget',
 './monitorHudClock','./steamHudRelay','./desktopHudOwner','./monitorHudCarrierRouter','./monitorOutput','./deckyMirrorExtensions','./monitorHudSampleSource']);
function checkImports(text,file,root,pins){
 const tree=ts.createSourceFile(file,text,ts.ScriptTarget.Latest,true,ts.ScriptKind.TS);
 for(const node of tree.statements){
  if(!ts.isImportDeclaration(node)&&!ts.isExportDeclaration(node))continue;
  if(node.isTypeOnly||node.importClause?.isTypeOnly)continue;
  const bindings=node.importClause?.namedBindings;
  if(bindings&&ts.isNamedImports(bindings)&&!node.importClause?.name&&bindings.elements.length&&bindings.elements.every(item=>item.isTypeOnly))continue;
  const spec=node.moduleSpecifier?.text;if(!spec)continue;
  if(!spec.startsWith('.'))throw Error('UNREVIEWED_EXTERNAL_PRODUCT_IMPORT:'+spec);
  if(injectedImports.has(spec))continue;
  const candidate=path.resolve(path.dirname(file),spec.endsWith('.ts')?spec:spec+'.ts');
  const relative=path.relative(root,candidate).replaceAll('\\','/');
  if(!pins[relative])throw Error('UNREVIEWED_PRODUCT_IMPORT_EDGE:'+relative);
 }
 function walk(node){if(ts.isCallExpression(node)&&(node.expression.kind===ts.SyntaxKind.ImportKeyword||node.expression.getText(tree)==='require'))throw Error('DYNAMIC_PRODUCT_IMPORT_DENIED');ts.forEachChild(node,walk);}
 walk(tree);
}

export function recordSource(file){
 const normalized=path.resolve(file),real=fs.realpathSync(normalized);
 let kind,relative,root;
 if(inside(config.repoRoot,real)){kind='current-product-source';root=config.repoRoot;}
 else if(config.beforeRoot&&inside(config.beforeRoot,real)){kind='retained-source-before-clock-demand';root=config.beforeRoot;}
 else throw new Error('UNREVIEWED_SOURCE_ROOT:'+file);
 relative=path.relative(root,real).replaceAll('\\','/');
 const expected=(kind==='current-product-source'?config.currentHashes:config.beforeHashes)[relative];
 if(!expected)throw new Error('UNREVIEWED_PRODUCT_IMPORT:'+relative);
 const bytes=fs.readFileSync(real),actual=sha256(bytes);
 if(actual!==expected)throw new Error('MONITOR_SOURCE_DRIFT:'+relative);
 checkImports(bytes.toString('utf8').replace(/^\uFEFF/,''),real,root,kind==='current-product-source'?config.currentHashes:config.beforeHashes);
 loaded.set(kind+':'+relative,{path:kind==='current-product-source'?relative:real,relative,kind,sha256:actual,loadedFrom:real});
 return bytes.toString('utf8').replace(/^\uFEFF/,'');
}
export const sourcePlugin={name:'monitor-reviewed-source-only',setup(build){build.onLoad({filter:/\.ts$/},args=>({contents:recordSource(args.path),loader:'ts',resolveDir:path.dirname(args.path)}));}};
export function recordPort(name,meaning){virtual.push({name,meaning});}
export function snapshotAudit(){return {schema:'ymcc.monitor-source-execution.v1',loadedProductSources:[...loaded.values()],sourceImportBoundary:'runtime imports restricted to reviewed TS sources or explicitly injected ports; external/dynamic product imports rejected',injectedPorts:virtual,
 inputConsumerStateFeedback:trace,sourceKind:'source-derived-fixture',hardwareIo:false,nativeAppStarted:false,hostStarted:false};}
export function writeAudit(){
 const audit=snapshotAudit();
 for(const file of audit.loadedProductSources){if(sha256(fs.readFileSync(file.loadedFrom))!==file.sha256)throw new Error('MONITOR_SOURCE_DRIFT:'+file.relative);}
 fs.writeFileSync(path.join(config.caseDir,'source-execution.json'),JSON.stringify(audit,null,2));return audit;
}

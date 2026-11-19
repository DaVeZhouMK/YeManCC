// Static ESM module identity only. Never imports a selected generator or invokes a compiler.
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {createRequire,isBuiltin} from 'node:module';
import {fileURLToPath} from 'node:url';
import {isDeepStrictEqual} from 'node:util';

export const GENERATOR_MODULE_CLOSURE_KIND='TYPESCRIPT_AST_GENERATOR_MODULE_CLOSURE_V1';
const SELF=fileURLToPath(import.meta.url),HOST_REPO=path.resolve(import.meta.dirname,'../../../..');
const PARSER_PATH='G:/YeManCC-Work/Testing/YMCC/Workspace/NodeTools/node_modules/typescript/lib/typescript.js';
const PARSER_SHA='3ae902c92cc44dace175c0e69e13a4b0899f6983c6121d76b9ab8dd5795e7675';
const PARSER_PACKAGE_SHA='822ef7ca6452205657b6288b066481ecf508bfbf43455d715cf7d3ec457561e6';
const live=new WeakMap(),digest=b=>createHash('sha256').update(b).digest('hex');
const need=(value,code)=>{if(!value)throw Error('GENERATOR_MODULE_CLOSURE_'+code);};
const key=p=>path.resolve(p).toLowerCase(),inside=(root,p)=>key(root)===key(p)||key(p).startsWith(key(root)+path.sep);
const plain=x=>x!==null&&typeof x==='object'&&!Array.isArray(x)&&Object.getPrototypeOf(x)===Object.prototype;
function exactKeys(x,allowed){need(plain(x)&&Object.keys(x).every(k=>allowed.includes(k)),'UNKNOWN_SELECTOR_OR_OPTIONS');}
function ordinary(file,directory=false){
 need(typeof file==='string'&&/^[a-z]:[\\/]/i.test(file)&&!file.slice(2).includes(':')&&!/[\x00-\x1f]/.test(file),'ORDINARY_ABSOLUTE_PATH');
 need(file.split(/[\\/]/).slice(1).every(x=>x&&x!=='.'&&x!=='..'&&!/[ .]$/.test(x)),'PATH_ALIAS');
 const full=path.resolve(file);for(let p=full;;p=path.dirname(p)){const st=fs.lstatSync(p);need(!st.isSymbolicLink()&&key(fs.realpathSync.native(p))===key(p),'REDIRECTED_PATH:'+p);if(p===full)need(directory?st.isDirectory():st.isFile(),'ORDINARY_FILE_REQUIRED:'+p);else need(st.isDirectory(),'PARENT_DIRECTORY_REQUIRED');if(path.dirname(p)===p)break;}return full;
}
function readStable(file){
 ordinary(file);const fd=fs.openSync(file,'r');try{const before=fs.fstatSync(fd,{bigint:true});need(before.isFile()&&before.size<=BigInt(Number.MAX_SAFE_INTEGER),'FILE_SIZE');const bytes=fs.readFileSync(fd),after=fs.fstatSync(fd,{bigint:true});for(const k of ['dev','ino','size','mtimeNs','ctimeNs'])need(before[k]===after[k],'CHANGED_DURING_READ:'+file);const named=fs.lstatSync(file,{bigint:true});need(named.ino===after.ino&&named.size===after.size&&named.mtimeNs===after.mtimeNs,'REPLACED_DURING_READ:'+file);ordinary(file);return {pin:{path:path.resolve(file),bytes:bytes.length,sha256:digest(bytes)},bytes};}finally{fs.closeSync(fd);}
}
const initialSelf=readStable(SELF).pin;
let parserState;
function parser(io){
 const anchor=path.join(HOST_REPO,'package.json'),req=createRequire(anchor);io.read(anchor,'parser-anchor');const resolved=req.resolve('typescript');need(key(resolved)===key(PARSER_PATH),'PARSER_RESOLUTION');
 const p=io.read(resolved,'parser'),pkgPath=path.resolve(path.dirname(resolved),'../package.json'),pkg=io.read(pkgPath,'parser-package');need(p.pin.sha256===PARSER_SHA&&pkg.pin.sha256===PARSER_PACKAGE_SHA,'EXACT_TYPESCRIPT593_PIN');
 const metadata=JSON.parse(pkg.bytes.toString('utf8'));need(metadata.version==='5.9.3','PARSER_PACKAGE_VERSION');
 if(!parserState){
  // Previously loaded third-party code has no byte observation from this reader.
  need(!req.cache[resolved],'UNATTESTED_PARSER_REQUIRE_CACHE');
  const before=new Set(Object.keys(req.cache)),ts=req(resolved),added=Object.keys(req.cache).filter(k=>!before.has(k));need(ts.version==='5.9.3'&&added.length===1&&key(added[0])===key(resolved),'PARSER_RUNTIME_MODULE_GRAPH');parserState={ts,path:resolved};
 }
 return parserState.ts;
}
function selection(options){
 exactKeys(options,['repoRoot','entries','additionalFiles']);need(typeof options.repoRoot==='string'&&path.isAbsolute(options.repoRoot),'REPO_ROOT');const repoRoot=ordinary(path.resolve(options.repoRoot),true);
 const choose=(list,entry)=>{need(Array.isArray(list)&&(!entry||list.length>0),'ENTRY_OR_ADDITIONAL_SELECTOR');const found=new Set();return list.map(name=>{need(typeof name==='string'&&name.length>0&&!/[?#\x00-\x1f]/.test(name),'FILE_SELECTOR');const file=path.resolve(repoRoot,name);need(inside(repoRoot,file),'SELECTOR_OUTSIDE_REPO');need(['.mjs','.js',...(entry?[]:['.json'])].includes(path.extname(file)),'SELECTOR_EXTENSION');need(!found.has(key(file)),'DUPLICATE_SELECTOR');found.add(key(file));return ordinary(file);}).sort((a,b)=>key(a).localeCompare(key(b)));};
 const entries=choose(options.entries,true),additionalFiles=choose(options.additionalFiles??[],false);need(!additionalFiles.some(p=>entries.some(e=>key(p)===key(e))),'CONFLICTING_SELECTOR_ROLES');return {repoRoot,entries,additionalFiles};
}
const CODE_EXECUTORS=new Set(['eval','Function','AsyncFunction','GeneratorFunction','constructor','require','requireResolve','importScripts','Worker','SharedWorker','createRequire','Module','getBuiltinModule','dlopen','_linkedBinding','registerHooks','_compile','_load','fork','spawn','spawnSync','execSync','execFile','execFileSync','runInContext','runInNewContext','runInThisContext','compileFunction','SourceTextModule','SyntheticModule']);
const HAZARD_BUILTINS=new Set(['node:module','node:worker_threads','node:child_process','node:vm']);
function build(options){
 need(!process.env.NODE_OPTIONS&&!process.env.NODE_PATH&&process.execArgv.length===0,'NODE_PRELOAD_OR_FLAGS');
 const selected=selection(options),seen=new Map(),purpose=new Map();
 const io={read(file,role){const k=key(file);let item=seen.get(k);if(!item){item=readStable(file);seen.set(k,item);}if(!purpose.has(role))purpose.set(role,new Set());purpose.get(role).add(k);return item;}};
 const self=io.read(SELF,'reader');need(self.pin.sha256===initialSelf.sha256,'LOADED_READER_SOURCE_DRIFT');const ts=parser(io),node=io.read(process.execPath,'node').pin;
 const modules=new Map(),data=new Map(),edges=[],builtins=[],unresolved=[],pending=[...selected.entries,...selected.additionalFiles];
 const report=(parent,node,reason,detail,text)=>{const pos=node?.getStart?.()??0;unresolved.push({parent,reason,detail,offset:Buffer.byteLength((text??'').slice(0,pos),'utf8'),utf16Offset:pos});};
 while(pending.length){
  const file=pending.pop(),k=key(file);if(modules.has(k)||data.has(k))continue;const input=io.read(file,'generator-input'),text=input.bytes.toString('utf8');need(Buffer.from(text,'utf8').equals(input.bytes),'INVALID_UTF8:'+file);
  if(path.extname(file)==='.json'){JSON.parse(text.replace(/^\uFEFF/,''));data.set(k,input.pin);continue;}
  modules.set(k,input.pin);const source=ts.createSourceFile(file,text,ts.ScriptTarget.Latest,true,ts.ScriptKind.JS);need(source.parseDiagnostics.length===0,'SYNTAX_ERROR:'+file+':'+source.parseDiagnostics.map(d=>ts.flattenDiagnosticMessageText(d.messageText,' ')).join('|'));
  // TS positions are UTF-16; external offsets are actual UTF-8 byte positions.
  const names=new Map();
  function propertyName(node){return node&&((ts.isIdentifier(node)||ts.isStringLiteral(node)||ts.isNoSubstitutionTemplateLiteral(node))?node.text:undefined);}
  function attrs(node,dynamic){
   if(!node)return false;
   if(!dynamic){const entries=node.elements??[];return entries.length===1&&propertyName(entries[0].name)==='type'&&ts.isStringLiteral(entries[0].value)&&entries[0].value.text==='json'&&node.token===ts.SyntaxKind.WithKeyword;}
   if(!ts.isObjectLiteralExpression(node)||node.properties.length!==1)return false;const p=node.properties[0];if(!ts.isPropertyAssignment(p)||propertyName(p.name)!=='with'||!ts.isObjectLiteralExpression(p.initializer)||p.initializer.properties.length!==1)return false;const q=p.initializer.properties[0];return ts.isPropertyAssignment(q)&&propertyName(q.name)==='type'&&ts.isStringLiteral(q.initializer)&&q.initializer.text==='json';
  }
  function edge(specNode,owner,kind,attributeNode,dynamic=false){
   if(!specNode||!ts.isStringLiteral(specNode)){report(file,owner,'NON_LITERAL_IMPORT',kind,text);return;}
   const specifier=specNode.text,pos=owner.getStart(source),row={parent:file,specifier,target:null,kind,offset:Buffer.byteLength(text.slice(0,pos),'utf8'),utf16Offset:pos};
   if(specifier.startsWith('node:')){need(!attributeNode,'BUILTIN_IMPORT_ATTRIBUTES');if(!isBuiltin(specifier)){unresolved.push({...row,reason:'UNKNOWN_BUILTIN'});return;}const resolved={...row,target:specifier};builtins.push(resolved);edges.push(resolved);if(HAZARD_BUILTINS.has(specifier))report(file,owner,'OPAQUE_BUILTIN_CAPABILITY',specifier,text);return;}
   if(!specifier.startsWith('./')&&!specifier.startsWith('../')){unresolved.push({...row,reason:'PACKAGE_OR_NON_RELATIVE_IMPORT'});return;}
   if(/[%?#\x00-\x1f\\]/.test(specifier)){unresolved.push({...row,reason:'UNSUPPORTED_MODULE_URL'});return;}
   const target=path.resolve(path.dirname(file),specifier);need(inside(selected.repoRoot,target),'IMPORT_OUTSIDE_REPO:'+specifier);const ext=path.extname(target);
   if(!['.mjs','.js','.json'].includes(ext)){unresolved.push({...row,reason:'UNSUPPORTED_MODULE_EXTENSION'});return;}
   ordinary(target);row.target=target;edges.push(row);pending.push(target);
   if(ext==='.json'&&!attrs(attributeNode,dynamic))report(file,owner,'JSON_IMPORT_ATTRIBUTE_REQUIRED',specifier,text);
   if(ext!=='.json'&&attributeNode)report(file,owner,'UNSUPPORTED_IMPORT_ATTRIBUTES',specifier,text);
  }
  // Collect bindings before visits so aliases of dangerous APIs cannot evade order checks.
  for(const statement of source.statements){if(ts.isImportDeclaration(statement)&&ts.isStringLiteral(statement.moduleSpecifier)){const clause=statement.importClause,spec=statement.moduleSpecifier.text;if(clause?.name)names.set(clause.name.text,spec);const bindings=clause?.namedBindings;if(bindings&&ts.isNamespaceImport(bindings))names.set(bindings.name.text,spec);if(bindings&&ts.isNamedImports(bindings))for(const binding of bindings.elements)names.set(binding.name.text,spec+':'+(binding.propertyName?.text??binding.name.text));}}

  // Resolve the two ambiguous names through lexical declarations/import receivers.
  // Ordinary data called binding and RegExp/owned-object exec are not capabilities.
  const scopes=new WeakMap(),rootScope={parent:null,decls:new Map()};
  function declareName(name,record,scope){
   if(ts.isIdentifier(name))scope.decls.set(name.text,record);
   else if(ts.isObjectBindingPattern(name))for(const element of name.elements){
    const member=propertyName(element.propertyName??element.name);
    declareName(element.name,{base:record,member},scope);
   }else if(ts.isArrayBindingPattern(name))for(const element of name.elements)if(ts.isBindingElement(element))declareName(element.name,{ordinary:true},scope);
  }
  function indexScopes(node,scope){
   if(node!==source&&(ts.isFunctionLike(node)||ts.isBlock(node)||ts.isCatchClause(node)))scope={parent:scope,decls:new Map()};
   scopes.set(node,scope);
   if(ts.isVariableDeclaration(node))declareName(node.name,{expression:node.initializer,node},scope);
   if(ts.isParameter(node))declareName(node.name,{ordinary:true},scope);
   if((ts.isFunctionDeclaration(node)||ts.isClassDeclaration(node))&&node.name)declareName(node.name,{ordinary:true},scope.parent??scope);
   if(ts.isImportDeclaration(node)&&ts.isStringLiteral(node.moduleSpecifier)){
    const spec=node.moduleSpecifier.text,clause=node.importClause;
    if(clause?.name)declareName(clause.name,{import:spec},scope);
    const bindings=clause?.namedBindings;
    if(bindings&&ts.isNamespaceImport(bindings))declareName(bindings.name,{import:spec},scope);
    if(bindings&&ts.isNamedImports(bindings))for(const element of bindings.elements)declareName(element.name,{import:spec,member:element.propertyName?.text??element.name.text},scope);
   }
   ts.forEachChild(node,child=>indexScopes(child,scope));
  }
  indexScopes(source,rootScope);
  function memberOrigin(base,member){
   if(base==='process'&&member==='binding')return 'process.binding';
   if(base==='child_process'&&member==='exec')return 'child_process.exec';
   return undefined;
  }
  function recordOrigin(record,seen){
   if(!record||seen.has(record))return undefined;seen.add(record);
   if(record.import){
    const origin=record.import==='node:process'?'process':record.import==='node:child_process'?'child_process':undefined;
    return record.member?memberOrigin(origin,record.member):origin;
   }
   if(record.base)return memberOrigin(recordOrigin(record.base,seen),record.member);
   return record.expression?receiverOrigin(record.expression,seen):undefined;
  }
  function receiverOrigin(node,seen=new Set()){
   if(!node)return undefined;
   if(ts.isParenthesizedExpression(node))return receiverOrigin(node.expression,seen);
   if(ts.isIdentifier(node)){
    for(let scope=scopes.get(node);scope;scope=scope.parent)if(scope.decls.has(node.text))return recordOrigin(scope.decls.get(node.text),seen);
    return node.text==='process'?'process':undefined;
   }
   if(ts.isPropertyAccessExpression(node)||ts.isElementAccessExpression(node))return memberOrigin(receiverOrigin(node.expression,seen),ts.isPropertyAccessExpression(node)?node.name.text:propertyName(node.argumentExpression));
   return undefined;
  }
  function visit(node){
   // TS intentionally parses TypeScript constructs in ScriptKind.JS without parse diagnostics.
   // This graph accepts JavaScript modules, so reject that grammar explicitly.
   need(!ts.isTypeNode(node)&&!ts.isInterfaceDeclaration(node)&&!ts.isTypeAliasDeclaration(node)&&!ts.isEnumDeclaration(node)&&!ts.isModuleDeclaration(node)&&!ts.isImportEqualsDeclaration(node)&&!ts.isAsExpression(node)&&!ts.isTypeAssertionExpression(node)&&!ts.isSatisfiesExpression(node)&&!ts.isNonNullExpression(node)&&!(ts.isExportAssignment(node)&&node.isExportEquals),'NON_JAVASCRIPT_SYNTAX:'+file);
   if(ts.isImportDeclaration(node)){if(node.importClause?.isTypeOnly)report(file,node,'TYPE_ONLY_ESM_IN_JS','import type',text);edge(node.moduleSpecifier,node,'import',node.attributes);}
   else if(ts.isExportDeclaration(node)&&node.moduleSpecifier){if(node.isTypeOnly)report(file,node,'TYPE_ONLY_ESM_IN_JS','export type',text);edge(node.moduleSpecifier,node,'export-from',node.attributes);}
   else if(ts.isCallExpression(node)&&node.expression.kind===ts.SyntaxKind.ImportKeyword){if(node.arguments.length<1||node.arguments.length>2)report(file,node,'IMPORT_ARGUMENT_SHAPE','import()',text);else edge(node.arguments[0],node,'dynamic-import',node.arguments[1],true);}
   else if(ts.isCallExpression(node)||ts.isNewExpression(node)){
    const expression=node.expression;let name=ts.isIdentifier(expression)?expression.text:ts.isPropertyAccessExpression(expression)?expression.name.text:ts.isElementAccessExpression(expression)?propertyName(expression.argumentExpression):undefined;
    const binding=ts.isIdentifier(expression)?names.get(expression.text):undefined;
    if(CODE_EXECUTORS.has(name)||name==='constructor'||binding&&[...HAZARD_BUILTINS].some(s=>binding===s||binding.startsWith(s+':')))report(file,node,'OPAQUE_DEPENDENCY_EXECUTION',name??binding,text);
    if(ts.isElementAccessExpression(expression)&&!ts.isStringLiteral(expression.argumentExpression)&&!ts.isNoSubstitutionTemplateLiteral(expression.argumentExpression))report(file,node,'COMPUTED_CALL_TARGET','computed property call',text);
   }

   // Catch direct, extracted, renamed and destructured process/child_process capabilities.
   if(ts.isIdentifier(node)||ts.isPropertyAccessExpression(node)||ts.isElementAccessExpression(node)){
    const origin=receiverOrigin(node);
    if(origin==='process.binding'||origin==='child_process.exec')report(file,node,'OPAQUE_DEPENDENCY_REFERENCE',origin,text);
   }
   // Any referenced require/eval/worker capability, including an alias assignment, is unresolved.
   if(ts.isIdentifier(node)&&CODE_EXECUTORS.has(node.text)){
    const p=node.parent,declarationName=(ts.isVariableDeclaration(p)||ts.isParameter(p)||ts.isFunctionDeclaration(p)||ts.isImportSpecifier(p))&&p.name===node;
    if(!declarationName)report(file,node,'OPAQUE_DEPENDENCY_REFERENCE',node.text,text);
   }
   if(ts.isElementAccessExpression(node)&&CODE_EXECUTORS.has(propertyName(node.argumentExpression)))report(file,node,'OPAQUE_DEPENDENCY_REFERENCE',propertyName(node.argumentExpression),text);
   if(ts.isElementAccessExpression(node)&&ts.isIdentifier(node.expression)&&['globalThis','global','process','module'].includes(node.expression.text)&&!ts.isStringLiteral(node.argumentExpression)&&!ts.isNoSubstitutionTemplateLiteral(node.argumentExpression))report(file,node,'OPAQUE_GLOBAL_CAPABILITY_LOOKUP',node.expression.text,text);
   if(ts.isCallExpression(node)&&ts.isPropertyAccessExpression(node.expression)&&node.expression.name.text==='resolve'&&ts.isMetaProperty(node.expression.expression)&&node.expression.expression.keywordToken===ts.SyntaxKind.ImportKeyword)report(file,node,'OPAQUE_IMPORT_META_RESOLVE','import.meta.resolve',text);
   if(ts.isJsxElement(node)||ts.isJsxSelfClosingElement(node)||ts.isDecorator(node))report(file,node,'UNSUPPORTED_JS_SYNTAX','JSX/decorator',text);
   ts.forEachChild(node,visit);
  }
  visit(source);
 }
 const order=(a,b)=>key(a.parent).localeCompare(key(b.parent))||a.offset-b.offset||JSON.stringify(a).localeCompare(JSON.stringify(b));
 const pins=role=>[...(purpose.get(role)??[])].map(k=>seen.get(k).pin);
 const graph={kind:GENERATOR_MODULE_CLOSURE_KIND,schemaVersion:1,repoRoot:selected.repoRoot,entries:selected.entries,additionalFiles:selected.additionalFiles,moduleClosureComplete:unresolved.length===0,modules:[...modules.values()].sort((a,b)=>key(a.path).localeCompare(key(b.path))),dataInputs:[...data.values()].sort((a,b)=>key(a.path).localeCompare(key(b.path))),edges:edges.sort(order),builtins:builtins.sort(order),unresolved:unresolved.sort(order),parserInputs:[...pins('reader'),...pins('parser-anchor'),...pins('parser'),...pins('parser-package')].sort((a,b)=>key(a.path).localeCompare(key(b.path))),parser:{name:'typescript',version:'5.9.3',implementation:path.resolve(PARSER_PATH),positionBasis:'offset=UTF8 bytes; utf16Offset=TypeScript code units'},runtime:{node,version:process.version,versions:{...process.versions},preloadOrCustomLoader:false},fullToolClosure:false,compilerExecuted:false,generatorExecuted:false,scope:'STATIC_ESM_MODULE_GRAPH_ONLY_NOT_DYNAMIC_DATA_IO_OR_COMPILER_AUTHORITY'};
 // Fresh closing reads of every member, parser, runtime and reader. No hash survives this pass.
 for(const {pin}of seen.values()){const now=readStable(pin.path).pin;need(isDeepStrictEqual(pin,now),'CLOSING_INPUT_DRIFT:'+pin.path);}
 return {selected,graph};
}
export function collectGeneratorModuleClosure(options){const state=build(options);const context=Object.freeze({kind:GENERATOR_MODULE_CLOSURE_KIND,moduleClosureComplete:state.graph.moduleClosureComplete,moduleCount:state.graph.modules.length,dataCount:state.graph.dataInputs.length,unresolvedCount:state.graph.unresolved.length,fullToolClosure:false,compilerExecuted:false});live.set(context,state);return context;}
export function generatorModuleClosureSnapshot(context){const state=live.get(context);need(state,'LIVE_BRANDED_CONTEXT_REQUIRED');return structuredClone(state.graph);}
export function recheckGeneratorModuleClosure(context){const state=live.get(context);need(state,'LIVE_BRANDED_CONTEXT_REQUIRED');const current=build(state.selected);need(isDeepStrictEqual(current.graph,state.graph),'EXACT_MODULE_MEMBERS_EDGES_OR_INPUT_DRIFT');return {kind:GENERATOR_MODULE_CLOSURE_KIND,status:'ALL_MODULE_MEMBERS_EDGES_AND_BYTES_RECHECKED',moduleClosureComplete:current.graph.moduleClosureComplete,modules:current.graph.modules.length,dataInputs:current.graph.dataInputs.length,edges:current.graph.edges.length,fullToolClosure:false,compilerExecuted:false};}

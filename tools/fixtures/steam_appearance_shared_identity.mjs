import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {pathToFileURL} from 'node:url';
import {steamSessionObserverPublication} from './steam_session_observer_publication.mjs';

// Preparation for the existing CSS-use consumer's explicit selection. The
// public identity implementation is preserved; only the original private
// publication adapter provides inert terminal facts. No installed app access.
function sharedReadBodyEnd(text, start) {
  let depth=0, mode='code', rawEnd='';
  for(let i=start;i<text.length;++i) {
    const c=text[i],n=text[i+1];
    if(mode==='raw'){if(text.startsWith(rawEnd,i)){i+=rawEnd.length-1;mode='code';}continue;}
    if(mode==='line'){if(c==='\n')mode='code';continue;}
    if(mode==='block'){if(c==='*'&&n==='/'){++i;mode='code';}continue;}
    if(mode==='string'||mode==='char'){if(c==='\\'){++i;continue;}if(c===(mode==='string'?'"':"'"))mode='code';continue;}
    if(c==='/'&&n==='/'){++i;mode='line';continue;}
    if(c==='/'&&n==='*'){++i;mode='block';continue;}
    if(c==='R'&&n==='"'){const open=text.indexOf('(',i+2);assert(open>=0&&open-i<=18);rawEnd=')'+text.slice(i+2,open)+'"';i=open;mode='raw';continue;}
    if(c==='"'){mode='string';continue;}if(c==="'"){mode='char';continue;}
    if(c==='{')++depth;if(c==='}'&&--depth===0)return i+1;
  }
  throw Error('Unclosed source body');
}

export function prepareAppearanceSharedIdentity(root,out){
  root=path.resolve(root);out=path.resolve(out);fs.mkdirSync(out,{recursive:true});
  const pins=[],seen=new Set(),hash=b=>crypto.createHash('sha256').update(b).digest('hex');
  const visit=relative=>{
    relative=path.normalize(relative);
    if(seen.has(relative))return;seen.add(relative);
    const source=path.join(root,relative),bytes=fs.readFileSync(source);
    pins.push({path:source,bytes:bytes.length,sha256:hash(bytes)});
    const target=path.join(out,relative);fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,bytes,{flag:'wx'});
    for(const match of bytes.toString('utf8').matchAll(/^\s*#\s*include\s+"([^"\r\n]+)"/gm)){
      if(relative===path.normalize('tools/steam_cssloader_use_selftest.cpp')&&match[1]==='shared-steam-read-main.inc'){
        assert(fs.existsSync(path.join(out,match[1])),'SHARED_MAIN_GENERATED_INCLUDE_REQUIRED');continue;
      }
      const local=path.join(path.dirname(relative),match[1]);
      if(fs.existsSync(path.join(root,local)))visit(local);
      else if(fs.existsSync(path.join(root,'native',match[1])))visit(path.join('native',match[1]));
      else if(fs.existsSync(path.join(root,'deps/json',match[1])))visit(path.join('deps/json',match[1]));
      else throw Error('Undeclared local include: '+match[1]);
    }
  };
  visit('native/steam_cssloader_use.h');visit('native/steam_document_read.h');visit('native/steam_session_observer.h');visit('native/decky_sidebar_diagnostics.h');
  // YMCC_FORMAL_DOCUMENT_CONSUMER_R214_VISIT_BEGIN
  visit('native/decky_binding_worker_entry.h');
  visit('native/steam_document_finite_worker.h');visit('native/steam_document_parent.h');
  // YMCC_FORMAL_DOCUMENT_CONSUMER_R214_VISIT_END
  const product=fs.readFileSync(path.join(root,'native/steam_session_observer.h'));
  const publication=steamSessionObserverPublication(product);
  // YMCC_FORMAL_DOCUMENT_CONSUMER_R214_RUN_ADAPTER_BEGIN
  // Publication first verifies its unchanged exact original/derived epoch.
  // Only the derived private compiler header gains this reversible run seam.
  const published=publication.bytes.toString('utf8');
  const declaration='    void run(const Changed& changed, const std::function<bool()>& needsRetry, const IdentityPublished& onIdentityPublished,const StartupWindowFact& onStartupWindowFact) {';
  const runStart=published.indexOf(declaration);
  assert(runStart>=0&&published.indexOf(declaration,runStart+1)<0,'FIXTURE_SOURCE_RUN_UNIQUE');
  const runOpen=published.indexOf('{',runStart),runEnd=sharedReadBodyEnd(published,runOpen);
  const runPrefix='\n// YMCC_FORMAL_DOCUMENT_CONSUMER_R214_RUN_BEGIN\n#ifdef YMCC_CSS_FINITE_DOCUMENT_FIXTURE\n#define YMCC_CSS_FINITE_DOCUMENT_SOURCE_ADAPTED 1\n        (void)changed;(void)needsRetry;(void)onIdentityPublished;\n        if(WaitForSingleObject(stopEvent,INFINITE)!=WAIT_OBJECT_0)throw std::runtime_error("fixture-source-stop-wait");\n#else\n';
  const runSuffix='\n#endif\n// YMCC_FORMAL_DOCUMENT_CONSUMER_R214_RUN_END\n';
  const fixtureText=published.slice(0,runOpen+1)+runPrefix+published.slice(runOpen+1,runEnd-1)+runSuffix+published.slice(runEnd-1);
  const fixtureBytes=Buffer.from(fixtureText);
  const fixtureEdits=[{marker:'YMCC_FORMAL_DOCUMENT_CONSUMER_R214_RUN_BEGIN',insertedBase64:Buffer.from(runPrefix).toString('base64')},
    {marker:'YMCC_FORMAL_DOCUMENT_CONSUMER_R214_RUN_END',insertedBase64:Buffer.from(runSuffix).toString('base64')}];
  let reversed=fixtureText;
  for(const entry of fixtureEdits){const inserted=Buffer.from(entry.insertedBase64,'base64').toString();assert.equal(reversed.split(inserted).length,2);reversed=reversed.replace(inserted,'');}
  assert(Buffer.from(reversed).equals(publication.bytes),'FIXTURE_RUN_REVERSE_BYTES');
  fs.writeFileSync(path.join(out,'native/steam_session_observer.h'),fixtureBytes);
  const fixtureSource={macro:'YMCC_CSS_FINITE_DOCUMENT_FIXTURE',originalPublication:publication.derived,
    derived:{path:'native/steam_session_observer.h',bytes:fixtureBytes.length,sha256:hash(fixtureBytes)},edits:fixtureEdits,
    scope:'Real Observer start/stop, SourceOwner/SourceRead and ListenerExecutor unchanged; only run body waits on its own stopEvent under the private macro'};
  fs.writeFileSync(path.join(out,'fixture-markers.json'),JSON.stringify(fixtureSource,null,2)+'\n',{flag:'wx'});
  // YMCC_FORMAL_DOCUMENT_CONSUMER_R214_RUN_ADAPTER_END
  const mainPath=path.join(root,'native/main.cpp'),main=fs.readFileSync(mainPath),text=main.toString('utf8');
  const start=text.indexOf('static json steamAppearanceSharedSteamRead(');
  if(start<0||text.indexOf('static json steamAppearanceSharedSteamRead(',start+1)!==-1)throw Error('Shared appearance main consumer missing or ambiguous');
  const open=text.indexOf('{',start),end=sharedReadBodyEnd(text,open);
  const fragment=text.slice(start,end)+"\n";
  fs.writeFileSync(path.join(out,'shared-steam-read-main.inc'),fragment,{flag:'wx'});
  pins.push({path:mainPath,bytes:main.length,sha256:hash(main)});
  // YMCC_FORMAL_DOCUMENT_CONSUMER_R214_PROVENANCE_BEGIN
  visit('tools/steam_cssloader_use_selftest.cpp');
  visit('tools/fixtures/steam_appearance_shared_identity.mjs');visit('tools/fixtures/steam_session_observer_publication.mjs');
  // YMCC_FORMAL_DOCUMENT_CONSUMER_R214_PROVENANCE_END
  const report={sources:pins,publication:{...publication,bytes:undefined},fixtureSource,mainFunction:{startUtf8Byte:Buffer.byteLength(text.slice(0,start)),endUtf8ByteExclusive:Buffer.byteLength(text.slice(0,end)),bytes:Buffer.byteLength(fragment),sha256:hash(Buffer.from(fragment))},scope:'Private preparation of the existing --shared-identity-only consumer'};
  fs.writeFileSync(path.join(out,'appearance-shared-inputs.json'),JSON.stringify(report,null,2)+"\n",{flag:'wx'});
  return report;
}
if(process.argv[1]&&import.meta.url===pathToFileURL(path.resolve(process.argv[1])).href){
  if(process.argv.length!==4)throw Error('Expected source root and fresh private output');
  prepareAppearanceSharedIdentity(process.argv[2],process.argv[3]);
}

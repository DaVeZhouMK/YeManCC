import {reverseReviewedDelta} from '../sleep-native-recovery-consumer/script-review.mjs';
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
export const sha256=bytes=>createHash('sha256').update(bytes).digest('hex');
const need=(ok,code)=>{if(!ok)throw Error(code);};
const lf=text=>text.replace(/^\uFEFF/,'').replace(/\r\n/g,'\n');
// Preserve offsets while masking comments and literals, then keep the complete balanced body.
export function completeDefinition(text,signature){
 text=lf(text);const masked=text.replace(/\/\/[^\n]*|\/\*[\s\S]*?\*\/|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'/g,m=>' '.repeat(m.length));
 need(masked.split(signature).length===2,'GAME_MSI_DEFINITION_NOT_UNIQUE:'+signature);
 const start=masked.indexOf(signature),open=start+signature.lastIndexOf('{');let depth=0;
 for(let i=open;i<masked.length;i++){if(masked[i]==='{')depth++;if(masked[i]==='}'&&--depth===0)return text.slice(start,i+1);}
 throw Error('GAME_MSI_DEFINITION_INCOMPLETE:'+signature);
}
export function materializeObserverContext({project,out,moduleDir,reviewSha256,compatibilityReview}){
 const reviewFile=path.join(moduleDir,'observer-context-review.json'),reviewBytes=fs.readFileSync(reviewFile);
 need(sha256(reviewBytes)===reviewSha256,'GAME_MSI_REVIEW_CHANGED');const review=JSON.parse(reviewBytes.toString('utf8').replace(/^\uFEFF/,''));
 let currentReview=null;
 if(compatibilityReview){need(compatibilityReview.manifest==='tools/ymcc_sim/global/sleep-game-pause-consumer/observer-context-current.json'&&compatibilityReview.sha256==='0263bb3bd398c969564588d3580439f7f36041f83804d31eb48b528278603a0e','GAME_MSI_CURRENT_REVIEW_BINDING');const bytes=fs.readFileSync(path.join(moduleDir,'observer-context-current.json'));need(sha256(bytes)===compatibilityReview.sha256,'GAME_MSI_CURRENT_REVIEW_CHANGED');currentReview=JSON.parse(bytes);need(currentReview.schemaVersion===1&&currentReview.kind==='exact-current-observer-context-pairing'&&currentReview.sourceApproval===false&&currentReview.defaultParentApprovalRenewed===false&&currentReview.priorReviewSha256===reviewSha256,'GAME_MSI_CURRENT_REVIEW_SCHEMA');}
 const bindings=review.sources.map(row=>{const bytes=fs.readFileSync(path.join(project,row.path)),actual=sha256(bytes);if(actual!==row.sha256){const pair=currentReview?.sourcePairs.find(x=>x.path===row.path);need(pair&&pair.beforeSha256===row.sha256,'GAME_MSI_SOURCE_NOT_REVIEWED:'+row.path);need(sha256(reverseReviewedDelta(bytes,pair))===row.sha256,'GAME_MSI_SOURCE_NOT_REVIEWED:'+row.path);}return {...row,currentSha256:actual,exactHistoricalPairing:actual!==row.sha256};});
 const definitions=review.definitions.map(row=>{const text=completeDefinition(fs.readFileSync(path.join(project,row.path),'utf8'),row.signature),actual=sha256(text);if(actual!==row.completeLfSha256){const pair=currentReview?.definitionPairs.find(x=>x.path===row.path&&x.signature===row.signature);need(pair&&pair.beforeSha256===row.completeLfSha256&&!row.token,'GAME_MSI_DEFINITION_NOT_REVIEWED:'+row.signature);need(sha256(reverseReviewedDelta(Buffer.from(text),pair))===row.completeLfSha256,'GAME_MSI_DEFINITION_NOT_REVIEWED:'+row.signature);}return {...row,currentCompleteLfSha256:actual,exactHistoricalPairing:actual!==row.completeLfSha256,text};});
 const template=fs.readFileSync(path.join(moduleDir,'observer-context.h.in'));need(sha256(template)===review.templateSha256,'GAME_MSI_TEMPLATE_CHANGED');let header=lf(template.toString('utf8'));
 for(const row of definitions.filter(x=>x.token)){need(header.split(row.token).length===2,'GAME_MSI_CONTEXT_TOKEN_NOT_UNIQUE');header=header.replace(row.token,row.text);}
 need(!header.includes('@@PRODUCT_'),'GAME_MSI_CONTEXT_UNRESOLVED');
 const headerPath=path.join(out,'msi-observer-context.h');fs.writeFileSync(headerPath,header,{flag:'wx'});
 const proof={schemaVersion:1,reviewSha256,compatibilityReview:compatibilityReview??null,bindings,definitions,header:{path:headerPath,sha256:sha256(header)},insertion:review.insertion,originalScript:currentReview?{...review.originalScript,sha256:bindings.find(row=>row.path===review.originalScript.path).currentSha256,historicalSha256:review.originalScript.sha256,exactHistoricalPairing:true}:review.originalScript,expectedContextChecks:12,hardwareValidated:false,scope:'Enabled product diagnostic Policy and complete helper with explicit memory exit/board/log inputs; no observer process/poll/launch runtime'};
 const proofPath=path.join(out,'msi-observer-context.json');fs.writeFileSync(proofPath,JSON.stringify(proof,null,2)+'\n',{flag:'wx'});return {...proof,proof:{path:proofPath,sha256:sha256(fs.readFileSync(proofPath))}};
}
export function verifyObserverInsertion(cpp,audit,context){
 const row=audit.gameObserverContext,text=lf(fs.readFileSync(cpp,'utf8')),include=context.insertion.include;
 need(row&&row.headerSha256===context.header.sha256&&row.reviewSha256===context.reviewSha256,'GAME_MSI_INSERTION_PROOF_MISSING');
 need(text.split(include).length===2&&text.includes(include+context.insertion.anchor),'GAME_MSI_INSERTION_NOT_EXACT');
 let restored=text.replace(include,'');for(const edit of [...(context.insertion.scaffoldingReplacements??[])].reverse()){need(restored.split(edit.after).length===2&&!restored.includes(edit.before),'GAME_MSI_SCAFFOLDING_REVERSE_NOT_EXACT');restored=restored.replace(edit.after,edit.before);}need(sha256(text)===row.generatedTextSha256&&sha256(restored)===row.originalTextSha256,'GAME_MSI_ORIGINAL_CPP_CHANGED');
 need(sha256(fs.readFileSync(cpp))===row.generatedFileSha256,'GAME_MSI_GENERATED_CPP_BYTES_CHANGED');return row;
}

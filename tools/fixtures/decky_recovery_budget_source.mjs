// Prepare the exact current native reservation slice for the original explicit
// recovery fixture. Four inert IO ports are adapted; no policy expression is.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import crypto from 'node:crypto';
export function prepareRecoveryBudgetSource(root,out){
 const file=path.join(root,'native/main.cpp'),bytes=fs.readFileSync(file),main=bytes.toString('utf8').replace(/\r\n/g,'\n');
 const start=main.indexOf('static bool deckySidebarReserveContextCleanup('),end=main.indexOf('\n// Called only inside the owned private child',start);
 assert(start>=0&&end>start&&main.indexOf('static bool deckySidebarReserveContextCleanup(',start+1)<0,'CURRENT_RESERVATION_SLICE_REQUIRED');
 const original=main.slice(start,end),mappings=[['ymcc::deckymirror::frontendBuildId','fixtureFrontendBuildId'],['ymcc::deckyupgrade::runningstore::reserve','fixtureUpgradeReserve'],['GetModuleFileNameW','fixtureImagePath']];let compiled=original;
 for(const [a,b]of mappings){assert.equal(compiled.split(a).length,2,'EXACT_RESERVATION_IO_PORT:'+a);compiled=compiled.replace(a,b);}
 const header=path.join(out,'actual-reservation.inc'),source='#pragma once\nnamespace budgetfixture {\n'+compiled+'\n}\n';fs.writeFileSync(header,source,{flag:'wx'});
 const sha=b=>crypto.createHash('sha256').update(b).digest('hex'),authority={path:file,bytes:bytes.length,sha256:sha(bytes)},before=!original.includes('RecoveryBudgetFacts* observation');
 const record={authority,originalSha256:sha(Buffer.from(original)),compiledSha256:sha(Buffer.from(compiled)),generatedSha256:sha(Buffer.from(source)),mappings,original,compiled,baseline:before,scope:'Exact native function; IO ports only. No installed settings or runningstore access.'};
 const manifest=path.join(out,'actual-reservation.json');fs.writeFileSync(manifest,JSON.stringify(record,null,2)+'\n',{flag:'wx'});return {header,manifest,baseline:before,record};
}

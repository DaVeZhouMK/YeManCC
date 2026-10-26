import {CASE_ID,MODULE,sourceFiles,artifactFiles} from '../../../fixtures/controller_game_feedback_writer/contract.mjs';
export {CASE_ID};
export function getControllerGameFeedbackWriterConsumerAdapters({selectedIds,readSources=sourceFiles,readArtifacts=artifactFiles}={}){
 if(selectedIds!==undefined&&(!Array.isArray(selectedIds)||selectedIds.some(id=>typeof id!=='string'||!id)))throw Error('WRITER_SELECTION_SHAPE');
 const deferred=Array.isArray(selectedIds)&&selectedIds.length>0&&!selectedIds.includes(CASE_ID);
 return [{
 id:CASE_ID,domain:'controller',title:'Actual managed pipe and same admitted source frame through serial XInput/MSI writer with memory hardware terminals',
 kind:'node',path:MODULE+'/run.mjs',args:['--evidence-dir','{outDir}/writer-consumer'],timeoutMs:900000,
 profiles:['desktop','full'],scope:'behavioral-mock',blockedExitCodes:[7],evidenceInputs:deferred?[MODULE+'/adapter.mjs','tools/fixtures/controller_game_feedback_writer/contract.mjs']:readSources(),artifactInputs:deferred?[]:readArtifacts(),...(deferred?{inputRegistrationDeferred:true,blockedReason:'WRITER_INPUTS_DEFERRED_OUTSIDE_EXPLICIT_SELECTION'}:{}),resultFiles:['writer-consumer/result.json'],
 boundary:'Current full production source only. Rebuilt owned managed24 fixture with actual ownership/runtime identity classes;19 actual Compile inputs. Sony13/354 plus Steam12/327 plus Xbox16/448 and1 separate persona assertion gives41/1130 in Release/Checked; old6/46, persona7/54 and Steam native protocol controls remain separate. Five Sony native, four Steam native and four Steam managed compiled source faults; four Xbox native and eight Xbox managed compiled source faults. Actual source token/frame/Inbox/serial writer; controlled SDK event and memory hardware. No actual SDK, device IO, product Main, install, full Coordinator or physical acceptance. Prototype candidates cannot run as this global leaf.'
}];}

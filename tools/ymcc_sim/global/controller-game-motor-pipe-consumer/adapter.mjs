import {CASE_ID,MODULE,sourceFiles,artifactFiles} from '../../../fixtures/controller_game_motor_pipe/contract.mjs';
export {CASE_ID};
export function getControllerGameMotorPipeConsumerAdapters(){return [{
 id:CASE_ID,domain:'controller',title:'Actual Sony motor decoder/mailbox through owned private pipe and real native frame/Inbox consumers',
 kind:'node',path:MODULE+'/run.mjs',args:['--evidence-dir','{outDir}/game-motor-pipe'],timeoutMs:900000,
 profiles:['desktop','full'],scope:'behavioral-mock',evidenceInputs:sourceFiles(),artifactInputs:artifactFiles(),
 resultFiles:['game-motor-pipe/result.json'],
 boundary:'Independent exact24/25 source fixture with two actual helper classes and19 actual Compile inputs rebuilt per owned case. Normal10 cases281 checks and stale-revision33 checks in Release/Checked; unchanged native controls and compiled guard-removal semantic negative. Actual decoder/mailbox/serializer/pipe/frame/Inbox; inert SDK event and controlled source snapshots. No real SDK, hardware, product Main, full Coordinator or physical forwarding.'
}];}

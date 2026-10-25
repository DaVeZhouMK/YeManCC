import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {execute as executeProducer} from '../../../fixtures/controller_game_motor_pipe/driver.mjs';
import {CASE_ID,REPO,WORKSPACE} from '../../../fixtures/controller_game_motor_pipe/contract.mjs';
import {runOwnedBuildBridge} from './owned-build-bridge.mjs';
export async function execute(evidenceDir){if(arguments.length!==1)throw Error('GLOBAL_BRIDGE_FORMAL_ONLY');return runOwnedBuildBridge({evidenceDir,caseId:CASE_ID,repo:REPO,workspace:WORKSPACE,produce:root=>executeProducer(root)});}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{if(process.argv.length!==4||process.argv[2]!=='--evidence-dir')throw Error('Use --evidence-dir ABS_FRESH_CASE_CHILD');const r=await execute(process.argv[3]);console.log(JSON.stringify({id:r.id,status:r.status,normalCases:10,normalChecksPerConfiguration:281,staleControlChecksPerConfiguration:33,physicalAcceptance:false}));}
 catch(error){console.error(error.stack??error.message);process.exitCode=2;}
}

import {pathToFileURL} from 'node:url';
import {PRIMARY_PROFILES,capturePrimarySourceBinding,inspectPrimaryStage,preparePrimaryStage,primaryPreparerMain} from './primary-preparers-contract.mjs';
export const ONEX_LIFECYCLE_PROFILE=PRIMARY_PROFILES.onex;
export {capturePrimarySourceBinding};
export const inspectOneXLifecycleInputs=options=>inspectPrimaryStage('onex',options);
export const prepareOneXLifecycle=options=>preparePrimaryStage('onex',options);
if(process.argv[1]&&import.meta.url===pathToFileURL(process.argv[1]).href){
  primaryPreparerMain('onex').catch(error=>{console.error(JSON.stringify({code:error.code??error.message,detail:error.detail??null}));process.exitCode=2;});
}

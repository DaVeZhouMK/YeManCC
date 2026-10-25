import {pathToFileURL} from 'node:url';
import {PRIMARY_PROFILES,capturePrimarySourceBinding,inspectPrimaryStage,preparePrimaryStage,primaryPreparerMain} from './primary-preparers-contract.mjs';
export const PUBLIC_FACTORY_PROFILE=PRIMARY_PROFILES.public;
export {capturePrimarySourceBinding};
export const inspectPublicFactoryInputs=options=>inspectPrimaryStage('public',options);
export const preparePublicFactory=options=>preparePrimaryStage('public',options);
if(process.argv[1]&&import.meta.url===pathToFileURL(process.argv[1]).href){
  primaryPreparerMain('public').catch(error=>{console.error(JSON.stringify({code:error.code??error.message,detail:error.detail??null}));process.exitCode=2;});
}

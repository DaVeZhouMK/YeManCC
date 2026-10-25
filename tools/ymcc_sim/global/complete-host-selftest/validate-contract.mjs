import fs from 'node:fs';import {validateContract} from './contract.mjs';
if(process.argv.length!==4||process.argv[2]!=='--contract')throw Error('FIXED_VALIDATOR_ARGUMENTS_REQUIRED');
validateContract(JSON.parse(fs.readFileSync(process.argv[3],'utf8').replace(/^\uFEFF/,'')));console.log('COMPLETE_HOST_DATA_CONTRACT_VALID');
import test from 'node:test';
import assert from 'node:assert/strict';
import {parseRealStoreArguments} from './run.mjs';

test('real-store requires explicit bounded selection and rejects ambiguous or unsupported options before work',()=>{
 const output='G:/YeManCC-Work/Testing/YMCC/Workspace/explicit-real-store-test';
 const base=['--real-store','--out',output];
 assert.deepEqual(parseRealStoreArguments(base),{profile:'Checked',selection:'both',budgetSeconds:600,prepareOnly:false,output});
 assert.equal(parseRealStoreArguments([...base,'--case','sharing-denial']).selection,'sharing-denial');
 assert.equal(parseRealStoreArguments([...base,'--case','success','--profile','Release','--prepare-only']).prepareOnly,true);
 for(const args of [[],['--out',output],['--real-store'],['--real-store','--out','relative'],[...base,'--real-store'],[...base,'--case','both','--case','success'],[...base,'--case','unknown'],[...base,'--profile','unknown'],[...base,'--out',output],[...base,'--prepare-only','--prepare-only'],[...base,'--budget-seconds','0'],[...base,'--budget-seconds','1801'],[...base,'--budget-seconds','Infinity'],[...base,'--profile'],[...base,'--unknown']])assert.throws(()=>parseRealStoreArguments(args));
 assert.equal(parseRealStoreArguments([...base,'--budget-seconds','1800']).budgetSeconds,1800);
});

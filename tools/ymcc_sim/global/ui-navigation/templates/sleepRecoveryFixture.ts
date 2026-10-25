let value={ok:true,enabled:true,phase:'idle',generation:0,healthVerified:false};
(window as any).__sleepRecoveryFixture={set:(patch:any)=>{Object.assign(value,patch);(window as any).__emitNativeEvent?.('sleep.recovery-auto',{...value});}};
export const getAutomaticSleepRecovery=async()=>({...value});
export const setAutomaticSleepRecovery=async(enabled:boolean)=>{value={...value,enabled};(window as any).__sleepWrites??=[];(window as any).__sleepWrites.push({name:'automaticRecovery',value:enabled});return {...value};};
export type AutomaticSleepRecoveryStatus=any;

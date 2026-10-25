export class GameMediaRevisionFence {
  private evictions=0;
  private entries=new Map<string,{revision:number;canonicalKey?:string}>();
  constructor(private limit=256){}
  observe(key:unknown,revision?:unknown,canonicalKey?:unknown){
    if(typeof key!=='string'||!key||key.length>100)return;
    const previous=this.entries.get(key),value=Number.isSafeInteger(revision)&&Number(revision)>=0?Number(revision):0;
    const alias=typeof canonicalKey==='string'&&canonicalKey&&canonicalKey!==key?canonicalKey:previous?.canonicalKey;
    this.entries.delete(key);this.entries.set(key,{revision:Math.max(previous?.revision||0,value),canonicalKey:alias});
    while(this.entries.size>this.limit){this.entries.delete(this.entries.keys().next().value!);this.evictions++;}
  }
  capture(){return this.evictions;}
  outdated(state:{mediaKey?:string;mediaBindingRevision?:number;mediaReadEpoch?:number}|null|undefined){
    if(!state?.mediaKey)return false;if(state.mediaReadEpoch!==undefined&&state.mediaReadEpoch<this.evictions)return true;const known=this.entries.get(state.mediaKey);if(!known)return false;
    return !!known.canonicalKey||known.revision>Number(state.mediaBindingRevision||0);
  }
}

export const gameMediaRevisions=new GameMediaRevisionFence();

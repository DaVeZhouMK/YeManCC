import {mkdirSync,writeFileSync} from 'node:fs';
import {join} from 'node:path';

const dir=process.argv[2];
if(!dir)throw Error('fixture directory required');
mkdirSync(dir,{recursive:true});
const u32=value=>{const b=Buffer.alloc(4);b.writeUInt32BE(value>>>0);return b;};
const words=(...items)=>Buffer.concat(items.map(u32));
const box=(type,...body)=>{const data=Buffer.concat(body);return Buffer.concat([u32(data.length+8),Buffer.from(type),data]);};
const videoTrack=(timescale,...timing)=>box('trak',box('tkhd',words(0,0,0,1)),box('mdia',
  box('mdhd',words(0,0,0,timescale)),box('hdlr',words(0,0,0x76696465)),box('minf',box('stbl',...timing))));
const withEdits=(track,edits)=>edits.length ? box('trak',track.subarray(8),...edits) : track;
const make=(timescale,runs,offsets,version=0,...edits)=>{
  const samples=runs.reduce((n,[count])=>n+count,0);
  const timing=[box('stts',words(0,runs.length,...runs.flat())),box('stsz',words(0,1,samples))];
  if(offsets)timing.push(box('ctts',words(version<<24,offsets.length,...offsets.flat())));
  const track=videoTrack(timescale,...timing);
  return Buffer.concat([box('moov',withEdits(track,edits)),box('mdat',Buffer.alloc(samples))]);
};
const edit=(version,...rows)=>box('edts',box('elst',words(version<<24,rows.length),...rows.map(([duration,time,rate])=>{
  if(version!==1)return words(duration,time,rate);
  const row=Buffer.alloc(20);
  row.writeBigUInt64BE(BigInt(duration));row.writeBigInt64BE(BigInt(time),8);row.writeUInt32BE(rate,16);
  return row;
})));
const edited=(...edits)=>make(1000,[[4,40]],null,0,...edits);
const fragmentTrack=(timescale,edits,...fragments)=>Buffer.concat([
  box('moov',withEdits(videoTrack(timescale,box('stts',words(0,0)),box('stsz',words(0,1,0))),edits),
    box('mvex',box('trex',words(0,1,1,40,0,0)))),...fragments,
]);
const fragmentsWithEdits=(edits,...fragments)=>fragmentTrack(1000,edits,...fragments);
const fragmented=(...fragments)=>fragmentsWithEdits([],...fragments);
const segment=({base=0n, offsets, durations, count=offsets?.length??durations?.length??2, version=0,
  tfhdDuration, tfdtVersion=0, omitTfdt=false, malformedRun=false})=>{
  const time=Buffer.alloc(tfdtVersion ? 8 : 4);
  if(tfdtVersion)time.writeBigUInt64BE(base);else time.writeUInt32BE(Number(base));
  const tfdt=omitTfdt?[]:[box('tfdt',u32(tfdtVersion<<24),time)];
  const tfhd=box('tfhd',words(0x020000|(tfhdDuration===undefined?0:8),1,...(tfhdDuration===undefined?[]:[tfhdDuration])));
  const flags=(version<<24)|(durations?0x100:0)|(offsets?0x800:0);
  const samples=Array.from({length:count},(_,i)=>[...(durations?[durations[i]]:[]),...(offsets?[offsets[i]]:[])]).flat();
  const trun=box('trun',words(flags,count,...(malformedRun?samples.slice(0,-1):samples)));
  return Buffer.concat([box('moof',box('traf',tfhd,...tfdt,trun)),box('mdat',Buffer.alloc(count))]);
};
const work={
  'no-compose-25': [make(1000,[[4,40]]),'low'],
  // Decode timestamps 0,40,80,120; display timestamps 0,60,80,140.
  'hidden-burst': [make(1000,[[4,40]],[[1,0],[1,20],[1,0],[1,20]]),'high'],
  // Signed offsets reorder the middle samples, but displayed spacing is 40.
  'signed-reorder': [make(1000,[[4,40]],[[1,0],[1,40],[1,-40],[1,0]],1),'low'],
  'short-decode-only': [make(1000,[[1,20],[1,60],[1,20],[1,60]],[[1,0],[1,20],[1,0],[1,20]]),'low'],
  'offset-short': [make(1000,[[4,40]],[[3,0]]),'unknown'],
  'offset-long': [make(1000,[[4,40]],[[5,0]]),'unknown'],
  'offset-empty': [make(1000,[[4,40]],[[0,1],[4,0]]),'unknown'],
  'duplicate-display': [make(1000,[[4,40]],[[1,40],[3,0]]),'unknown'],
  'unsupported-offset-version': [make(1000,[[4,40]],[[4,0]],2),'unknown'],
  'one-tick-rounding': [make(1000,[[2,33],[1,34],[1,33]]),'low'],
  'one-tick-rounding-repeated': [make(1000,Array.from({length:40},()=>[[2,33],[1,34]]).flat()),'low'],
  'coarse-local-60-fps-burst': [make(60,[[20,1],[20,4]]),'high'],
  'coarse-local-60-fps-after-pause': [make(60,[[20,4],[20,1]]),'high'],
  'coarse-one-tick-compensated': [make(60,[[1,1],[1,3],[4,2]]),'low'],
  // A last-sample duration is not an interval to a following presentation.
  'coarse-short-tail-not-burst': [make(60,[[1,4],[1,1],[1,1]]),'low'],
  'classic-25-short-tail': [make(1000,[[20,40],[1,5]]),'low'],
  'classic-30-short-tail': [make(30000,[[29,1000],[1,100]]),'low'],
  'constant-offset-30-short-tail': [make(30000,[[29,1000],[1,100]],[[30,1000]]),'low'],
  'classic-fast-gap-before-tail': [make(30000,[[28,1000],[1,500],[1,100]]),'high'],
  // Decode at 30 FPS, but presentation starts at ticks 0,1,2,3 before the long pause.
  'composition-local-60-fps-burst': [make(60,[[8,2]],[[1,0],[1,-1],[1,-2],[1,-3],[4,4]],1),'high'],
  'actual-60-coarse-clock': [make(1000,[[1,16],[2,17]]),'high'],
  'unit-trimmed-v0': [edited(edit(0,[120,40,0x10000])),'low'],
  'unit-with-delay-v1': [edited(edit(1,[30,-1,0x10000],[120,40,0x10000])),'low'],
  'edit-double-rate': [edited(edit(0,[80,0,0x20000])),'unknown'],
  'edit-fractional-rate-v1': [edited(edit(1,[120,0,0x14000])),'unknown'],
  'edit-dwell': [edited(edit(0,[160,0,0])),'unknown'],
  'edit-joined-segments': [edited(edit(0,[60,0,0x10000],[60,40,0x10000])),'unknown'],
  'edit-no-media': [edited(edit(0,[30,-1,0x10000])),'unknown'],
  'edit-invalid-media-time': [edited(edit(1,[120,-2,0x10000])),'unknown'],
  'edit-count-truncated': [edited(box('edts',box('elst',words(0,2,120,40,0x10000)))),'unknown'],
  'edit-duplicates': [edited(edit(0,[120,40,0x10000]),edit(0,[120,40,0x10000])),'unknown'],
  'classic-open-ended-edit': [edited(edit(0,[0,40,0x10000])),'unknown'],
  'fragment-open-ended-edit': [fragmentsWithEdits([edit(0,[0,40,0x10000])],segment({}),segment({base:80n})),'low'],
  'fragment-open-ended-fast': [fragmentsWithEdits([edit(1,[0,16,0x10000])],segment({tfhdDuration:16})),'high'],
  'fragment-open-ended-double-rate': [fragmentsWithEdits([edit(0,[0,40,0x20000])],segment({})),'unknown'],
  'fragment-open-ended-discontinuity': [fragmentsWithEdits([edit(0,[0,40,0x10000])],
    segment({}),segment({base:100n})),'unknown'],
  'fragment-defaults-25': [fragmented(segment({}),segment({base:80n})),'low'],
  'fragment-tfhd-fast': [fragmented(segment({tfhdDuration:16})),'high'],
  'fragment-hidden-burst': [fragmented(segment({offsets:[0,20,0,20]})),'high'],
  'fragment-local-60-fps-cross-boundary': [fragmentTrack(60,[],
    segment({durations:[4,4,4,1]}),segment({base:13n,durations:[1,1,1,4,4,4]})),'high'],
  'fragment-quantized-30-cross-boundary': [fragmented(
    segment({durations:[33,33]}),segment({base:66n,durations:[34,33,33,34]})),'low'],
  'fragment-30-short-tail': [fragmentTrack(30000,[],segment({durations:[1000,1000,100]})),'low'],
  'fragment-signed-cross-boundary': [fragmented(segment({offsets:[0,40],version:1}),
    segment({base:80n, offsets:[-40,0],version:1})),'low'],
  'fragment-short-decode-only': [fragmented(segment({durations:[20,60,20,60],offsets:[0,20,0,20]})),'low'],
  'fragment-duplicate-display': [fragmented(segment({offsets:[40,0,0,0]})),'unknown'],
  'fragment-late-decode-base': [fragmented(segment({}),segment({base:100n})),'unknown'],
  'fragment-overlap-decode-base': [fragmented(segment({}),segment({base:40n})),'unknown'],
  'fragment-missing-decode-base': [fragmented(segment({omitTfdt:true})),'unknown'],
  'fragment-wide-initial-base': [fragmented(segment({base:0x1000000000n,tfdtVersion:1,offsets:[0xffffffff,0xffffffff]}),
    segment({base:0x1000000050n,tfdtVersion:1,offsets:[0xffffffff,0xffffffff]})),'low'],
  'fragment-truncated-composition': [fragmented(segment({offsets:[0,1],malformedRun:true})),'unknown'],
};
for(const [name,[bytes,expected]] of Object.entries(work)){
  const path=join(dir,name+'.mp4');writeFileSync(path,bytes);console.log(JSON.stringify({path,expected}));
}

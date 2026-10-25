from pathlib import Path
p=Path(r"G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\tools\monitor_fast_cache_selftest.mjs")
s=p.read_text(encoding='utf-8')
old=''' const hasFast=source.includes('static void monitorSnapshotPublishFrames(');
 const fast=hasFast?extract(source,'static void monitorSnapshotPublishFrames('):'';
 return `namespace ${name} {\\nstatic std::mutex g_monitorSnapshotMx;\\nstatic std::atomic<unsigned long long> g_monitorSnapshotOversizeDrops{0};\\n${extract(source,'struct MonitorSnapshotState')} ;\\nstatic MonitorSnapshotState g_monitorSnapshot;\\nstatic std::function<void()> barrier;\\nstatic void monitorTestBarrierPause(int){if(barrier)barrier();}\\n${functions}\\n${fast}\\nstatic bool laneTop=true,laneHealthy=true;\\nstatic void fast(std::uint64_t generation,long long at,std::string frame){${hasFast?'monitorSnapshotPublishFrames(generation,at,std::move(frame));':'monitorSnapshotPublish(generation,laneTop,true,laneHealthy,at,g_monitorSnapshot.topText,std::move(frame));'}}\\n}\\n`;'''
new=''' const hasFast=source.includes('static void monitorSnapshotPublishFrames(');
 const fast=hasFast?extract(source,'static void monitorSnapshotPublishFrames('):'';
 const hasBorrowed=source.includes('static void monitorSnapshotPublishFramesBorrowed(');
 const borrowed=hasBorrowed?extract(source,'static void monitorSnapshotPublishFramesBorrowed('):'';
 const call=hasBorrowed?'monitorSnapshotPublishFramesBorrowed(generation,at,frame);':hasFast?'monitorSnapshotPublishFrames(generation,at,std::move(frame));':'monitorSnapshotPublish(generation,laneTop,true,laneHealthy,at,g_monitorSnapshot.topText,std::move(frame));';
 return `namespace ${name} {\\nstatic std::mutex g_monitorSnapshotMx;\\nstatic std::atomic<unsigned long long> g_monitorSnapshotOversizeDrops{0};\\n${extract(source,'struct MonitorSnapshotState')} ;\\nstatic MonitorSnapshotState g_monitorSnapshot;\\nstatic std::function<void()> barrier;\\nstatic void monitorTestBarrierPause(int){if(barrier)barrier();}\\n${functions}\\n${fast}\\n${borrowed}\\nstatic bool laneTop=true,laneHealthy=true;\\nstatic void fast(std::uint64_t generation,long long at,std::string frame){${call}}\\n}\\n`;'''
assert old in s
p.write_text(s.replace(old,new,1),encoding='utf-8')

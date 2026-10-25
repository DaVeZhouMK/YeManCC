from pathlib import Path
for rel in [r"tools/monitor_fast_cache_selftest.mjs",r"tools/monitor_fast_publish_gate_selftest.mjs"]:
 p=Path(r"G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC")/rel;s=p.read_text(encoding='utf-8')
 old='worker.match(/monitorSnapshotPublishFrames\\(/g)||[]).length===2'
 new='worker.match(/monitorSnapshotPublishFrames(?:Borrowed)?\\(/g)||[]).length===2'
 if old in s:s=s.replace(old,new,1)
 old2='fast.match(/monitorSnapshotPublishFrames\\(/g)||[]).length===2'
 new2='fast.match(/monitorSnapshotPublishFrames(?:Borrowed)?\\(/g)||[]).length===2'
 if old2 in s:s=s.replace(old2,new2,1)
 p.write_text(s,encoding='utf-8');print(rel,'patched',old in s or old2 in s)

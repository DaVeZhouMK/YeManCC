// Internal opt-in extension of usbip_upgrade_selftest.mjs. No product body is patched.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';

const core=path.resolve('G:/YeManCC-Work/Testing/YMCC');
const evidence=path.join(core,'Evidence');
const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const identity=file=>({path:file,bytes:fs.statSync(file).size,sha256:sha(fs.readFileSync(file))});
function unique(text,anchor) {
  // Only the known anchor's line endings vary; never normalize source text/bytes.
  const lf=anchor.replace(/\r\n/g,'\n');
  const variants=[...new Set([lf,lf.replace(/\n/g,'\r\n')])];
  const message='Missing/ambiguous exact anchor: '+anchor;
  assert(anchor.length>0,message);
  let match;
  for(const candidate of variants) {
    for(let index=text.indexOf(candidate);index>=0;index=text.indexOf(candidate,index+1)) {
      // A leading LF inside CRLF belongs to that same CRLF occurrence.
      if(candidate.startsWith('\n')&&index>0&&text[index-1]==='\r')continue;
      assert(!match,message); // Reject duplicates across both variants, including overlaps.
      match={index,anchor:candidate};
    }
  }
  assert(match,message);
  return match;
}
function executable(name) {
  for(const dir of (process.env.PATH||'').split(path.delimiter)) {
    const file=path.resolve(dir.replace(/^"|"$/g,''),name);
    if(fs.existsSync(file)&&fs.statSync(file).isFile())return fs.realpathSync.native(file);
  }
  throw new Error('Required executable not found: '+name);
}
function extract(file,name,startAnchor,endAnchor) {
  const bytes=fs.readFileSync(file),text=bytes.toString('utf8');
  assert(bytes.equals(Buffer.from(text,'utf8')),'Source must round-trip UTF-8');
  const first=unique(text,startAnchor),last=unique(text,endAnchor);
  const start=first.index,end=last.index;
  assert(end>start,'Invalid exact slice: '+name);
  const startByte=Buffer.byteLength(text.slice(0,start),'utf8'),endByte=Buffer.byteLength(text.slice(0,end),'utf8');
  const fragment=bytes.subarray(startByte,endByte);
  assert(fragment.equals(Buffer.from(text.slice(start,end),'utf8')));
  return {name,source:file,startAnchor:first.anchor,endAnchor:last.anchor,startByte,endByteExclusive:endByte,
    startLine:text.slice(0,start).split('\n').length,bytes:fragment.length,sha256:sha(fragment),fragment};
}

// All fixture JSON describes input data. Decisions, anchoring, moves and continuation
// are supplied by the exact production slices, never by a parallel implementation.
const harness=String.raw`
using System.Text;
using System.Text.Json;
using System.Security.Cryptography;
using State = YeManCC.UsbipMaintenanceState;

// Exact owned filesystem transports: ordinary calls forward unchanged to real
// Win32-backed .NET IO; only explicitly selected fault cases alter a receipt.
internal static class FixtureFiles
{
    internal static string Root="";internal static bool ShortWrite,FailFlush,FailPublish,CorruptAfterPublish;
    internal static void Check(string path,bool ancestors=false){var full=Path.GetFullPath(path);var root=Path.GetFullPath(Root).TrimEnd(Path.DirectorySeparatorChar);
        if(Root.Length==0||!(full==root||full.StartsWith(root+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase)||ancestors&&(root+Path.DirectorySeparatorChar).StartsWith(full.TrimEnd(Path.DirectorySeparatorChar)+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase)))throw new IOException("Fixture rejects path outside owned root");}
    internal static FileAttributes GetAttributes(string path){Check(path,true);return System.IO.File.GetAttributes(path);}
    internal static bool Exists(string path){Check(path);return System.IO.File.Exists(path);}
    internal static void Delete(string path){Check(path);System.IO.File.Delete(path);}
    internal static void WriteAllText(string path,string text){Check(path);System.IO.File.WriteAllText(path,text);}
    internal static void Move(string source,string target,bool overwrite){Check(source);Check(target);
        if(FailPublish&&source.EndsWith(".tmp",StringComparison.Ordinal))throw new IOException("fixture-publish-failed");
        System.IO.File.Move(source,target,overwrite);
        if(CorruptAfterPublish&&source.EndsWith(".tmp",StringComparison.Ordinal)&&Path.GetFileName(target)=="pending.json")System.IO.File.WriteAllText(target,"{after-publication");
    }
}
internal sealed class FixtureFileStream : System.IO.FileStream
{
    internal FixtureFileStream(string path,FileMode mode,FileAccess access,FileShare share):base(Checked(path),mode,access,share){}
    private static string Checked(string path){FixtureFiles.Check(path);return path;}
    public override void Write(ReadOnlySpan<byte> bytes){if(FixtureFiles.ShortWrite&&bytes.Length>1)base.Write(bytes[..(bytes.Length/2)]);else base.Write(bytes);}
    public override void Flush(bool flushToDisk){base.Flush(flushToDisk);if(FixtureFiles.FailFlush)throw new IOException("fixture-flush-failed");}
}
internal static class Program
{
    private static readonly string V1=State.BootSessionPrefix+"{11111111-1111-4111-8111-111111111111}";
    private static readonly string V2=State.BootSessionPrefix+"{22222222-2222-4222-8222-222222222222}";
    private const string Loader1="aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa", Loader2="bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";
    private static readonly string Core=new('a',64);
    private static readonly Version Target=new("1.2.3.4");
    private static int checks;
    private static void Need(bool value,string assertion) { if(!value)throw new InvalidOperationException("ASSERT:"+assertion); checks++; }
    private static void Bytes(string file,byte[] expected,string assertion) => Need(File.Exists(file)&&File.ReadAllBytes(file).SequenceEqual(expected),assertion);
    private static byte[] Record(object? session,bool legacy=false,string? core=null,string? target=null,string operation="usbip-uninstall")
    {
        var row=new Dictionary<string,object?> { ["schemaVersion"]=3,["operation"]=operation,["coreSha256"]=core??Core,
            ["targetVersion"]=target??Target.ToString(),["bootIdentifier"]=Loader1,["bootSequence"]=7,["uptimeMs"]=0,["opaque"]="  preserved input  " };
        if(!legacy)row["bootSessionIdentity"]=session;
        var text=JsonSerializer.Serialize(row,new JsonSerializerOptions { WriteIndented=true }).Replace("\n","\r\n")+"\r\n";
        return Encoding.UTF8.GetPreamble().Concat(Encoding.UTF8.GetBytes(text)).ToArray();
    }
    private sealed class Files : IDisposable
    {
        internal readonly string Dir,P,A,Previous,Completed,Backup,AttemptBackup,Sentinel,PreviousBlocker;
        internal readonly byte[] SentinelBytes=Encoding.UTF8.GetBytes("unrelated sentinel\r\n");
        private readonly (string Path,byte[] Content)[] leftovers;
        private bool ownsPreviousDirectory;
        internal Files(string root,string name)
        {
            Dir=Path.Combine(root,name); if(Directory.Exists(Dir)||File.Exists(Dir))throw new IOException("Case must be fresh: "+Dir);
            Directory.CreateDirectory(Dir); P=Path.Combine(Dir,"pending.json"); A=Path.Combine(Dir,"attempt.json");
            Previous=P+".previous-boot"; Completed=Previous+".completed"; Backup=P+".before-boot-session-v2";
            AttemptBackup=A+".before-boot-session-v2"; Sentinel=Path.Combine(Dir,"unrelated.keep");
            PreviousBlocker=Path.Combine(Previous,"unrelated.keep");
            // Orphaned atomic-write names and valid-looking unrelated records are inert.
            // Every scenario checks them before fixture-owned, exact-path cleanup.
            leftovers=new (string Path,byte[] Content)[] {
                (P+".11111111111141118111111111111111.tmp",Record(V1)),
                (A+".22222222222242228222222222222222.tmp",Record(null,legacy:true)),
                (Previous+".33333333333343338333333333333333.tmp",Record(V1,operation:"usbip-install")),
                (Completed+".44444444444444448444444444444444.tmp",Record(V1)),
                (Path.Combine(Dir,"unrelated.tmp"),SentinelBytes),
                (Path.Combine(Dir,"unrelated-transaction.json"),Record(V1,operation:"usbip-install"))
            };
            File.WriteAllBytes(Sentinel,SentinelBytes);
            foreach(var leftover in leftovers)File.WriteAllBytes(leftover.Path,leftover.Content);
            State.Configure(Dir,Guid.Parse(Loader1),7,V2);
        }
        internal void Pending(byte[] bytes)=>File.WriteAllBytes(P,bytes);
        internal void Retire(bool prepare=true)=>State.RetireConfirmedPreviousBoot(prepare);
        internal bool Complete()=>UsbipUpgrade.WindowsRuntime.CompletePreviousBootRecord(P,Core,Target,State.CurrentBootIdentifier,State.CurrentBootSequence,State.CurrentBootSessionIdentity);
        internal void Retained(byte[] bytes,string assertion)
        {
            Bytes(P,bytes,assertion); Need(!File.Exists(Previous)&&!File.Exists(Completed)&&!File.Exists(Backup),assertion+"_NO_MOVE_OR_BASELINE");
        }
        internal void UnrelatedPreserved()
        {
            Bytes(Sentinel,SentinelBytes,"UNRELATED_SENTINEL_PRESERVED");
            foreach(var leftover in leftovers)Bytes(leftover.Path,leftover.Content,"ORPHAN_NEVER_CONSUMED_OR_DELETED:"+Path.GetFileName(leftover.Path));
        }
        internal void BlockPreviousDirectory()
        {
            Need(!File.Exists(Previous)&&!Directory.Exists(Previous),"DIRECTORY_BLOCKER_FRESH");
            Directory.CreateDirectory(Previous); ownsPreviousDirectory=true;
            File.WriteAllBytes(PreviousBlocker,SentinelBytes);
        }
        internal void ReleasePreviousDirectory()
        {
            Need(ownsPreviousDirectory&&Directory.Exists(Previous),"DIRECTORY_BLOCKER_STILL_PRESENT");
            Ordinary(Previous); Bytes(PreviousBlocker,SentinelBytes,"DIRECTORY_BLOCKER_SENTINEL_PRESERVED");
            DeleteOwnedFile(PreviousBlocker); Directory.Delete(Previous,recursive:false); ownsPreviousDirectory=false;
        }
        private static void Ordinary(string entry)
        {
            if((File.GetAttributes(entry)&FileAttributes.ReparsePoint)!=0)throw new IOException("Unexpected reparse: "+entry);
        }
        private void DeleteOwnedFile(string file)
        {
            Need(Path.GetFullPath(file).StartsWith(Path.GetFullPath(Dir)+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase),"CLEANUP_STRICTLY_INSIDE_OWNED_CASE");
            if(!File.Exists(file))return;
            Ordinary(file); File.Delete(file);
        }
        public void Dispose()
        {
            // Delete only exact paths created by this case; never recursive or wildcard cleanup.
            Ordinary(Dir);
            foreach(var source in new[]{Previous,Completed}) {
                var prefix=source+".ymcc-history-";
                foreach(var history in System.IO.Directory.EnumerateFiles(Dir).Where(path=>path.StartsWith(prefix,StringComparison.Ordinal)&&path.EndsWith(".json",StringComparison.Ordinal))) {
                    Ordinary(history);var bytes=File.ReadAllBytes(history);var digest=Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(bytes)).ToLowerInvariant();
                    Need(history==prefix+digest+".json","OWNED_HISTORY_DIGEST_VERIFIED");DeleteOwnedFile(history);
                }
            }
            foreach(var source in new[]{P,A}) {
                var prefix=source+".ymcc-before-";
                foreach(var backup in System.IO.Directory.EnumerateFiles(Dir).Where(path=>path.StartsWith(prefix,StringComparison.Ordinal)&&path.EndsWith(".bak",StringComparison.Ordinal))) {
                    Ordinary(backup);var bytes=File.ReadAllBytes(backup);var digest=Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(bytes)).ToLowerInvariant();
                    Need(backup==prefix+digest+".bak","OWNED_PREIMAGE_DIGEST_VERIFIED");DeleteOwnedFile(backup);
                }
            }
            if(ownsPreviousDirectory) {
                Ordinary(Previous); DeleteOwnedFile(PreviousBlocker);
                Directory.Delete(Previous,recursive:false); ownsPreviousDirectory=false;
            }
            foreach(var file in new[]{P,A,Previous,Completed,Backup,AttemptBackup,Sentinel}.Concat(leftovers.Select(item=>item.Path)))DeleteOwnedFile(file);
            Directory.Delete(Dir,recursive:false);
        }
    }
    private static void Normal(Files f)
    {
        var original=Record(V1); f.Pending(original); f.Retire();
        Need(!File.Exists(f.P),"DIFFERENT_V2_PENDING_RETIRED"); Bytes(f.Previous,original,"DIFFERENT_V2_MOVE_BYTES");
        Need(State.LastTransition=="previous-boot-proven;normal-continuation"&&State.AclCalls==1,"DIFFERENT_V2_TRANSITION");
        f.Retire(); Bytes(f.Previous,original,"REPEAT_RETIRE_NO_DUPLICATE"); Need(State.AclCalls==1,"REPEAT_NO_EXTRA_ACL");
        Need(f.Complete(),"NORMAL_REAL_ARCHIVE"); Bytes(f.Completed,original,"NORMAL_ARCHIVE_BYTES");
        Need(!f.Complete()&&!File.Exists(f.Previous)&&!File.Exists(f.P),"REPEAT_ARCHIVE_NO_DUPLICATE");
        Bytes(f.Sentinel,f.SentinelBytes,"NORMAL_SENTINEL");
    }
    private static void Blocked(Files f,object? recorded,string? current,string assertion)
    {
        var original=Record(recorded); f.Pending(original); State.CurrentBootIdentifier=Guid.Parse(Loader2); State.CurrentBootSequence=8;
        State.CurrentBootSessionIdentity=current; f.Retire(); f.Retained(original,assertion);
        Need(State.LastTransition==(recorded is not string valid || !State.ValidBootSession(valid)?"record-transition-failed;fence-retained":"same-or-unknown-boot;fence-retained")&&State.AclCalls==0,"BLOCKED_TRANSITION");
        Need(!f.Complete(),"BLOCKED_NO_ARCHIVE");
    }
    private static void Legacy(Files f)
    {
        var original=Record(null,legacy:true); f.Pending(original); File.WriteAllBytes(f.A,original);
        State.CurrentBootSessionIdentity=V1; f.Retire();
        Need(State.LastTransition=="legacy-baselined;one-real-reboot-still-required"&&State.AclCalls==2,"LEGACY_BASELINE_ONLY");
        Bytes(f.Backup,original,"LEGACY_BOM_CRLF_BACKUP"); Bytes(f.AttemptBackup,original,"ATTEMPT_EXACT_BACKUP");
        var baseline=File.ReadAllBytes(f.P); var attemptBaseline=File.ReadAllBytes(f.A);
        using(var doc=JsonDocument.Parse(baseline)) {
            Need(doc.RootElement.GetProperty("bootSessionIdentity").GetString()==V1,"LEGACY_CURRENT_V2_ADDED");
            Need(doc.RootElement.GetProperty("bootSessionEvidence").GetString()=="legacy-baseline-only;no-prior-reboot-inferred","LEGACY_EVIDENCE");
        }
        Need(!baseline.SequenceEqual(original)&&!File.Exists(f.Previous)&&!f.Complete(),"LEGACY_NOT_PRIOR_REBOOT");
        State.CurrentBootIdentifier=Guid.Parse(Loader2); State.CurrentBootSequence=8; f.Retire();
        Bytes(f.P,baseline,"LEGACY_SAME_V2_STILL_BLOCKED"); Bytes(f.A,attemptBaseline,"ATTEMPT_NOT_REANCHORED");
        Need(!File.Exists(f.Previous)&&State.AclCalls==2,"LEGACY_NO_DUPLICATE_MIGRATION");
        State.CurrentBootSessionIdentity=V2; f.Retire(); Bytes(f.Previous,baseline,"LEGACY_NEXT_V2_MOVE");
        Need(f.Complete(),"LEGACY_NEXT_V2_ARCHIVE"); Bytes(f.Completed,baseline,"LEGACY_ARCHIVE_BASELINE_BYTES");
        Bytes(f.Backup,original,"LEGACY_BACKUP_REMAINS_ORIGINAL"); Bytes(f.AttemptBackup,original,"ATTEMPT_BACKUP_REMAINS");
    }
    private static void EarlyReject(Files f,bool oversize)
    {
        var original=oversize?Encoding.UTF8.GetBytes(new string(' ',4097)):Encoding.UTF8.GetPreamble().Concat(Encoding.UTF8.GetBytes("{broken\r\n")).ToArray();
        var attempt=Record(null,legacy:true); f.Pending(original); File.WriteAllBytes(f.A,attempt); f.Retire();
        f.Retained(original,oversize?"OVERSIZE_RETAINED":"BAD_JSON_RETAINED"); Bytes(f.A,attempt,"EARLY_RETURN_SKIPS_ATTEMPT");
        Need(!File.Exists(f.AttemptBackup)&&State.AclCalls==0,"EARLY_RETURN_NO_BASELINE");
        Need(State.LastTransition==(oversize?"pending-oversize;fence-retained":"record-transition-failed;fence-retained"),"EARLY_RETURN_TRANSITION");
    }
    private static void WrongIdentity(Files f,bool wrongCore)
    {
        var original=Record(V1,core:wrongCore?new string('b',64):Core,target:wrongCore?Target.ToString():"9.8.7.6");
        f.Pending(original); f.Retire(); Need(!File.Exists(f.P),"WRONG_IDENTITY_STILL_RETIRES_BY_BOOT");
        Bytes(f.Previous,original,"WRONG_IDENTITY_PREVIOUS_BYTES"); Need(!f.Complete(),"CONTINUATION_REFUSES_WRONG_IDENTITY");
        Bytes(f.Previous,original,"WRONG_IDENTITY_PREVIOUS_RETAINED"); Need(!File.Exists(f.Completed),"WRONG_IDENTITY_NOT_COMPLETED");
    }
    private static FileStream Exclusive(string file)=>new(file,FileMode.Open,FileAccess.Read,FileShare.None);
    private static void SharingViolation(Action read,string assertion)
    {
        bool blocked=false;
        try { read(); } catch(IOException error) when((error.HResult&0xffff) is 32 or 33) { blocked=true; }
        Need(blocked,assertion);
    }
    private static void FailedRetirement(Files f,byte[] original,byte[] attempt,string assertion)
    {
        Bytes(f.P,original,assertion+"_PENDING_ORIGINAL_BYTES"); Bytes(f.A,attempt,assertion+"_ATTEMPT_UNTOUCHED");
        Need(!File.Exists(f.Completed)&&!File.Exists(f.Backup)&&!File.Exists(f.AttemptBackup),assertion+"_NO_ARCHIVE_OR_BASELINE");
        Need(State.BootCalls==1&&State.AclCalls==1&&State.LastTransition=="record-transition-failed;fence-retained",assertion+"_MOVE_FAILED");
        f.UnrelatedPreserved();
    }
    private static void RetryRetirement(Files f,byte[] original,byte[] attempt,string assertion)
    {
        int priorBootCalls=State.BootCalls;f.Retire(); Bytes(f.Previous,original,assertion+"_RETRY_MOVES_ORIGINAL_BYTES");
        Need(!File.Exists(f.P)&&State.BootCalls==priorBootCalls+1&&State.LastTransition=="previous-boot-proven;normal-continuation",assertion+"_RETRY_CONTINUES");
        Bytes(f.AttemptBackup,attempt,assertion+"_ATTEMPT_BASELINED_ONLY_AFTER_RETRY");
        Need(f.Complete(),assertion+"_RETRY_COMPLETES"); Bytes(f.Completed,original,assertion+"_COMPLETED_ORIGINAL_BYTES");
        Need(!File.Exists(f.Previous)&&!f.Complete(),assertion+"_NO_DUPLICATE_COMPLETION"); f.UnrelatedPreserved();
    }
    private static void PendingReadLocked(Files f)
    {
        var original=Record(V1); var attempt=Record(null,legacy:true); f.Pending(original); File.WriteAllBytes(f.A,attempt);
        using(Exclusive(f.P)) {
            SharingViolation(()=>{ File.ReadAllText(f.P); },"PENDING_REAL_EXCLUSIVE_READ_FAILURE");
            f.Retire();
            Need(File.Exists(f.P)&&!File.Exists(f.Previous)&&!File.Exists(f.Completed)&&!File.Exists(f.Backup),"READ_LOCK_NO_MOVE_OR_BASELINE");
            Bytes(f.A,attempt,"READ_LOCK_ATTEMPT_UNTOUCHED");
            Need(!File.Exists(f.AttemptBackup)&&State.BootCalls==1&&State.AclCalls==0&&State.LastTransition=="record-transition-failed;fence-retained","READ_LOCK_TRANSITION");
            Need(!f.Complete(),"READ_LOCK_NO_CONTINUATION"); f.UnrelatedPreserved();
        }
        f.Retained(original,"READ_LOCK_RELEASED_ORIGINAL_BYTES");
        RetryRetirement(f,original,attempt,"READ_LOCK");
    }
    private static void PreviousDestinationBlocked(Files f,bool directory)
    {
        var original=Record(V1,operation:directory?"usbip-uninstall":"usbip-install");
        var attempt=Record(null,legacy:true); f.Pending(original); File.WriteAllBytes(f.A,attempt);
        if(directory) {
            f.BlockPreviousDirectory(); f.Retire(); FailedRetirement(f,original,attempt,"PREVIOUS_DIRECTORY");
            Need(!File.Exists(f.Previous)&&Directory.Exists(f.Previous)&&!f.Complete(),"PREVIOUS_DIRECTORY_NOT_A_TRANSACTION");
            f.ReleasePreviousDirectory();
        } else {
            File.WriteAllBytes(f.Previous,f.SentinelBytes);
            using(Exclusive(f.Previous)) {
                SharingViolation(()=>{ File.ReadAllText(f.Previous); },"PREVIOUS_REAL_EXCLUSIVE_LOCK");
                f.Retire(); FailedRetirement(f,original,attempt,"PREVIOUS_FILE_LOCK");
                Need(File.Exists(f.Previous)&&!f.Complete(),"PREVIOUS_FILE_LOCK_NO_CONTINUATION");
            }
            Bytes(f.Previous,f.SentinelBytes,"PREVIOUS_FILE_LOCK_DESTINATION_PRESERVED");
            f.Retire();Bytes(f.P,original,"UNKNOWN_PREVIOUS_REMAINS_BLOCKING_AFTER_UNLOCK");Bytes(f.Previous,f.SentinelBytes,"UNKNOWN_PREVIOUS_NO_SILENT_OVERWRITE");File.Delete(f.Previous);
        }
        // Retry only after the fixture explicitly releases its unrelated blocker.
        RetryRetirement(f,original,attempt,directory?"PREVIOUS_DIRECTORY":"PREVIOUS_FILE_LOCK");
    }
    private static void CompletedDestinationLocked(Files f)
    {
        var original=Record(V1,operation:"usbip-install"); f.Pending(original); f.Retire();
        Bytes(f.Previous,original,"COMPLETED_LOCK_PREVIOUS_READY"); File.WriteAllBytes(f.Completed,f.SentinelBytes);
        using(Exclusive(f.Completed)) {
            SharingViolation(()=>{ File.ReadAllText(f.Completed); },"COMPLETED_REAL_EXCLUSIVE_LOCK");
            Need(!f.Complete(),"COMPLETED_LOCK_RETURNS_FALSE"); Bytes(f.Previous,original,"COMPLETED_LOCK_PREVIOUS_ORIGINAL_BYTES");
            Need(!File.Exists(f.P)&&File.Exists(f.Completed),"COMPLETED_LOCK_NO_PENDING_RECREATED"); f.UnrelatedPreserved();
        }
        Bytes(f.Completed,f.SentinelBytes,"COMPLETED_LOCK_DESTINATION_PRESERVED");
        Bytes(f.Previous,original,"COMPLETED_LOCK_RELEASED_PREVIOUS_ORIGINAL_BYTES");
        Need(!f.Complete(),"UNKNOWN_COMPLETED_REMAINS_BLOCKING_AFTER_UNLOCK");Bytes(f.Completed,f.SentinelBytes,"UNKNOWN_COMPLETED_NO_SILENT_OVERWRITE");File.Delete(f.Completed);
        Need(f.Complete(),"COMPLETED_LOCK_RELEASE_RETRY_SUCCEEDS"); Bytes(f.Completed,original,"COMPLETED_LOCK_RETRY_ORIGINAL_BYTES");
        Need(!File.Exists(f.Previous)&&!File.Exists(f.P)&&!f.Complete(),"COMPLETED_LOCK_RETRY_NO_DUPLICATE");
        f.UnrelatedPreserved();
    }
    private static void OrphanTemporaries(Files f)
    {
        // Matching JSON in .tmp files does not authorize manufacture of either transaction.
        Need(!f.Complete(),"ORPHANS_NO_PREVIOUS_CONTINUATION"); f.Retire();
        Need(State.LastTransition=="no-pending-fence"&&State.BootCalls==1&&State.AclCalls==0,"ORPHANS_NO_RETIREMENT_PORTS");
        Need(!File.Exists(f.P)&&!File.Exists(f.A)&&!File.Exists(f.Previous)&&!File.Exists(f.Completed)&&!File.Exists(f.Backup)&&!File.Exists(f.AttemptBackup),"ORPHANS_NEVER_PROMOTED");
        f.UnrelatedPreserved();
        var original=Record(null,legacy:true); f.Pending(original); State.CurrentBootSessionIdentity=V1; f.Retire();
        Bytes(f.Backup,original,"ORPHANS_LEGACY_EXACT_BACKUP"); f.UnrelatedPreserved();
        var baseline=File.ReadAllBytes(f.P); State.CurrentBootSessionIdentity=V2; f.Retire();
        Bytes(f.Previous,baseline,"ORPHANS_ONLY_CANONICAL_PENDING_MOVED");
        Need(f.Complete(),"ORPHANS_ONLY_CANONICAL_PREVIOUS_COMPLETED"); Bytes(f.Completed,baseline,"ORPHANS_CANONICAL_COMPLETED_BYTES");
        f.Retire(); Need(State.LastTransition=="no-pending-fence"&&!f.Complete(),"ORPHANS_NOT_REPLAYED_AFTER_COMPLETION");
        Need(!File.Exists(f.P)&&!File.Exists(f.A)&&!File.Exists(f.Previous)&&!File.Exists(f.AttemptBackup),"ORPHANS_NOT_RECREATED_AFTER_COMPLETION");
        f.UnrelatedPreserved();
    }

    private static IEnumerable<(string Name,Action<Files> Body)> StormCases()
    {
        byte[] Changed(string key,string json) {
            var text=Encoding.UTF8.GetString(Record(V1)).TrimStart('\uFEFF');
            var row=System.Text.Json.Nodes.JsonNode.Parse(text)!.AsObject();row[key]=System.Text.Json.Nodes.JsonNode.Parse(json);
            return Encoding.UTF8.GetBytes(row.ToJsonString());
        }
        Action<Files> Held(byte[] raw)=>f=>{
            f.Pending(raw);bool pending=false;Exception? error=null;try{pending=State.Pending;}catch(Exception e){error=e;}
            f.Retire();Need(error is null&&pending,"STORM_BAD_RECORD_NEVER_RELEASES_FENCE");f.Retained(raw,"STORM_BAD_ORIGINAL_PRESERVED");Need(!f.Complete(),"STORM_BAD_NEVER_COMPLETES");
        };
        foreach(var (key,values) in new (string,string[])[] {
            ("schemaVersion",new[]{"4","99","3.0","\"3\"","true","null","-1"}),
            ("operation",new[]{"true","null","[]","\"unknown\""}),
            ("coreSha256",new[]{"true","null","12","\"short\""}),
            ("targetVersion",new[]{"false","null","[]","\"not-a-version\""}),
            ("uptimeMs",new[]{"-1","1.5","true","null","\"0\""}),
            ("bootSequence",new[]{"-1","4294967296","3.0","false","null"}),
            ("bootIdentifier",new[]{"null","false","\"invalid\"","\"00000000-0000-0000-0000-000000000000\""})
        })foreach(var json in values)yield return("storm-invalid-"+key+"-"+json.Replace('"','_'),Held(Changed(key,json)));
        foreach(var key in new[]{"coreSha256","targetVersion","operation"}) {
            var row=System.Text.Json.Nodes.JsonNode.Parse(Encoding.UTF8.GetString(Record(V1)).TrimStart('\uFEFF'))!.AsObject();row.Remove(key);
            yield return("storm-missing-"+key,Held(Encoding.UTF8.GetBytes(row.ToJsonString())));
        }
        var original=Encoding.UTF8.GetString(Record(V1)).TrimStart('\uFEFF').Trim();
        foreach(var field in new[]{"\"schemaVersion\":99,","\"schema\\u0056ersion\":99,","\"opaque\":{\"a\":1,\"a\":2},"})
            yield return("storm-duplicate-"+Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(Encoding.UTF8.GetBytes(field)))[..8],Held(Encoding.UTF8.GetBytes(original.Insert(1,field))));
        var nested="0";for(int i=0;i<24;++i)nested="{\"next\":"+nested+"}";
        yield return("storm-depth-boundary",Held(Encoding.UTF8.GetBytes(original[..^1]+",\"unknown\":"+nested+"}")));
        var prefix=Encoding.UTF8.GetBytes(original[..^1]+",\"badText\":\"");var suffix=Encoding.UTF8.GetBytes("\"}");
        yield return("storm-invalid-UTF8",Held(prefix.Concat(new byte[]{255}).Concat(suffix).ToArray()));
        foreach(var root in new[]{"[]","null","12","\"text\"","{}"})yield return("storm-pure-boot-"+root.Replace('"','_'),f=>{
            using var document=JsonDocument.Parse(root);bool held=false;Exception? error=null;try{held=State.RecordRequiresReboot(document.RootElement,State.CurrentBootIdentifier,State.CurrentBootSequence,0,V2);}catch(Exception e){error=e;}
            Need(error is null&&held,"STORM_PURE_BOOT_READER_NEVER_THROWS_OR_RELEASES_UNKNOWN");
        });
        yield return("storm-pending-path-is-directory",f=>{
            Directory.CreateDirectory(f.P);try{Need(State.Pending,"STORM_DIRECTORY_NOT_MISSING");f.Retire();Need(Directory.Exists(f.P)&&!File.Exists(f.Previous),"STORM_DIRECTORY_UNTOUCHED");}finally{Directory.Delete(f.P,recursive:false);}
        });
        yield return("storm-pending-parent-is-file",f=>{
            string blocker=Path.Combine(f.Dir,"parent-is-file");File.WriteAllBytes(blocker,f.SentinelBytes);
            try{State.Configure(Path.Combine(blocker,"child"),Guid.Parse(Loader1),7,V2);Need(State.Pending,"STORM_FILE_PARENT_NOT_MISSING");}
            finally{State.Configure(f.Dir,Guid.Parse(Loader1),7,V2);Bytes(blocker,f.SentinelBytes,"STORM_BAD_PARENT_RETAINED");File.Delete(blocker);}
        });
        foreach(var encoding in new[]{Encoding.Unicode,Encoding.BigEndianUnicode})yield return("storm-supported-"+encoding.WebName,f=>{
            var raw=encoding.GetPreamble().Concat(encoding.GetBytes(original)).ToArray();f.Pending(raw);Need(!State.Pending,"STORM_VALID_UTF16_BOOT_RECOGNIZED");f.Retire();Bytes(f.Previous,raw,"STORM_UTF16_MOVE_EXACT");Need(f.Complete(),"STORM_UTF16_CONTINUATION");Bytes(f.Completed,raw,"STORM_UTF16_COMPLETED_EXACT");
        });

        foreach(var kind in new[]{"bad","future","locked","directory"})yield return("storm-writer-protect-"+kind,f=>{
            var raw=kind=="future"?Changed("schemaVersion","99"):kind=="bad"?Encoding.UTF8.GetBytes("{broken"):Record(V1);
            if(kind=="directory")Directory.CreateDirectory(f.P);else f.Pending(raw);
            using var held=kind=="locked"?Exclusive(f.P):null;bool refused=false;
            using var parsed=JsonDocument.Parse(Encoding.UTF8.GetString(Record(V2)).TrimStart('\uFEFF'));
            try{State.WriteRecord(f.P,parsed.RootElement);}catch{refused=true;}
            Need(refused,"STORM_WRITER_UNKNOWN_OR_IO_REFUSED");held?.Dispose();
            if(kind=="directory"){Need(Directory.Exists(f.P),"STORM_DIRECTORY_NOT_OVERWRITTEN");Directory.Delete(f.P,recursive:false);}else Bytes(f.P,raw,"STORM_WRITER_ORIGINAL_RETAINED");
            Need(!System.IO.Directory.EnumerateFiles(f.Dir).Any(path=>Path.GetFileName(path).EndsWith(".tmp")&&!Path.GetFileName(path).Contains("11111111111141118111111111111111")&&!Path.GetFileName(path).Contains("22222222222242228222222222222222")&&!Path.GetFileName(path).Contains("33333333333343338333333333333333")&&!Path.GetFileName(path).Contains("44444444444444448444444444444444")&&Path.GetFileName(path)!="unrelated.tmp"),"STORM_OWNED_TEMP_RELEASED");
        });
        yield return("storm-writer-ready-preimage-and-extension",f=>{
            var original=Record(V1);f.Pending(original);using var next=JsonDocument.Parse("{\"schemaVersion\":3,\"operation\":\"usbip-install\",\"coreSha256\":\""+Core+"\",\"targetVersion\":\""+Target+"\",\"bootSessionIdentity\":\""+V2+"\"}");
            State.WriteRecord(f.P,next.RootElement);var current=State.ReadRecord(f.P,true);Need(current.Kind==State.RecordKind.Ready&&current.Value.GetProperty("opaque").GetString()=="  preserved input  ","STORM_WRITER_UNOWNED_EXTENSION_PRESERVED");
            var backup=f.P+".ymcc-before-"+Convert.ToHexString(SHA256.HashData(original)).ToLowerInvariant()+".bak";Bytes(backup,original,"STORM_WRITER_PREIMAGE_EXACT");
        });
        yield return("storm-writer-publication-lock-explicit-retry",f=>{
            var original=Record(V1);f.Pending(original);using var next=JsonDocument.Parse(Encoding.UTF8.GetString(Record(V2)).TrimStart('\uFEFF'));bool blocked=false;
            using(var held=new FileStream(f.P,FileMode.Open,FileAccess.Read,FileShare.Read)){try{State.WriteRecord(f.P,next.RootElement);}catch{blocked=true;}}
            Need(blocked,"STORM_WRITER_PUBLISH_LOCK_FAILS");Bytes(f.P,original,"STORM_PUBLISH_FAIL_ORIGINAL_RETAINED");State.WriteRecord(f.P,next.RootElement);Need(State.ReadRecord(f.P).Kind==State.RecordKind.Ready,"STORM_PUBLISH_LOCK_RELEASE_RETRY");
        });
        foreach(var json in new[]{"[]","null","{\"schemaVersion\":99}","{\"mutationStarted\":\"no\"}"})yield return("storm-invalid-output-"+Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(json)))[..8],f=>{
            var original=Record(V1);f.Pending(original);using var value=JsonDocument.Parse(json);bool refused=false;try{State.WriteRecord(f.P,value.RootElement);}catch{refused=true;}Need(refused,"STORM_INVALID_WRITER_PAYLOAD_REFUSED");Bytes(f.P,original,"STORM_INVALID_OUTPUT_PRESERVES_SOURCE");
        });
        yield return("storm-owned-completion-duplicate-cannot-clear",f=>{
            var raw="{\"schemaVersion\":99,\"schemaVersion\":3,\"operation\":\"usbip-install\",\"session\":\"owner\",\"coreSha256\":\""+Core+"\",\"targetVersion\":\""+Target+"\",\"bootSessionIdentity\":\""+V2+"\"}";var bytes=Encoding.UTF8.GetBytes(raw);f.Pending(bytes);bool refused=false;try{UsbipUpgrade.WindowsRuntime.CompleteOwnedInstallRecord(f.P,Core,Target,"owner",V2);}catch{refused=true;}Need(refused,"STORM_DUPLICATE_COMPLETION_REFUSED");Bytes(f.P,bytes,"STORM_DUPLICATE_COMPLETION_ORIGINAL_PRESERVED");
        });
        yield return("storm-owned-completion-retains-exact-record",f=>{
            var bytes=Encoding.UTF8.GetBytes("{\"schemaVersion\":3,\"operation\":\"usbip-install\",\"session\":\"owner\",\"coreSha256\":\""+Core+"\",\"targetVersion\":\""+Target+"\",\"bootSessionIdentity\":\""+V2+"\"}");f.Pending(bytes);
            Need(UsbipUpgrade.WindowsRuntime.CompleteOwnedInstallRecord(f.P,Core,Target,"owner",V2),"STORM_OWNED_COMPLETION_SUCCEEDS");var archive=f.P+".owned-completed-"+Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant()+".json";Bytes(archive,bytes,"STORM_COMPLETED_ORIGINAL_EXACT");Need(!File.Exists(f.P),"STORM_ACTIVE_PENDING_RETIRED");File.Delete(archive);
        });

        yield return("storm-healthy-history-does-not-permanently-block-next-upgrade",f=>{
            var current=Record(V1);var older=Record(V2);f.Pending(current);File.WriteAllBytes(f.Previous,older);File.WriteAllBytes(f.Completed,older);
            f.Retire();Bytes(f.Previous,current,"STORM_NEXT_PENDING_CAN_RETIRE");var hash=Convert.ToHexString(SHA256.HashData(older)).ToLowerInvariant();Bytes(f.Previous+".ymcc-history-"+hash+".json",older,"STORM_OLD_PREVIOUS_RETAINED");
            Need(f.Complete(),"STORM_NEXT_TRANSACTION_CAN_COMPLETE");Bytes(f.Completed,current,"STORM_NEW_COMPLETED_EXACT");Bytes(f.Completed+".ymcc-history-"+hash+".json",older,"STORM_OLD_COMPLETED_RETAINED");Need(!File.Exists(f.P)&&!File.Exists(f.Previous),"STORM_NO_ACTIVE_DEBT_FABRICATED");
        });

        foreach(var fault in new[]{"short","flush","publish"})yield return("storm-writer-io-"+fault+"-retry",f=>{
            var original=Record(V1);f.Pending(original);using var next=JsonDocument.Parse(Encoding.UTF8.GetString(Record(V2)).TrimStart('\uFEFF'));bool refused=false;
            try{FixtureFiles.ShortWrite=fault=="short";FixtureFiles.FailFlush=fault=="flush";FixtureFiles.FailPublish=fault=="publish";State.WriteRecord(f.P,next.RootElement);}catch{refused=true;}
            finally{FixtureFiles.ShortWrite=FixtureFiles.FailFlush=FixtureFiles.FailPublish=false;}
            Need(refused,"STORM_IO_FAULT_NEVER_ACKNOWLEDGES");Bytes(f.P,original,"STORM_IO_FAULT_KEEPS_ORIGINAL");State.WriteRecord(f.P,next.RootElement);Need(State.ReadRecord(f.P).Value.GetProperty("bootSessionIdentity").GetString()==V2,"STORM_IO_RELEASE_RETRY_SUCCEEDS");
        });
        yield return("storm-independent-postpublication-readback",f=>{
            var original=Record(V1);f.Pending(original);using var next=JsonDocument.Parse(Encoding.UTF8.GetString(Record(V2)).TrimStart('\uFEFF'));bool refused=false;
            try{FixtureFiles.CorruptAfterPublish=true;State.WriteRecord(f.P,next.RootElement);}catch{refused=true;}finally{FixtureFiles.CorruptAfterPublish=false;}
            Need(refused,"STORM_CORRUPT_POSTPUBLICATION_NOT_SUCCESS");Need(State.Pending,"STORM_CORRUPT_PUBLISHED_PENDING_REMAINS_FENCED");var digest=Convert.ToHexString(SHA256.HashData(original)).ToLowerInvariant();Bytes(f.P+".ymcc-before-"+digest+".bak",original,"STORM_POSTPUBLICATION_PREIMAGE_RETAINED");
        });

        foreach(var kind in new[]{"malformed","future","locked","directory"})yield return("storm-resume-unknown-"+kind+"-not-fresh-install",f=>{
            var original=kind=="future"?Changed("schemaVersion","99"):kind=="malformed"?Encoding.UTF8.GetBytes("{broken"):Record(V1);
            if(kind=="directory")Directory.CreateDirectory(f.Previous);else File.WriteAllBytes(f.Previous,original);
            using var held=kind=="locked"?Exclusive(f.Previous):null;bool refused=false;try{UsbipUpgrade.WindowsRuntime.PreviousBootMatches(f.Previous,Core,Target,State.CurrentBootIdentifier,State.CurrentBootSequence,V2);}catch{refused=true;}
            Need(refused,"STORM_UNKNOWN_RESUME_NEVER_FALLS_THROUGH_TO_FRESH_INSTALL");held?.Dispose();if(kind=="directory")Directory.Delete(f.Previous,recursive:false);else Bytes(f.Previous,original,"STORM_UNKNOWN_RESUME_ORIGINAL_RETAINED");
            File.WriteAllBytes(f.Previous,Record(V1));Need(UsbipUpgrade.WindowsRuntime.PreviousBootMatches(f.Previous,Core,Target,State.CurrentBootIdentifier,State.CurrentBootSequence,V2),"STORM_REPAIRED_RESUME_CAN_RETRY");
        });
        yield return("storm-malformed-replacement-retry",f=>{
            var raw=Encoding.UTF8.GetBytes("{broken");f.Pending(raw);Need(State.Pending,"STORM_BAD_BLOCKS");f.Retire();f.Retained(raw,"STORM_BAD_RETRY_PRESERVED");
            var valid=Record(V1);f.Pending(valid);Need(!State.Pending,"STORM_EXPLICIT_VALID_REPLACEMENT_RELOADS");f.Retire();Bytes(f.Previous,valid,"STORM_VALID_REPLACEMENT_CAN_CONTINUE");
        });
    }
    public static int Main(string[] args)
    {
        string active="selection"; string? firstFailed=null; var results=new List<object>(); Exception? failure=null; int selectedCount=0; var started=System.Diagnostics.Stopwatch.StartNew();
        try {
            Need(args.Length==1||(args.Length==3&&args[1]=="--only"),"EXACT_HARNESS_ARGUMENTS");
            FixtureFiles.Root=args[0];
            var cases=new (string Name,Action<Files> Body)[] {
                ("normal",Normal),
                ("same-v2",f=>Blocked(f,V1,V1,"SAME_V2_PENDING_PRESERVED")),
                ("missing-current-v2",f=>Blocked(f,V1,null,"MISSING_CURRENT_V2_RETAINED")),
                ("bad-current-v2",f=>Blocked(f,V1,"broken","BAD_CURRENT_V2_RETAINED")),
                ("bad-record-v2",f=>Blocked(f,"broken",V2,"BAD_RECORD_V2_RETAINED")),
                ("nonstring-record-v2",f=>Blocked(f,12,V2,"NONSTRING_RECORD_V2_RETAINED")),
                ("prepare-false",f=>{
                    var original=Record(null,legacy:true); f.Pending(original); File.WriteAllBytes(f.A,original);
                    State.BootPreparationAllowed=false; State.RejectAcl=true; f.Retire(false); f.Retained(original,"PREPARE_FALSE_BYTES");
                    Bytes(f.A,original,"PREPARE_FALSE_ATTEMPT"); Need(!File.Exists(f.AttemptBackup)&&State.BootCalls==0&&State.AclCalls==0&&State.LastTransition=="not-run","PREPARE_FALSE_NO_PORTS");
                }),
                ("boot-preparation-refused",f=>{
                    var original=Record(null,legacy:true); f.Pending(original); File.WriteAllBytes(f.A,original); State.BootPreparationAllowed=false; f.Retire();
                    f.Retained(original,"BOOT_UNAVAILABLE_RETAINED"); Bytes(f.A,original,"BOOT_UNAVAILABLE_SKIPS_ATTEMPT");
                    Need(State.BootCalls==1&&State.AclCalls==0&&!File.Exists(f.AttemptBackup)&&State.LastTransition=="boot-session-unavailable;fence-retained","BOOT_UNAVAILABLE_TRANSITION");
                }),
                ("no-pending",f=>{
                    f.Retire(); Need(!File.Exists(f.P)&&!File.Exists(f.A)&&!File.Exists(f.Previous)&&!File.Exists(f.Completed)&&!File.Exists(f.Backup)&&!File.Exists(f.AttemptBackup),"NO_PENDING_NOT_MANUFACTURED");
                    Need(State.AclCalls==0&&State.LastTransition=="no-pending-fence","NO_PENDING_TRANSITION");
                }),
                ("oversize",f=>EarlyReject(f,true)), ("bad-json",f=>EarlyReject(f,false)),
                ("acl-port-refused",f=>{
                    var original=Record(V1); var attempt=Record(null,legacy:true); f.Pending(original); File.WriteAllBytes(f.A,attempt); State.RejectAcl=true; f.Retire();
                    f.Retained(original,"ACL_REFUSAL_PRESERVES_BYTES"); Bytes(f.A,attempt,"ACL_EXCEPTION_SKIPS_ATTEMPT");
                    Need(!File.Exists(f.AttemptBackup)&&State.AclCalls==1&&State.LastTransition=="record-transition-failed;fence-retained","ACL_EXCEPTION_TRANSITION");
                }),
                ("legacy-baseline",Legacy),
                ("legacy-existing-backup",f=>{
                    f.Pending(Record(null,legacy:true)); File.WriteAllBytes(f.Backup,f.SentinelBytes); State.CurrentBootSessionIdentity=V1; f.Retire();
                    Bytes(f.Backup,f.SentinelBytes,"EXISTING_BACKUP_NEVER_OVERWRITTEN");
                    Need(!JsonDocument.Parse(File.ReadAllText(f.P)).RootElement.TryGetProperty("bootSessionIdentity",out _),"EXISTING_BACKUP_CONFLICT_PREVENTS_BASELINE");
                    File.Delete(f.Backup);f.Retire();using var doc=JsonDocument.Parse(File.ReadAllText(f.P));Need(doc.RootElement.GetProperty("bootSessionIdentity").GetString()==V1,"RELEASED_BACKUP_CONFLICT_ALLOWS_BASELINE");
                }),
                ("legacy-loader-fallback",f=>{
                    var original=Record(null,legacy:true); f.Pending(original); State.CurrentBootIdentifier=Guid.Parse(Loader2); f.Retire();
                    Bytes(f.Previous,original,"LEGACY_LOADER_FALLBACK_MOVE_BYTES"); Need(!File.Exists(f.Backup)&&!File.Exists(f.P),"LEGACY_FALLBACK_NO_BASELINE");
                    Need(f.Complete(),"LEGACY_LOADER_FALLBACK_ARCHIVE"); Bytes(f.Completed,original,"LEGACY_FALLBACK_ARCHIVE_BYTES");
                }),
                ("wrong-core",f=>WrongIdentity(f,true)), ("wrong-target",f=>WrongIdentity(f,false)),
                ("existing-destinations",f=>{
                    var original=Record(V1); f.Pending(original); File.WriteAllBytes(f.Previous,f.SentinelBytes); File.WriteAllBytes(f.Completed,f.SentinelBytes); File.WriteAllBytes(f.Backup,f.SentinelBytes);
                    f.Retire();Bytes(f.P,original,"PENDING_PRESERVED_ON_UNKNOWN_DESTINATION");Bytes(f.Previous,f.SentinelBytes,"UNKNOWN_PREVIOUS_PRESERVED");Bytes(f.Completed,f.SentinelBytes,"UNKNOWN_COMPLETED_PRESERVED");
                    Need(!f.Complete(),"UNKNOWN_PREVIOUS_CANNOT_COMPLETE");File.Delete(f.Previous);f.Retire();Bytes(f.Previous,original,"EXPLICIT_DESTINATION_RELEASE_RETRY");Need(!f.Complete(),"UNKNOWN_COMPLETED_STILL_BLOCKS");
                    File.Delete(f.Completed);Need(f.Complete(),"EXPLICIT_COMPLETED_RELEASE_RETRY");Bytes(f.Completed,original,"COMPLETED_ORIGINAL_BYTES");Bytes(f.Backup,f.SentinelBytes,"UNRELATED_BACKUP_PRESERVED");
                }),
                ("pending-read-locked",PendingReadLocked),
                ("previous-destination-locked",f=>PreviousDestinationBlocked(f,false)),
                ("previous-destination-directory",f=>PreviousDestinationBlocked(f,true)),
                ("completed-destination-locked",CompletedDestinationLocked),
                ("orphan-temporaries",OrphanTemporaries)
            }.Concat(StormCases()).ToArray();
            var selected=cases.Where(c=>args.Length==1||c.Name==args[2]).ToArray(); selectedCount=selected.Length; Need(selected.Length>0,"ZERO_SELECTION_REFUSED");
            Need(args.Length==1||selected.Length==1,"EXACT_SINGLE_NEGATIVE_CASE");
            foreach(var test in selected) {
                active=test.Name; var clock=System.Diagnostics.Stopwatch.StartNew();
                using(var f=new Files(args[0],active)) {
                    try { test.Body(f); f.UnrelatedPreserved(); results.Add(new { name=active,status="PASS",wallMs=clock.Elapsed.TotalMilliseconds }); }
                    catch(Exception error) {
                        failure??=error;firstFailed??=active;
                        var observed=new Dictionary<string,object?>();foreach(var file in new[]{f.P,f.Previous,f.Completed})try{observed[Path.GetFileName(file)]=File.Exists(file)?Convert.ToBase64String(File.ReadAllBytes(file)):null;}catch(Exception readError){observed[Path.GetFileName(file)]=readError.GetType().Name;}
                        results.Add(new { name=active,status="FAIL",error=error.ToString(),observed,wallMs=clock.Elapsed.TotalMilliseconds });
                    }
                }
            }
        } catch(Exception error) { failure=error; }
        Console.WriteLine(JsonSerializer.Serialize(new { status=failure is null?"PASS":"FAIL",checks,selectedCases=selectedCount,
            passedCases=results.Count(x=>x.GetType().GetProperty("status")!.GetValue(x)!.Equals("PASS")),results,failedCase=failure is null?null:firstFailed??active,error=failure?.ToString(),wallMs=started.Elapsed.TotalMilliseconds,
            hardwareIo=false,realAcl="NOT_RUN",bootProvider="declared fixture port; not real V2 provider" }));
        return failure is null?0:1;
    }
}
`;

export async function runPendingFileTransition({repo,out}) {
  const started=performance.now();
  assert.equal(process.platform,'win32','This bounded fixture requires Windows/.NET 10');
  assert(path.isAbsolute(out),'Output must be absolute'); out=path.resolve(out);
  const relative=path.relative(evidence,out);
  assert(relative&&!relative.startsWith('..'+path.sep)&&relative!=='..'&&!path.isAbsolute(relative),'Output must be strictly below core Evidence');
  assert(!fs.existsSync(out),'Output must be fresh and exclusively created by this invocation');
  assert(fs.statSync(path.dirname(out)).isDirectory(),'Create the Evidence parent before invocation');
  const powershell=executable('powershell.exe'),dotnet=executable('dotnet.exe');
  // Establish exclusive Evidence ownership before a child can use any temporary path.
  for(let dir=path.dirname(out);;dir=path.dirname(dir)) {
    const info=fs.lstatSync(dir); assert(info.isDirectory()&&!info.isSymbolicLink(),'Ordinary output ancestors required: '+dir);
    if(path.dirname(dir)===dir)break;
  }
  fs.mkdirSync(out); // atomic exclusive ownership, never adopt an existing empty directory
  const generated=path.join(out,'generated'),temp=path.join(out,'temp'),cases=path.join(out,'cases');
  for(const dir of [generated,temp,cases])fs.mkdirSync(dir);
  const preflight=spawnSync(powershell,['-NoProfile','-NonInteractive','-Command',
    "$ErrorActionPreference='Stop'; $p=Get-Item -LiteralPath $env:YMCC_FIXTURE_PARENT -Force; while($null -ne $p){if($p -isnot [IO.DirectoryInfo] -or (($p.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)){throw 'Ordinary directory without reparse ancestors required'}; $p=$p.Parent}"],
    {encoding:'utf8',windowsHide:true,timeout:90000,maxBuffer:1<<20,env:{...process.env,YMCC_FIXTURE_PARENT:out,TEMP:temp,TMP:temp,TMPDIR:temp}});
  if(preflight.error)throw preflight.error;
  assert.equal(preflight.status,0,'Output ancestor check failed: '+preflight.stdout+preflight.stderr);
  const preflightMs=performance.now()-started;
  const inputs=['native/UsbipMaintenanceState.cs','GamepadPrerequisites/UsbipUpgrade.cs','GamepadPrerequisites/UsbipUpgradeSelfTest.cs',
    'tools/usbip_upgrade_selftest.mjs','tools/fixtures/usbip_pending_transition.mjs','global.json'].map(f=>path.join(repo,f));
  const result={schemaVersion:1,status:'FAIL',existingOriginalId:'controller.usbip-version-upgrade',caseFilter:'pending-file-transition',
    actualConsumer:'extracted unmodified production file-transition methods with declared boot and ACL ports',
    sourceInputsBefore:[],sourceInputsAfter:[],fragments:[],commands:[],timings:{preflightMs,buildMs:null,runMs:null,negativeMs:null},
    commandTimeoutMs:90000,externalOwnedGuardMs:120000,originalExecute196Rerun:false,defaultCanonicalExpanded:false,newGlobalIds:0,
    negatives:{status:'NOT_RUN_COORDINATOR_OWNED',maximum:2},
    notRun:['original real wrapper/Execute196','real Windows V2 provider','real machine ACL','physical boot/device/driver','HIDMaestro SDK methods']};
  const env={...process.env,TEMP:temp,TMP:temp,TMPDIR:temp,DOTNET_CLI_HOME:path.join(out,'dotnet-home'),
    NUGET_PACKAGES:path.join(out,'nuget'),NUGET_HTTP_CACHE_PATH:path.join(out,'nuget-http'),DOTNET_CLI_TELEMETRY_OPTOUT:'1',
    DOTNET_SKIP_FIRST_TIME_EXPERIENCE:'1',DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE:'1',MSBUILDDISABLENODEREUSE:'1'};
  let failure; let generatedInputs=[];
  function run(name,args) {
    const clock=performance.now(),r=spawnSync(dotnet,args,{cwd:repo,env,encoding:'utf8',windowsHide:true,timeout:90000,maxBuffer:4<<20});
    const wallMs=performance.now()-clock; result.timings[name+'Ms']=wallMs;
    fs.writeFileSync(path.join(out,name+'.log'),(r.stdout||'')+(r.stderr||''));
    result.commands.push({name,exe:dotnet,args,wallMs,exitCode:r.status,signal:r.signal,error:r.error?String(r.error):null});
    if(r.error)throw r.error;
    assert.equal(r.status,0,name+' failed: '+r.stdout+r.stderr);
    return r;
  }
  try {
    result.sourceInputsBefore=inputs.map(identity); result.toolsBefore=[process.execPath,dotnet,powershell].map(identity);
    const state=inputs[0],upgrade=inputs[1];
    const parts=[
      extract(state,'PendingPath','    internal static string PendingPath =>','    // Same provider/key/mutex'),
      extract(state,'BootSessionPrefix','    internal const string BootSessionPrefix =','    private static string? cachedBootSession;'),
      extract(state,'ValidBootSession','    internal static bool ValidBootSession(string? value) =>','    internal static string? CurrentBootSessionIdentity'),
      extract(state,'RecordStorage','    // Bounded record storage shared by setup and Host.','    internal static bool RequiresReboot(Guid? recordedBoot, Guid? currentBoot) =>'),
      extract(state,'RequiresReboot.Guid.overloads.and.RecordRequiresReboot','    internal static bool RequiresReboot(Guid? recordedBoot, Guid? currentBoot) =>','    internal static bool Pending'),
      extract(state,'Pending','    internal static bool Pending','    internal static bool RequiresReboot(long recordedUptime, long currentUptime) =>'),
      extract(state,'RequiresReboot.uptime','    internal static bool RequiresReboot(long recordedUptime, long currentUptime) =>','    internal static void PrepareProtectedDirectory()'),
      extract(state,'RetireConfirmedPreviousBoot','    internal static void RetireConfirmedPreviousBoot(bool prepare)','    private static bool AnchorLegacyBootSession(string path)'),
      extract(state,'AnchorLegacyBootSession','    private static bool AnchorLegacyBootSession(string path)','\r\n}\r\n'),
      extract(upgrade,'PreviousBootMatches','        internal static bool PreviousBootMatches(string previous,','        public void ConfirmCurrent(Inventory inventory)'),
      extract(upgrade,'CompleteOwnedInstallRecord','        internal static bool CompleteOwnedInstallRecord(string pendingPath,','        // Actual terminal preflight.'),
      extract(upgrade,'CompletePreviousBootRecord','        internal static bool CompletePreviousBootRecord(string pendingPath,','        internal static bool ContinuationMatches(JsonElement record,'),
      extract(upgrade,'ContinuationMatches','        internal static bool ContinuationMatches(JsonElement record,','        private void WritePending(string operation)')
    ];
    let production=Buffer.from(`using System.Text.Json;\nusing System.Security.Cryptography;\nusing File=FixtureFiles;\nusing FileStream=FixtureFileStream;\nnamespace YeManCC { internal static class UsbipMaintenanceState {\n`);
    function append(part) {
      const offset=production.length; production=Buffer.concat([production,part.fragment,Buffer.from('\n')]);
      const {fragment,...metadata}=part; result.fragments.push({...metadata,generatedFile:path.join(generated,'Production.cs'),generatedStartByte:offset,
        generatedEndByteExclusive:offset+fragment.length,generatedSha256:sha(production.subarray(offset,offset+fragment.length)),verbatim:true});
      assert(production.subarray(offset,offset+fragment.length).equals(fragment),'Generated fragment differs: '+part.name);
    }
    for(const part of parts.filter(part=>part.source===state))append(part);
    production=Buffer.concat([production,Buffer.from(String.raw`
    // Declared external ports only. These do not query the registry, uptime or ACL.
    internal static string DirectoryPath = "";
    internal static Guid? CurrentBootIdentifier;
    internal static uint? CurrentBootSequence;
    internal static string? CurrentBootSessionIdentity;
    private static string lastTransition = "not-run";
    internal static string LastTransition => lastTransition;
    internal static bool BootPreparationAllowed, RejectAcl;
    internal static int BootCalls, AclCalls;
    private static bool PrepareBootSessionIdentity() { BootCalls++; return BootPreparationAllowed; }
    internal static void PrepareProtectedDirectory() { AclCalls++; if(RejectAcl)throw new UnauthorizedAccessException("fixture ACL admission rejected"); }
    internal static void Configure(string directory,Guid? boot,uint? sequence,string? session) {
        DirectoryPath=directory; CurrentBootIdentifier=boot; CurrentBootSequence=sequence; CurrentBootSessionIdentity=session;
        lastTransition="not-run"; BootPreparationAllowed=true; RejectAcl=false; BootCalls=0; AclCalls=0;
    }
} }
internal static class UsbipUpgrade { internal static class WindowsRuntime {
`)]);
    for(const part of parts.filter(part=>part.source===upgrade))append(part);
    production=Buffer.concat([production,Buffer.from('\n} }\n')]);
    fs.writeFileSync(path.join(generated,'Production.cs'),production,{flag:'wx'});
    fs.writeFileSync(path.join(generated,'Program.cs'),harness,{flag:'wx'});
    const project=path.join(generated,'Fixture.csproj');
    fs.writeFileSync(project,'<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0-windows</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><EnableDefaultCompileItems>false</EnableDefaultCompileItems><UseSharedCompilation>false</UseSharedCompilation></PropertyGroup><ItemGroup><Compile Include="Production.cs" /><Compile Include="Program.cs" /></ItemGroup></Project>',{flag:'wx'});
    fs.writeFileSync(path.join(generated,'NuGet.Config'),'<configuration><packageSources><clear /></packageSources></configuration>',{flag:'wx'});
    generatedInputs=['Production.cs','Program.cs','Fixture.csproj','NuGet.Config'].map(f=>path.join(generated,f));
    result.generatedBefore=generatedInputs.map(identity);
    fs.writeFileSync(path.join(out,'extraction.json'),JSON.stringify({sourceInputs:result.sourceInputsBefore,fragments:result.fragments},null,2)+'\n',{flag:'wx'});
    result.negatives.plans=[
      {name:'remove-reboot-admission',privateFile:path.join(generated,'Production.cs'),from:'if (!RecordRequiresReboot(doc.RootElement)) {',to:'if (true) {',
        harnessArgs:[path.join(out,'negative-reboot-cases'),'--only','same-v2'],requiredAssertion:'ASSERT:SAME_V2_PENDING_PRESERVED'},
      {name:'rewrite-move-as-json',privateFile:path.join(generated,'Production.cs'),from:'PreserveRecord(PendingPath, PendingPath + ".previous-boot", read, removeSource: true);',
        to:'File.WriteAllText(PendingPath + ".previous-boot", JsonSerializer.Serialize(doc.RootElement)); File.Delete(PendingPath);',
        harnessArgs:[path.join(out,'negative-rewrite-cases'),'--only','normal'],requiredAssertion:'ASSERT:DIFFERENT_V2_MOVE_BYTES'}
    ];
    run('build',['build',project,'-c','Release','-v:q','--disable-build-servers','-p:UseSharedCompilation=false']);
    const consumer=run('run',[path.join(generated,'bin/Release/net10.0-windows/Fixture.dll'),cases]);
    result.consumer=JSON.parse(consumer.stdout.trim());
    assert.equal(result.consumer.status,'PASS'); assert(result.consumer.passedCases>0,'Zero selected cases rejected');
    assert.equal(result.consumer.passedCases,result.consumer.selectedCases,'All table-selected scenarios must pass');
    assert.equal(result.consumer.results.length,result.consumer.selectedCases,'Result count must be derived from scenario selection');
    result.status='PASS';
  } catch(error) { failure=error; result.error=error.stack||String(error); }
  try {
    result.sourceInputsAfter=inputs.map(identity); assert.deepEqual(result.sourceInputsAfter,result.sourceInputsBefore,'Source input drift');
    result.toolsAfter=[process.execPath,dotnet,powershell].map(identity); assert.deepEqual(result.toolsAfter,result.toolsBefore,'Tool drift');
    result.generatedAfter=generatedInputs.map(identity); if(generatedInputs.length)assert.deepEqual(result.generatedAfter,result.generatedBefore,'Generated input drift');
    if(generatedInputs.length) {
      const compiled=fs.readFileSync(path.join(generated,'Production.cs'));
      for(const part of result.fragments)assert.equal(sha(compiled.subarray(part.generatedStartByte,part.generatedEndByteExclusive)),part.sha256,'Compiled slice drift: '+part.name);
    }
  } catch(error) { if(!failure)failure=error; result.identityError=error.stack||String(error); }
  result.status=failure?'FAIL':result.status; result.timings.totalMs=performance.now()-started;
  fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2)+'\n',{flag:'wx'});
  if(failure)throw failure;
  console.log(JSON.stringify({status:result.status,caseFilter:result.caseFilter,passedCases:result.consumer.passedCases,result:path.join(out,'result.json')}));
}

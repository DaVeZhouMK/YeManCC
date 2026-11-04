using System;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Diagnostics;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using YeManInputHost.Diagnostics;

// Same CPU diagnostic fixture, bounded file-I/O mode. No Host/device is started.
internal static class CostRetentionFixture
{
    private static readonly List<object> Checks = new();
    private static void Check(string name, bool pass) { Checks.Add(new { name, pass }); if (!pass) throw new Exception(name); }
    private static string Row(int sequence, int payload = 240) => JsonSerializer.Serialize(new { sequence, payload = new string('x', payload) });
    private static IEnumerable<JsonElement> Rows(string path) => File.ReadLines(path).Where(x => !string.IsNullOrWhiteSpace(x)).Select(x => JsonDocument.Parse(x).RootElement.Clone());
    private static string DirectoryFor(string root, string name) { var path = Path.Combine(root, name); Directory.CreateDirectory(path); return path; }
    private static void Seed(string path, long bytes)
    {
        var record = Encoding.UTF8.GetBytes(Row(0, 960) + "\n");
        using var f = new FileStream(path, FileMode.Create, FileAccess.Write, FileShare.Read);
        // Seed complete lines; this is a historical fixture, not repeated producer cost.
        while (f.Length + record.Length <= bytes) f.Write(record);
    }
    public static int Main(string[] args)
    {
        if (args.Length != 1) return 2; var root = Path.GetFullPath(args[0]); Directory.CreateDirectory(root);
        var begin = Stopwatch.StartNew();
        try {
            Check("production-active-cap-exactly-20000000-bytes", HostCostLog.FileMaxBytes == 20_000_000 && HostCostArchive.TotalMaxBytes == 20_000_000);
            var path = DirectoryFor(root, "repeated-compaction"); var file = Path.Combine(path, "input-host-cost.log");
            const int cap = 8192; bool bounded = true, allAccepted = true;
            for (int i = 0; i < 400; ++i) { allAccepted &= HostCostLog.Append(path, Row(i), cap, 1024, false); bounded &= new FileInfo(file).Length <= cap; }
            var rows = Rows(file).ToArray(); var sequences = rows.Where(x => x.TryGetProperty("sequence", out _)).Select(x => x.GetProperty("sequence").GetInt32()).ToArray();
            Check("latest-writes-never-rejected-at-cap", allAccepted);
            Check("every-post-append-size-stays-within-cap", bounded);
            Check("latest-row-and-monotonic-retained-order", sequences.Length > 1 && sequences[^1] == 399 && sequences.SequenceEqual(sequences.Order()) && sequences[0] > 0);
            Check("retention-front-and-end-markers-have-explicit-history-gap", rows.Any(x => x.TryGetProperty("event", out var e) && e.GetString() == "log-retention-begin" && !x.GetProperty("historyComplete").GetBoolean()) && rows.Any(x => x.TryGetProperty("event", out var e) && e.GetString() == "log-retention-end"));
            var latestBegin=rows.First(x=>x.TryGetProperty("event",out var e)&&e.GetString()=="log-retention-begin");
            Check("retention-markers-pair-by-unique-compaction-id",rows.Any(x=>x.TryGetProperty("event",out var e)&&e.GetString()=="log-retention-end"&&x.GetProperty("compactionId").GetString()==latestBegin.GetProperty("compactionId").GetString()));
            Check("no-pending-rotations-or-ZIPs-produced", Directory.GetFiles(path).Select(Path.GetFileName).SequenceEqual(new[] { "input-host-cost.log" }));

            var pendingRoot = DirectoryFor(root, "pending-backpressure");
            var pendingPath = Path.Combine(pendingRoot, "input-host-cost.log.pending"); File.WriteAllText(pendingPath, "old-pending\n");
            bool newPass = true; for (int i = 0; i < 150; ++i) newPass &= HostCostLog.Append(pendingRoot, Row(i), cap, 1024, false);
            Check("legacy-pending-does-not-block-newest-row", newPass && Rows(Path.Combine(pendingRoot, "input-host-cost.log")).Last().GetProperty("sequence").GetInt32() == 149);
            Check("producer-does-not-compress-or-touch-pending", File.ReadAllText(pendingPath) == "old-pending\n" && !Directory.GetFiles(pendingRoot, "*.zip").Any());
            var baselineRoot = DirectoryFor(root, "baseline-reproducer"); File.WriteAllText(Path.Combine(baselineRoot, "input-host-cost.log.pending"), "old-pending\n");
            bool rejected = false;
            for (int i = 0; i < 150; ++i) rejected |= !YeManInputHost.Diagnostics.Baseline.HostCostLog.Append(baselineRoot, Row(i), cap, 1024, false);
            Check("original-writer-reproduces-new-row-loss-with-pending", rejected);

            var actualRoot = DirectoryFor(root, "production-20MB"); var actual = Path.Combine(actualRoot, "input-host-cost.log");
            Seed(actual, HostCostLog.FileMaxBytes - 1024); var originalSize = new FileInfo(actual).Length;
            var compact = Stopwatch.StartNew(); bool actualWrite = HostCostLog.Append(actualRoot, Row(900001, 4096), scheduleArchive:false); compact.Stop();
            var retained = Rows(actual).ToArray();
            Check("real-20MB-boundary-compacts-and-appends-newest", actualWrite && new FileInfo(actual).Length <= 20_000_000 && retained[^1].GetProperty("sequence").GetInt32() == 900001);
            Check("real-boundary-retains-some-historical-complete-lines", retained.Length > 100 && retained[0].GetProperty("droppedPrefixBytes").GetInt64() > 0);
            Check("real-cap-compaction-does-not-split-a-UTF8-JSON-row", retained[0].GetProperty("retainedSourceBytes").GetInt64() < originalSize && retained[^1].GetProperty("payload").GetString()!.Length == 4096);
            var oversizedRoot = DirectoryFor(root, "oversize-legacy-active"); var oversized = Path.Combine(oversizedRoot, "input-host-cost.log"); Seed(oversized, 24_000_000);
            Check("legacy-oversize-shrinks-on-first-successful-write", HostCostLog.Append(oversizedRoot, Row(900002), scheduleArchive:false) && new FileInfo(oversized).Length <= 20_000_000 && Rows(oversized).Last().GetProperty("sequence").GetInt32() == 900002);
            var utfRoot = DirectoryFor(root, "中文 UTF8"); var utfFile = Path.Combine(utfRoot, "input-host-cost.log");
            bool utfPass = true; for (int i = 0; i < 100; ++i) utfPass &= HostCostLog.Append(utfRoot, JsonSerializer.Serialize(new { sequence=i, payload=string.Concat(Enumerable.Repeat("手柄🎮", 80)) }), cap, 4096, false);
            Check("UTF8-path-and-compaction-keep-complete-newest-payload", utfPass && Rows(utfFile).Last().GetProperty("payload").GetString()!.Contains("手柄🎮"));
            var small = DirectoryFor(root, "small-fixture-cap");
            Check("tiny-cap-never-overflows-for-markers", Enumerable.Range(0,20).All(i => HostCostLog.Append(small, "{\"n\":"+i+"}", 128, 64, false)) && new FileInfo(Path.Combine(small,"input-host-cost.log")).Length <= 128);
            var partial = DirectoryFor(root,"partial-tail"); var partialFile = Path.Combine(partial,"input-host-cost.log"); File.WriteAllText(partialFile,"{\"incomplete\":");
            Check("unterminated-old-row-does-not-merge-new-record", HostCostLog.Append(partial,Row(900003),cap,1024,false) && JsonDocument.Parse(File.ReadAllLines(partialFile)[^1]).RootElement.GetProperty("sequence").GetInt32()==900003);
            var invalid = DirectoryFor(root,"invalid-destination"); Directory.CreateDirectory(Path.Combine(invalid,"input-host-cost.log"));
            Check("IO-failure-stays-local-and-does-not-escape", !HostCostLog.Append(invalid,Row(1),cap,1024,false));
            var oversizeRecord = DirectoryFor(root,"oversize-record"); HostCostLog.Append(oversizeRecord,Row(2,10000),cap,1024,false);
            Check("oversize-row-has-explicit-omission-diagnostic",Rows(Path.Combine(oversizeRecord,"input-host-cost.log")).Last().GetProperty("kind").GetString()=="cost-record-omitted");

            var legacy = DirectoryFor(root, "legacy-cleanup"); var active = Path.Combine(legacy,"input-host-cost.log"); File.WriteAllText(active,Row(900004)+"\n");
            var archive="input-host-cost.log.archive.0639293641608700009.c497f607e9ea60f5c783074d.zip";
            foreach(var name in new[]{archive,archive+".tmp","input-host-cost.log.pending","input-host-cost.log.1"})File.WriteAllText(Path.Combine(legacy,name),"old");
            foreach(var name in new[]{"unrelated.zip","input-host-cost.log.archive.unowned.zip","input-host-cost.log.extra"})File.WriteAllText(Path.Combine(legacy,name),"keep");
            HostCostArchive.ProcessPending(legacy);
            Check("legacy-owned-archives-pending-and-rotation-retired",new[]{archive,archive+".tmp","input-host-cost.log.pending","input-host-cost.log.1"}.All(n=>!File.Exists(Path.Combine(legacy,n))));
            Check("cleanup-preserves-active-and-unrelated-files",Rows(active).Last().GetProperty("sequence").GetInt32()==900004 && new[]{"unrelated.zip","input-host-cost.log.archive.unowned.zip","input-host-cost.log.extra"}.All(n=>File.ReadAllText(Path.Combine(legacy,n))=="keep"));
            Check("legacy-cleanup-receipt-explicit-no-history-completeness-claim",JsonDocument.Parse(File.ReadAllText(Path.Combine(legacy,"input-host-cost-retention.json"))).RootElement.GetProperty("historyComplete").GetBoolean()==false);
            var locked=Path.Combine(legacy,"input-host-cost.log.pending");File.WriteAllText(locked,"locked-old");
            using(var hold=new FileStream(locked,FileMode.Open,FileAccess.ReadWrite,FileShare.None)) {
                HostCostArchive.ProcessPending(legacy);
                Check("legacy-IO-failure-keeps-newest-producer-working",HostCostLog.Append(legacy,Row(900005),cap,1024,false));
            }
            HostCostArchive.ProcessPending(legacy);Check("explicit-retry-retires-previously-locked-legacy-file",!File.Exists(locked));
            var scheduled=DirectoryFor(root,"actual-one-shot-cleanup");var scheduledPending=Path.Combine(scheduled,"input-host-cost.log.pending");File.WriteAllText(scheduledPending,"legacy");
            Check("normal-append-queues-only-legacy-cleanup",HostCostLog.Append(scheduled,Row(900006)));
            bool retired=SpinWait.SpinUntil(()=>!File.Exists(scheduledPending)&&File.Exists(Path.Combine(scheduled,"input-host-cost-retention.json"))&&!JsonSerializer.SerializeToElement(HostCostArchive.Snapshot()).GetProperty("workerQueued").GetBoolean(),3000);
            Check("production-one-shot-worker-finishes-without-new-ZIPs",retired&&!Directory.GetFiles(scheduled,"*.zip").Any()&&Rows(Path.Combine(scheduled,"input-host-cost.log")).Last().GetProperty("sequence").GetInt32()==900006);
            var result=new {pass=true,checks=Checks,scope="actual-production-cost-writers;owned-files-only;no-Host-device-controller-start",elapsedMs=begin.Elapsed.TotalMilliseconds,production20MBCompactionMs=compact.Elapsed.TotalMilliseconds,activeMaxBytes=HostCostLog.FileMaxBytes,originalPendingBugReproduced=rejected};
            File.WriteAllText(Path.Combine(root,"RESULT.json"),JsonSerializer.Serialize(result,new JsonSerializerOptions {WriteIndented=true})+"\n");
            Console.WriteLine(JsonSerializer.Serialize(new{pass=true,checks=Checks.Count,production20MBCompactionMs=compact.Elapsed.TotalMilliseconds}));return 0;
        } catch(Exception ex) {
            File.WriteAllText(Path.Combine(root,"RESULT.json"),JsonSerializer.Serialize(new{pass=false,checks=Checks,error=ex.ToString()},new JsonSerializerOptions{WriteIndented=true}));Console.Error.WriteLine(ex);return 1;
        }
    }
}

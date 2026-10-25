// FAN979: actual temperature-monitor class extracted with mapping/clock boundaries replaced.
// Native monitor_freshness.h supplies the numeric oracle. No Host/HC/device or CPU benchmark.
using System.Buffers.Binary;
using System.IO.MemoryMappedFiles;
using System.Reflection;
using System.Text;
using System.Text.Json;
using YeManFanHost;

internal static class FixtureClock {
    public static long Monotonic;
    public static long EpochMs;
    public static DateTimeOffset UtcNow => DateTimeOffset.FromUnixTimeMilliseconds(EpochMs);
}
internal static class FixtureMaps {
    // The reader now caches an open section/view. Update the live producer
    // section instead of replacing a detached byte[] which the reader cannot see.
    private static readonly Dictionary<string, MemoryMappedFile> Producers = new();
    private static readonly string Prefix = @"Local\YMCC-Fan-Freshness-" + Guid.NewGuid().ToString("N") + "-";
    public static Action<MemoryMappedViewAccessor>? AfterReading;
    public static int ReadingVisits, WriterChanges, LiveFrameUpdates;
    private static MemoryMappedFile? CurrentMap;
    public static void Reset() {
        foreach (var producer in Producers.Values) producer.Dispose();
        Producers.Clear(); CurrentMap = null; AfterReading = null;
        ReadingVisits = WriterChanges = LiveFrameUpdates = 0;
    }
    public static void SetFrame(string name, byte[] bytes) {
        if (!Producers.TryGetValue(name, out var producer))
            Producers.Add(name, producer = MemoryMappedFile.CreateNew(Prefix + name, bytes.Length));
        using var writer = producer.CreateViewAccessor();
        writer.WriteArray(0, bytes, 0, bytes.Length); ++LiveFrameUpdates;
    }
    public static MemoryMappedFile OpenExisting(string name) {
        if (!Producers.TryGetValue(name, out var producer)) throw new FileNotFoundException("fixture mapping unavailable");
        CurrentMap = producer;
        return MemoryMappedFile.OpenExisting(Prefix + name, MemoryMappedFileRights.Read);
    }
    public static void Reading(MemoryMappedViewAccessor view) { ++ReadingVisits; if(AfterReading is not null) { using var writer=CurrentMap!.CreateViewAccessor(); AfterReading(writer); ++WriterChanges; } }
    public static byte[] Frame(ulong time, uint period=3000, uint version=2, uint sensorOffset=48, double temperature=70) {
        var bytes=new byte[64+316];
        void U32(int at,uint value)=>BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(at,4),value);
        U32(0,0x53695748);U32(4,version);U32(8,1);BinaryPrimitives.WriteUInt64LittleEndian(bytes.AsSpan(12,8),time);
        U32(20,sensorOffset);U32(24,392);U32(28,0);U32(32,64);U32(36,316);U32(40,1);U32(44,period);
        Encoding.ASCII.GetBytes("CPU Package").CopyTo(bytes,64+12);
        BinaryPrimitives.WriteInt64LittleEndian(bytes.AsSpan(64+284,8),BitConverter.DoubleToInt64Bits(temperature));return bytes;
    }
}
internal static class Program {
    private static int checks; private static readonly List<string> failures=[];
    private static readonly MethodInfo Poll=typeof(HWiNFOTemperatureMonitor).GetMethod("Poll",BindingFlags.Instance|BindingFlags.NonPublic)!;
    private static void Check(bool pass,string name){++checks;if(!pass)failures.Add(name);}
    private sealed class MonitorCase:IDisposable {
        public readonly HWiNFOTemperatureMonitor Monitor=new(); public int Healthy,Changed;
        public MonitorCase(){FixtureMaps.Reset();
            typeof(HWiNFOTemperatureMonitor).GetField("sampleReceived",BindingFlags.Instance|BindingFlags.NonPublic)!.SetValue(Monitor,(Action<float?>)(_=>++Changed));
            typeof(HWiNFOTemperatureMonitor).GetField("healthySampleReceived",BindingFlags.Instance|BindingFlags.NonPublic)!.SetValue(Monitor,(Action)(()=>++Healthy));}
        public void Tick(long tick,long utcMs,byte[] a){FixtureClock.Monotonic=tick;FixtureClock.EpochMs=utcMs;FixtureMaps.SetFrame("fixtureA",a);Poll.Invoke(Monitor,[null]);}
        public void Dispose(){Monitor.Dispose();FixtureMaps.Reset();}
    }
    public static int Main(string[] args){
        var oracle=JsonDocument.Parse(File.ReadAllText(args[0]));int vectors=0;
        foreach(var group in oracle.RootElement.GetProperty("groups").EnumerateArray()){
            using var m=new MonitorCase();foreach(var row in group.GetProperty("rows").EnumerateArray()){
                var before=m.Healthy;
                m.Tick(row.GetProperty("tick").GetInt64(),row.GetProperty("utcMs").GetInt64(),FixtureMaps.Frame(row.GetProperty("pollTime").GetUInt64(),row.GetProperty("wirePeriod").GetUInt32(),row.GetProperty("version").GetUInt32(),row.GetProperty("sensorOffset").GetUInt32()));
                bool expected=row.GetProperty("expectedHealthy").GetBoolean();++vectors;
                Check((m.Healthy==before+1)==expected,group.GetProperty("name").GetString()+" t="+row.GetProperty("tick")+" matches actual native freshness verdict");
            }
            Check(m.Changed<=1,"steady temperature never causes periodic repeated dispatch: "+group.GetProperty("name").GetString());
        }
        const long utc=1700000000000;const ulong producer=1700000000;
        {
            using var m=new MonitorCase();m.Tick(0,utc,FixtureMaps.Frame(producer));var visits=FixtureMaps.ReadingVisits;
            m.Tick(6000,utc+6000,FixtureMaps.Frame(producer));
            Check(FixtureMaps.ReadingVisits==visits,"known stale producer rejected before full temperature label/value scan");
            Check(m.Healthy==1,"frozen mapping cannot keep refreshing health timestamp");
        }
        {
            using var m=new MonitorCase();m.Tick(0,utc,FixtureMaps.Frame(producer,temperature:60));m.Tick(500,utc+500,FixtureMaps.Frame(producer,temperature:80));
            Check(m.Changed==2&&m.Healthy==2,"same integer-second timestamp does not cache changed temperature values");
        }
        {
            using var m=new MonitorCase();FixtureMaps.AfterReading=view=>view.Write(8,42U);m.Tick(0,utc,FixtureMaps.Frame(producer));
            Check(FixtureMaps.WriterChanges==1,"fixture producer really changes header via a separate writable view");
            Check(m.Healthy==0&&m.Changed==0,"header revision changes during scan are not published as a successful sample");
            FixtureMaps.AfterReading=null;m.Tick(1000,utc+1000,FixtureMaps.Frame(producer+1));Check(m.Healthy==1&&m.Changed==1,"next stable frame remains reachable after inconsistent frame");
        }
        {
            using var m=new MonitorCase();FixtureMaps.AfterReading=view=>view.Write(12,producer+1);m.Tick(0,utc,FixtureMaps.Frame(producer));
            Check(FixtureMaps.WriterChanges==1,"fixture producer really changes poll time while reader remains read-only");
            Check(m.Healthy==0&&m.Changed==0,"producer time changes during scan retire the mixed snapshot");
        }
        {
            using var m=new MonitorCase();var future=producer+3600;
            FixtureMaps.SetFrame("fixtureB",FixtureMaps.Frame(future+1));
            m.Tick(0,utc,FixtureMaps.Frame(future));m.Tick(6000,utc,FixtureMaps.Frame(future));var before=m.Healthy;
            m.Tick(12000,utc,FixtureMaps.Frame(future));m.Tick(18000,utc,FixtureMaps.Frame(future));
            Check(before==2&&m.Healthy==before,"fallback between old future-dated mappings cannot reset freshness indefinitely after wall-clock rollback");
        }
        {
            using var m=new MonitorCase();m.Tick(0,utc,FixtureMaps.Frame(producer,temperature:double.NaN));Check(m.Healthy==0&&m.Changed==0,"valid producer cannot bless invalid temperature");
            m.Tick(1000,utc+1000,FixtureMaps.Frame(producer+1));Check(m.Healthy==1,"next valid temperature is still publishable");
            m.Monitor.Dispose();var before=m.Healthy;var visits=FixtureMaps.ReadingVisits;m.Tick(2000,utc+2000,FixtureMaps.Frame(producer+2));
            Check(m.Healthy==before&&FixtureMaps.ReadingVisits==visits,"disposed monitor does not begin another scan or callback");
        }
        Console.WriteLine(JsonSerializer.Serialize(new {ok=failures.Count==0,checks,vectors,failed=failures.Count,failures,
            scope="exact-monitor-class-with-clock-and-map-io-fixtures-native-freshness-oracle-no-hc-host-device-no-cpu-benchmark"},new JsonSerializerOptions{WriteIndented=true}));
        return failures.Count==0?0:1;
    }
}


using System.Diagnostics;
using System.Reflection;
using System.Text.Json;
using Microsoft.Win32.SafeHandles;

namespace YeManCC.Lighting;

public interface ILightingHidTransport : IDisposable
{
    bool IsConnected { get; }
    bool Write(byte[] report);
}

// This substitutes native terminal functions and FileStream, while the actual
// transport's path/attributes/caps checks, cleanup, Write and Dispose execute.
internal static class TransportProbe
{
    internal static ushort Vendor=0x1A86, Product=0xFE00, Page=0xFF00, Usage=1, Length=65;
    internal static int Probes, Opened, Writes, Disposed, Preparsed, Freed, Bytes;
    internal static bool AttributesOk=true, PreparsedOk=true, CapsOk=true, ChangeIdentity, ChangePid, ThrowWrite, ShortWrite, ThrowDispose, Block;
    internal static SafeFileHandle? LastOpened;
    internal static readonly List<string> Paths=new();
    internal static void Reset()
    {
        Vendor=0x1A86; Product=0xFE00; Page=0xFF00; Usage=1; Length=65;
        Probes=Opened=Writes=Disposed=Preparsed=Freed=Bytes=0;LastOpened=null;
        AttributesOk=PreparsedOk=CapsOk=true;
        ChangeIdentity=ChangePid=ThrowWrite=ShortWrite=ThrowDispose=Block=false; Paths.Clear();
    }
    internal static SafeFileHandle Probe(string path)
    { Probes++; Paths.Add(path); return new SafeFileHandle((IntPtr)101,false); }
    internal static SafeFileHandle OpenHandle(string path,FileMode mode,FileAccess access,FileShare share,FileOptions options)
    { Opened++; Paths.Add(path); return LastOpened=new SafeFileHandle((IntPtr)103,false); }
    internal static bool FillAttributes(out ushort vendor,out ushort product)
    { vendor=ChangeIdentity && Opened>0?(ushort)0xFFFF:Vendor; product=ChangePid&&Opened>0?(Product==0xFE00?(ushort)0x1305:(ushort)0xFE00):Product; return AttributesOk; }
}
internal sealed class FixtureFileStream : IDisposable
{
    internal SafeFileHandle SafeFileHandle { get; }
    private bool disposed;
    internal FixtureFileStream(SafeFileHandle handle,FileAccess access,int size,bool isAsync) => SafeFileHandle=handle;
    internal async ValueTask WriteAsync(ReadOnlyMemory<byte> bytes,CancellationToken token)
    {
        TransportProbe.Writes++;
        if(TransportProbe.Block) await Task.Delay(Timeout.Infinite,token);
        if(TransportProbe.ShortWrite) { TransportProbe.Bytes+=32; throw new IOException("fixture incomplete native write"); }
        if(TransportProbe.ThrowWrite) throw new IOException("fixture native write failed");
        token.ThrowIfCancellationRequested(); TransportProbe.Bytes+=bytes.Length;
    }
    public void Dispose()
    {
        if(disposed) return; disposed=true; TransportProbe.Disposed++; SafeFileHandle.Dispose();
        if(TransportProbe.ThrowDispose) throw new IOException("fixture stream disposal failed");
    }
}
internal static class TransportReplayEntry
{
    private const string PathFe=@"\\?\hid#vid_1a86&pid_fe00#offline-selected";
    private const string Path13=@"\\?\hid#vid_1a86&pid_1305#offline-selected";
    private static int checks;
    private sealed class FixtureAssertion(string message):Exception(message);
    private static void Check(bool value,string message) { if(!value) throw new FixtureAssertion(message); checks++; }
    private static ILightingHidTransport Open(string path=PathFe,CancellationToken token=default,Action? current=null) => WindowsHidTransport.OpenX2MiniPro(path,token,current??(()=>{}));
    private static void Reject<T>(Action operation,string message) where T:Exception
    { try { operation(); } catch(T) { checks++; return; } throw new FixtureAssertion(message); }
    public static int Main(string[] args)
    {
        if(args.Length!=2 || args[0]!="--case" || args[1]!="x2mini-terminal") { Console.Error.WriteLine("exact --case x2mini-terminal required"); return 64; }
        var cases=new List<(string Name,Action Run)>();
        void Add(string name,Action run)=>cases.Add((name,run));
        foreach(var pid in new ushort[]{0xFE00,0x1305})
        {
            var value=pid;
            Add("transport-exact-1a86-"+value.ToString("x4")+"-ff00-1",()=> {
                TransportProbe.Product=value; using var port=Open(value==0xFE00?PathFe:Path13);
                Check(port.IsConnected,"actual connected terminal"); Check(port.Write(new byte[65]),"actual complete write"); port.Dispose(); port.Dispose();
                Check(TransportProbe.Opened==1 && TransportProbe.Probes==1 && TransportProbe.Writes==1 && TransportProbe.Bytes==65 && TransportProbe.Disposed==1,"one owned write and idempotent release");
                Check(TransportProbe.Preparsed==2 && TransportProbe.Freed==2,"both preparsed blocks freed");
                Check(TransportProbe.Paths.All(p=>p==(value==0xFE00?PathFe:Path13)),"only selected endpoint inspected/opened");
            });
        }
        foreach(var kind in new[]{"vendor","pid","page","usage","length64","length66","attributes","preparsed","caps","changed","pid-swap-fe00-1305","pid-swap-1305-fe00"})
        {
            var value=kind;
            Add("transport-reject-"+value,()=> {
                switch(value) {
                    case "vendor":TransportProbe.Vendor=0xFFFF;break;
                    case "pid":TransportProbe.Product=0xFFFF;break;
                    case "page":TransportProbe.Page=0xFF01;break;
                    case "usage":TransportProbe.Usage=2;break;
                    case "length64":TransportProbe.Length=64;break;
                    case "length66":TransportProbe.Length=66;break;
                    case "attributes":TransportProbe.AttributesOk=false;break;
                    case "preparsed":TransportProbe.PreparsedOk=false;break;
                    case "caps":TransportProbe.CapsOk=false;break;
                    case "changed":TransportProbe.ChangeIdentity=true;break;
                    case "pid-swap-fe00-1305":TransportProbe.ChangePid=true;break;
                    case "pid-swap-1305-fe00":TransportProbe.Product=0x1305;TransportProbe.ChangePid=true;break;
                }
                Reject<IOException>(()=>{using var port=Open();},"invalid descriptor/identity accepted");
                Check(TransportProbe.Writes==0,"reject before all writes");
                Check(TransportProbe.Opened==(value=="changed"||value.StartsWith("pid-swap")?1:0),"reject before writable open except required postopen recheck");
                Check(TransportProbe.Preparsed==TransportProbe.Freed,"preparsed data returned on failure");
                if(TransportProbe.Opened>0)Check(TransportProbe.LastOpened!.IsClosed,"failed reopen returns handle before stream ownership");
            });
        }
        foreach(var path in new[]{"",@"\\?\hid#vid_ffff&pid_fe00#other",@"\\?\hid#vid_1a86&pid_0001#other","ambiguous"})
        {
            var value=path;
            Add("transport-reject-path-"+(value.Length==0?"empty":value=="ambiguous"?"ambiguous":value.Contains("ffff")?"wrong-vid":"wrong-pid"),()=> {
                Reject<ArgumentException>(()=>{using var port=Open(value);},"invalid path accepted");
                Check(TransportProbe.Probes==0 && TransportProbe.Opened==0,"invalid path never probes or discovers alternate devices");
            });
        }
        Add("transport-two-available-endpoints-exact-owner-only",()=> {
            using var port=Open(Path13); Check(TransportProbe.Paths.All(p=>p==Path13),"no discovery or alternate endpoint from two eligible PIDs");
        });
        Add("transport-precancel-no-terminal",()=> {
            using var stop=new CancellationTokenSource(); stop.Cancel();
            Reject<OperationCanceledException>(()=>{using var port=Open(token:stop.Token);},"cancel accepted"); Check(TransportProbe.Probes==0 && TransportProbe.Opened==0,"cancel before probe");
        });
        Add("transport-owner-obsolete-no-terminal",()=> {
            Reject<IOException>(()=>{using var port=Open(current:()=>throw new IOException("obsolete"));},"obsolete accepted"); Check(TransportProbe.Probes==0,"owner before probe");
        });
        Add("transport-report-64-66-nonzero-id-rejected",()=> {
            using var port=Open();
            var nonzero=new byte[65];nonzero[0]=1;
            foreach(var report in new[]{new byte[64],new byte[66],nonzero})
                Reject<ArgumentException>(()=>port.Write(report),"malformed write accepted");
            Check(TransportProbe.Writes==0,"invalid report never enters stream");
        });
        foreach(var mode in new[]{"short","throw","dispose","cancel-blocked"})
        {
            var value=mode;
            Add("transport-"+value+"-resource-return",()=> {
                using var stop=new CancellationTokenSource(); var port=Open(token:stop.Token);
                TransportProbe.ShortWrite=value=="short"; TransportProbe.ThrowWrite=value=="throw"; TransportProbe.ThrowDispose=value=="dispose"; TransportProbe.Block=value=="cancel-blocked";
                try {
                    if(value=="dispose") { Check(port.Write(new byte[65]),"before dispose"); Reject<IOException>(port.Dispose,"dispose failure hidden"); }
                    else if(value=="cancel-blocked") { stop.CancelAfter(40); Reject<OperationCanceledException>(()=>port.Write(new byte[65]),"blocked write not canceled"); }
                    else Reject<IOException>(()=>port.Write(new byte[65]),"write failure hidden");
                } finally { port.Dispose(); }
                Check(TransportProbe.Writes==1 && TransportProbe.Disposed==1,"no write retry and exact stream return");
                if(value=="short") Check(TransportProbe.Bytes==32,"partial terminal acceptance preserved as exception");
                Reject<ObjectDisposedException>(()=>port.Write(new byte[65]),"write after dispose accepted");
            });
        }
        var selected=cases.Select(c=>c.Name).ToArray();
        var planFile=Environment.GetEnvironmentVariable("YMCC_LIGHT_TRANSPORT_SELECTION_LOG");
        if(planFile is not null)File.WriteAllText(planFile,JsonSerializer.Serialize(new{selector=args[1],selected}));
        var rows=new List<object>(); var watch=Stopwatch.StartNew();
        foreach(var c in cases)
        {
            TransportProbe.Reset();
            try { c.Run(); rows.Add(new{name=c.Name,status="PASS",group="terminal"}); }
            catch(Exception e) { rows.Add(new{name=c.Name,status="FAIL",group="terminal",failureKind=e is FixtureAssertion?"ASSERTION":"UNEXPECTED_EXCEPTION",error=e.ToString()}); Console.WriteLine(JsonSerializer.Serialize(new{ok=false,selector=args[1],selected,cases=rows,checks,milliseconds=watch.Elapsed.TotalMilliseconds,hardwareIo=false}));return 2; }
        }
        Console.WriteLine(JsonSerializer.Serialize(new{ok=true,selector=args[1],selected,cases=rows,checks,milliseconds=watch.Elapsed.TotalMilliseconds,hardwareIo=false,physicalDescriptorVerified=false}));return 0;
    }
}

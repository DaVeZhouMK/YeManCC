using System.IO;
using System.Reflection;
using System.Text.Json;
using System.Text;
using System.Buffers.Binary;
using YeManFanBackend.Capabilities;
using YeManBatteryHost;

// The production operation and frozen type/owner declarations run against a
// memory IO terminal. No real transport factory or hardware is loaded.
internal static class AyaneoOemLightingRestoreTests
{
    private static int checks;
    private static void Check(bool yes,string why){++checks;if(!yes)throw new Exception(why);}
    private static void Reject<T>(Action body,string why) where T:Exception
    {try{body();}catch(T){checks++;return;}throw new Exception(why);}
    internal static int Run()
    {
        checks=0;
        var hcPin=typeof(Program).GetMethod("HcPinForProfile",BindingFlags.NonPublic|BindingFlags.Static)!.Invoke(null,new object[]{"ymcc-device-core-v1"});
        Check((string)hcPin! == "f79edd9dc1c60d5b5e9f3c456174b3c049ea6087ea0c988facd95bc1c174b93c","actual helper HC profile accepts current DeviceCore runtime");
        var names=new[]{"AYANEO2","AYANEO2S","AYANEOAIR","AYANEOAIR1S","AYANEOAIRLite","AYANEOAIRPlusAMD","AYANEOAIRPlusAMDMendocino","AYANEOAIRPlusIntel","AYANEOAIRPro","AYANEOFlip1SKB","AYANEOFlipKB","AYANEOKUN","AYANEOSlide"};
        foreach(var name in names){var row=HcLightingCapabilityCatalog.ResolveOemOwnership("HandheldCompanion.Devices."+name);Check(row is not null,"eligible "+name);Check(row!.SharedChargeIndicatorDeclared==(name is "AYANEOAIR" or "AYANEOSlide"),"charge declaration "+name);}
        foreach(var name in new[]{"AYANEO3","AYANEONEXT2","AYANEODeviceCEc","AYANEODeviceCEii","ClawCG3EM","XboxROGAllyX","OneXPlayer3","GPDWin5","Unknown"})
            Check(HcLightingCapabilityCatalog.ResolveOemOwnership("HandheldCompanion.Devices."+name) is null,"no borrowed ownership "+name);
        var legacy=HcLightingCapabilityCatalog.ResolveOemOwnership(typeof(HandheldCompanion.Devices.AYANEO2).FullName)!;
        var modern=HcLightingCapabilityCatalog.ResolveOemOwnership(typeof(HandheldCompanion.Devices.AYANEOAIRPlusAMD).FullName)!;
        var trace=new (ushort,byte)[]{(0x4E,0x2E),(0x4F,0x11),(0x4E,0x2F),(0x4F,0xD1),(0x4E,0x2E),(0x4F,0x10),(0x4E,0x2F),(0x4F,0x87),(0x4E,0x2E),(0x4F,0x12),(0x4E,0x2F),(0x4F,0x00)};
        var l=new MemoryIo(legacy.Protocol);var r=AyaneoOemLightingRestore.Apply(legacy,l,default);Check(r.RequestCompleted&&r.Writes==1&&l.LegacyCalls==1&&l.Enters==0&&l.Exits==0,"legacy only releases ownership");
        l=new(legacy.Protocol){ReleaseOk=false};r=AyaneoOemLightingRestore.Apply(legacy,l,default);Check(!r.RequestCompleted&&r.Reason=="oem-restore-write-failed","legacy refusal");
        var m=new MemoryIo(modern.Protocol);r=AyaneoOemLightingRestore.Apply(modern,m,default);Check(r.RequestCompleted&&r.Writes==12&&m.Writes.SequenceEqual(trace)&&m.Enters==1&&m.Exits==1&&m.LegacyCalls==0,"exact CEii trace");
        m=new(legacy.Protocol);r=AyaneoOemLightingRestore.Apply(modern,m,default);Check(!r.RequestCompleted&&r.Reason=="ownership-transport-mismatch"&&m.Enters==0&&m.LegacyCalls==0,"mismatched transport stays inert");
        using(var stop=new CancellationTokenSource()){stop.Cancel();m=new(modern.Protocol);r=AyaneoOemLightingRestore.Apply(modern,m,stop.Token);Check(!r.RequestCompleted&&r.Writes==0&&m.Enters==0&&m.Exits==0,"cancel before admission");}
        for(var n=1;n<=12;n++){
            using var stop=new CancellationTokenSource();m=new(modern.Protocol){AfterWrite=i=>{if(i==n)stop.Cancel();}};r=AyaneoOemLightingRestore.Apply(modern,m,stop.Token);
            Check(m.Writes.SequenceEqual(trace.Take(n))&&m.Exits==1,"cancel keeps exact prefix and exits "+n);
            Check(r.RequestCompleted==(n==12)&&r.Writes==n,"cancel result describes completed ordinary calls "+n);
            m=new(modern.Protocol){ThrowAt=n};r=AyaneoOemLightingRestore.Apply(modern,m,default);Check(!r.RequestCompleted&&r.Writes==n&&m.Writes.Count==n&&m.Exits==1,"no late write after failure "+n);
        }
        m=new(modern.Protocol){EntryFails=true};r=AyaneoOemLightingRestore.Apply(modern,m,default);Check(!r.RequestCompleted&&m.Writes.Count==0&&m.Exits==1,"failed enter still attempts exit");
        m=new(modern.Protocol){ExitFails=true};r=AyaneoOemLightingRestore.Apply(modern,m,default);Check(!r.RequestCompleted&&m.Writes.Count==12&&r.Reason=="oem-restore-write-failed","exit failure cannot claim completion");
        foreach(var type in new[]{typeof(HandheldCompanion.Devices.AYANEO2),typeof(HandheldCompanion.Devices.AYANEOAIRPlusAMD)}){
            MemoryIo? opened=null;var row=HcLightingCapabilityCatalog.ResolveOemOwnership(type.FullName)!;
            var receipt=Program.ExecuteAyaneoOemRestore(type,type.FullName,"memory-runtime",typeof(AyaneoOemLightingRestoreTests).Assembly,15000,(cap,root,asm,token)=>{
                Check(cap==row&&root=="memory-runtime"&&token.CanBeCanceled,"actual consumer carries admitted capability and timeout");return opened=new MemoryIo(cap.Protocol);});
            Check(opened!.Disposed,"actual helper owns terminal disposal");
            Check((string)receipt["deviceClass"]! ==type.FullName&&(string)receipt["action"]! =="restore-oem","actual receipt satisfies native identity/action contract");
            Check((string)receipt["capabilityOperation"]! =="lighting.restore-oem"&&(string)receipt["capabilityCatalogSha256"]! ==HcLightingCapabilityCatalog.CatalogSha256,"catalog operation identity");
            Check((bool)receipt["ownershipRequestCompleted"]!&&!(bool)receipt["physicalEffectVerified"]!&&!(bool)receipt["ownershipReadbackAvailable"]!,"completion cannot claim physical readback");
            opened=null;receipt=Program.ExecuteAyaneoOemRestore(type,type.FullName,"memory-runtime",typeof(AyaneoOemLightingRestoreTests).Assembly,15000,(cap,root,asm,token)=>opened=new MemoryIo(cap.Protocol){ReleaseOk=false,EntryFails=true});
            Check(opened!.Disposed&&!(bool)receipt["ownershipRequestCompleted"]!,"terminal rejection disposed and returned honestly");
        }
        var opens=0;IAyaneoOemRestoreIo Unexpected(LightingOemOwnershipCapability c,string root,Assembly a,CancellationToken t){opens++;throw new Exception("must remain inert");}
        Reject<NotSupportedException>(()=>Program.ExecuteAyaneoOemRestore(typeof(string),null,"memory",typeof(string).Assembly,100,Unexpected),"unknown consumer admitted");
        Reject<InvalidOperationException>(()=>Program.ExecuteAyaneoOemRestore(typeof(HandheldCompanion.Devices.AYANEO2),"HandheldCompanion.Devices.AYANEOAIRPlusAMD","memory",typeof(string).Assembly,100,Unexpected),"mismatched expected class admitted");
        Reject<ArgumentOutOfRangeException>(()=>Program.ExecuteAyaneoOemRestore(typeof(HandheldCompanion.Devices.AYANEO2),null,"memory",typeof(string).Assembly,0,Unexpected),"unbounded operation admitted");
        Check(opens==0,"all refused operations precede transport factory");
        CheckedWindowsTransport(modern,trace);
        return checks;
    }
    private static readonly string MutexPrefix=@"Local\YMCC.CeiiSoftware."+Guid.NewGuid().ToString("N")+".";
    private static Mutex PrivateMutex(string name) => new(false,MutexPrefix+name.Replace('\\','_'));
    private static readonly byte[] Module=[1,2,3]; // Injected module bytes, never submitted to a real driver.
    private static IAyaneoOemRestoreIo CheckedIo(MemoryKernel kernel,CancellationToken stop=default)
        => AyaneoOemRestoreWindowsPorts.OpenCeii(stop,()=>AyaneoOemRestoreWindowsPorts.CreateCheckedCeiiSession(
            stop,()=>kernel,()=>Module.ToArray(),()=>@"\\.\PawnIO"),PrivateMutex);
    private static byte[] Packet(string name,params long[] values)
    {
        var bytes=new byte[32+8*values.Length];Encoding.ASCII.GetBytes(name).CopyTo(bytes,0);
        for(var n=0;n<values.Length;n++)BinaryPrimitives.WriteInt64LittleEndian(bytes.AsSpan(32+n*8,8),values[n]);
        return bytes;
    }
    private static List<(uint Code,byte[] Input)> Expected((ushort Port,byte Value)[] trace)
    {
        var rows=new List<(uint,byte[])>{(unchecked((uint)-1582161788),Module.ToArray()),
            (unchecked((uint)-1582161660),Packet("ioctl_select_slot",1))};
        foreach(var item in new (ushort,byte)[]{(0x4E,0x87),(0x4E,0x01),(0x4E,0x55),(0x4E,0xAA)}.Concat(trace))
            rows.Add((unchecked((uint)-1582161660),Packet("ioctl_pio_outb",item.Item1,item.Item2)));
        return rows;
    }
    private static void Prefix(MemoryKernel kernel,List<(uint Code,byte[] Input)> expected,int count,string why)
    {
        Check(kernel.Calls.Count==count,why+" count");
        for(var i=0;i<count;i++)Check(kernel.Calls[i].Code==expected[i].Code&&kernel.Calls[i].Input.SequenceEqual(expected[i].Input),why+" packet "+i);
    }
    private static void CheckedWindowsTransport(LightingOemOwnershipCapability modern,(ushort Port,byte Value)[] trace)
    {
        var expected=Expected(trace);Check(expected.Count==18,"one load, one slot, four exact IT87 unlocks and twelve CEii data calls");
        var success=new MemoryKernel();
        using(var io=CheckedIo(success)){
            Reject<ArgumentOutOfRangeException>(()=>io.WritePort(0x80,0),"unbounded port accepted");
            Reject<InvalidOperationException>(()=>Task.Run(()=>io.WritePort(0x4E,0)).GetAwaiter().GetResult(),"cross-thread port accepted");
            Check(success.Calls.Count==0,"invalid calls stay before kernel IO");
            Check(AyaneoOemLightingRestore.Apply(modern,io,default).RequestCompleted,"checked CEii success");
            Prefix(success,expected,18,"exact CEii transaction");
        }
        Check(success.Disposes==1,"checked success disposes exact kernel owner once");
        foreach(var name in new[]{"AYANEOAIRPlusAMD","AYANEOAIRPlusAMDMendocino","AYANEOAIRPlusIntel","AYANEOSlide"}){
            var row=HcLightingCapabilityCatalog.ResolveOemOwnership("HandheldCompanion.Devices."+name)!;
            var kernel=new MemoryKernel();using(var io=CheckedIo(kernel))Check(AyaneoOemLightingRestore.Apply(row,io,default).RequestCompleted,"all existing CEii rows share checked transaction "+name);
            Prefix(kernel,expected,18,"model-specific CEii "+name);
        }
        foreach(var failure in new[]{"nack","length","throw"})for(var at=0;at<18;at++){
            var kernel=new MemoryKernel{FailAt=at,Failure=failure};
            using(var io=CheckedIo(kernel)){
                var result=AyaneoOemLightingRestore.Apply(modern,io,default);
                Check(!result.RequestCompleted&&result.Reason=="oem-restore-write-failed","unconfirmed IOCTL cannot claim request completion "+failure+" "+at);
                Prefix(kernel,expected,at+1,"no call after unconfirmed IOCTL "+failure+" "+at);
                Reject<IOException>(()=>io.WritePort(0x4F,0),"faulted session must not retry/fallback");
                Check(kernel.Calls.Count==at+1,"faulted session stays sealed");
            }
            Check(kernel.Disposes==1,"failed kernel owner disposed once");
        }
        for(var at=0;at<18;at++){
            using var stop=new CancellationTokenSource();var position=at;
            var kernel=new MemoryKernel{AfterCall=n=>{if(n==position)stop.Cancel();}};
            using(var io=CheckedIo(kernel,stop.Token)){
                var result=AyaneoOemLightingRestore.Apply(modern,io,stop.Token);
                Check(result.RequestCompleted==(at==17),"cancel acknowledges only already completed full transaction "+at);
                if(at!=17)Check(result.Reason=="oem-restore-cancelled","cancel reason remains explicit");
                Prefix(kernel,expected,at+1,"cancel stops before later kernel call "+at);
            }
            Check(kernel.Disposes==1,"cancel still disposes owner");
        }
        using(var stop=new CancellationTokenSource()){
            stop.Cancel();var opens=0;Reject<OperationCanceledException>(()=>AyaneoOemRestoreWindowsPorts.OpenCeii(stop.Token,()=>{opens++;throw new Exception();},PrivateMutex),"cancelled factory entered");Check(opens==0,"cancelled factory stays dormant");
        }
        Reject<IOException>(()=>AyaneoOemRestoreWindowsPorts.OpenCeii(default,()=>throw new IOException("factory failure"),PrivateMutex),"failed factory did not propagate");
        var openFailure=new MemoryKernel{OpenFails=true};using(var io=CheckedIo(openFailure))Check(!AyaneoOemLightingRestore.Apply(modern,io,default).RequestCompleted,"failed kernel open not completed");Check(openFailure.Disposes==1&&openFailure.Calls.Count==0,"failed open retains owner until disposal");
        var closeFailure=new MemoryKernel{DisposeFails=true};
        var actual=typeof(HandheldCompanion.Devices.AYANEOAIRPlusAMD);var assembly=typeof(AyaneoOemLightingRestoreTests).Assembly;
        Reject<IOException>(()=>Program.ExecuteAyaneoOemRestore(actual,actual.FullName,"memory",assembly,15000,(_,_,_,token)=>CheckedIo(closeFailure,token)),"helper cannot return success after disposal failure");
        // The real consumer receipt must preserve a low-level refusal.
        var refused=new MemoryKernel{FailAt=7,Failure="nack"};
        var receipt=Program.ExecuteAyaneoOemRestore(actual,actual.FullName,"memory",assembly,15000,(_,_,_,token)=>CheckedIo(refused,token));
        Check(!(bool)receipt["ownershipRequestCompleted"]!&&!(bool)receipt["ownershipReadbackAvailable"]!&&!(bool)receipt["physicalEffectVerified"]!,"actual helper never promotes rejected write or physical effect");
        var next=new MemoryKernel();using(var io=CheckedIo(next))Check(AyaneoOemLightingRestore.Apply(modern,io,default).RequestCompleted,"all failure branches return both named bus leases");
    }
    private sealed class MemoryKernel:ISuperXKernelChannel
    {
        public readonly List<(uint Code,byte[] Input)> Calls=[];
        public int FailAt=-1,Disposes;public string Failure="";public bool OpenFails,DisposeFails;
        public Action<int>? AfterCall;
        public void Open(string path){Check(path==@"\\.\PawnIO","memory driver path");if(OpenFails)throw new IOException("open failed");}
        public SuperXIoResult Ioctl(uint code,byte[] input){var n=Calls.Count;Calls.Add((code,input.ToArray()));if(n==FailAt){if(Failure=="throw")throw new IOException("transport failed");if(Failure=="length")return new(true,1,0);return new(false,0,5);}AfterCall?.Invoke(n);return new(true,0,0);}
        public void Dispose(){Disposes++;if(DisposeFails)throw new IOException("close failed");}
    }
    private static int PinnedLibraryOnly()
    {
        checks=0;
        var root=Environment.GetEnvironmentVariable("YMCC_OEM_FIXED_LPC_ROOT") ?? throw new InvalidOperationException("explicit fixed library root required");
        var create=typeof(AyaneoOemRestoreWindowsPorts).GetMethod("CreateCheckedSession",BindingFlags.NonPublic|BindingFlags.Static)!;
        // Session construction/loading is dormant. No SelectSlot, WritePort,
        // kernel creation, driver operation or product mutex is called here.
        using(var session=(ISuperXPortSession)create.Invoke(null,new object[]{root,CancellationToken.None})!)
            Check(session is SuperXCheckedLpcSession,"production factory loads the exact existing library and constructs a dormant session");
        var bytes=SuperXFixedLpcModule.ReadImage();
        Check(bytes.Length>0&&Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(bytes)).Equals(SuperXFixedLpcModule.ModuleSha256,StringComparison.OrdinalIgnoreCase),"actual pinned embedded LpcIO module bytes");
        var rejected=Path.Combine(Environment.CurrentDirectory,"rejected-library");Directory.CreateDirectory(rejected);
        File.WriteAllBytes(Path.Combine(rejected,"LibreHardwareMonitorLib.dll"),File.ReadAllBytes(Path.Combine(root,"LibreHardwareMonitorLib.dll")).Concat(new byte[]{0}).ToArray());
        foreach(var path in new[]{rejected,Path.Combine(Environment.CurrentDirectory,"missing-library")}){
            try{create.Invoke(null,new object[]{path,CancellationToken.None});throw new Exception("wrong or missing runtime admitted");}
            catch(TargetInvocationException e) when(e.InnerException is YeMan.SharedRuntime.RuntimeDependencyFailureException error){
                Check(error.Code==(path==rejected?"DISK_HASH_MISMATCH":"RUNTIME_FILE_UNAVAILABLE"),"library failure remains precise before dormant session");
            }
        }
        return checks;
    }
    public static int Main(string[] args){
        if(args.Length!=1||args[0] is not ("--oem-restore-only" or "--oem-library-only"))throw new ArgumentException("explicit OEM-only software selection required");
        var n=args[0]=="--oem-library-only"?PinnedLibraryOnly():Run();
        Console.WriteLine(JsonSerializer.Serialize(new{status="PASS",checks=n,selection=args[0],hardwareIo=false,hcAssemblyLoaded=false,scope=args[0]=="--oem-library-only"?"actual pinned library/module load and dormant production session only":"actual OEM consumer and checked Windows factory/session with memory kernel; private named mutexes only"}));return 0;
    }
    private sealed class MemoryIo(LightingOemOwnershipProtocol protocol):IAyaneoOemRestoreIo
    {
        public LightingOemOwnershipProtocol Protocol=>protocol;
        public bool ReleaseOk=true,EntryFails,ExitFails,Disposed;public int ThrowAt=-1,LegacyCalls,Enters,Exits;
        public List<(ushort,byte)> Writes=[];public Action<int>? AfterWrite;
        public bool ReleaseLegacy(){LegacyCalls++;return ReleaseOk;}
        public void EnterRam(){Enters++;if(EntryFails)throw new IOException("entry failed");}
        public void ExitRam(){Exits++;if(ExitFails)throw new IOException("exit failed");}
        public void WritePort(ushort port,byte value){Writes.Add((port,value));if(Writes.Count==ThrowAt)throw new IOException("write failed");AfterWrite?.Invoke(Writes.Count);}
        public void Dispose(){Disposed=true;}
    }
}

namespace HandheldCompanion
{
    // Same type/signatures at the approved-HC reflection seam, pure memory only.
    public sealed class OpenLibSys:IDisposable
    {
        public static string Failure="";public static List<string> Events=[];public static List<(ushort,byte)> Ports=[];
        public static void Reset(){Failure="";Events.Clear();Ports.Clear();}
        public bool InitializeOls(){Events.Add("init");if(Failure=="init-throws")throw new IOException("injected init");return Failure!="init-refused";}
        public void EnterSuperIoConfig(ushort a,ushort b){if(a!=0x4E||b!=0x4F)throw new Exception("wrong config ports");Events.Add("enter");if(Failure=="entry-throws")throw new IOException("injected enter");}
        public void ExitSuperIoConfig(ushort a,ushort b){Events.Add("exit");if(Failure=="exit-throws")throw new IOException("injected exit");}
        public void WriteIoPortByte(ushort port,byte value){Ports.Add((port,value));}
        public void Dispose(){Events.Add("dispose");if(Failure=="dispose-throws")throw new IOException("injected dispose");}
    }
}

using System.Diagnostics;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using Microsoft.Win32;
using YeManBatteryHost;
using YeManFanBackend.Capabilities;

// Bounded extension of BatteryHostTests: real router/controllers and ABI, with
// memory journals, recorded EC, virtual time and injected event/owner ports.
internal static class InProcessFixture
{
    static int count;
    static void Check(bool value,string message="in-process assertion failed")
    {if(!value)throw new Exception(message);}
    static void Test(string name,Action test){test();count++;Console.WriteLine("PASS in-process: "+name);}
    static JsonElement Element(object value)=>JsonSerializer.SerializeToElement(value,Policy.Json);
    static object Request(string op,string? fence=null,int? target=null)=>new
    {op,fence,target,protocol=RuntimeContract.Protocol,sourceId=RuntimeContract.SourceId};
    static JsonElement Call(InProcessRuntime runtime,object request,bool roots=true,string? power=null,string? state=null)
    {
        object envelope=roots?new {powerRoot=power??@"C:\fixture\PowerControl",stateRoot=state??@"C:\fixture\State",request}:new {request};
        using var document=JsonDocument.Parse(runtime.Invoke(JsonSerializer.SerializeToUtf8Bytes(envelope,Policy.Json)));
        return document.RootElement.Clone();
    }
    static bool Ok(JsonElement result)=>result.TryGetProperty("ok",out var ok)&&ok.GetBoolean();
    static string Fence(JsonElement result)=>result.GetProperty("fence").GetString()!;
    static void Error(JsonElement result)=>Check(result.TryGetProperty("error",out _),"expected JSON error: "+result);

    sealed class Token(Action? dispose=null) : IDisposable
    {public void Dispose()=>dispose?.Invoke();}
    sealed class Clock
    {
        public long Now,Next;
        public Action? Callback;
        public int Starts,Stops,Fires;
        public TimeSpan Interval;
        public IDisposable Schedule(Action tick,TimeSpan interval)
        {
            Check(Starts++==0,"timer restarted");Callback=tick;Interval=interval;Next=Now+(long)interval.TotalMilliseconds;
            return new Token(()=>Stops++);
        }
        public void Advance(long milliseconds)
        {
            long until=Now+milliseconds;
            while(Stops==0&&Callback is not null&&Next<=until)
            {Now=Next;Next+=(long)Interval.TotalMilliseconds;Fires++;Callback();}
            Now=until;
        }
        public void Late()=>Callback?.Invoke();
    }
    sealed class DisplayPort : IEcAccess
    {
        public byte Raw=180;
        public int Reads,Writes;
        public byte Read(ushort register)
        {Check(register==HcSecondaryDisplayCapabilityCatalog.BrightnessRegister,"unexpected display read");Reads++;return Raw;}
        public void Write(ushort register,byte value)
        {
            Check(register==HcSecondaryDisplayCapabilityCatalog.BrightnessRegister||
                register==HcSecondaryDisplayCapabilityCatalog.LatchRegister,"unexpected display write");
            Writes++;if(register==HcSecondaryDisplayCapabilityCatalog.BrightnessRegister)Raw=value;
        }
        public void Dispose(){}
    }
    sealed class Harness : IDisposable
    {
        public readonly Rig Battery=new("ayaneo-cec-bypass");
        public readonly DisplayPort Display=new();
        public readonly Clock Clock=new();
        public readonly InProcessRuntime Runtime;
        public int Domains,Batteries,Displays,Subscriptions,Unsubscriptions,Owners,OwnerStops;
        public Action<PowerModes>? Power;
        public Action? Ending;
        public Func<IChargeProvider>? Open;
        public Action? BeforeDomains;
        public Harness(Func<IDisposable>? acquireOwner=null,TimeSpan? timeout=null)
        {
            Runtime=new InProcessRuntime((power,state,ready)=>
            {
                BeforeDomains?.Invoke();Domains++;
                Check(power==@"C:\fixture\PowerControl"&&state==@"C:\fixture\State","unexpected bound roots");
                return new HostDomainRouter(()=>
                {
                    Batteries++;
                    Battery.Controller=new Controller(Battery.Id,Battery.Store,_=>
                    {Battery.Opens++;return Open?.Invoke()??Battery.Device;},()=> (Battery.Present,Battery.Ac,Battery.Percent));
                    return Battery.Controller;
                },domainReady=>
                {
                    Displays++;
                    return new SecondaryDisplayController(new Identity("AYANEO","FLIP DS","","","",2,80),
                        Battery.Store,_=>Display,domainReady);
                },ready);
            },Clock.Schedule,(power,ending)=>
            {
                Subscriptions++;Power=power;Ending=ending;
                return new Token(()=>Unsubscriptions++);
            },timeout,acquireOwner??(()=>{Owners++;return new Token(()=>OwnerStops++);}));
        }
        public JsonElement Call(string op,string? fence=null,int? target=null)=>InProcessFixture.Call(Runtime,Request(op,fence,target));
        public JsonElement Stop()=>InProcessFixture.Call(Runtime,new {op="shutdown"},false);
        public void Dispose(){_ = Runtime.Shutdown();}
    }
    sealed class BlockingProvider(Fake inner) : IChargeProvider
    {
        public readonly ManualResetEventSlim Entered=new(false),Continue=new(false);
        public bool Block;
        public bool CanReadLimit=>inner.CanReadLimit;
        public Reading Read()
        {
            if(Block){Entered.Set();if(!Continue.Wait(TimeSpan.FromSeconds(3)))throw new IOException("fixture read timeout");}
            return inner.Read();
        }
        public void Write(int value,Reading before)=>inner.Write(value,before);
        public void Dispose(){}
    }

    public static void Run()
    {
        Test("pure hello, malformed input and validation create no owner/domain/timer",()=>
        {
            using var h=new Harness();
            var hello=Call(h.Runtime,new {op="hello"},false);
            Check(Ok(hello)&&hello.GetProperty("execution").GetString()=="in-process");
            Check(hello.GetProperty("processId").GetInt32()==Environment.ProcessId&&hello.GetProperty("pollingIntervalMs").GetInt32()==60000);
            Check(hello.GetProperty("sourceId").GetString()==RuntimeContract.SourceId);
            Error(Call(h.Runtime,new {op="get"}));
            Error(Call(h.Runtime,new {op="get",protocol=RuntimeContract.Protocol,sourceId="wrong"}));
            Error(Call(h.Runtime,Request("get"),power:"relative"));
            Error(Call(h.Runtime,Request("get"),state:""));
            Error(Call(h.Runtime,Request("get"),false));
            Error(Call(h.Runtime,Request("unknown")));
            using var invalid=JsonDocument.Parse(h.Runtime.Invoke(Encoding.UTF8.GetBytes("{")));Error(invalid.RootElement);
            Check(h.Domains==0&&h.Owners==0&&h.Subscriptions==0&&h.Clock.Starts==0);
            Check(h.Stop().GetProperty("cleanupCompleted").GetBoolean());
            Check(h.Domains==0&&h.Owners==0);
        });
        Test("user get/set, exact 60s boundary, inactive ticks, release/refresh/restore and roots",()=>
        {
            using var h=new Harness();h.Battery.Percent=90;
            var read=h.Call("get");Check(Ok(read)&&h.Batteries==1&&h.Displays==0);
            int idle=h.Battery.Device.Reads;h.Clock.Advance(60000);Check(h.Battery.Device.Reads==idle,"inactive timer touched hardware");
            Check(Ok(h.Call("set",Fence(read),80))&&h.Battery.Device.Raw==0x55);
            Error(Call(h.Runtime,Request("get"),state:@"C:\other"));
            Check(h.Domains==1&&h.Owners==1&&h.Clock.Starts==1&&h.Clock.Interval.TotalMilliseconds==60000);
            int active=h.Battery.Device.Reads;h.Battery.Percent=70;
            h.Clock.Advance(59999);Check(h.Battery.Device.Reads==active&&h.Battery.Device.Raw==0x55,"early tick");
            h.Clock.Advance(1);Check(h.Battery.Device.Reads>active&&h.Battery.Device.Raw==0xaa,"60 second tick missing");
            Check(Ok(h.Call("release"))&&h.Battery.Device.Raw==0xaa);
            int released=h.Battery.Device.Reads;h.Clock.Advance(60000);Check(h.Battery.Device.Reads==released);
            Check(Ok(h.Call("refresh"))&&Ok(h.Call("restore")));
            Check(h.Call("get").GetProperty("requestedLimit").GetInt32()==100,"release restarted old target");
            Check(!Ok(h.Call("set","stale",80)),"stale fence accepted");
            Check(Ok(h.Stop())&&h.Clock.Stops==1&&h.Unsubscriptions==1&&h.OwnerStops==1);
        });
        Test("display-only operations isolate corrupt battery journals and dormant charge policy",()=>
        {
            using var h=new Harness();h.Battery.Store.Data[Controller.RecoveryFile]="{bad journal";
            var read=h.Call("secondaryDisplay.get");Check(Ok(read)&&h.Displays==1&&h.Batteries==0);
            Check(Ok(h.Call("secondaryDisplay.set",Fence(read),50))&&h.Display.Raw==127);
            read=h.Call("secondaryDisplay.get");Check(Ok(h.Call("secondaryDisplay.toggle",Fence(read)))&&h.Display.Raw==0);
            read=h.Call("secondaryDisplay.get");Check(Ok(h.Call("secondaryDisplay.toggle",Fence(read)))&&h.Display.Raw==127);
            Check(Ok(h.Call("secondaryDisplay.release")));
            int reads=h.Display.Reads;h.Clock.Advance(120000);
            Check(h.Display.Reads==reads&&h.Battery.Opens==0&&h.Batteries==0,"display timer created battery");
            Check(h.Battery.Store.Data[Controller.RecoveryFile]=="{bad journal");
            Check(Ok(h.Stop())&&h.Display.Raw==127&&h.Batteries==0);
        });
        Test("suspend/resume guards, terminal seal and late callbacks never restart",()=>
        {
            using var h=new Harness();h.Battery.Percent=90;
            var read=h.Call("get");Check(Ok(h.Call("set",Fence(read),80)));
            h.Power!(PowerModes.Suspend);Check(h.Battery.Device.Raw==0xaa);
            int reads=h.Battery.Device.Reads;h.Clock.Advance(60000);Check(h.Battery.Device.Reads==reads);
            Error(h.Call("secondaryDisplay.get"));Check(h.Displays==0,"suspend created display");
            h.Power(PowerModes.Resume);Check(h.Battery.Device.Raw==0x55);
            h.Ending!();var receipt=h.Stop();Check(Ok(receipt)&&receipt.GetProperty("cleanupCompleted").GetBoolean());
            Check(receipt.GetProperty("hardwareRestoration").GetString()=="not-confirmed");
            Check(h.Battery.Device.Raw==0xaa&&h.OwnerStops==1);
            reads=h.Battery.Device.Reads;int writes=h.Battery.Device.Writes;
            h.Clock.Late();h.Power(PowerModes.Resume);h.Power(PowerModes.Suspend);h.Ending();
            Error(h.Call("get"));Error(h.Call("secondaryDisplay.get"));
            Check(h.Battery.Device.Reads==reads&&h.Battery.Device.Writes==writes&&h.Clock.Starts==1&&h.OwnerStops==1);
            Check(Ok(Call(h.Runtime,new {op="hello"},false)),"sealed hello lost identity");
        });
        Test("unconfirmed restoration retains the original journal and reports no hardware proof",()=>
        {
            using var h=new Harness();h.Battery.Percent=90;
            var read=h.Call("get");Check(Ok(h.Call("set",Fence(read),80)));
            string journal=h.Battery.Store.Data[Controller.RecoveryFile];h.Battery.Device.FailRead=true;
            var receipt=h.Stop();Check(receipt.GetProperty("cleanupCompleted").GetBoolean());
            Check(receipt.GetProperty("hardwareRestoration").GetString()=="not-confirmed");
            Check(h.Battery.Store.Data[Controller.RecoveryFile]==journal&&h.Battery.Device.Raw==0x55,"unconfirmed journal lost");
        });
        Test("blocked device operation has bounded shutdown and holds owner until cleanup",()=>
        {
            using var h=new Harness(timeout:TimeSpan.FromMilliseconds(60));
            var block=new BlockingProvider(h.Battery.Device);h.Open=()=>block;h.Battery.Percent=90;
            var read=h.Call("get");Check(Ok(h.Call("set",Fence(read),80)));block.Block=true;
            var operation=Task.Run(()=>h.Call("get"));
            try
            {
                Check(block.Entered.Wait(TimeSpan.FromSeconds(1)),"fixture operation did not enter");
                var watch=Stopwatch.StartNew();var receipt=h.Stop();watch.Stop();
                Check(watch.ElapsedMilliseconds<1000&&!Ok(receipt)&&receipt.GetProperty("cleanupPending").GetBoolean(),"shutdown waited for domain gate");
                Check(h.OwnerStops==0&&h.Battery.Store.Data.ContainsKey(Controller.RecoveryFile));
                Error(h.Call("get"));h.Clock.Late();h.Power!(PowerModes.Resume);
            }
            finally{block.Block=false;block.Continue.Set();}
            Check(operation.Wait(TimeSpan.FromSeconds(1)),"fixture operation stuck");
            Check(SpinWait.SpinUntil(()=>h.OwnerStops==1,1000),"lease released before/without cleanup");
            Check(h.Battery.Device.Raw==0xaa&&Ok(h.Stop()));
        });
        Test("shutdown during lazy initialization cannot publish a new timer or open a domain",()=>
        {
            using var h=new Harness(timeout:TimeSpan.FromMilliseconds(60));
            using var entered=new ManualResetEventSlim(false);using var resume=new ManualResetEventSlim(false);
            h.BeforeDomains=()=>{entered.Set();if(!resume.Wait(TimeSpan.FromSeconds(3)))throw new IOException("fixture init timeout");};
            var operation=Task.Run(()=>h.Call("get"));
            try
            {
                Check(entered.Wait(TimeSpan.FromSeconds(1)));Check(h.Stop().GetProperty("cleanupPending").GetBoolean());
                Check(h.Clock.Starts==0&&h.Subscriptions==0&&h.OwnerStops==0);
            }
            finally{resume.Set();}
            Check(operation.Wait(TimeSpan.FromSeconds(1)));Error(operation.Result);
            Check(SpinWait.SpinUntil(()=>h.OwnerStops==1,1000));
            Check(h.Batteries==0&&h.Displays==0&&h.Clock.Starts==0&&h.Subscriptions==0);
        });
        Test("dedicated owner thread rejects a second runtime before facts and domains",()=>
        {
            string name=@"Local\YMCC.BatteryCharge.InProcessFixture."+Guid.NewGuid().ToString("N");
            using var first=new Harness(()=>InProcessOwnerLease.Acquire(name));
            using var second=new Harness(()=>InProcessOwnerLease.Acquire(name));
            Check(Ok(first.Call("get")));
            var busy=second.Call("get");Error(busy);Check(busy.GetProperty("error").GetString()!.Contains("owner-busy"));
            Check(second.Domains==0&&second.Batteries==0&&second.Subscriptions==0&&second.Clock.Starts==0);
            Check(Ok(first.Stop()));Check(Ok(second.Call("get")),"owner thread did not release mutex");
            Check(Ok(second.Stop()));
        });
        Test("late owner handshake self-cleans and a timed-out join remains retryable",()=>
        {
            string name=@"Local\YMCC.BatteryCharge.InProcessFixture."+Guid.NewGuid().ToString("N");
            using var entered=new ManualResetEventSlim(false);using var resume=new ManualResetEventSlim(false);
            var lease=new InProcessOwnerLease(name,()=>
            {entered.Set();if(!resume.Wait(TimeSpan.FromSeconds(3)))throw new IOException("fixture owner timeout");},TimeSpan.FromMilliseconds(40));
            try
            {
                bool rejected=false;
                try{lease.Start(TimeSpan.FromMilliseconds(80));}catch(IOException){rejected=true;}
                Check(rejected&&entered.IsSet&&!lease.Completion.IsCompleted,"late startup was not canceled");
                bool pending=false;
                try{lease.Dispose();}catch(IOException){pending=true;}
                Check(pending&&!lease.Completion.IsCompleted,"join timeout was not observable");
            }
            finally{resume.Set();}
            Check(lease.Completion.Wait(TimeSpan.FromSeconds(1)),"owner did not dispose its stop event after timeout");
            lease.Dispose();lease.Dispose();
            using var next=InProcessOwnerLease.Acquire(name);
        });
        Test("actual Cdecl ABI owns exact UTF8 bytes and returns JSON or empty transport failure",Abi);
        Console.WriteLine($"{count} in-process groups passed; virtual 60s clock, memory journals, fake device ports only.");
    }

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    delegate int NativeInvoke(nint input,int length,nint output,nint outputLength);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    delegate void NativeFree(nint memory);
    static void Abi()
    {
        var invoke=Marshal.GetDelegateForFunctionPointer<NativeInvoke>(typeof(InProcessEntry).GetMethod("Invoke")!.MethodHandle.GetFunctionPointer());
        var free=Marshal.GetDelegateForFunctionPointer<NativeFree>(typeof(InProcessEntry).GetMethod("Free")!.MethodHandle.GetFunctionPointer());
        nint pointer=Marshal.AllocHGlobal(256),output=Marshal.AllocHGlobal(nint.Size),length=Marshal.AllocHGlobal(sizeof(int));
        try
        {
            JsonElement Invoke(string json)
            {
                var input=Encoding.UTF8.GetBytes(json);Marshal.Copy(input,0,pointer,input.Length);
                Check(invoke(pointer,input.Length,output,length)==0,"ABI transport failed");
                nint memory=Marshal.ReadIntPtr(output);int size=Marshal.ReadInt32(length);
                Check(memory!=0&&size>0);
                try
                {
                    var bytes=new byte[size];Marshal.Copy(memory,bytes,0,size);
                    Check(bytes[^1]==(byte)'}'&&!bytes.Contains((byte)0),"ABI length includes terminator/trailing bytes");
                    using var document=JsonDocument.Parse(bytes);return document.RootElement.Clone();
                }
                finally{free(memory);}
            }
            var identity=Invoke("{\"request\":{\"op\":\"hello\"}}");
            Check(Ok(identity)&&identity.GetProperty("processId").GetInt32()==Environment.ProcessId);
            Check(identity.GetProperty("execution").GetString()=="in-process"&&identity.GetProperty("pollingIntervalMs").GetInt32()==60000);
            Error(Invoke("{"));Error(Invoke("{\"request\":{\"op\":\"get\"}}"));
            foreach(int size in new[]{-1,0,InProcessEntry.MaxRequestBytes+1})
            {
                Marshal.WriteIntPtr(output,(nint)123);Marshal.WriteInt32(length,123);
                Check(invoke(pointer,size,output,length)!=0&&Marshal.ReadIntPtr(output)==0&&Marshal.ReadInt32(length)==0);
            }
            Check(invoke(0,1,output,length)!=0&&Marshal.ReadIntPtr(output)==0&&Marshal.ReadInt32(length)==0);
            Check(invoke(pointer,1,0,length)!=0&&Marshal.ReadInt32(length)==0);
            Check(invoke(pointer,1,output,0)!=0&&Marshal.ReadIntPtr(output)==0);
            Check(Ok(Invoke("{\"request\":{\"op\":\"shutdown\"}}")));
            Check(Ok(Invoke("{\"request\":{\"op\":\"shutdown\"}}")));
            free(0);
        }
        finally{Marshal.FreeHGlobal(pointer);Marshal.FreeHGlobal(output);Marshal.FreeHGlobal(length);}
    }
}

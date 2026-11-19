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

// Directed extension of BatteryHostTests using its original Rig/MemoryStore.
// Actual in-process runtime/router/controller/EcProvider, inert leaf IO only.
internal static class SoftwarePolicyStarvationFixture
{
    internal static readonly string[] Names=["ready-after-collector-read","ready-after-journal","ready-after-ec-final-read","suspend-while-read-blocked","shutdown-while-read-blocked","valid-tick-control","suspend-restoration-control","owned-pause-resume-read-loss-control","suspend-refusal-resumes-current-policy","admission-rejection-clear-failure-remains-unconfirmed"];
    sealed class Token(Action close):IDisposable {public void Dispose()=>close();}
    sealed class Journal(MemoryStore memory):Store("unused-inert-battery-journal")
    {
        public Action? Saved;
        public override T? Load<T>(string name) where T:default=>memory.Load<T>(name);
        public override void Save<T>(string name,T value){memory.Save(name,value);if(name==Controller.RecoveryFile)Saved?.Invoke();}
        public override void Clear(string name)=>memory.Clear(name);
    }
    sealed class Port:IEcAccess
    {
        public byte Raw=0xaa;
        public int Reads,Opens,Closes;
        public Action? AfterRead;
        public readonly List<int> Writes=[];
        public byte Read(ushort register)
        {
            if(register!=0x1e)throw new IOException("Unexpected fixture address");
            Reads++;var value=Raw;AfterRead?.Invoke();return value;
        }
        public void Write(ushort register,byte value)
        {if(register!=0x1e)throw new IOException("Unexpected fixture write address");Writes.Add(value);Raw=value;}
        public void Dispose(){Closes++;}
    }
    static JsonElement Call(InProcessRuntime runtime,string op,string? fence=null,int? target=null)
    {
        var input=new {powerRoot=@"C:\fixture\PowerControl",stateRoot=@"C:\fixture\State",request=new {op,fence,target,protocol=RuntimeContract.Protocol,sourceId=RuntimeContract.SourceId}};
        using var response=JsonDocument.Parse(runtime.Invoke(JsonSerializer.SerializeToUtf8Bytes(input,Policy.Json)));
        return response.RootElement.Clone();
    }
    static bool Ok(JsonElement value)=>value.TryGetProperty("ok",out var ok)&&ok.GetBoolean();
    internal static void Run(string scenario,string output)
    {
        if(!Names.Contains(scenario,StringComparer.Ordinal))throw new ArgumentException("Unknown selected scenario");
        var rig=new Rig("ayaneo-cec-bypass");var port=new Port();var journal=new Journal(rig.Store);
        bool ready=true;Func<bool>? runtimeReady=null;Action? tick=null;int owners=0,ownerCloses=0,timers=0,timerCloses=0,events=0,eventCloses=0;
        Controller? controller=null;
        var runtime=new InProcessRuntime((_,_,current)=>
        {
            runtimeReady=current;
            return new HostDomainRouter(()=>controller=new Controller(rig.Id,journal,route=>new EcProvider(route,_=>{port.Opens++;return port;}),()=> (rig.Present,rig.Ac,rig.Percent),()=>current()&&ready),_=>throw new IOException("Display not selected"),current);
        },(callback,_)=>{timers++;tick=callback;return new Token(()=>timerCloses++);},(_,_)=>{events++;return new Token(()=>eventCloses++);},TimeSpan.FromMilliseconds(60),()=>{owners++;return new Token(()=>ownerCloses++);});
        using var entered=new ManualResetEventSlim();using var release=new ManualResetEventSlim();
        Task? worker=null,boundary=null;string? error=null;bool setup=false,joined=false,sealedBeforeRelease=false,stopped=false;
        int baselineWrites=0;JsonElement stopReceipt=default;
        try
        {
            rig.Percent=scenario is "suspend-restoration-control" or "owned-pause-resume-read-loss-control"?90:75;
            var initial=Call(runtime,"get");if(!Ok(initial))throw new IOException("Initial read: "+initial);
            var applied=Call(runtime,"set",initial.GetProperty("fence").GetString(),80);
            if(!Ok(applied)||tick is null||controller is null)throw new IOException("Setup apply: "+applied);
            baselineWrites=port.Writes.Count;
            if(scenario=="admission-rejection-clear-failure-remains-unconfirmed")
            {
                rig.Percent=90;int arm=1;port.AfterRead=()=>{if(Interlocked.Exchange(ref arm,0)!=0){ready=false;rig.Store.FailClear=true;entered.Set();}};
                tick();ready=true;runtime.PowerChanged(PowerModes.Resume);tick();setup=entered.IsSet&&!Ok(Call(runtime,"get"))&&rig.Store.Data.ContainsKey(Controller.RecoveryFile);
            }
            else if(scenario=="owned-pause-resume-read-loss-control")
            {
                if(port.Raw!=0x55)throw new IOException("Expected existing owned pause");
                rig.Percent=70;int arm=1;port.AfterRead=()=>{if(Interlocked.Exchange(ref arm,0)!=0){ready=false;entered.Set();}};
                tick();setup=entered.IsSet;
            }
            else if(scenario=="suspend-restoration-control")
            {
                if(port.Raw!=0x55)throw new IOException("Expected an owned pause before restoration");
                runtime.PowerChanged(PowerModes.Suspend);setup=true;
            }
            else
            {
                if(port.Raw!=0xaa||baselineWrites!=0)throw new IOException("Setup unexpectedly wrote pause");
                rig.Percent=90;int readsSinceArm=0;
                if(scenario.StartsWith("ready-after-",StringComparison.Ordinal))
                {
                    if(scenario=="ready-after-journal")journal.Saved=()=>{ready=false;entered.Set();};
                    else port.AfterRead=()=>{if(++readsSinceArm==(scenario=="ready-after-collector-read"?1:2)){ready=false;entered.Set();}};
                    tick();setup=entered.IsSet;
                }
                else if(scenario is "suspend-while-read-blocked" or "shutdown-while-read-blocked" or "suspend-refusal-resumes-current-policy")
                {
                    int arm=1;
                    port.AfterRead=()=>{if(Interlocked.Exchange(ref arm,0)!=0){entered.Set();if(!release.Wait(TimeSpan.FromSeconds(2)))throw new IOException("Original two-second inert read barrier expired");}};
                    worker=Task.Run(tick);
                    if(!entered.Wait(TimeSpan.FromSeconds(2)))throw new IOException("Tick never reached read barrier");
                    if(scenario is "suspend-while-read-blocked" or "suspend-refusal-resumes-current-policy")
                    {
                        boundary=Task.Run(()=>runtime.PowerChanged(PowerModes.Suspend));
                        sealedBeforeRelease=SpinWait.SpinUntil(()=>runtimeReady is not null&&!runtimeReady(),1000);
                    }
                    else
                    {
                        stopReceipt=JsonSerializer.SerializeToElement(runtime.Shutdown(),Policy.Json);stopped=true;
                        sealedBeforeRelease=runtimeReady is not null&&!runtimeReady()&&stopReceipt.GetProperty("cleanupPending").GetBoolean()&&ownerCloses==0;
                    }
                    setup=sealedBeforeRelease;release.Set();
                    if(!worker.Wait(TimeSpan.FromSeconds(2)))throw new IOException("Tick did not return after read release");
                    if(boundary is not null&&!boundary.Wait(TimeSpan.FromSeconds(2)))throw new IOException("Suspend callback did not return");
                }
                else {tick();setup=true;}
            }
        }
        catch(Exception failure){error=failure.ToString();}
        finally
        {
            release.Set();port.AfterRead=null;journal.Saved=null;
            joined=(worker is null||worker.Wait(TimeSpan.FromSeconds(2)))&&(boundary is null||boundary.Wait(TimeSpan.FromSeconds(2)));
        }
        var writesBeforeResume=port.Writes.Skip(baselineWrites).ToArray();
        if(scenario=="suspend-refusal-resumes-current-policy"){runtime.PowerChanged(PowerModes.Resume);tick?.Invoke();}
        var beforeCleanup=port.Writes.Skip(baselineWrites).ToArray();
        var journalRetainedBeforeCleanup=rig.Store.Data.ContainsKey(Controller.RecoveryFile);
        var newPauses=beforeCleanup.Count(value=>value==0x55);
        var expected=scenario=="suspend-refusal-resumes-current-policy"?writesBeforeResume.Length==0&&newPauses==1&&port.Raw==0x55:
            scenario=="valid-tick-control"?newPauses==1&&port.Raw==0x55:
            scenario is "suspend-restoration-control" or "owned-pause-resume-read-loss-control"?newPauses==0&&beforeCleanup.SequenceEqual(new[]{0xaa})&&port.Raw==0xaa:newPauses==0;
        // Re-enable only the inert readiness port to allow cleanup of existing debt.
        ready=true;var final=JsonSerializer.SerializeToElement(runtime.Shutdown(),Policy.Json);
        if(!SpinWait.SpinUntil(()=>ownerCloses==owners,1000))error=(error??"")+" owner cleanup did not complete";
        final=JsonSerializer.SerializeToElement(runtime.Shutdown(),Policy.Json);
        int writesAtStop=port.Writes.Count;tick?.Invoke();runtime.PowerChanged(PowerModes.Resume);
        var cleanup=Ok(final)&&ownerCloses==owners&&timerCloses==timers&&eventCloses==events&&port.Opens==port.Closes&&port.Writes.Count==writesAtStop;
        bool passed=setup&&joined&&error is null&&expected&&cleanup;
        var report=new {scenario,status=passed?"PASS_SELECTED":setup&&joined&&error is null&&cleanup?"FAIL_SELECTED":"FAIL_SETUP_OR_CLEANUP",setupPassed=setup,expiredPauseSubmissions=scenario=="suspend-refusal-resumes-current-policy"?writesBeforeResume.Count(value=>value==0x55):scenario.EndsWith("control",StringComparison.Ordinal)?0:newPauses,policyPauseSubmissions=newPauses,journalRetainedBeforeCleanup,journalRetainedAfterCleanup=rig.Store.Data.ContainsKey(Controller.RecoveryFile),selection="software-policy-starvation",executedScenarios=new[]{scenario},writeOrderBeforeResume=writesBeforeResume,writeOrderBeforeCleanup=beforeCleanup,rawBeforeCleanup=beforeCleanup.LastOrDefault(port.Raw),allWriteOrder=port.Writes,owners,ownerCloses,timers,timerCloses,events,eventCloses,port.Opens,port.Closes,allOwnedTasksJoined=joined,cleanupPassed=cleanup,sealedBeforeRelease,shutdownWhileBlocked=stopped,error,realHardwareCalls=0,realCpuPressure=false,scope="Actual InProcessRuntime/HostDomainRouter/Controller/EcProvider with original Rig/MemoryStore and inert EC/scheduler/event/owner leaves. Barrier delay models starvation; not a physical CPU/watt/E-core test."};
        using var file=new FileStream(output,FileMode.CreateNew);JsonSerializer.Serialize(file,report,new JsonSerializerOptions{WriteIndented=true});
        Console.WriteLine(JsonSerializer.Serialize(report));Environment.ExitCode=passed?0:1;
    }
}

internal static class ChargeHardeningFixture
{
    sealed class Token : IDisposable{public void Dispose(){}}
    sealed class Port(Fake inner) : IChargeProvider
    {
        internal Action? AfterRead;internal int FailCloses,Closes,Opens;
        public bool CanReadLimit=>inner.CanReadLimit;
        public Reading Read(){var v=inner.Read();var f=AfterRead;AfterRead=null;f?.Invoke();return v;}
        public void Write(int value,Reading before)=>inner.Write(value,before);
        public void Dispose(){Closes++;if(FailCloses>0){FailCloses--;throw new IOException("transient exact owner close");}}
    }
    sealed class H : IDisposable
    {
        internal readonly Rig Rig;internal readonly Port Provider;
        internal SharedSystemPowerV1 Current=new(){Size=48,Version=1,Generation=10,PublicationRevision=1,SuspendSerial=1,Flags=3};
        readonly NativePowerReader.Callback facts=()=>7UL|8UL|16UL|(90UL<<8);
        readonly NativeSystemPowerReader.Callback reader;
        internal readonly ManualResetEventSlim Entered=new(),Release=new();
        internal volatile bool Hold;internal Action Tick=()=>{};
        internal readonly Action Notify;internal readonly Func<int> Outstanding,Running;
        readonly Func<byte[],byte[]> send;internal readonly object Runtime;
        internal H(string route="ayaneo-cec-bypass",bool negative=false,bool journal=false)
        {
            Rig=new Rig(route);Provider=new Port(Rig.Device);
            if(journal){Rig.Store.Save(Controller.SettingsFile,new Preference(1,Rig.Id.Key,route,80,true));Rig.Store.Save(Controller.RecoveryFile,new Recovery(1,Rig.Id.Key,route,100,80,false));}
            reader=(ref SharedSystemPowerV1 value,uint bytes)=>{value=Current;return bytes==48?1:0;};
            void Queue(Action work){if(!ThreadPool.QueueUserWorkItem(_=>{if(Hold){Entered.Set();if(!Release.Wait(3000))throw new IOException("queue barrier timeout");}work();}))throw new IOException("actual queue rejected");}
            HostDomainRouter Domains(string _,string __,Func<bool> ready,Func<(bool Present,bool Ac,int? Percent)> power)=>new(()=>new Controller(Rig.Id,Rig.Store,_=>{Provider.Opens++;return Provider;},power,ready),_=>throw new Exception("unexpected display"),ready);
            if(negative)
            {
#if BATTERY_HARDENING_NEGATIVES
                var r=new InProcessRuntimeMissingDrain(createPowerDomains:Domains,useSharedSystemPower:true,queueSystemPower:Queue,schedule:(tick,_)=>{Tick=tick;return new Token();},subscribe:(_,_)=>new Token(),acquireOwner:()=>new Token(),shutdownTimeout:TimeSpan.FromMilliseconds(100));
                Runtime=r;send=b=>r.Invoke(b);Notify=r.NotifySystemPower;Outstanding=()=>r.SystemPowerWorkOutstanding;Running=()=>r.SystemPowerWorkRunning;
#else
                throw new IOException("Directed negative requires generated hardening inputs and BATTERY_HARDENING_NEGATIVES");
#endif
            }
            else
            {
                var r=new InProcessRuntime(createPowerDomains:Domains,useSharedSystemPower:true,queueSystemPower:Queue,schedule:(tick,_)=>{Tick=tick;return new Token();},subscribe:(_,_)=>new Token(),acquireOwner:()=>new Token(),shutdownTimeout:TimeSpan.FromMilliseconds(100));
                Runtime=r;send=b=>r.Invoke(b);Notify=r.NotifySystemPower;Outstanding=()=>r.SystemPowerWorkOutstanding;Running=()=>r.SystemPowerWorkRunning;
            }
            _=Call("get");Need(SpinWait.SpinUntil(()=>Outstanding()==0,2000),"initial pool callback did not return");
        }
        internal JsonElement Call(string op)
        {
            object request=new{op,protocol=RuntimeContract.Protocol,sourceId=RuntimeContract.SourceId};
            object envelope=op=="shutdown"?new{request}:(object)new{request,powerRoot=@"C:\fixture\Power",stateRoot=@"C:\fixture\State",nativePowerReader=((ulong)Marshal.GetFunctionPointerForDelegate(facts)).ToString(),nativeSystemPowerReader=((ulong)Marshal.GetFunctionPointerForDelegate(reader)).ToString()};
            using var d=JsonDocument.Parse(send(JsonSerializer.SerializeToUtf8Bytes(envelope,Policy.Json)));return d.RootElement.Clone();
        }
        public void Dispose(){Hold=false;Release.Set();_=Call("shutdown");Need(SpinWait.SpinUntil(()=>Outstanding()==0,2000),"pool cleanup debt");_=Call("shutdown");GC.KeepAlive(facts);GC.KeepAlive(reader);Entered.Dispose();Release.Dispose();}
    }
    static void Need(bool v,string reason){if(!v)throw new Exception(reason);}
    static bool Complete(JsonElement e)=>e.GetProperty("cleanupCompleted").GetBoolean();
    static void Pool(bool running,bool negative)
    {
        using var h=new H(negative:negative);
        object? gate=null;
        try
        {
            if(running){gate=h.Runtime.GetType().GetField("operationGate",BindingFlags.NonPublic|BindingFlags.Instance)!.GetValue(h.Runtime)!;Monitor.Enter(gate);}
            else h.Hold=true;
            h.Notify();Need(running?SpinWait.SpinUntil(()=>h.Running()==1,2000):h.Entered.Wait(2000),"actual pool boundary absent");
            var receipt=h.Call("shutdown");
            Need(h.Outstanding()==1,"queued/running registration lost");
            if(negative)Need(Complete(receipt),"mutation did not expose premature completion");
            else Need(!Complete(receipt)&&receipt.GetProperty("cleanupPending").GetBoolean(),"complete while actual pool callback outstanding");
            Console.WriteLine(JsonSerializer.Serialize(new{scenario=running?"actual-pool-running-stop":"actual-pool-queued-stop",negative,receipt,outstanding=h.Outstanding(),running=h.Running()}));
        }
        finally{if(gate is not null)Monitor.Exit(gate);h.Hold=false;h.Release.Set();}
        Need(SpinWait.SpinUntil(()=>h.Outstanding()==0,2000),"callback did not finally return");Need(Complete(h.Call("shutdown")),"returned callback did not permit complete");
    }
    static void Refresh(bool serial)
    {
        using var h=new H("oxp-x1-amd-ec",journal:true);
        string settings=h.Rig.Store.Data[Controller.SettingsFile],journal=h.Rig.Store.Data[Controller.RecoveryFile];
        h.Provider.AfterRead=()=>{if(serial)h.Current.SuspendSerial++;else h.Current.Generation++;h.Current.PublicationRevision++;};
        var receipt=h.Call("refresh");Need(receipt.TryGetProperty("error",out _)||!receipt.GetProperty("ok").GetBoolean(),"old refresh published success");
        Need(h.Rig.Store.Data[Controller.SettingsFile]==settings&&h.Rig.Store.Data[Controller.RecoveryFile]==journal,"old refresh persisted acknowledgement");
        Console.WriteLine(JsonSerializer.Serialize(new{scenario=serial?"refresh-suspend-serial":"refresh-generation",receipt,persistenceUnchanged=true}));
    }
    static void Retry(bool exhaust)
    {
        var rig=new Rig("ayaneo-cec-bypass");var p=new Port(rig.Device);int opens=0;
        var c=new Controller(rig.Id,rig.Store,_=>{opens++;return p;},()=> (true,true,(int?)90),()=>true);
        Need(c.Apply(c.Read().Fence,80).Ok,"policy setup failed");p.FailCloses=exhaust?20:2;c.Suspend();int before=opens,closes=p.Closes;
        c.Resume();Need(opens==before,"resume opened a terminal before old close");
        for(int i=0;i<10;i++)c.Tick();
        if(exhaust)Need(opens==before&&p.Closes-closes==3,"retry was unbounded or opened new terminal");
        else Need(opens>before&&rig.Device.Raw==0x55,"existing tick did not return old owner and resume policy");
        p.FailCloses=0;c.Shutdown();Console.WriteLine(JsonSerializer.Serialize(new{scenario=exhaust?"cleanup-retry-budget":"cleanup-retry-recovers",opensBefore=before,opens,closeAttempts=p.Closes-closes}));
    }
    static void RefreshNegative()
    {
#if BATTERY_HARDENING_NEGATIVES
        var rig=new Rig();bool ready=true;var p=new Port(rig.Device);
        rig.Store.Save(Controller.SettingsFile,new Preference(1,rig.Id.Key,rig.Route.RouteId,80,true));rig.Store.Save(Controller.RecoveryFile,new Recovery(1,rig.Id.Key,rig.Route.RouteId,100,80,false));
        var c=new MissingRefreshController(rig.Id,rig.Store,_=>p,()=> (true,true,(int?)90),()=>ready);
        p.AfterRead=()=>ready=false;_=c.Acknowledge();Need(!rig.Store.Data.ContainsKey(Controller.RecoveryFile),"missing-refresh-check mutation not detected");Console.WriteLine("EXPECTED_FAIL refresh missing post-read admission cleared journal");
#else
        throw new IOException("Directed negative requires generated hardening inputs and BATTERY_HARDENING_NEGATIVES");
#endif
    }
    static void Health(bool negative)
    {
        var rig=new Rig("ayaneo-cec-bypass");int percent=50;
        Action suspend,resume,tick,shutdown;Func<Snapshot> read;Func<string,int,Snapshot> apply;
        if(negative)
        {
#if BATTERY_HARDENING_NEGATIVES
            var c=new HealthyJournalController(rig.Id,rig.Store,_=>rig.Device,()=> (true,true,(int?)percent),()=>true);
            suspend=c.Suspend;resume=c.Resume;tick=c.Tick;shutdown=c.Shutdown;read=c.Read;apply=c.Apply;
#else
            throw new IOException("Directed negative requires generated hardening inputs and BATTERY_HARDENING_NEGATIVES");
#endif
        }
        else
        {
            var c=new Controller(rig.Id,rig.Store,_=>rig.Device,()=> (true,true,(int?)percent),()=>true);
            suspend=c.Suspend;resume=c.Resume;tick=c.Tick;shutdown=c.Shutdown;read=c.Read;apply=c.Apply;
        }
        try
        {
            Need(apply(read().Fence,80).Ok,"healthy policy setup failed");suspend();resume();
            Need(rig.Store.Data.ContainsKey(Controller.RecoveryFile),"healthy active policy must retain ownership journal");
            int reads=rig.Device.Reads,writes=rig.Device.Writes;
            var stableRawBytes=new List<int>();
            for(int i=0;i<4;i++){tick();stableRawBytes.Add(rig.Device.Raw);}
            Need(stableRawBytes.All(value=>value==0xaa),"stable pre-threshold byte changed");
            percent=90;tick();int highRaw=rig.Device.Raw;bool pause=highRaw==0x55;
            percent=50;tick();int lowRaw=rig.Device.Raw;
            bool returned=rig.Device.Raw==0xaa;bool live=pause&&returned&&rig.Device.Reads-reads>=6&&rig.Device.Writes-writes==2;
            if(negative)Need(!live&&!pause,"healthy-journal semantic mutation was not detected");
            else Need(live,"healthy Resume journal stopped policy after three ticks");
            Console.WriteLine(JsonSerializer.Serialize(new{scenario="healthy-resume-after-four-stable-ticks",negative,stableTicks=4,stablePercent=50,target=80,highPercent=90,lowPercent=50,stableRawBytes,highRaw,lowRaw,executedTicks=6,actualReads=rig.Device.Reads-reads,actualWrites=rig.Device.Writes-writes,pause,returned,policyLive=live}));
            if(negative)Console.WriteLine("EXPECTED_FAIL healthy active journal misclassified as pending cleanup");
        }
        finally{shutdown();}
    }
    internal static void Run(string mode)
    {
        if(mode=="health-only"){Health(false);Console.WriteLine("PASS_HEALTH_ONLY original108Executed=false priorFourExecuted=false");return;}
        if(mode=="negative-health"){Health(true);return;}
        if(mode=="negative-drain"){Pool(false,true);Console.WriteLine("EXPECTED_FAIL missing drain accepted outstanding callback");return;}
        if(mode=="negative-refresh"){RefreshNegative();return;}
        if(mode!="positive")throw new Exception("unknown hardening selection");
        Pool(false,false);Pool(true,false);Refresh(false);Refresh(true);Retry(false);Retry(true);Health(false);
        Console.WriteLine("PASS_HARDENING scenarios=7 original108Executed=false");
    }
}

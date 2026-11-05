using System.Text.Json;
using YeManBatteryHost;

internal static class OneXFieldFeedbackFixture
{
    sealed class CallbackPorts(ushort expected,byte initial) : IEcAccess
    {
        internal byte Value=initial;
        internal int Reads,Writes,Opens,Closes;
        internal Action<int>? BeforeRead,BeforeOpen;
        public byte Read(ushort register)
        {
            if(register!=expected)throw new Exception("EC pre-read changed address space");
            Reads++;BeforeRead?.Invoke(Reads);return Value;
        }
        public void Write(ushort register,byte value)
        {
            if(register!=expected)throw new Exception("EC write changed address space");
            Writes++;Value=value;
        }
        public void Dispose(){Closes++;}
    }
    static EcProvider EcConsumer(Route route,CallbackPorts ports,bool direct,Action<bool,string> check)=>new(route,actual=>
    {
        check(actual==direct,"EC consumer changed the selected bus");
        ports.Opens++;ports.BeforeOpen?.Invoke(ports.Opens);return ports;
    });
    static void FinalWriteAdmission(Identity identity,Action<bool,string> check)
    {
        var route=Policy.Resolve(identity)!;
        foreach(var phase in new[]{"open","read"})
        foreach(var change in new[]{"stable","stop","power","ready","conflict"})
        {
            var ports=new CallbackPorts(0x04a3,100);var store=new MemoryStore();
            bool present=true,ready=true;int edges=0;Controller? controller=null;
            void Change(int ordinal)
            {
                // Read(), Apply preflight, then the real EcProvider.Write pre-read.
                if(ordinal!=3)return;
                edges++;
                if(change=="stop")controller!.Shutdown();
                else if(change=="power")present=false;
                else if(change=="ready")ready=false;
                else if(change=="conflict")ports.Value=95;
            }
            if(phase=="open")ports.BeforeOpen=Change;else ports.BeforeRead=Change;
            // Keep the normal IChargeProvider factory and real Controller.ProviderLease dispatch.
            controller=new Controller(identity,store,r=>EcConsumer(r,ports,true,check),()=> (present,true,90),()=>ready);
            var before=controller.Read();check(before.Ok&&before.CurrentLimit==100,"EC threshold precondition failed");
            var after=controller.Apply(before.Fence,80);
            check(edges==1&&ports.Reads>=3,"final EC pre-read callback did not execute");
            if(change=="stable")
                check(after.Ok&&after.CurrentLimit==80&&ports.Writes==1&&ports.Value==80,"admitted EC threshold did not write and read back exactly once");
            else
            {
                string reason=change switch
                {
                    "power"=>"未检测到内置电池",
                    "conflict"=>"充电状态已被其他软件改变，请重新读取",
                    _=>"正在停止、睡眠或恢复，充电设置稍后可用"
                };
                check(!after.Ok&&after.Reason==reason&&ports.Writes==0,"EC final pre-read stop/power/conflict bypassed admission: "+phase+"/"+change);
                check(ports.Value==(change=="conflict"?95:100),"rejected EC threshold changed the original byte");
            }
            int writes=ports.Writes;controller.Shutdown();
            check(ports.Writes==writes&&ports.Opens==ports.Closes,"EC final admission leaked access or replayed on shutdown");
            Console.WriteLine($"EC final admission {phase}/{change}: edges={edges}, writes={ports.Writes}, access={ports.Opens}/{ports.Closes}");
        }
        foreach(var routeId in new[]{"ayaneo-cec-bypass","ayaneo-ceii-bypass"})
        foreach(var ending in new[]{"suspend","stop"})
        {
            var rig=new Rig(routeId);bool direct=routeId=="ayaneo-ceii-bypass";
            byte original=direct?(byte)0x65:(byte)0xaa,paused=direct?(byte)1:(byte)0x55;
            var ports=new CallbackPorts(direct?(ushort)0xd1d1:(ushort)0x1e,original);var store=new MemoryStore();
            var controller=new Controller(rig.Id,store,r=>EcConsumer(r,ports,direct,check),()=> (true,true,90));
            var before=controller.Read();var applied=controller.Apply(before.Fence,80);
            check(applied.Ok&&ports.Writes==1&&ports.Value==paused,"software bypass precondition did not own its pause byte");
            if(ending=="suspend")controller.Suspend();else controller.Shutdown();
            check(ports.Writes==2&&ports.Value==original&&store.Load<Recovery>(Controller.RecoveryFile) is null,"final EC admission blocked software "+ending+" restoration: "+routeId);
            controller.Shutdown();
            check(ports.Writes==2&&ports.Opens==ports.Closes,"software return replayed or leaked EC access");
            Console.WriteLine($"EC software return {routeId}/{ending}: writes={ports.Writes}, original=0x{original:x2}, access={ports.Opens}/{ports.Closes}");
        }
    }
    internal static int Run()
    {
        string root=Environment.GetEnvironmentVariable("YMCC_ONEX_FIELD_FIXTURE_ROOT")??throw new IOException("fresh fixture evidence root required");
        Directory.CreateDirectory(root);TraceLog.Configure(root);
        int assertions=0;
        void Check(bool result,string reason){assertions++;if(!result)throw new Exception(reason);}
        var identity=new Identity("ONE-NETBOOK","ONEXPLAYER APEX","ONEXPLAYER APEX","AMD RYZEN AI MAX+ 395","field-bios",255,255);
        foreach(var observed in new[]{identity,identity with{EcMajor=0,EcMinor=3},identity with{EcMajor=1,EcMinor=2},identity with{Bios="0.12",EcMajor=0,EcMinor=12}})
        {
            int opens=0;
            var controller=new Controller(observed,new MemoryStore(),_=>{opens++;throw new Exception("rejected firmware opened hardware provider");},()=> (true,true,75));
            var result=controller.Read();
            Check(!result.Ok&&!result.Supported&&opens==0,"firmware rejection opened or certified provider");
            var records=File.ReadLines(Path.Combine(root,"battery-charge-host.log")).Select(text=>JsonDocument.Parse(text)).ToArray();
            try
            {
                var entry=records.Last(d=>d.RootElement.GetProperty("event").GetString()=="firmware-admission-rejected").RootElement.GetProperty("detail");
                var facts=entry.GetProperty("observed");var required=entry.GetProperty("required");
                Check(entry.GetProperty("route").GetString()=="oxp-x1-amd-ec"&&entry.GetProperty("deviceClass").GetString()=="OneXPlayerApex","actual APEX was substituted with Intel OXP3 route");
                Check(facts.GetProperty("ecMajor").GetInt32()==observed.EcMajor&&facts.GetProperty("ecMinor").GetInt32()==observed.EcMinor&&facts.GetProperty("bios").GetString()==observed.Bios,"firmware raw facts were replaced or omitted");
                Check(facts.GetProperty("ecMajorKnown").GetBoolean()==(observed.EcMajor<255)&&facts.GetProperty("ecMinorKnown").GetBoolean()==(observed.EcMinor<255),"unknown SMBIOS sentinel became a known version");
                Check(required.GetProperty("kind").GetString()=="hc-ec-components"&&required.GetProperty("majorMin").GetInt32()==1&&required.GetProperty("minorMin").GetInt32()==3,"required component gate was changed or hidden");
                Check(!entry.GetProperty("allowed").GetBoolean()&&!entry.GetProperty("providerOpened").GetBoolean()&&entry.GetProperty("scope").GetString()!.Contains("no-EC-IO"),"diagnostic scope became hardware evidence");
            }
            finally{foreach(var record in records)record.Dispose();}
            controller.Shutdown();Check(opens==0,"shutdown probed a rejected route");
        }
        int admitted=0;var valid=identity with{EcMajor=1,EcMinor=3};var route=Policy.Resolve(valid)!;
        var accepted=new Controller(valid,new MemoryStore(),_=>{admitted++;return new Fake(route);},()=> (true,true,75));
        Check(accepted.Read().Ok&&admitted==1,"valid source-defined version was disabled by diagnostic repair");
        accepted.Shutdown();
        FinalWriteAdmission(valid,Check);
        Console.WriteLine($"OneX firmware diagnostic assertions: {assertions}");return assertions;
    }
}

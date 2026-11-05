using System.Text.Json;
using YeManBatteryHost;

internal static class SuperXChargeLifecycleFixture
{
    static int checks;
    static void Check(bool value,string reason)
    {checks++;if(!value)throw new Exception(reason);}
    static readonly Identity Id=new("ONE-NETBOOK","ONEXPLAYER SUPER X","untrusted-system-product","AMD","",0,0);
    sealed class Token : IDisposable
    {
        internal int Calls;
        internal bool Fail;
        public void Dispose(){Calls++;if(Fail)throw new IOException("retained registration/owner");}
    }
    sealed class DebtProvider : IChargeProvider
    {
        internal bool Fail;
        internal int Closed,Reads,Writes;
        public bool CanReadLimit=>true;
        public Reading Read(){Reads++;return new(80,80);}
        public void Write(int value,Reading before){Writes++;}
        public void Dispose(){Closed++;if(Fail)throw new IOException("retained exact provider");}
    }
    sealed class Reader(Func<byte> read) : ISuperXChargeReader
    {
        public byte ReadLimit()=>read();
        public void Dispose(){}
    }
    sealed class Writer : ISuperXChargeWriter
    {
        internal int Writes;
        public void WriteLimit(byte value){Writes++;}
        public void Dispose(){}
    }
    static JsonElement Call(InProcessRuntime runtime,string root,string op)
    {
        object request=new {op,protocol=RuntimeContract.Protocol,sourceId=RuntimeContract.SourceId};
        object envelope=op=="shutdown"?new {request}:new {powerRoot=Path.Combine(root,"PowerControl"),stateRoot=Path.Combine(root,"State"),request};
        using var doc=JsonDocument.Parse(runtime.Invoke(JsonSerializer.SerializeToUtf8Bytes(envelope,Policy.Json)));
        return doc.RootElement.Clone();
    }
    static bool Flag(JsonElement result,string name)=>result.TryGetProperty(name,out var value)&&value.ValueKind==JsonValueKind.True;
    static Controller Controller(DebtProvider provider,Action? opened=null)=>new(Id,new MemoryStore(),_=>{opened?.Invoke();return provider;},()=> (true,true,80));
    static HostDomainRouter Router(Controller controller,Func<bool> ready)=>new(()=>controller,_=>throw new Exception("display domain must remain dormant"),ready);
    internal static int Run()
    {
        string root=Environment.GetEnvironmentVariable("YMCC_SUPERX_FIXTURE_ROOT")??throw new IOException("fresh fixture evidence root required");
        Directory.CreateDirectory(root);TraceLog.Configure(root);
        var provider=new DebtProvider{Fail=true};int opens=0;
        var controller=Controller(provider,()=>opens++);
        Check(!controller.Read().Ok&&opens==1&&provider.Closed==1,"initial cleanup failure was hidden");
        Check(!controller.Read().Ok&&opens==1,"new provider opened over exact cleanup debt");
        Check(!controller.Acknowledge().Ok&&opens==1&&provider.Closed==2,"refresh did not retry exact original provider");
        bool threw=false;try{controller.Shutdown();}catch(IOException){threw=true;}
        Check(threw&&opens==1&&provider.Closed==3,"shutdown lost cleanup debt");
        provider.Fail=false;Check(!controller.Acknowledge().Ok&&opens==1&&provider.Closed==4,"stopped controller reopened after exact debt cleanup");
        int writes=provider.Writes;controller.Shutdown();Check(provider.Writes==writes&&opens==1,"firmware threshold reset or endpoint reopened on stop");
        var refreshProvider=new DebtProvider{Fail=true};int refreshOpens=0;
        var refreshing=Controller(refreshProvider,()=>refreshOpens++);
        Check(!refreshing.Read().Ok&&refreshOpens==1,"refresh debt setup failed");
        refreshProvider.Fail=false;
        Check(refreshing.Acknowledge().Ok&&refreshOpens==2&&refreshProvider.Closed==3,"live refresh did not clear exact old debt before new endpoint");
        refreshing.Shutdown();

        foreach(bool powerChange in new[]{false,true})
        {
            int reads=0;bool ready=true,present=true;var writer=new Writer();
            var guarded=new Controller(Id,new MemoryStore(),_=>new SuperXChargeProvider(new Reader(()=>
            {
                reads++;if(reads==3){if(powerChange)present=false;else ready=false;}return 80;
            }),writer),()=> (present,true,80),()=>ready);
            var before=guarded.Read();Check(before.Ok,"precondition read failed");
            var after=guarded.Apply(before.Fence,85);
            Check(!after.Ok&&reads>=3&&writer.Writes==0,"stop/power change during final UMA pre-read admitted a port writer");
            guarded.Shutdown();Check(writer.Writes==0,"shutdown replayed an unadmitted threshold");
        }

        var debt=new DebtProvider{Fail=true};var timer=new Token{Fail=true};var events=new Token{Fail=true};var owner=new Token{Fail=true};
        int ownerOpens=0,domainOpens=0,endpointOpens=0;Action? tick=null;
        var runtime=new InProcessRuntime(createDomains:(_,_,ready)=>{domainOpens++;return Router(Controller(debt,()=>endpointOpens++),ready);},
            schedule:(callback,_)=>{tick=callback;return timer;},subscribe:(_,_)=>events,
            shutdownTimeout:TimeSpan.FromMilliseconds(200),acquireOwner:()=>{ownerOpens++;return owner;});
        Check(!Flag(Call(runtime,root,"get"),"ok")&&endpointOpens==1,"domain debt precondition failed");
        var receipt=Call(runtime,root,"shutdown");
        Check(!Flag(receipt,"ok")&&Flag(receipt,"cleanupPending")&&!Flag(receipt,"cleanupCompleted")&&owner.Calls==0,"failed registrations/domain released owner");
        Check(timer.Calls==1&&events.Calls==1&&debt.Closed==2,"shutdown did not attempt all exact resources");
        timer.Fail=false;events.Fail=false;receipt=Call(runtime,root,"shutdown");
        Check(Flag(receipt,"cleanupPending")&&owner.Calls==0&&timer.Calls==2&&events.Calls==2,"domain debt was replaced or owner escaped");
        debt.Fail=false;receipt=Call(runtime,root,"shutdown");
        Check(Flag(receipt,"cleanupPending")&&owner.Calls==1,"owner close failure was hidden");
        receipt=Call(runtime,root,"shutdown");
        Check(Flag(receipt,"cleanupPending")&&owner.Calls==2&&timer.Calls==2&&events.Calls==2,"successful resources closed again");
        owner.Fail=false;receipt=Call(runtime,root,"shutdown");
        Check(Flag(receipt,"ok")&&Flag(receipt,"cleanupCompleted")&&!Flag(receipt,"cleanupPending")&&owner.Calls==3,"explicit retry did not return retained owner");
        int debtCloses=debt.Closed;tick!();Check(Call(runtime,root,"get").TryGetProperty("error",out _)&&ownerOpens==1&&domainOpens==1&&endpointOpens==1,"late callback/request revived stopped generation");
        Check(Flag(Call(runtime,root,"shutdown"),"ok")&&owner.Calls==3&&debt.Closed==debtCloses&&debt.Writes==0,"completed shutdown replayed cleanup or firmware writes");

        // Alias registrations are disposed once even when used as timer and event token.
        var alias=new Token();var aliasOwner=new Token();
        var aliasRuntime=new InProcessRuntime(createDomains:(_,_,ready)=>Router(Controller(new DebtProvider()),ready),
            schedule:(_,_)=>alias,subscribe:(_,_)=>alias,acquireOwner:()=>aliasOwner);
        Check(Flag(Call(aliasRuntime,root,"get"),"ok"),"alias start failed");
        Check(Flag(Call(aliasRuntime,root,"shutdown"),"ok")&&alias.Calls==1&&aliasOwner.Calls==1,"aliased registration was closed twice");

        // A failed schedule cannot discard its already-returned subscription.
        var failedSubscription=new Token{Fail=true};var scheduleOwner=new Token();int subscriptions=0;
        var scheduleRuntime=new InProcessRuntime(createDomains:(_,_,ready)=>Router(Controller(new DebtProvider()),ready),
            schedule:(_,_)=>throw new IOException("schedule rejected"),subscribe:(_,_)=>{subscriptions++;return failedSubscription;},acquireOwner:()=>scheduleOwner);
        Check(Call(scheduleRuntime,root,"get").TryGetProperty("error",out _)&&failedSubscription.Calls==1,"startup cleanup failure hidden");
        Check(Call(scheduleRuntime,root,"get").TryGetProperty("error",out _)&&subscriptions==1,"new monitor created over failed subscription cleanup");
        Check(Flag(Call(scheduleRuntime,root,"shutdown"),"cleanupPending")&&scheduleOwner.Calls==0,"startup debt released owner");
        failedSubscription.Fail=false;Check(Flag(Call(scheduleRuntime,root,"shutdown"),"ok")&&scheduleOwner.Calls==1,"startup debt was not retained for explicit cleanup");

        // Real task/event ordering: stop seals admission while an admitted schedule
        // is still pending; its late returned timer belongs to the same cleanup.
        using var entered=new ManualResetEventSlim(false);using var finish=new ManualResetEventSlim(false);
        var lateTimer=new Token();var lateEvents=new Token();var lateOwner=new Token();int lateOwners=0;
        var pendingRuntime=new InProcessRuntime(createDomains:(_,_,ready)=>Router(Controller(new DebtProvider()),ready),
            schedule:(_,_)=>{entered.Set();if(!finish.Wait(TimeSpan.FromSeconds(3)))throw new IOException("fixture release absent");return lateTimer;},
            subscribe:(_,_)=>lateEvents,shutdownTimeout:TimeSpan.FromMilliseconds(100),acquireOwner:()=>{lateOwners++;return lateOwner;});
        var operation=Task.Run(()=>Call(pendingRuntime,root,"get"));
        try
        {
            Check(entered.Wait(TimeSpan.FromSeconds(3)),"pending operation never entered");
            receipt=Call(pendingRuntime,root,"shutdown");
            Check(Flag(receipt,"cleanupPending")&&!Flag(receipt,"cleanupCompleted")&&lateOwner.Calls==0,"timeout was treated as terminal cleanup");
            Check(Call(pendingRuntime,root,"get").TryGetProperty("error",out _)&&lateOwners==1,"stop admitted replacement owner");
        }
        finally{finish.Set();}
        Check(operation.Wait(TimeSpan.FromSeconds(3))&&operation.Result.TryGetProperty("error",out _),"late operation published success");
        Check(Flag(Call(pendingRuntime,root,"shutdown"),"ok")&&lateTimer.Calls==1&&lateEvents.Calls==1&&lateOwner.Calls==1,"late returned registration escaped drain");
        Console.WriteLine($"Super X lifecycle boundary assertions: {checks}");return checks;
    }
}
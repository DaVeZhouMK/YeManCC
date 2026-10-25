using System.Text.Json;
using System.Diagnostics;
using YeManInputHost.Diagnostics;
internal static partial class Program
{
    private static JsonElement Observe(HostSession session, ulong sequence=3)=>Send(session,"TUPLE_SUBMIT_STATS",sequence);
    private static bool HasObservation(JsonElement r)=>r.TryGetProperty("gameOutputObservation",out _);
    private static JsonElement Observation(JsonElement r)
    { Check(HasObservation(r),"GAME_OUTPUT_OBSERVATION_MISSING"); return r.GetProperty("gameOutputObservation"); }
    private static int RunGameGroups(bool candidate, List<Dictionary<string,object>>? more=null)
    {
        var rows = new List<Dictionary<string,object>>();
        rows.Add(Group("actual Host callback reaches current response",()=>{
            var session=Ready();var controller=session.ObservedController!;
            controller.Emit(0,2,new byte[]{3,0,0,128,64},41);
            var receipt=Observation(Observe(session));
            Check(receipt.GetProperty("sdkSequence").GetUInt32()==41,"SDK_SEQUENCE_NOT_OBSERVED");
            Check(!receipt.GetProperty("gameFeedbackForwardingProven").GetBoolean(),"FALSE_FORWARDING_PROOF");
        }));
        if(more is not null) rows.AddRange(more);
        var passed=rows.All(r=>(bool)r["pass"]);
        Console.WriteLine(JsonSerializer.Serialize(new{passed,candidate,cases=rows,deviceIo=false,realHostStarted=false,gameFeedbackForwardingProven=false}));
        return passed?0:1;
    }
    // CANDIDATE_ONLY
    private static int GameOutputMain()
    {
        var rows=new List<Dictionary<string,object>>();
        foreach(var pair in new[]{("steamdeck","steam-deck-composite"),("elite","xbox-elite-v2"),("dualsense-edge","dualsense-edge")})
        rows.Add(Group(pair.Item1+" admitted target subscribes exactly once and preserves payload",()=>{
            var session=new HostSession("fake-nonce");
            JsonElement Command(string cmd,ulong n){var m=Message(cmd,n);m["persona"]=pair.Item1;m["profileIdentity"]=pair.Item2;return Send(session,m,out _);}
            Fixture.OnObserve=()=>Fixture.Creates==0||Fixture.Sweeps>0?Empty():pair.Item1=="steamdeck"?Arrival(Fixture.NextRoot):new VirtualEndpointObservation(true,1,0,1,0,0,true,"persona-arrival");
            Accept(Command("HELLO",1));Check(session.ObservedController is null);
            var prepare=Command("PREPARE_TARGET",2);Accept(prepare);Check(!HasObservation(prepare));
            var controller=session.ObservedController!;Check(controller.SubscriberCount==1,"SUBSCRIPTION_MISSING");
            byte[] data={0,1,128,255};controller.Emit(0,2,data,17);Array.Fill<byte>(data,99);
            var r=Observation(Command("TUPLE_SUBMIT_STATS",3));
            Check(r.GetProperty("payloadBase64").GetString()=="AAGA/w==","REUSED_BUFFER_CORRUPTION");
            Check(r.GetProperty("reportId").GetInt32()==2&&r.GetProperty("payloadBytes").GetInt32()==4);
            Check(!r.GetProperty("protocolDecoded").GetBoolean()&&!r.GetProperty("physicalAcceptance").GetBoolean());
            Check(!HasObservation(Command("TUPLE_SUBMIT_STATS",4)),"DUPLICATE_DELIVERY");
        }));
        rows.Add(Group("callbacks coalesce within one owned mailbox",()=>{
            var s=Ready();for(uint i=0;i<100;i++)s.ObservedController!.Emit(1,9,new byte[]{(byte)i},i);
            var r=Observation(Observe(s));Check(r.GetProperty("observed").GetUInt64()==100&&r.GetProperty("coalesced").GetUInt64()==99);
            Check(r.GetProperty("source").GetInt32()==1&&r.GetProperty("sdkSequence").GetUInt32()==99);
            Check(r.GetProperty("payloadBase64").GetString()=="Yw==");
        }));
        rows.Add(Group("wrong event sender cannot publish",()=>{
            var s=Ready();s.ObservedController!.SnapshotCallback()!(new HMController(),new(HMOutputSource.Output,2,new byte[]{7},1));
            Check(!HasObservation(Observe(s)),"FOREIGN_SENDER_ACCEPTED");
        }));
        rows.Add(Group("wrong tuple rejection preserves pending observation",()=>{
            var s=Ready();s.ObservedController!.Emit(0,2,new byte[]{7},12);var wrong=Message("TUPLE_SUBMIT_STATS",3,run:"foreign");
            Check(!Send(s,wrong,out _).GetProperty("ok").GetBoolean());
            Check(Observation(Observe(s)).GetProperty("sdkSequence").GetUInt32()==12,"ERROR_DRAINED_CURRENT_OBSERVATION");
        }));
        rows.Add(Group("same tuple rejected sequence cannot drain current data",()=>{
            var s=Ready();s.ObservedController!.Emit(0,2,new byte[]{7},12);
            Check(!Observe(s,99).GetProperty("ok").GetBoolean());
            Check(Observation(Observe(s)).GetProperty("sdkSequence").GetUInt32()==12,"REJECTED_SEQUENCE_DRAINED_PENDING");
        }));
        rows.Add(Group("actual appender requires full current tuple and successful response",()=>{
            var s=Ready();s.ObservedController!.Emit(0,2,new byte[]{7},12);
            foreach(var field in new[]{"runId","epoch","powerGeneration","targetId","ownerIdentity","persona","profileIdentity","startupNonce","configRevision","configHash"}){
                var foreign=Message("TUPLE_SUBMIT_STATS",3);
                foreign[field]=field is "epoch" or "powerGeneration" or "configRevision"?99UL:field=="persona"?"elite":field=="profileIdentity"?"xbox-elite-v2":"foreign";
                // Persona/profile mutations must still form an admitted pair before SameTuple.
                if(field is "persona" or "profileIdentity"){foreign["persona"]="elite";foreign["profileIdentity"]="xbox-elite-v2";}
                if(field=="configHash")foreign[field]=new string('b',64);
                Check(s.AppendDirect("{}",JsonSerializer.Serialize(foreign),true)=="{}","TUPLE_FIELD_ESCAPED_"+field);
            }
            Check(s.AppendDirect("{}",JsonSerializer.Serialize(Message("TUPLE_SUBMIT_STATS",3)),false)=="{}","REJECTED_RESPONSE_DRAINED_PENDING");
            Check(Observation(Observe(s)).GetProperty("sdkSequence").GetUInt32()==12);
        }));
        rows.Add(Group("idempotent prepare replay cannot drain pending observation",()=>{
            var s=Ready();s.ObservedController!.Emit(0,2,new byte[]{7},12);
            var replay=Send(s,"PREPARE_TARGET",2,true);Accept(replay);Check(!HasObservation(replay));
            Check(Observation(Observe(s)).GetProperty("sdkSequence").GetUInt32()==12);
        }));
        rows.Add(Group("quiesce detaches and rejects already copied callback",()=>{
            var s=Ready();var c=s.ObservedController!;var callback=c.SnapshotCallback()!;
            c.Emit(0,2,new byte[]{1},1);Accept(Send(s,"QUIESCE",3));
            Check(c.SubscriberCount==0,"QUIESCE_STILL_SUBSCRIBED");callback(c,new(HMOutputSource.Output,2,new byte[]{9},2));
            Check(!HasObservation(Observe(s,4)),"QUIESCED_CALLBACK_ESCAPED");
        }));
        rows.Add(Group("failed output barrier keeps resources but revokes callback before retry",()=>{
            var s=Ready();var c=s.ObservedController!;var callback=c.SnapshotCallback()!;
            Fixture.OutputStops=false;Fixture.Events.Clear();Reject(Send(s,"RELEASE_TARGET",3),"release-local-dispose-failed");NoDispose();
            Check(s.HasController&&s.HasContext&&c.SubscriberCount==0,"RELEASE_REVOCATION_MISSING");
            callback(c,new(HMOutputSource.Output,2,new byte[]{1},1));Check(!HasObservation(Observe(s,4)));
            Fixture.OutputStops=true;Accept(Send(s,"RELEASE_TARGET",5));Check(!s.HasController&&!s.HasContext);
        }));
        rows.Add(Group("recreate rejects previous target callback and resets generation counters",()=>{
            var s=Ready();var old=s.ObservedController!;var callback=old.SnapshotCallback()!;old.Emit(0,2,new byte[]{1},1);
            Check(Observation(Observe(s)).GetProperty("generation").GetUInt64()==1);
            Fixture.OnSweep=()=>Fixture.Current=Empty();Accept(Send(s,"RECREATE_TARGET",4));
            var current=s.ObservedController!;Check(!ReferenceEquals(old,current)&&old.SubscriberCount==0&&current.SubscriberCount==1);
            callback(old,new(HMOutputSource.Output,2,new byte[]{99},99));Check(!HasObservation(Observe(s,5)),"OLD_TARGET_REAUTHORIZED");
            current.Emit(0,2,new byte[]{5},2);var r=Observation(Observe(s,6));
            Check(r.GetProperty("generation").GetUInt64()==2&&r.GetProperty("observed").GetUInt64()==1);
        }));
        rows.Add(Group("oversized report rejected whole and 256 bytes retained",()=>{
            var s=Ready();s.ObservedController!.Emit(0,2,new byte[257],1);Check(!HasObservation(Observe(s)));
            var data=Enumerable.Range(0,256).Select(i=>(byte)i).ToArray();s.ObservedController!.Emit(3,5,data,2);
            var r=Observation(Observe(s,4));Check(r.GetProperty("oversized").GetUInt64()==1&&r.GetProperty("payloadBytes").GetInt32()==256);
            Check(Convert.FromBase64String(r.GetProperty("payloadBase64").GetString()!).SequenceEqual(data),"PAYLOAD_BOUND_BROKEN");
        }));
        rows.Add(Group("failed admission never subscribes",()=>{
            var s=new HostSession("fake-nonce");Accept(Send(s,"HELLO",1));Fixture.OnObserve=()=>Empty();
            Check(!Send(s,"PREPARE_TARGET",2).GetProperty("ok").GetBoolean());Check(!s.HasController&&!HasObservation(Observe(s)));
        }));
        rows.Add(Group("subscription failure follows original prepare cleanup",()=>{
            var s=new HostSession("fake-nonce");Accept(Send(s,"HELLO",1));Fixture.OnSweep=()=>Fixture.Current=Empty();
            OutputEventFaults.Add=true;
            try {Reject(Send(s,"PREPARE_TARGET",2),"prepare-rejected");Check(!s.HasController&&!s.HasContext);}
            finally {OutputEventFaults.Add=false;}
            Check(Fixture.Events.Contains("output-stop")&&Fixture.Events.Contains("controller-dispose"),"ADD_FAILURE_SKIPPED_RELEASE");
        }));
        rows.Add(Group("unsubscribe failure revokes observation and retains original release duties",()=>{
            var s=Ready();var c=s.ObservedController!;var callback=c.SnapshotCallback()!;OutputEventFaults.Remove=true;
            try {
                var r=Send(s,"RELEASE_TARGET",3);Reject(r,"release-local-dispose-failed");
                Check(r.GetProperty("detail").GetString()!.Contains("game-output-detach:"),"DETACH_ERROR_LOST");
                Check(Fixture.Events.Contains("output-stop")&&Fixture.Events.Contains("controller-dispose")&&Fixture.Events.Contains("context-dispose"),"REMOVE_FAILURE_SKIPPED_RELEASE");
                callback(c,new(HMOutputSource.Output,2,new byte[]{1},1));Check(!HasObservation(Observe(s,4)));
            } finally {OutputEventFaults.Remove=false;}
            Send(s,Message("SHUTDOWN",5),out _);Check(c.SubscriberCount==0,"DETACH_RETRY_LOST");
        }));
        rows.Add(Group("message UTF8 budget preserves pending data until it fits",()=>{
            var o=new HostGameOutputObservation();var c=new object();var b=o.Bind(c);o.Observe(b,c,0,2,new byte[256],1);
            var large="{\"unicode\":\""+new string('中',100)+"\"}";
            Check(ReferenceEquals(o.TryAppend(large,c,400),large),"UTF8_BUDGET_EXCEEDED");
            using var doc=JsonDocument.Parse(o.TryAppend("{}",c,4096));Check(HasObservation(doc.RootElement));
            Check(o.TryAppend("{}",c,4096)=="{}");
        }));
        rows.Add(Group("revoked copied callback cannot republish before disposal",()=>{
            var o=new HostGameOutputObservation();var c=new object();var b=o.Bind(c);
            o.Observe(b,c,0,2,new byte[]{1},1);o.Revoke();o.Observe(b,c,0,2,new byte[]{2},2);
            Check(o.TryAppend("{}",c,4096)=="{}","REVOKED_BINDING_REPUBLISHED");
        }));
        rows.Add(Group("same controller rebind cannot accept an old lease",()=>{
            var o=new HostGameOutputObservation();var c=new object();var old=o.Bind(c);o.Revoke();var current=o.Bind(c);
            o.Observe(old,c,0,2,new byte[]{1},1);Check(o.TryAppend("{}",c,4096)=="{}","OLD_BINDING_ACCEPTED");
            o.Observe(current,c,0,2,new byte[]{2},2);using var doc=JsonDocument.Parse(o.TryAppend("{}",c,4096));
            Check(Observation(doc.RootElement).GetProperty("generation").GetUInt64()==2);
        }));
        rows.Add(Group("callback racing revoke never publishes to the new generation",()=>{
            var o=new HostGameOutputObservation();var c=new object();var old=o.Bind(c);var data=new byte[]{1};
            using var ready=new ManualResetEventSlim();using var go=new ManualResetEventSlim();
            var worker=Task.Run(()=>{ready.Set();go.Wait();for(uint i=0;i<20000;i++)o.Observe(old,c,0,2,data,i);});
            Check(ready.Wait(1000));o.Revoke();var current=o.Bind(c);go.Set();Check(worker.Wait(5000));
            Check(o.TryAppend("{}",c,4096)=="{}","RACING_OLD_BINDING_ACCEPTED");
            o.Observe(current,c,0,2,data,1);using var doc=JsonDocument.Parse(o.TryAppend("{}",c,4096));Check(Observation(doc.RootElement).GetProperty("observed").GetUInt64()==1);
        }));
        rows.Add(Group("idle path returns original response without allocation",()=>{
            var o=new HostGameOutputObservation();var c=new object();o.Bind(c);var response="{}";
            for(int i=0;i<1000;i++)o.TryAppend(response,c,4096);
            long start=GC.GetAllocatedBytesForCurrentThread();for(int i=0;i<100000;i++)Check(ReferenceEquals(response,o.TryAppend(response,c,4096)));
            Check(GC.GetAllocatedBytesForCurrentThread()-start==0,"IDLE_RESPONSE_ALLOCATION");
        }));
        rows.Add(Group("callback local cost recorded without product CPU percentage",()=>{
            var o=new HostGameOutputObservation();var c=new object();var b=o.Bind(c);var data=new byte[64];
            for(uint i=0;i<1000;i++)o.Observe(b,c,0,2,data,i);
            long allocated=GC.GetAllocatedBytesForCurrentThread(),start=Stopwatch.GetTimestamp();
            for(uint i=0;i<100000;i++)o.Observe(b,c,0,2,data,i);
            var elapsed=Stopwatch.GetElapsedTime(start);var delta=GC.GetAllocatedBytesForCurrentThread()-allocated;
            Check(delta==0,"CALLBACK_ALLOCATION");
            Console.WriteLine(JsonSerializer.Serialize(new{kind="local-cost",iterations=100000,payloadBytes=64,elapsedUs=elapsed.TotalMicroseconds,allocatedBytes=delta,wholeProcessCpuPercent=(double?)null}));
        }));
        return RunGameGroups(true,rows);
    }
}

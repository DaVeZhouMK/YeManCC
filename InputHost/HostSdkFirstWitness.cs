using System;
using System.Collections.Generic;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Threading;
using HIDMaestro;

internal static partial class Program
{
    // Passive memory-only SDK evidence. Existing command replies carry bounded samples
    // to Native's journal. No disk, worker, timer, device read or second SDK call.
    private sealed class HostSdkFirstWitness
    {
        private static int s_selected;
        private const int ProcessSampleLimit=256, PendingLimit=16, ReportBytes=512;
        private static readonly JsonSerializerOptions Options=new(){IncludeFields=true};
        private readonly object _gate=new();private readonly Queue<object> _pending=new();
        private object? _controller;private CommandEnvelope? _binding;private JsonElement? _lifetime;private bool _lifetimeKnown;
        private long _generation,_ordinal,_dropped,_emissionFailures;private int _seen,_returnedSeen,_capped,_pendingCount,_coverageDirty,_budgetFailures;
        private int _budgetExhausted;
        private void Drop(long count=1){if(count<=0)return;Interlocked.Add(ref _dropped,count);Volatile.Write(ref _coverageDirty,1);}
        internal readonly record struct Attempt(long Generation,object? Sample,int KindBit=0,bool FirstAttempt=false);
        internal bool HasPending=>Volatile.Read(ref _budgetExhausted)==0&&(Volatile.Read(ref _pendingCount)!=0||Volatile.Read(ref _coverageDirty)!=0);
        internal void BindLifetime(object? controller,object? lifetime)
        {
            if(!Monitor.TryEnter(_gate)){Drop();return;}
            try{Drop(_pending.Count);_pending.Clear();_pendingCount=0;_controller=controller;
                _binding=null;_seen=0;_returnedSeen=0;_capped=0;_budgetFailures=0;_budgetExhausted=0;_ordinal=0;++_generation;_lifetime=null;_lifetimeKnown=false;
                try{_lifetime=lifetime is null?null:JsonSerializer.SerializeToElement(lifetime,Options);}catch{Drop();Volatile.Write(ref _coverageDirty,1);}
                if(_lifetime is {} life&&life.TryGetProperty("lifetimeToken",out var token)&&token.ValueKind==JsonValueKind.String&&
                    life.TryGetProperty("closed",out var closed)&&closed.ValueKind==JsonValueKind.False)_lifetimeKnown=!string.IsNullOrEmpty(token.GetString());
                }finally{Monitor.Exit(_gate);}
        }
        private static bool Same(CommandEnvelope a,CommandEnvelope b)=>a.RunId==b.RunId&&a.Epoch==b.Epoch&&a.PowerGeneration==b.PowerGeneration&&
            a.Persona==b.Persona&&a.ProfileIdentity==b.ProfileIdentity&&a.TargetId==b.TargetId&&a.ConfigRevision==b.ConfigRevision&&a.ConfigHash==b.ConfigHash&&a.StartupNonce==b.StartupNonce;
        internal Attempt Begin(string host,object controller,CommandEnvelope binding,CommandEnvelope canonical,ulong sequence,bool commandKnown,string reason,long? encoder,byte[]? report,HMGamepadState? state)
        {
            if(!Monitor.TryEnter(_gate)){Drop();return default;}
            try{
                if(!ReferenceEquals(controller,_controller)){Drop(_pending.Count);_pending.Clear();_pendingCount=0;_controller=controller;_lifetime=null;_lifetimeKnown=false;_binding=null;_seen=0;_returnedSeen=0;_capped=0;_ordinal=0;++_generation;}
                if(_binding is null||!Same(_binding,binding)){_binding=binding;_seen=0;_returnedSeen=0;_capped=0;++_generation;}
                var ordinal=++_ordinal;
                bool nonzero=canonical.Buttons!=0||canonical.BackButtons!=0||canonical.ScreenButtons!=0||canonical.Lx!=0||canonical.Ly!=0||canonical.Rx!=0||canonical.Ry!=0||canonical.Lt!=0||canonical.Rt!=0;
                var kind=(nonzero?1:0)|(reason=="live-frame"?0:2);var bit=1<<kind;
                if(((_seen&_returnedSeen)&bit)!=0||(_capped&bit)!=0)return default;
                if(Volatile.Read(ref s_selected)>=ProcessSampleLimit){Drop();_capped|=bit;return default;}
                var firstAttempt=(_seen&bit)==0;_seen|=bit;
                object sdk=report is not null?new {kind="SubmitRawReport-argument",bytes=report.Length,
                    hex=Convert.ToHexString(report.AsSpan(0,Math.Min(report.Length,ReportBytes))),hexBytes=Math.Min(report.Length,ReportBytes),truncated=report.Length>ReportBytes,
                    sha256=Convert.ToHexString(SHA256.HashData(report)),scope="actual-before-SDK-byte-array-argument"}:
                    (object)new {kind="SubmitState-argument",state=JsonSerializer.SerializeToElement(state,Options),scope="actual-HMGamepadState;encoded-report-unavailable"};
                var sample=new {schema=1,kind="host-sdk-first-witness",tick=Environment.TickCount64,ordinal=ordinal.ToString(System.Globalization.CultureInfo.InvariantCulture),
                    selection=firstAttempt?(nonzero?"first-controls-nonzero-attempt":"first-controls-zero-attempt"):
                        (nonzero?"first-controls-nonzero-return-candidate":"first-controls-zero-return-candidate"),reason,encoderIndex=encoder,
                    controlClassScope="buttons/sticks/triggers/back/screen-buttons;IMU-or-touch-only-not-classified",
                    request=new {hostInstanceId=host,runId=binding.RunId,epoch=binding.Epoch,powerGeneration=binding.PowerGeneration,persona=binding.Persona,
                        profileIdentity=binding.ProfileIdentity,targetId=binding.TargetId,configRevision=binding.ConfigRevision,configHash=binding.ConfigHash,
                        commandSequence=sequence,sampleSequence=canonical.SampleSequence,monotonicTick=canonical.MonotonicTick,
                        requestId=commandKnown?binding.RequestId:null,commandSequenceSource=commandKnown?"actual-command-envelope":"last-context-only"},
                    targetCreationLifetimeEvidence=_lifetime,lifetimeKnown=_lifetimeKnown,lifetimeSnapshotScope="captured-at-create;closed-is-not-current-dispose-result",sdkArguments=sdk,
                    canonical=new {buttons=canonical.Buttons,backButtons=canonical.BackButtons,screenButtons=canonical.ScreenButtons,
                        lx=canonical.Lx,ly=canonical.Ly,rx=canonical.Rx,ry=canonical.Ry,lt=canonical.Lt,rt=canonical.Rt},
                    driverDeliveryProven=false,steamConsumptionProven=false,gameConsumptionProven=false};
                if(Interlocked.Increment(ref s_selected)>ProcessSampleLimit){Drop();_capped|=bit;return default;}
                return new(_generation,sample,bit,firstAttempt);
            }catch{Drop();Volatile.Write(ref _coverageDirty,1);return default;}finally{Monitor.Exit(_gate);}
        }
        internal void Complete(Attempt attempt,Exception? exception)
        {
            if(attempt.Sample is null)return;
            if(!Monitor.TryEnter(_gate)){Drop();return;}
            try{
                if(attempt.Generation!=_generation){Drop();return;}
                var firstReturned=exception is null&&(_returnedSeen&attempt.KindBit)==0;
                if(firstReturned)_returnedSeen|=attempt.KindBit;
                if(!attempt.FirstAttempt&&!firstReturned)return;
                if(_pending.Count>=PendingLimit){Drop();return;}
                _budgetFailures=0;Volatile.Write(ref _budgetExhausted,0);
                _pending.Enqueue(new {sample=attempt.Sample,outcome=exception is null?"returned":"threw",firstAttempt=attempt.FirstAttempt,firstReturned,
                    exceptionType=exception?.GetType().FullName,exceptionHResult=exception?.HResult,scope="SDK-call-only;not-driver/game/physical-feel-proof"});
                Volatile.Write(ref _pendingCount,_pending.Count);
            }catch{Drop();}finally{Monitor.Exit(_gate);}
        }
        private string Suffix(int emitted,string witness)=>
            "\"sdkFirstWitnesses\":["+witness+"],\"sdkFirstWitnessCoverage\":"+JsonSerializer.Serialize(new {
                emittedSamples=emitted,dropped=Interlocked.Read(ref _dropped),emissionFailures=_emissionFailures,
                remainingSamples=_pending.Count-emitted,processSampleLimit=ProcessSampleLimit,replyBudgetDeferralLimit=4,
                attemptAndReturnedSeparate=true,scope="response-emission-only;not-durable-native-receipt"})+"}";
        internal string TryAppend(string response,int maxBytes)
        {
            if(!HasPending||!Monitor.TryEnter(_gate))return response;
            try{
                if(!response.EndsWith('}'))return response;
                // One bounded witness per existing ACK. Compact ACKs can carry these
                // optional keys; Native sends them through its original DOM validator.
                Interlocked.Exchange(ref _coverageDirty,0);
                int count=_pending.Count==0?0:1;
                var witness=count==0?"":JsonSerializer.Serialize(_pending.Peek(),Options);
                var suffix=(response=="{}"?"":",")+Suffix(count,witness);
                if(Encoding.UTF8.GetByteCount(response)+Encoding.UTF8.GetByteCount(suffix)-1>maxBytes){
                    ++_emissionFailures;Volatile.Write(ref _coverageDirty,1);
                    if(++_budgetFailures<4)return response;
                    Drop(_pending.Count);_pending.Clear();Volatile.Write(ref _pendingCount,0);
                    Interlocked.Exchange(ref _coverageDirty,0);
                    suffix=(response=="{}"?"":",")+Suffix(0,"");
                    if(Encoding.UTF8.GetByteCount(response)+Encoding.UTF8.GetByteCount(suffix)-1>maxBytes){
                        // Keep counters for the next new unique sample/lifecycle,
                        // but never retry this oversized batch on every later frame.
                        Volatile.Write(ref _coverageDirty,1);Volatile.Write(ref _budgetExhausted,1);return response;
                    }
                    count=0;
                }
                var result=response[..^1]+suffix;if(count!=0)_pending.Dequeue();
                Volatile.Write(ref _pendingCount,_pending.Count);_budgetFailures=0;return result;
            }catch{
                ++_emissionFailures;Volatile.Write(ref _coverageDirty,1);
                if(++_budgetFailures>=4){Drop(_pending.Count);_pending.Clear();Volatile.Write(ref _pendingCount,0);Volatile.Write(ref _budgetExhausted,1);}
                return response;
            }finally{Monitor.Exit(_gate);}
        }
    }
}

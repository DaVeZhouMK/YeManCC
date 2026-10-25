using System.IO;
using System.Diagnostics;
using System.Reflection;
using System.Text.Json;
using System.Windows.Threading;
using Mono.Cecil;
using Mono.Cecil.Cil;
using Ports=ConsumerFixture.FactoryPorts;
using StaPorts=ConsumerFixture.StaPorts;
using ThreadMonitor=System.Threading.Monitor;
static partial class Harness
{
    static readonly List<object> StaEvidence=new();
    static T StaCall<T>(object backend,Func<T> action,string operation="sta-fixture-call")
    {
        var method=backend.GetType().GetMethods(All).Single(m=>m.Name=="OnStaBounded"&&m.IsGenericMethodDefinition);
        try{return (T)method.MakeGenericMethod(typeof(T)).Invoke(backend,[action,operation])!;}
        catch(TargetInvocationException error)when(error.InnerException is not null){throw error.InnerException;}
    }
    static Exception? CaptureSta(Action action){try{action();return null;}catch(Exception error){return error;}}
    static T AwaitSta<T>(Task<T> task){Check(task.Wait(TimeSpan.FromSeconds(6)),"Owned test caller must reach a terminal result");return task.GetAwaiter().GetResult();}
    static void CleanSta(object backend)
    {
        StaPorts.IdentityHook=null;StaPorts.StartHook=null;Ports.OnStage=null;
        try{((IDisposable)backend).Dispose();}catch{ /* Original unstarted-thread disposal is separately asserted. */ }
        var threads=StaPorts.Threads.Values.Concat(StaPorts.Starts.Select(row=>row.Thread)).Distinct().ToArray();
        foreach(var thread in threads.Where(t=>t.IsAlive))
        {
            var dispatcher=Dispatcher.FromThread(thread);
            if(dispatcher is not null)try{dispatcher.BeginInvokeShutdown(DispatcherPriority.Send);}catch(InvalidOperationException){}
        }
        foreach(var thread in threads.Where(t=>t.IsAlive))thread.Join(TimeSpan.FromSeconds(2));
        Check(threads.All(t=>!t.IsAlive),"Fixture cleanup must retire every owned lifecycle thread");Ports.Enabled=false;
    }
    static void StaCase(string id,Action<object> action)=>Run(id,()=>
    {
        Ports.Reset();StaPorts.Reset();var backend=NewFactoryBackend();
        try{action(backend);}finally
        {
            CleanSta(backend);StaEvidence.Add(new{id,createdThreads=StaPorts.Threads.Count,startAttempts=StaPorts.Starts.Count,
                startGateHeld=StaPorts.Starts.Select(row=>row.GateHeld).ToArray(),readOnlyOsIdentityCalls=StaPorts.OsIdentityReads,
                allOwnedThreadsExited=StaPorts.Threads.Values.All(t=>!t.IsAlive)});
        }
    });
    static void RunStaCases()
    {
        StaCase("sta-existing-real-dispatcher-selftest",_=>
        {
            var result=(IReadOnlyList<string>)Candidate.GetType("YeManFanHost.RealHcBackend",true)!.GetMethod("RunStaDispatcherSelfTest",All)!.Invoke(null,null)!;
            Check(result.Any(x=>x=="hc-single-message-pumped-sta-lifecycle-dispatcher"),"Existing selftest must execute its actual STA roundtrips");
            Check(StaPorts.Threads.Count==1&&StaPorts.Threads.Values.All(t=>!t.IsAlive),"Existing real selftest must retire its thread");
        });
        StaCase("sta-public-handshake-real-pump",backend=>
        {
            var ids=new List<int>();var apartments=new List<ApartmentState>();var pumps=new List<bool>();
            Ports.OnStage=_=>{ids.Add(Environment.CurrentManagedThreadId);apartments.Add(Thread.CurrentThread.GetApartmentState());pumps.Add(SynchronizationContext.Current is DispatcherSynchronizationContext);};
            var caller=Environment.CurrentManagedThreadId;var result=Invoke(backend,"LoadAndHandshake")!;
            Check(Get(result,"Supported")is true&&Get(backend,"IsLoaded")is true,"Actual public handshake completes the actual factory");
            Check(ids.Count>4&&ids.Distinct().Count()==1&&ids[0]!=caller&&apartments.All(a=>a==ApartmentState.STA)&&pumps.All(x=>x),"All actual factory stages execute on one real message-pumped STA");
            var identity=Get(backend,"StaThreadIdentityForDiagnostics")!;
            Check(identity is not null&&Convert.ToInt32(Get(identity,"ManagedThreadId"))==ids[0]&&Convert.ToUInt32(Get(identity,"OsThreadId"))>0,"Published thread identity comes from the actual read-only OS endpoint");
            var count=Ports.Calls.Count;Invoke(backend,"LoadAndHandshake");Check(Ports.Calls.Count==count&&StaPorts.Threads.Count==1,"Repeated public handshake reuses one completed factory and one thread");
        });
        StaCase("sta-public-initialization-retry-same-thread",backend=>
        {
            Ports.Failure="device.initialize";Ports.Failures=1;FactoryFailure(()=>Invoke(backend,"LoadAndHandshake"));var first=Get(backend,"staThread");
            Invoke(backend,"LoadAndHandshake");Check(Get(backend,"IsLoaded")is true&&ReferenceEquals(Get(backend,"staThread"),first)&&Ports.Count("device.initialize")==2&&Ports.Count("assembly.configure")==1,"Public retry resumes the same actual factory on the same STA");
        });
        StaCase("sta-public-reentrant-handshake-refused",backend=>
        {
            Exception? nested=null;Ports.OnStage=stage=>{if(stage=="device.initialize"){Ports.OnStage=null;nested=CaptureSta(()=>Invoke(backend,"LoadAndHandshake"));}};
            Invoke(backend,"LoadAndHandshake");Check(nested is not null&&Get(nested,"Code")?.ToString()=="HC_NOT_INITIALIZED"&&Ports.Count("device.initialize")==1,"Same-STA public reentry cannot publish or repeat a partial factory");
        });
        StaCase("sta-normal-disposal-joins-owned-thread",backend=>
        {
            Invoke(backend,"LoadAndHandshake");var thread=(Thread)Get(backend,"staThread")!;((IDisposable)backend).Dispose();((IDisposable)backend).Dispose();
            Check(!thread.IsAlive&&Get(backend,"StaThreadIdentityForDiagnostics")is null&&Get(backend,"IsLoaded")is false,"Normal public lifecycle disposal joins its one thread and clears identity/readiness");
        });
        StaCase("sta-start-failure-is-terminal-and-disposable",backend=>
        {
            var marker=new ThreadStateException("STA_CONTROLLED_START_FAILURE");StaPorts.StartHook=(_,_)=>throw marker;
            var failure=CaptureSta(()=>Invoke(backend,"LoadAndHandshake"));
            Check(ReferenceEquals(failure,marker),"Actual thread-start boundary must preserve its original failure");
            Check(Get(backend,"staThread")is null&&Get(backend,"staDispatcherStopping")is true&&StaPorts.OsIdentityReads==0,"Failed thread start must clear its unstarted owner and close admission");
            Check(CaptureSta(()=>((IDisposable)backend).Dispose())is null,"Disposal after failed Start cannot join an unstarted thread");
            Reject(()=>Invoke(backend,"LoadAndHandshake"),"ObjectDisposedException");
        });
        StaCase("sta-disposed-before-start-does-not-bootstrap",backend=>
        {
            ((IDisposable)backend).Dispose();Reject(()=>Invoke(backend,"LoadAndHandshake"),"ObjectDisposedException");
            Check(StaPorts.Starts.IsEmpty&&StaPorts.Threads.IsEmpty&&Ports.Calls.Count==0,"Disposed public entry cannot bootstrap a thread or factory");
        });
        StaCase("sta-disposal-from-own-thread-stops-pump",backend=>
        {
            Ports.OnStage=stage=>{if(stage=="device.initialize"){Ports.OnStage=null;((IDisposable)backend).Dispose();}};
            Reject(()=>Invoke(backend,"LoadAndHandshake"),"ObjectDisposedException");var thread=(Thread)Get(backend,"staThread")!;
            Check(thread.Join(TimeSpan.FromSeconds(1)),"Disposal on the lifecycle STA must stop its own message pump after the current callback returns");
            Check(Get(backend,"IsLoaded")is false&&Get(backend,"StaThreadIdentityForDiagnostics")is null,"Self-disposal cannot leave a late ready identity");
        });
        StaCase("sta-disposal-during-startup-does-not-leak-pump",backend=>
        {
            using var entered=new ManualResetEventSlim();using var release=new ManualResetEventSlim();
            StaPorts.IdentityHook=()=>{entered.Set();if(!release.Wait(TimeSpan.FromSeconds(5)))throw new InvalidOperationException("Identity scheduling probe not released");};
            var call=Task.Run(()=>CaptureSta(()=>Invoke(backend,"LoadAndHandshake")));
            try
            {
                Check(entered.Wait(TimeSpan.FromSeconds(2)),"Actual STA must reach its read-only identity boundary");
                var disposal=Task.Run(()=>CaptureSta(()=>((IDisposable)backend).Dispose()));var error=AwaitSta(disposal);
                release.Set();var failure=AwaitSta(call);Check(error is null&&failure is ObjectDisposedException,"Startup disposal must settle and refuse its waiting public caller");
                var thread=(Thread)Get(backend,"staThread")!;Check(thread.Join(TimeSpan.FromSeconds(1)),"A disposed pre-publication STA must not enter an unowned message pump");
                Check(Ports.Calls.Count==0,"Stopped startup cannot run factory stages");
            }finally{release.Set();AwaitSta(call);}
        });
        StaCase("sta-ended-pump-cannot-be-reused",backend=>
        {
            Invoke(backend,"LoadAndHandshake");var thread=(Thread)Get(backend,"staThread")!;
            StaCall(backend,()=>{Dispatcher.CurrentDispatcher.BeginInvokeShutdown(DispatcherPriority.Send);return true;});
            Check(thread.Join(TimeSpan.FromSeconds(2)),"Controlled existing dispatcher shutdown must finish");
            Reject(()=>Invoke(backend,"EnsureStaDispatcher"),"ObjectDisposedException");
            Reject(()=>Invoke(backend,"LoadAndHandshake"),"ObjectDisposedException");
            Check(StaPorts.Starts.Count==1&&Get(backend,"StaThreadIdentityForDiagnostics")is null,"Ended lifecycle cannot silently restart or retain stale identity");
        });
        StaCase("sta-concurrent-cold-start-has-one-owner",backend=>
        {
            using var entered=new ManualResetEventSlim();using var release=new ManualResetEventSlim();using var secondEntered=new ManualResetEventSlim();using var secondAttempt=new ManualResetEventSlim();int starts=0;
            StaPorts.StartHook=(_,_)=>{if(Interlocked.Increment(ref starts)==1){entered.Set();if(!release.Wait(TimeSpan.FromSeconds(4)))throw new InvalidOperationException("Start scheduling probe not released");}else secondEntered.Set();};
            var first=Task.Run(()=>CaptureSta(()=>Invoke(backend,"LoadAndHandshake")));Task<Exception?>? second=null;
            try
            {
                Check(entered.Wait(TimeSpan.FromSeconds(2)),"First actual thread start must reach the scheduling cut");
                second=Task.Run(()=>{secondAttempt.Set();return CaptureSta(()=>Invoke(backend,"LoadAndHandshake"));});Check(secondAttempt.Wait(TimeSpan.FromSeconds(2)),"Second caller reaches the actual public entry");secondEntered.Wait(TimeSpan.FromMilliseconds(250));release.Set();
                var a=AwaitSta(first);var b=AwaitSta(second);
                Check(a is null&&b is null&&StaPorts.Starts.Count==1&&StaPorts.Threads.Count==1,"Concurrent first public calls must start exactly one lifecycle thread without ThreadStateException");
                Check(Ports.Count("device.initialize")==1&&Get(backend,"IsLoaded")is true,"Cold concurrent callers must share one initialized factory");
                Check(StaPorts.Starts.All(row=>row.GateHeld),"The actual Thread.Start boundary must remain inside its published owner gate");
            }finally{release.Set();AwaitSta(first);if(second is not null)AwaitSta(second);}
        });
        StaCase("sta-dispose-before-thread-start-does-not-join-unstarted",backend=>
        {
            using var entered=new ManualResetEventSlim();using var release=new ManualResetEventSlim();
            StaPorts.StartHook=(_,_)=>{entered.Set();if(!release.Wait(TimeSpan.FromSeconds(4)))throw new InvalidOperationException("Start scheduling probe not released");};
            var first=Task.Run(()=>CaptureSta(()=>Invoke(backend,"LoadAndHandshake")));Task<Exception?>? disposal=null;
            try
            {
                Check(entered.Wait(TimeSpan.FromSeconds(2)),"Start boundary must be reached before concurrent disposal");
                disposal=Task.Run(()=>CaptureSta(()=>((IDisposable)backend).Dispose()));
                SpinWait.SpinUntil(()=>Get(backend,"hcFactoryDisposed")is 1,1000);disposal.Wait(TimeSpan.FromMilliseconds(100));release.Set();
                var closed=AwaitSta(disposal);var caller=AwaitSta(first);
                Check(closed is null&&caller is ObjectDisposedException,"Concurrent disposal must not Join an unstarted thread or allow a late public handshake");
            }finally{release.Set();AwaitSta(first);if(disposal is not null)AwaitSta(disposal);}
        });
        StaCase("sta-busy-admission-serializes-public-handshake",backend=>
        {
            using var entered=new ManualResetEventSlim();using var release=new ManualResetEventSlim();
            var first=Task.Run(()=>StaCall(backend,()=>{entered.Set();Check(release.Wait(TimeSpan.FromSeconds(4)),"Owned foreground action released");return 7;}));
            Task<Exception?>? second=null;try
            {
                Check(entered.Wait(TimeSpan.FromSeconds(2)),"Actual existing work item must run");second=Task.Run(()=>CaptureSta(()=>Invoke(backend,"LoadAndHandshake")));
                Check(Ports.Count("device.initialize")==0,"Busy public handshake cannot bypass the existing STA owner");release.Set();
                Check(AwaitSta(first)==7&&AwaitSta(second)is null&&Ports.Count("device.initialize")==1&&StaPorts.Threads.Count==1,"Admitted public work continues once on the original real STA");
            }finally{release.Set();AwaitSta(first);if(second is not null)AwaitSta(second);}
        });
        StaCase("sta-workitem-error-retires-before-next-call",backend=>
        {
            var marker=new IOException("STA_ACTUAL_WORK_FAULT");var failure=CaptureSta(()=>StaCall<int>(backend,()=>throw marker));
            Check(ReferenceEquals(failure,marker)&&Get(backend,"activeStaWorkItem")is null,"Actual dispatcher returns the original work failure and retires its owner");
            Invoke(backend,"LoadAndHandshake");Check(Get(backend,"IsLoaded")is true&&StaPorts.Threads.Count==1,"Failed action cannot strand the next public handshake");
        });
        StaCase("sta-pump-exit-rejects-pending-public-work",backend=>
        {
            Invoke(backend,"LoadAndHandshake");using var entered=new ManualResetEventSlim();using var release=new ManualResetEventSlim();
            Set(backend,"Diagnostic",(Action<string,object?>)((stage,details)=>
            {
                if(stage=="hc-operation.return"&&details is not null&&Get(details,"operation")?.ToString()=="sta-pending-gate")
                {
                    entered.Set();if(!release.Wait(TimeSpan.FromSeconds(5)))throw new InvalidOperationException("Return scheduling cut was not released");
                    Dispatcher.CurrentDispatcher.InvokeShutdown();
                }
            }));
            var first=Task.Run(()=>StaCall(backend,()=>7,"sta-pending-gate"));Task<Exception?>? second=null;
            try
            {
                Check(entered.Wait(TimeSpan.FromSeconds(2)),"Existing work has retired before the controlled dispatcher shutdown");
                second=Task.Run(()=>CaptureSta(()=>Invoke(backend,"LoadAndHandshake")));
                Check(SpinWait.SpinUntil(()=>Get(backend,"activeStaWorkItem")is not null,2000),"Next actual public work item is queued on the paused pump");
                lock(Get(backend,"staGate")!){Check(Get(backend,"activeStaWorkItem")is not null,"Pending owner remains current after actual BeginInvoke admission");}
                var started=Stopwatch.StartNew();release.Set();Check(second.Wait(TimeSpan.FromSeconds(18)),"Actual bounded caller must settle after its pump ends");
                var error=second.GetAwaiter().GetResult();
                Check(error is ObjectDisposedException&&started.Elapsed<TimeSpan.FromSeconds(3),"Pending public work must be rejected promptly by its ended dispatcher, without a fabricated operation timeout");
                Check(Get(backend,"activeStaWorkItem")is null&&Get(backend,"OperationTimedOut")is false&&Ports.Count("device.initialize")==1,"Aborted queued work retires once, does no device work and cannot claim a hardware timeout");
                Check(AwaitSta(first)==7,"Preceding owner keeps its completed return");
            }
            finally{release.Set();Set(backend,"Diagnostic",null);AwaitSta(first);if(second is not null)Check(second.Wait(TimeSpan.FromSeconds(18)),"Pending test caller exited");}
        });
        StaCase("sta-failed-factory-disposal-joins-thread",backend=>
        {
            Ports.Failure="device.initialize";Ports.Failures=1;FactoryFailure(()=>Invoke(backend,"LoadAndHandshake"));var thread=(Thread)Get(backend,"staThread")!;
            ((IDisposable)backend).Dispose();Check(!thread.IsAlive&&Get(backend,"IsLoaded")is false&&Get(backend,"activeStaWorkItem")is null,"Failed public factory keeps one disposable thread and no stale active work");
        });
    }
    static int RunStaCasesMode(string[] args)
    {
        RunStaCases();var failed=Cases.Any(c=>JsonSerializer.Serialize(c).Contains("\"status\":\"FAIL\""));
        File.WriteAllText(args[3],JsonSerializer.Serialize(new{schemaVersion=1,status=failed?"FAIL":"PASS",checks=Checks,cases=Cases,staEvidence=StaEvidence,candidateHostSha256=Hash(args[0]),nativeAttempts=NativeAttempts,hostMainStarted=false,hardwareIo=false,
            scope="Actual public handshake, STA threads/message pumps and work admission; six I/O leaves plus one verified scheduling probe; real read-only OS thread IDs",productionValidationExecuted=false},new JsonSerializerOptions{WriteIndented=true}));return failed?2:0;
    }
    static void WidenStaBranches(MethodDefinition method)
    {
        var opcodes=typeof(OpCodes).GetFields(BindingFlags.Public|BindingFlags.Static).Where(field=>field.FieldType==typeof(OpCode)).Select(field=>(OpCode)field.GetValue(null)!).ToArray();
        foreach(var instruction in method.Body.Instructions.Where(i=>i.OpCode.OperandType==OperandType.ShortInlineBrTarget))
            instruction.OpCode=opcodes.Single(opcode=>opcode.Code.ToString()==instruction.OpCode.Code.ToString()[..^2]);
    }
    static int MutateStaOwnership(string[] args,string kind)
    {
        var source=Path.GetFullPath(args[0]);var target=Path.GetFullPath(args[1]);using var image=AssemblyDefinition.ReadAssembly(source);
        var backend=image.MainModule.GetType("YeManFanHost.RealHcBackend");
        var method=kind=="pending-work"?backend.NestedTypes.Single(t=>t.Name.StartsWith("StaWorkItem`",StringComparison.Ordinal)).Methods.Single(m=>m.Name=="RejectUnexecuted"):
            backend.Methods.Single(m=>m.Name==(kind=="self-stop"?"Dispose":kind=="start-atomic"?"EnsureStaDispatcher":"StaDispatcherLoop"));
        var il=method.Body.GetILProcessor();
        if(kind=="self-stop")
        {
            var call=method.Body.Instructions.Single(i=>i.Operand is MethodReference m&&m.DeclaringType.FullName==typeof(Dispatcher).FullName&&m.Name=="BeginInvokeShutdown");var after=call.Next;
            foreach(var instruction in new[]{
                il.Create(OpCodes.Call,image.MainModule.ImportReference(typeof(Thread).GetProperty(nameof(Thread.CurrentThread))!.GetMethod!)),
                il.Create(OpCodes.Ldarg_0),il.Create(OpCodes.Ldfld,backend.Fields.Single(f=>f.Name=="staThread")),
                il.Create(OpCodes.Bne_Un,call),il.Create(OpCodes.Pop),il.Create(OpCodes.Pop),il.Create(OpCodes.Br,after)})il.InsertBefore(call,instruction);
        }
        else if(kind=="startup-stop")
        {
            var stopping=method.Body.Instructions.Single(i=>i.OpCode==OpCodes.Ldfld&&i.Operand is FieldReference f&&f.Name=="staDispatcherStopping");
            stopping.OpCode=OpCodes.Pop;stopping.Operand=null;il.InsertAfter(stopping,il.Create(OpCodes.Ldc_I4_0));
            var disposed=method.Body.Instructions.Single(i=>i.Operand is MethodReference m&&m.DeclaringType.FullName==typeof(Volatile).FullName&&m.Name=="Read"&&i.Previous.Operand is FieldReference f&&f.Name=="hcFactoryDisposed");
            disposed.OpCode=OpCodes.Pop;disposed.Operand=null;il.InsertAfter(disposed,il.Create(OpCodes.Ldc_I4_0));
        }
        else if(kind=="ended-owner")
        {
            foreach(var name in new[]{"staDispatcher","staDispatcherStopping"})
            {
                var store=method.Body.Instructions.Single(i=>i.OpCode==OpCodes.Stfld&&i.Operand is FieldReference f&&f.Name==name&&(name!="staDispatcher"||i.Previous.OpCode==OpCodes.Ldnull));
                store.OpCode=OpCodes.Pop;store.Operand=null;il.InsertAfter(store,il.Create(OpCodes.Pop));
            }
        }
        else if(kind=="pending-work")
        {
            var failure=method.Body.Instructions.Single(i=>i.Operand is MethodReference call&&call.Name=="set_Failure");
            failure.OpCode=OpCodes.Pop;failure.Operand=null;il.InsertAfter(failure,il.Create(OpCodes.Pop));
        }
        else if(kind=="start-atomic")
        {
            var call=method.Body.Instructions.Single(i=>i.Operand is MethodReference m&&m.DeclaringType.FullName==typeof(Thread).FullName&&m.Name=="Start");
            var temporary=new VariableDefinition(image.MainModule.ImportReference(typeof(Thread)));method.Body.Variables.Add(temporary);
            var start=il.Create(OpCodes.Nop);var handler=il.Create(OpCodes.Ldarg_0);var after=il.Create(OpCodes.Nop);
            foreach(var instruction in new[]{il.Create(OpCodes.Stloc,temporary),il.Create(OpCodes.Ldarg_0),il.Create(OpCodes.Ldfld,backend.Fields.Single(f=>f.Name=="staGate")),
                il.Create(OpCodes.Call,image.MainModule.ImportReference(typeof(ThreadMonitor).GetMethod(nameof(ThreadMonitor.Exit),[typeof(object)])!)),start,il.Create(OpCodes.Ldloc,temporary)})il.InsertBefore(call,instruction);
            var cursor=call;
            foreach(var instruction in new[]{il.Create(OpCodes.Leave,after),handler,il.Create(OpCodes.Ldfld,backend.Fields.Single(f=>f.Name=="staGate")),
                il.Create(OpCodes.Call,image.MainModule.ImportReference(typeof(ThreadMonitor).GetMethod(nameof(ThreadMonitor.Enter),[typeof(object)])!)),il.Create(OpCodes.Endfinally),after}){il.InsertAfter(cursor,instruction);cursor=instruction;}
            method.Body.ExceptionHandlers.Insert(0,new ExceptionHandler(ExceptionHandlerType.Finally){TryStart=start,TryEnd=handler,HandlerStart=handler,HandlerEnd=after});
        }
        else throw new InvalidOperationException("Unknown STA fault: "+kind);
        WidenStaBranches(method);
        Directory.CreateDirectory(Path.GetDirectoryName(target)!);image.Write(target);var changed=FactoryChangedMethods(source,target);
        Check(changed.Length==1&&changed[0]==method.FullName,"STA fault changes exactly its one named original method");
        File.WriteAllText(args[2],JsonSerializer.Serialize(new{status="PASS",sourceSha256=Hash(source),faultSha256=Hash(target),changedMethods=changed,mutation=kind,
            scope="Only self-shutdown, pre-publication stop, ended-owner retirement, or atomic Start ownership is disconnected; existing factory and device admission retained",hardwareIo=false}));return 0;
    }
    static int InstrumentStaPorts(string[] args)
    {
        var source=Path.GetFullPath(args[0]);var target=Path.GetFullPath(args[1]);var intermediate=target+".five-leaves.dll";
        InstrumentFactoryLeaves([source,intermediate,args[2]+".five-leaves.json"]);
        using(var image=AssemblyDefinition.ReadAssembly(intermediate))
        {
            var backend=image.MainModule.GetType("YeManFanHost.RealHcBackend");var identity=backend.Methods.Single(m=>m.Name=="ReadCurrentOsThreadId");
            Check(identity.IsPInvokeImpl&&identity.PInvokeInfo.EntryPoint=="GetCurrentThreadId","Only the read-only OS identity pinvoke is rebound");
            identity.PInvokeInfo=null;identity.IsPInvokeImpl=false;identity.ImplAttributes=Mono.Cecil.MethodImplAttributes.IL|Mono.Cecil.MethodImplAttributes.Managed;identity.Body=new Mono.Cecil.Cil.MethodBody(identity);
            var il=identity.Body.GetILProcessor();il.Append(il.Create(OpCodes.Call,image.MainModule.ImportReference(typeof(StaPorts).GetMethod(nameof(StaPorts.ReadOsThreadId))!)));il.Append(il.Create(OpCodes.Ret));
            var ensure=backend.Methods.Single(m=>m.Name=="EnsureStaDispatcher");var starts=ensure.Body.Instructions.Where(i=>i.Operand is MethodReference call&&call.DeclaringType.FullName==typeof(Thread).FullName&&call.Name=="Start").ToArray();
            Check(starts.Length==1,"Exactly one original Thread.Start call is probed");var start=starts[0];var code=ensure.Body.GetILProcessor();
            code.InsertBefore(start,code.Create(OpCodes.Dup));code.InsertBefore(start,code.Create(OpCodes.Ldarg_0));code.InsertBefore(start,code.Create(OpCodes.Call,image.MainModule.ImportReference(typeof(StaPorts).GetMethod(nameof(StaPorts.BeforeThreadStart))!)));
            WidenStaBranches(ensure);
            image.Write(target);
        }
        using var original=AssemblyDefinition.ReadAssembly(source);using var modified=AssemblyDefinition.ReadAssembly(target);
        var rebound=modified.MainModule.GetType("YeManFanHost.RealHcBackend").Methods.Single(m=>m.Name=="ReadCurrentOsThreadId");
        Check(!rebound.IsPInvokeImpl&&rebound.HasBody&&rebound.Body.Instructions.Count==2,"Written identity port must be executable managed IL, not a remaining PInvoke flag");
        var current=modified.MainModule.GetType("YeManFanHost.RealHcBackend").Methods.Single(m=>m.Name=="EnsureStaDispatcher");
        var probe=current.Body.Instructions.Single(i=>i.Operand is MethodReference call&&call.DeclaringType.FullName==typeof(StaPorts).FullName&&call.Name==nameof(StaPorts.BeforeThreadStart));
        Check(probe.Previous.OpCode==OpCodes.Ldarg_0&&probe.Previous.Previous.OpCode==OpCodes.Dup,"Scheduling probe is only dup/owner/call before the retained original Start");
        var remove=new[]{probe.Previous.Previous,probe.Previous,probe};foreach(var instruction in remove)current.Body.Instructions.Remove(instruction);
        var unchanged=original.MainModule.GetType("YeManFanHost.RealHcBackend").Methods.Single(m=>m.Name=="EnsureStaDispatcher");
        Check(FactoryMethodCanon(unchanged)==FactoryMethodCanon(current),"Removing only the scheduling probe restores every original startup instruction, local and exception boundary");
        var changed=FactoryChangedMethods(source,target);var expected=new[]{"ValidateFormalSessionCapabilityAcl","ValidateDeploymentGenerationForHcLoad","ValidateGamepadMotion","ConfigureHcRuntime","ResolveDependency","ReadCurrentOsThreadId","EnsureStaDispatcher"};
        Check(changed.Length==7&&expected.All(name=>changed.Count(m=>m.Contains("::"+name+"("))==1),"Only six I/O leaves plus the verified scheduling probe change");
        File.WriteAllText(args[2],JsonSerializer.Serialize(new{status="PASS",sourceSha256=Hash(source),instrumentedSha256=Hash(target),changedMethods=changed,originalStaInstructionsRetained=true,branchEncodingNormalized=true,readOnlyOsIdentityEndpoint="kernel32.GetCurrentThreadId",hardwareIo=false,productionValidationExecuted=false}));return 0;
    }
}

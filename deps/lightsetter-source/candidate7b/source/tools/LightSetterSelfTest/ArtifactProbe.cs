using System.IO;
using System.Reflection;
using System.Runtime.ExceptionServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;
using System.Windows.Media;

// The existing LightSetter fixture now also checks the exact shipping DLL.
// Invoke only pure parsing/action/receipt methods with memory device terminals.
internal static class LightSetterArtifactProbe
{
    private sealed class ProbeContext : AssemblyLoadContext
    {
        internal readonly List<string> Attempts = [];
        internal ProbeContext() : base("light-setter-artifact-probe", isCollectible: false) { }
        protected override Assembly? Load(AssemblyName name)
        {
            var id = name.Name ?? "";
            if (id.StartsWith("System.", StringComparison.Ordinal) || id.StartsWith("Microsoft.Win32.", StringComparison.Ordinal) ||
                id is "System" or "mscorlib" or "netstandard" or "WindowsBase" or "PresentationCore" or "PresentationFramework")
                return Default.LoadFromAssemblyName(name);
            Attempts.Add(name.FullName);
            throw new InvalidOperationException("Target dependency denied: " + name.FullName);
        }
    }
    private static object? Invoke(Type type, string method, params object?[] values)
    {
        try { return (type.GetMethod(method, BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic)
            ?? throw new MissingMethodException(type.FullName, method)).Invoke(null, values); }
        catch (TargetInvocationException ex) when (ex.InnerException is not null)
        { Exception root=ex.InnerException; while(root is TargetInvocationException nested && nested.InnerException is not null) root=nested.InnerException; ExceptionDispatchInfo.Capture(root).Throw(); throw; }
    }
    private static bool CallsMethod(MethodInfo caller, MethodInfo target)
    {
        var bytes = caller.GetMethodBody()?.GetILAsByteArray() ?? [];
        var codes = typeof(System.Reflection.Emit.OpCodes).GetFields(BindingFlags.Public | BindingFlags.Static)
            .Where(f => f.FieldType == typeof(System.Reflection.Emit.OpCode))
            .Select(f => (System.Reflection.Emit.OpCode)f.GetValue(null)!).ToDictionary(c => c.Value);
        for (int at = 0; at < bytes.Length;)
        {
            short value = bytes[at++];
            if (value == 0xfe) value = (short)(0xfe00 | bytes[at++]);
            var op = codes[value];
            if (op.OperandType == System.Reflection.Emit.OperandType.InlineMethod &&
                caller.Module.ResolveMethod(BitConverter.ToInt32(bytes, at)) == target) return true;
            at += op.OperandType switch
            {
                System.Reflection.Emit.OperandType.InlineNone => 0,
                System.Reflection.Emit.OperandType.ShortInlineBrTarget or System.Reflection.Emit.OperandType.ShortInlineI or System.Reflection.Emit.OperandType.ShortInlineVar => 1,
                System.Reflection.Emit.OperandType.InlineVar => 2,
                System.Reflection.Emit.OperandType.InlineI8 or System.Reflection.Emit.OperandType.InlineR => 8,
                System.Reflection.Emit.OperandType.InlineSwitch => 4 + 4 * BitConverter.ToInt32(bytes, at),
                _ => 4,
            };
        }
        return false;
    }
    private static string Hash(string file) => Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(file))).ToLowerInvariant();
    public static int Main(string[] args)
    {
        if (args.Length is not (3 or 4) || (args.Length == 4 && args[3] is not ("--legacy-open-only" or "--legacy-onex-only" or "--legacy-cache-only" or "--legacy-fly-only"))) { Console.Error.WriteLine("dll expected-sha256 fresh-result-json required"); return 3; }
        var target = Path.GetFullPath(args[0]);
        var destination = Path.GetFullPath(args[2]);
        if (File.Exists(destination)) { Console.Error.WriteLine("Fresh result required"); return 3; }
        var checks = new List<object>();
        var nativeAttempts = new List<string>();
        var context = new ProbeContext();
        string? error = null;
        int? observedPowerWrites = null; int? observedOemWrites = null; int? observedSerialWrites = null; int? observedDriverInitializations = null;
        void Check(bool pass, string name)
        {
            checks.Add(new { name, passed = pass });
            if (!pass) throw new InvalidOperationException("CHECK_FAILED: " + name);
        }
        void Reject<T>(Action body, string name) where T : Exception
        {
            try { body(); } catch (T) { Check(true, name); return; }
            Check(false, name);
        }
        try
        {
            Check(args[1].Length == 64 && Hash(target) == args[1].ToLowerInvariant(), "exact supplied artifact SHA256");
            var assembly = context.LoadFromAssemblyPath(target);
            NativeLibrary.SetDllImportResolver(assembly, (name, _, _) =>
            { nativeAttempts.Add(name); throw new InvalidOperationException("Native target call denied: " + name); });
            Check(assembly.GetName().Name == "YeManLightSetter", "shipping assembly identity");
            var program = assembly.GetType("Program", throwOnError: true)!;
            if(args.Length==4 && args[3]=="--legacy-fly-only")
            {
                var fly=new HandheldCompanion.Devices.OneXPlayerOneXFlyF1Pro();
                Check(Invoke(program,"OpenLightingDevice",fly) is true,"actual old helper opens OneXFly inherited lifecycle");
                observedOemWrites=fly.EcWrites.Count;observedDriverInitializations=fly.DriverInitializations;
                Check(observedOemWrites==0 && observedDriverInitializations==0,"bound HID-only OneXFly lighting must not initialize OLS or change OEM registers");
            }
            if(args.Length==4 && args[3]=="--legacy-cache-only")
            {
                var cached=new HandheldCompanion.Devices.OneXPlayerX1();var queue=assembly.GetType("OneXQueuedLighting",throwOnError:true)!;
                var outcome=Invoke(queue,"Execute",cached,new Func<string>(()=>(string)Invoke(program,"ExecuteLedAction",cached,cached.GetType(),"brightness","solid","off",10,20,30,40,50,60,0,75)!),5000,false)!;
                observedSerialWrites=cached.Port.Writes.Count;
                Check(outcome.GetType().GetProperty("Note")!.GetValue(outcome) as string=="ok" && observedSerialWrites==3,
                    "initial brightness zero must write brightness and both off packets despite default cache");
            }
            if (args.Length == 4 && args[3] == "--legacy-onex-only")
            {
                var oldOneX = new HandheldCompanion.Devices.OneXPlayerX1();
                Check(Invoke(program,"OpenLightingDevice",oldOneX) is true,"old actual helper reaches OneX virtual Open");
                observedOemWrites=oldOneX.EcWrites.Count; observedDriverInitializations=oldOneX.DriverInitializations;
                Check(observedOemWrites==0 && observedDriverInitializations==0,"OneX lighting must not initialize OLS or change OEM registers");
            }
            if (args.Length == 4 && args[3] == "--legacy-open-only")
            {
                Check(program.GetMethod("OpenLightingDevice", BindingFlags.NonPublic | BindingFlags.Static) is null,
                    "old helper predates the lighting-only boundary");
                var legacyDevice = new HandheldCompanion.Devices.ClawA2VM();
                Check(Invoke(program, "Invoke", legacyDevice, "Open", Array.Empty<object>()) is true,
                    "actual old helper invokes the virtual device Open");
                observedPowerWrites = legacyDevice.PowerWrites;
                Check(observedPowerWrites == 0, "lighting request must not enter power-limit writes");
            }
            var openBoundary = program.GetMethod("OpenLightingDevice", BindingFlags.NonPublic | BindingFlags.Static)
                ?? throw new MissingMethodException("OpenLightingDevice");
            Check(CallsMethod(program.GetMethod("Run", BindingFlags.NonPublic | BindingFlags.Static)!, openBoundary),
                "actual Run is wired to the lighting-only boundary");
            foreach (var msi in new HandheldCompanion.Devices.ClawA1M[] {
                new HandheldCompanion.Devices.ClawA1M(), new HandheldCompanion.Devices.ClawA2VM(),
                new HandheldCompanion.Devices.MSI.ClawCG3EM(), new InheritedClawLightingFixture() })
            {
                Check(Invoke(program, "OpenLightingDevice", msi) is true && msi.OpenCalls == 1 && msi.IsOpen,
                    "actual helper retains MSI complete lighting open state: " + msi.GetType().Name);
                Check(msi.PowerWrites == 0 && msi.ModeWrites == 0 && msi.RomWrites == 0 && msi.GeneralOpenCalls == 0,
                    "MSI lighting avoids power/mode/ROM writes: " + msi.GetType().Name);
                Check(msi.ObservedFirmware == 0x217, "MSI lighting preserves firmware descriptor: " + msi.GetType().Name);
                Check(Invoke(program, "ExecuteLedAction", msi, msi.GetType(), "color", "solid", "off", 10,20,30,40,50,60,50,75) as string == "ok" && msi.LedWrites == 2 && msi.LedFirmware == 0x217,
                    "real helper color and brightness reach ready MSI lighting: " + msi.GetType().Name);
                var failed = (HandheldCompanion.Devices.ClawA1M)Activator.CreateInstance(msi.GetType())!;
                failed.OpenResult = false;
                Check(Invoke(program, "OpenLightingDevice", failed) is false && !failed.IsOpen && failed.PowerWrites == 0,
                    "MSI base-open false result is preserved: " + msi.GetType().Name);
            }
            var missing = new HandheldCompanion.Devices.ClawA1M(); missing.RemoveHid();
            Check(Invoke(program,"OpenLightingDevice",missing) is false && missing.OpenCalls == 0 && missing.LedWrites == 0,
                "missing MSI HID cannot claim readiness or open");
            var disconnected = new HandheldCompanion.Devices.ClawA1M(); disconnected.Hid.IsConnected = false;
            Check(Invoke(program,"OpenLightingDevice",disconnected) is false && disconnected.OpenCalls == 0,
                "disconnected MSI HID is refused before base open");
            var unsafeMsi = new HandheldCompanion.Devices.ClawA2VM(); unsafeMsi.EnableOpenLibForFixture();
            Reject<InvalidOperationException>(() => Invoke(program, "OpenLightingDevice", unsafeMsi),
                "unreviewed OpenLib requirement is refused before opening");
            Check(unsafeMsi.OpenCalls == 0 && unsafeMsi.PowerWrites == 0, "refusal has no Open or TDP side effect");
            var sessionType=assembly.GetType("OneXLightingSession",throwOnError:true)!;
            var run=program.GetMethod("Run",BindingFlags.NonPublic|BindingFlags.Static)!;
            var prepare=sessionType.GetMethod("Prepare",BindingFlags.NonPublic|BindingFlags.Static)!;
            Check(CallsMethod(run,prepare),"actual Run prepares the OneX lighting transport session");
            Check(run.GetMethodBody()!.ExceptionHandlingClauses.Any(c=>c.Flags==ExceptionHandlingClauseOptions.Finally) &&
                (CallsMethod(run,typeof(IDisposable).GetMethod("Dispose")!) || CallsMethod(run,sessionType.GetMethod("Dispose")!)),
                "actual Run releases the OneX transport in finally");
            IDisposable? Prepare(object d)=>(IDisposable?)Invoke(sessionType,"Prepare",d);
            foreach(var oneX in new HandheldCompanion.Devices.OneXPlayerX1[] {new(),new HandheldCompanion.Devices.OneXPlayerX1AMD(),new HandheldCompanion.Devices.OneXPlayerX1Intel(),new HandheldCompanion.Devices.OneXPlayerX1Pro()})
            {
                oneX.ForgetPort(); var lease=Prepare(oneX)!;var port=oneX.Port;
                Check(port.IsOpen && port.PortName=="COM3" && port.BaudRate==115200 && port.DataBits==8 &&
                    port.Parity==System.IO.Ports.Parity.Even && port.StopBits==System.IO.Ports.StopBits.Two && port.WriteTimeout==1000,
                    "OneX lighting preserves serial configuration with bounded writes: "+oneX.GetType().Name);
                Check(Invoke(program,"OpenLightingDevice",oneX) is true && oneX.EcWrites.Count==0 && oneX.DriverInitializations==0,
                    "OneX lighting avoids driver and all OEM register writes: "+oneX.GetType().Name);
                var queue=assembly.GetType("OneXQueuedLighting",throwOnError:true)!;
                var outcome=Invoke(queue,"Execute",oneX,new Func<string>(()=>(string)Invoke(program,"ExecuteLedPreset",oneX,oneX.GetType(),"preset-high-value",42)!),5000,true)!;
                Check(outcome.GetType().GetProperty("Note")!.GetValue(outcome) as string=="ok" && port.Writes.SequenceEqual(new[]{"brightness:3","status-left:True","status-right:True","preset:483"}),
                    "actual OneX session and queue complete the requested writes: "+oneX.GetType().Name);
                lease.Dispose();lease.Dispose();
                Check(!oneX.HasPort && !port.IsOpen && port.CloseCalls==1 && port.DisposeCalls==1 && oneX.EcWrites.Count==0,
                    "owned OneX serial transport closes exactly once without general Close: "+oneX.GetType().Name);
            }
            var queued=assembly.GetType("OneXQueuedLighting",throwOnError:true)!;
            var cachePreparation=queued.GetMethod("PrepareRequestCache",BindingFlags.Static|BindingFlags.NonPublic)!;
            Check(CallsMethod(program.GetMethod("ExecuteLedAction",BindingFlags.Static|BindingFlags.NonPublic)!,cachePreparation),
                "actual action consumer prepares requested serial cache before dispatch");
            Check(CallsMethod(program.GetMethod("ExecuteLedPreset",BindingFlags.Static|BindingFlags.NonPublic)!,cachePreparation),
                "actual preset consumer prepares explicit brightness cache before dispatch");
            object ApplyCached(HandheldCompanion.Devices.OneXPlayerX1 target,string action,int? bright,string status="off")=>
                Invoke(queued,"Execute",target,new Func<string>(()=>action=="preset"
                    ?(string)Invoke(program,"ExecuteLedPreset",target,target.GetType(),"preset-high-value",bright)!
                    :(string)Invoke(program,"ExecuteLedAction",target,target.GetType(),action,"solid",status,10,20,30,40,50,60,bright,75)!),5000,action=="preset")!;
            foreach(var model in new[]{typeof(HandheldCompanion.Devices.OneXPlayerX1),typeof(HandheldCompanion.Devices.OneXPlayerX1AMD),typeof(HandheldCompanion.Devices.OneXPlayerX1Intel),typeof(HandheldCompanion.Devices.OneXPlayerX1Pro)})
            foreach(var request in new[]{
                (Action:"brightness",Brightness:(int?)0,Status:"off",Expected:new[]{"brightness:0","status-left:False","status-right:False"}),
                (Action:"status",Brightness:(int?)null,Status:"off",Expected:new[]{"status-left:False","status-right:False"}),
                (Action:"status",Brightness:(int?)null,Status:"on",Expected:new[]{"status-left:True","status-right:True"}),
                (Action:"off",Brightness:(int?)null,Status:"off",Expected:new[]{"status-left:False","status-right:False","brightness:0","color-controller","color-left","color-right"}),
                (Action:"preset",Brightness:(int?)0,Status:"off",Expected:new[]{"brightness:0","status-left:False","status-right:False","preset:483"}),
                (Action:"color",Brightness:(int?)0,Status:"off",Expected:new[]{"color-controller","color-left","color-right","brightness:0","status-left:False","status-right:False"})})
            {
                var cachedDevice=(HandheldCompanion.Devices.OneXPlayerX1)Activator.CreateInstance(model)!;
                var outcome=ApplyCached(cachedDevice,request.Action,request.Brightness,request.Status);
                Check(outcome.GetType().GetProperty("Note")!.GetValue(outcome) as string=="ok" &&
                    cachedDevice.Port.Writes.SequenceEqual(request.Expected) && (int)outcome.GetType().GetProperty("WriteTasks")!.GetValue(outcome)! ==request.Expected.Length,
                    "explicit cached serial request writes all required packets: "+model.Name+"/"+request.Action+"/"+request.Status);
                Check(cachedDevice.DriverInitializations==0 && cachedDevice.EcWrites.Count==0,
                    "cache repair remains lighting-only: "+model.Name+"/"+request.Action+"/"+request.Status);
            }
            foreach(var failedValue in new[]{"brightness:0","status-left:False","status-right:False"})
            {
                var failedWrite=new HandheldCompanion.Devices.OneXPlayerX1();failedWrite.Port.FailValue=failedValue;
                var outcome=ApplyCached(failedWrite,"brightness",0);
                Check(outcome.GetType().GetProperty("Note")!.GetValue(outcome) as string=="onex-write-queue-failed" &&
                    (int)outcome.GetType().GetProperty("FailedTasks")!.GetValue(outcome)! ==1 && failedWrite.Port.Writes.Count==3,
                    "initial zero preserves each serial failure: "+failedValue);
                failedWrite.Port.FailValue=null;outcome=ApplyCached(failedWrite,"brightness",0);
                Check(outcome.GetType().GetProperty("Note")!.GetValue(outcome) as string=="ok" && failedWrite.Port.Writes.Count==6,
                    "retry cannot trust cached state from failed serial transaction: "+failedValue);
            }
            foreach(var fly in new HandheldCompanion.Devices.OneXPlayerOneXFly[]{new(),new HandheldCompanion.Devices.OneXPlayerOneXFlyF1Pro()})
            {
                var discoveriesBefore=HandheldCompanion.Utils.DeviceUtils.Discoveries;
                var flyLease=Prepare(fly)!;
                Check(Invoke(program,"OpenLightingDevice",fly) is true && fly.DriverInitializations==0 && fly.EcWrites.Count==0,
                    "bound OneXFly HID lighting avoids inherited OLS and OEM writes: "+fly.GetType().Name);
                Check(Invoke(program,"ExecuteLedAction",fly,fly.GetType(),"color","solid","off",10,20,30,40,50,60,50,75) as string=="ok" && fly.HidWriteAttempts==2,
                    "actual OneXFly color and brightness use bound HID: "+fly.GetType().Name);
                Check(Invoke(program,"ExecuteLedAction",fly,fly.GetType(),"off","solid","off",10,20,30,40,50,60,null,75) as string=="ok" && fly.HidWriteAttempts==4,
                    "actual OneXFly off reaches brightness zero and black HID writes: "+fly.GetType().Name);
                fly.Hid.FailWrite=true;
                Check(Invoke(program,"ExecuteLedAction",fly,fly.GetType(),"color","solid","off",10,20,30,40,50,60,50,75) as string=="setLedColor-false" && fly.HidWriteAttempts==6 && fly.DriverInitializations==0,
                    "OneXFly HID failure cannot fall back to general Open: "+fly.GetType().Name);
                flyLease.Dispose();flyLease.Dispose();
                Check(fly.Hid.IsConnected && fly.EcWrites.Count==0 && HandheldCompanion.Utils.DeviceUtils.Discoveries==discoveriesBefore,
                    "bound OneXFly HID session never discovers or owns a serial port: "+fly.GetType().Name);
            }
            foreach(var type in new[]{typeof(HandheldCompanion.Devices.OneXPlayerOneXFly),typeof(HandheldCompanion.Devices.OneXPlayerOneXFlyF1Pro)})
            {
                var absent=(HandheldCompanion.Devices.OneXPlayerOneXFly)Activator.CreateInstance(type)!;absent.ForgetHid();
                Reject<InvalidOperationException>(()=>Prepare(absent),"missing bound OneXFly HID is refused: "+type.Name);
                Check(absent.DriverInitializations==0 && absent.EcWrites.Count==0 && absent.HidWriteAttempts==0,
                    "missing OneXFly HID never enters inherited Open: "+type.Name);
                var disconnectedFly=(HandheldCompanion.Devices.OneXPlayerOneXFly)Activator.CreateInstance(type)!;disconnectedFly.Hid.IsConnected=false;
                Reject<InvalidOperationException>(()=>Prepare(disconnectedFly),"disconnected bound OneXFly HID is refused: "+type.Name);
            }
            Reject<InvalidOperationException>(()=>Prepare(new UnknownFlyOpenFixture()),"unknown OneXFly lifecycle override is refused");
            var apex=new HandheldCompanion.Devices.OneXPlayer.OneXPlayerApex();
            Check(Invoke(program,"ValidatePresetDeviceIdentity",apex.GetType(),"HandheldCompanion.Devices.OneXPlayerApex","preset") is null,
                "Apex logical identity accepts its exact runtime namespace through existing short-name guard");
            using(Prepare(apex))
            {
                Check(Invoke(program,"OpenLightingDevice",apex) is true && apex.EcWrites.Count==0 && apex.DriverInitializations==0,
                    "actual nested Apex uses inherited narrow X1 serial session");
                var outcome=Invoke(queued,"Execute",apex,new Func<string>(()=>(string)Invoke(program,"ExecuteLedPreset",apex,apex.GetType(),"LEDPreset_OneXPlayerX1_Preset09",0)!),5000,true)!;
                Check(outcome.GetType().GetProperty("Note")!.GetValue(outcome) as string=="ok" && apex.Port.Writes.SequenceEqual(new[]{"brightness:0","status-left:False","status-right:False","preset:483"}),
                    "actual Apex named preset and zero brightness complete four ordered serial writes");
            }
            var borrowed=new HandheldCompanion.Devices.OneXPlayerX1();var borrowedPort=borrowed.Port;var discoveries=HandheldCompanion.Utils.DeviceUtils.Discoveries;
            Prepare(borrowed)!.Dispose();
            Check(borrowed.HasPort && borrowedPort.IsOpen && borrowedPort.DisposeCalls==0 && HandheldCompanion.Utils.DeviceUtils.Discoveries==discoveries,
                "borrowed OneX transport is neither rediscovered nor closed");
            var closedOneX=new HandheldCompanion.Devices.OneXPlayerX1();closedOneX.Port.IsOpen=false;
            Reject<InvalidOperationException>(()=>Prepare(closedOneX),"existing closed OneX port cannot report ready");
            var disabledOneX=new HandheldCompanion.Devices.OneXPlayerX1 {EnableSerialPort=false};
            Reject<InvalidOperationException>(()=>Prepare(disabledOneX),"disabled non-Mini serial transport cannot report ready");
            var miniOneX=new HandheldCompanion.Devices.OneXPlayerX1Mini();discoveries=HandheldCompanion.Utils.DeviceUtils.Discoveries;
            using(Prepare(miniOneX)) Check(Invoke(program,"OpenLightingDevice",miniOneX) is true && miniOneX.EcWrites.Count==0 && miniOneX.DriverInitializations==0 && HandheldCompanion.Utils.DeviceUtils.Discoveries==discoveries,
                "Mini legacy lighting uses bound HID without serial discovery or OLS or OEM writes");
            Check(Invoke(program,"ExecuteLedAction",miniOneX,miniOneX.GetType(),"brightness","solid","off",10,20,30,40,50,60,50,75) as string=="ok" && miniOneX.HidWrites==1 && miniOneX.Port.Writes.Count==0,
                "actual Mini legacy brightness uses HID and never inherited serial writes");
            miniOneX.Hid.FailWrite=true;
            Check(Invoke(program,"ExecuteLedAction",miniOneX,miniOneX.GetType(),"brightness","solid","off",10,20,30,40,50,60,50,75) as string=="setLedBrightness-false" && miniOneX.HidWrites==1,
                "Mini HID write failure remains failed without serial fallback");
            miniOneX.Hid.IsConnected=false;
            Reject<InvalidOperationException>(()=>Prepare(miniOneX),"disconnected Mini lighting HID cannot claim success");
            var unknownOneX=new UnknownOneXOpenFixture();
            Reject<InvalidOperationException>(()=>Prepare(unknownOneX),"unreviewed OneX Open override is refused before transport");
            var rows=HandheldCompanion.Utils.DeviceUtils.Rows;
            foreach(var names in new[]{Array.Empty<string>(),new[]{"CH340 without a port"},new[]{"CH340 (COM3)","CH340 (COM4)"}})
            {
                HandheldCompanion.Utils.DeviceUtils.Rows=names.Select(n=>new HandheldCompanion.Utils.SerialRow(n)).ToList();
                var unavailable=new HandheldCompanion.Devices.OneXPlayerX1();unavailable.ForgetPort();
                Reject<InvalidOperationException>(()=>Prepare(unavailable),"missing or ambiguous OneX serial selection is refused: "+string.Join(',',names));
                Check(!unavailable.HasPort && unavailable.DriverInitializations==0 && unavailable.EcWrites.Count==0,"serial selection failure has no unrelated effects");
            }
            HandheldCompanion.Utils.DeviceUtils.Rows=rows;
            foreach(var falseOpen in new[]{false,true})
            {
                System.IO.Ports.SerialPort.FailOpen=!falseOpen;System.IO.Ports.SerialPort.FalseOpen=falseOpen;
                var failedOpen=new HandheldCompanion.Devices.OneXPlayerX1();failedOpen.ForgetPort();
                if(falseOpen) Reject<InvalidOperationException>(()=>Prepare(failedOpen),"serial false Open is refused");
                else Reject<IOException>(()=>Prepare(failedOpen),"serial busy Open preserves failure");
                var failedPort=System.IO.Ports.SerialPort.LastCreated!;
                Check(!failedOpen.HasPort && !failedPort.IsOpen && failedPort.DisposeCalls==1,"failed serial Open releases only its owned port");
            }
            System.IO.Ports.SerialPort.FailOpen=false;System.IO.Ports.SerialPort.FalseOpen=false;
            var actionFailure=new HandheldCompanion.Devices.OneXPlayerX1();actionFailure.ForgetPort();var actionLease=Prepare(actionFailure)!;var actionPort=actionFailure.Port;
            Reject<IOException>(()=>{using(actionLease)throw new IOException("Injected LED action failure");},"action exception reaches caller after transport cleanup");
            Check(!actionFailure.HasPort && actionPort.DisposeCalls==1,"action failure releases owned OneX transport");
            var closeFailure=new HandheldCompanion.Devices.OneXPlayerX1();closeFailure.ForgetPort();var closeLease=Prepare(closeFailure)!;var closePort=closeFailure.Port;
            System.IO.Ports.SerialPort.FailClose=true;
            Reject<TargetInvocationException>(()=>closeLease.Dispose(),"serial Close error cannot silently succeed");
            System.IO.Ports.SerialPort.FailClose=false;
            Check(!closeFailure.HasPort && closePort.DisposeCalls==1,"serial Close failure still disposes and clears owned transport");
            Check(Prepare(new OrdinaryLightingOpenFixture()) is null,"unrelated models have no OneX transport session");
            var ordinary = new OrdinaryLightingOpenFixture();
            Check(Invoke(program, "OpenLightingDevice", ordinary) is true && ordinary.TransportOpens == 1,
                "other models retain their required virtual transport Open");
            string Action(object device, string action, int? brightness = null) => (string)Invoke(program, "ExecuteLedAction",
                device, device.GetType(), action, "solid", "off", 10, 20, 30, 40, 50, 60, brightness, 75)!;
            var noOp = new HandheldCompanion.Devices.ArtifactNoOpDevice();
            Reject<NotSupportedException>(() => Action(noOp, "color"), "inherited no-op color cannot report success");
            Reject<NotSupportedException>(() => Action(noOp, "brightness", 50), "inherited no-op brightness cannot report success");
            Check(noOp.Calls == 0, "inherited no-op methods were never invoked");
            var device = new FakeDevice();
            Check(Action(device, "color", 50) == "ok" && device.Calls.SequenceEqual(new[] { "color", "brightness" }), "real artifact forwards color then brightness to memory device");
            device.ColorOk = false;
            Check(Action(device, "color") == "setLedColor-false", "real artifact preserves false color result");
            device.ColorOk = true; device.BrightnessOk = false;
            Check(Action(device, "brightness", 50) == "setLedBrightness-false", "real artifact preserves false brightness result");
            device.StatusOk = false;
            Check(Action(device, "off") == "off-partial", "real artifact cannot claim partial off succeeded");
            foreach (var note in new string?[] { "setLedColor-false", "setLedBrightness-false", "off-partial", "", null })
                Check((int)Invoke(program, "ExitCodeForNote", note)! != 0, "nonzero exit contract: " + (note ?? "null"));
            Check((int)Invoke(program, "ExitCodeForNote", "ok")! == 0, "only successful note maps to exit zero");
            foreach (var invalid in new[] { Array.Empty<string>(), new[] { "--action", "bad" },
                new[] { "--action", "status", "--status", "invalid" }, new[] { "--action", "color", "--level", "bad" },
                new[] { "--action", "preset" }, new[] { "--unknown-flag" } })
                Reject<ArgumentException>(() => Invoke(program, "Run", (object)invalid), "invalid request refused before assembly/device access: " + string.Join(' ', invalid));
            var consumer = assembly.GetType("DeviceCoreConsumer", throwOnError: true)!;
            const string deviceClass = "HandheldCompanion.Devices.OneXPlayerX1Mini";
            Dictionary<string, object?> Receipt() => new() { ["deviceClass"] = deviceClass, ["action"] = "brightness", ["note"] = "ok",
                ["ready"] = true, ["opened"] = true, ["released"] = true, ["physicalEffectVerified"] = false, ["writeAttempts"] = 1 };
            object? Read(Dictionary<string, object?> row) => Invoke(consumer, "ReadReceipt", JsonSerializer.Serialize(row), deviceClass, "brightness", new { brightness = 50 });
            Check(Read(Receipt()) is not null, "exact completed memory receipt accepted");
            foreach (var mutation in new Dictionary<string, object?> { ["deviceClass"] = "wrong", ["action"] = "color", ["ready"] = false,
                ["opened"] = false, ["released"] = false, ["physicalEffectVerified"] = true, ["writeAttempts"] = 0, ["note"] = "unknown" })
            {
                var row = Receipt(); row[mutation.Key] = mutation.Value;
                Reject<InvalidOperationException>(() => Read(row), "invalid receipt rejected: " + mutation.Key);
            }
            Check(context.Attempts.Count == 0, "no target dependency resolution attempted");
            Check(nativeAttempts.Count == 0, "no target native call attempted");
            Check(!AppDomain.CurrentDomain.GetAssemblies().Any(a => a.GetName().Name == "HandheldCompanion"), "HC assembly never loaded");
            Check(Hash(target) == args[1].ToLowerInvariant(), "artifact bytes unchanged after behavior probes");
        }
        catch (Exception ex) { error = ex.GetBaseException().Message; }
        Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
        var report = new { status = error is null ? "PASS" : "FAIL", passed = error is null, artifact = target,
            expectedSha256 = args[1].ToLowerInvariant(), actualSha256 = File.Exists(target) ? Hash(target) : null,
            checks, checksPassed = checks.Count(c => (bool)c.GetType().GetProperty("passed")!.GetValue(c)!), error,
            observedPowerWrites, observedOemWrites, observedSerialWrites, observedDriverInitializations, dependencyAttempts = context.Attempts, nativeAttempts, mainEntryInvoked = false, hardwareIo = false,
            scope = "Actual supplied shipping DLL pure parsing/actions/receipt code; memory devices; no HC factory or physical transport" };
        File.WriteAllText(destination, JsonSerializer.Serialize(report, new JsonSerializerOptions { WriteIndented = true }));
        Console.WriteLine(JsonSerializer.Serialize(new { report.status, report.checksPassed, report.error, report.actualSha256, report.hardwareIo }));
        return error is null ? 0 : 2;
    }
}
namespace HandheldCompanion.Devices
{
    public class IDevice
    {
        public int Calls;
        protected bool UseOpenLib, DeviceOpen;
        protected Dictionary<int, FixtureHidDevice> hidDevices = new() { [1] = new() };
        public bool OpenResult = true;
        public int OpenCalls, PowerWrites;
        public virtual bool IsOpen => DeviceOpen;
        public virtual bool Open() { if (IsOpen) return true; OpenCalls++; return DeviceOpen = OpenResult; }
        public void EnableOpenLibForFixture() { UseOpenLib = true; }
        public virtual bool SetLedColor(Color a, Color b, FakeLed level, int speed) { Calls++; return true; }
        public virtual bool SetLedBrightness(int brightness) { Calls++; return true; }
    }
    public sealed class ArtifactNoOpDevice : IDevice { }
    public class ClawA1M : IDevice
    {
        private bool ClawOpen;
        protected int Firmware;
        public int ModeWrites, RomWrites, GeneralOpenCalls, LedWrites, LedFirmware;
        public int ObservedFirmware => Firmware;
        public FixtureHidDevice Hid => hidDevices[1];
        public void RemoveHid() => hidDevices.Clear();
        public override bool IsOpen => DeviceOpen && ClawOpen;
        public override bool Open()
        {
            GeneralOpenCalls++;
            if (!base.Open()) return false;
            Firmware = Hid.Attributes.Version;
            PowerWrites += 2; ModeWrites++; RomWrites++; ClawOpen = true; return true;
        }
        public override bool SetLedColor(Color a, Color b, FakeLed level, int speed)
        { if (!IsOpen) return false; LedWrites++; LedFirmware = Firmware; return true; }
        public override bool SetLedBrightness(int brightness)
        { if (!IsOpen) return false; LedWrites++; LedFirmware = Firmware; return true; }
    }
    public class ClawA2VM : ClawA1M
    { public override bool Open() { if (!base.Open()) return false; PowerWrites += 2; return true; } }
    public sealed class FixtureHidDevice
    {
        public bool IsConnected { get; set; } = true;
        public FixtureHidAttributes Attributes { get; } = new();
    }
    public sealed class FixtureHidAttributes { public int Version { get; set; } = 0x217; }
}
internal sealed class InheritedClawLightingFixture : HandheldCompanion.Devices.ClawA2VM { }
internal sealed class OrdinaryLightingOpenFixture : HandheldCompanion.Devices.IDevice
{
    public int TransportOpens;
    public override bool Open() { TransportOpens++; return base.Open(); }
}
namespace HandheldCompanion.Devices.MSI
{
    public class ClawCG3EM : HandheldCompanion.Devices.ClawA1M
    { public override bool Open() { if (!base.Open()) return false; PowerWrites += 2; return true; } }
}

internal sealed class UnknownOneXOpenFixture : HandheldCompanion.Devices.OneXPlayerX1 { public override bool Open()=>base.Open(); }

internal sealed class UnknownFlyOpenFixture : HandheldCompanion.Devices.OneXPlayerOneXFly { public override bool Open()=>base.Open(); }

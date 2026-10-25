using System.Windows.Media;
using System.Reflection;
using System.Text.Json;

// Link the production helper, but invoke only its pure action/contract methods.
// No HC assembly, factory, WMI, HID, EC, driver or real device is loaded.
internal static class LightSetterSelfTest
{
    private static int checks;
    private static void Check(bool condition, string name) { if (!condition) throw new Exception(name); checks++; }
    private static object? Call(string name, params object?[] args) => typeof(Program).GetMethod(name, BindingFlags.NonPublic | BindingFlags.Static)!.Invoke(null, args);
    private static string Action(FakeDevice device, string action, int? brightness = null) => (string)Call("ExecuteLedAction", device, device.GetType(), action, "solid", "off", 10, 20, 30, 40, 50, 60, brightness, 75)!;
    private static void Reject(Type exception, Action body, string name)
    {
        try { body(); } catch (TargetInvocationException e) when (exception.IsInstanceOfType(e.InnerException)) { checks++; return; }
        throw new Exception(name + " was accepted");
    }
    public static void Main()
    {
        var device = new FakeDevice();
        Check(Action(device, "color", 50) == "ok", "successful color plus brightness");
        Check(device.Calls.SequenceEqual(new[] { "color", "brightness" }), "color action order");
        Check(device.Main == Color.FromArgb(255, 10, 20, 30) && device.Second == Color.FromArgb(255, 40, 50, 60) && device.Speed == 75, "color arguments forwarded");
        device.ColorOk = false;
        Check(Action(device, "color") == "setLedColor-false", "color failure preserved");
        device.ColorOk = true; device.BrightnessOk = false;
        Check(Action(device, "color", 50) == "setLedBrightness-false", "combined brightness failure preserved");
        Check(Action(device, "brightness", 50) == "setLedBrightness-false", "brightness failure preserved");
        device.StatusOk = false;
        Check(Action(device, "status") == "setLedStatus-false", "status failure preserved");
        device.Calls.Clear();
        Check(Action(device, "off") == "off-partial", "partial off fails");
        Check(device.Calls.SequenceEqual(new[] { "status", "brightness", "color" }), "all off steps attempted on bool failure");
        device.BrightnessOk = true; device.StatusOk = true;
        Check(Action(device, "off") == "ok", "full off success");
        Check(device.Main == Colors.Black && device.Second == Colors.Black && device.Brightness == 0 && !device.Status, "off arguments");
        foreach (var note in new string?[] { "setLedColor-false", "setLedBrightness-false", "setLedStatus-false", "off-partial", "", null })
            Check((int)Call("ExitCodeForNote", note)! != 0, "failed note must return nonzero: " + note);
        Check((int)Call("ExitCodeForNote", "ok")! == 0, "only ok succeeds");
        Reject(typeof(ArgumentException), () => Action(device, "invalid"), "unknown action");
        Reject(typeof(MissingMethodException), () => Call("ResolveSetLedColorContract", typeof(object)), "missing method");
        Reject(typeof(AmbiguousMatchException), () => Call("ResolveSetLedColorContract", typeof(AmbiguousDevice)), "ambiguous method");
        Reject(typeof(InvalidOperationException), () => Call("ResolveSetLedColorContract", typeof(VoidDevice)), "void callback is not bool success");
        var lockedHash = (string)typeof(Program).GetField("ExpectedHcSha256", BindingFlags.NonPublic | BindingFlags.Static)!.GetRawConstantValue()!;
        Check(lockedHash == "0c5132a9d13aebfc5add2aa7c9ac54e8daaa8ebc816a0c68d099bb9685dc2e49", "HC dependency remains locked");
        Check(!AppDomain.CurrentDomain.GetAssemblies().Any(x => x.GetName().Name == "HandheldCompanion"), "HC must never load in this test");
        Call("ValidatePresetDeviceIdentity", typeof(FakePresetDevice), "HandheldCompanion.Devices.FakePresetDevice", "preset");checks++;
        Reject(typeof(InvalidOperationException),()=>Call("ValidatePresetDeviceIdentity",typeof(FakePresetDevice),"HandheldCompanion.Devices.Unrelated","preset"),"preset class mismatch");
        Call("ValidatePresetDeviceIdentity",typeof(FakePresetDevice),"HandheldCompanion.Devices.AynLoki","color");checks++;
        var presets = new FakePresetDevice();
        string Preset(FakePresetDevice target, string? id, int? brightness = null) => (string)Call("ExecuteLedPreset", target, target.GetType(), id, brightness)!;
        Check(Preset(presets, "preset-high-value", 67) == "ok", "named preset reaches independent method");
        Check(ReferenceEquals(presets.Selected, presets.LEDPresets[1]) && presets.Selected!.Value == 0x1e3, "actual preset object and multi-byte numeric value retained");
        Check(presets.Calls.SequenceEqual(new[] { "brightness", "preset" }), "HC brightness-before-preset order");
        presets.Calls.Clear();Reject(typeof(ArgumentException), () => Preset(presets, "missing", 10), "unknown preset");Check(presets.Calls.Count == 0, "invalid ID rejected before either write");
        Reject(typeof(ArgumentException), () => Preset(presets, null), "missing ID");
        presets.LEDPresets.Add(new FakePreset("preset-high-value", 8));Reject(typeof(ArgumentException), () => Preset(presets, "preset-high-value"), "duplicate ID");presets.LEDPresets.RemoveAt(2);
        presets.PresetOk=false;Check(Preset(presets,"preset-first")=="setLEDPreset-false", "preset false is propagated");presets.PresetOk=true;presets.BrightnessOk=false;
        Check(Preset(presets,"preset-first",10)=="setLedBrightness-false", "brightness failure does not turn into success");
        Reject(typeof(ArgumentException),()=>Call("ExecuteLedAction",device,device.GetType(),"color","preset","off",10,20,30,40,50,60,null,75),"enum128 is not a named preset");
        var oneX=new HandheldCompanion.Devices.OneXPlayerX1();
        var ordered=OneXQueuedLighting.Execute(oneX,()=>{oneX.Write("first");oneX.Write("second");oneX.Write("third");return "ok";});
        Check(ordered.Note=="ok"&&ordered.Managed&&ordered.Completed&&ordered.WriteTasks==3&&ordered.FailedTasks==0,"wait for all actual queue continuations");
        Check(oneX.Port.Writes.SequenceEqual(new[]{"first","second","third"}),"actual FIFO order is retained");
        for(int failed=0;failed<3;failed++){
            var broken=new HandheldCompanion.Devices.OneXPlayerX1();broken.Port.FailValue=new[]{"first","second","third"}[failed];
            var result=OneXQueuedLighting.Execute(broken,()=>{broken.Write("first");broken.Write("second");broken.Write("third");return "ok";});
            Check(result.Note=="onex-write-queue-failed"&&result.Completed&&result.WriteTasks==3&&result.FailedTasks==1,"earlier write fault survives HC's successful final continuation");
            Check(broken.Port.Writes.Count==3,"test reaches final continuation after earlier fault");
        }
        var closed=new HandheldCompanion.Devices.OneXPlayerX1();closed.Port.IsOpen=false;int calls=0;
        Check(OneXQueuedLighting.Execute(closed,()=>{calls++;return "ok";}).Note=="onex-serial-port-unavailable"&&calls==0,"closed port never calls the action");
        var disabled=new HandheldCompanion.Devices.OneXPlayerX1{EnableSerialPort=false};Check(OneXQueuedLighting.Execute(disabled,()=>{calls++;return "ok";}).Note=="onex-serial-port-unavailable"&&calls==0,"disabled port never calls action");
        var blocked=new HandheldCompanion.Devices.OneXPlayerX1();using var release=new ManualResetEventSlim(false);blocked.Port.Release=release;
        var timeout=OneXQueuedLighting.Execute(blocked,()=>{blocked.Write("blocked");return "ok";},20);
        Check(timeout.Note=="onex-write-queue-timeout"&&!timeout.Completed,"bounded wait cannot claim a blocked write completed");
        release.Set();Check(SpinWait.SpinUntil(()=>blocked.Port.Writes.Count==1,1000),"release fixture task before ending test");
        var unrelated=OneXQueuedLighting.Execute(new object(),()=>"explicit-false");Check(!unrelated.Managed&&unrelated.Note=="explicit-false","unrelated families retain original direct action");
        var actual=new HandheldCompanion.Devices.OneXPlayerX1();var apply=OneXQueuedLighting.Execute(actual,()=>(string)Call("ExecuteLedPreset",actual,actual.GetType(),"preset-high-value",42)!);
        Check(apply.Note=="ok"&&apply.Completed&&actual.Port.Writes.SequenceEqual(new[]{"brightness:3","status-left:True","status-right:True","preset:483"}),"actual helper preset function through actual HC queue to fake serial write");
        var noWrite=new HandheldCompanion.Devices.OneXPlayerX1();
        Check(OneXQueuedLighting.Execute(noWrite,()=>"ok").Note=="onex-no-request-write-observed","cached-success without requested write cannot claim an applied transaction");
        var mini=new HandheldCompanion.Devices.OneXPlayerX1Mini();int hidCalls=0;
        var hid=OneXQueuedLighting.Execute(mini,()=>{hidCalls++;return "hid-ok";});
        Check(hid.Note=="hid-ok"&&!hid.Managed&&hidCalls==1,"Mini HID operations bypass its disabled inherited serial port");
        var missingPreset=OneXQueuedLighting.Execute(mini,()=>{hidCalls++;return "unexpected";},preset:true);
        Check(missingPreset.Note=="onex-serial-port-unavailable"&&hidCalls==1,"Mini cannot claim inherited serial presets work");
        Check(!AppDomain.CurrentDomain.GetAssemblies().Any(x => x.GetName().Name == "HandheldCompanion"),"HC still never loaded");
        Console.WriteLine(JsonSerializer.Serialize(new { ok = true, checks, hardwareWrites = false, HCAssemblyLoaded = false, actualPinnedHCQueue = true }));
    }
}
public enum FakeLed { Solid = 1 }
public class FakeDevice
{
    public bool ColorOk = true, BrightnessOk = true, StatusOk = true, Status;
    public int Brightness, Speed;
    public Color Main, Second;
    public List<string> Calls = [];
    public bool SetLedColor(Color main, Color second, FakeLed level, int speed) { Calls.Add("color"); Main = main; Second = second; Speed = speed; return ColorOk; }
    public bool SetLedBrightness(int brightness) { Calls.Add("brightness"); Brightness = brightness; return BrightnessOk; }
    public bool SetLedStatus(bool status) { Calls.Add("status"); Status = status; return StatusOk; }
}
public class AmbiguousDevice : FakeDevice { public bool SetLedColor(int value) => true; }
public class VoidDevice { public void SetLedColor(Color main, Color second, FakeLed level, int speed) { } }

public sealed record FakePreset(string NameKey, int Value);
public class FakePresetDevice
{
 public List<FakePreset> LEDPresets {get;}=[new("preset-first",13),new("preset-high-value",0x1e3)];
 public List<string> Calls {get;}=[];public bool PresetOk=true,BrightnessOk=true;public FakePreset? Selected;
 public bool SetLedBrightness(int value){Calls.Add("brightness");return BrightnessOk;}
 public bool SetLEDPreset(FakePreset preset){Calls.Add("preset");Selected=preset;return PresetOk;}
}

using System.IO;
using System.IO.MemoryMappedFiles;
using System.IO.Pipes;
using System.Text;
using Microsoft.Win32.SafeHandles;
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
    private static void RejectDirect(Type exception, Action body, string name)
    {
        try { body(); } catch (Exception e) when (exception.IsInstanceOfType(e)) { checks++; return; }
        throw new Exception(name + " was accepted");
    }
    private static void PublishHandoff(MemoryMappedViewAccessor view, string token, string path, uint state, long sequence)
    {
        var tokenBytes = new byte[Oxp3OwnerLease.TokenLength];
        var ascii = Encoding.ASCII.GetBytes(token);
        Array.Copy(ascii, tokenBytes, Math.Min(ascii.Length, tokenBytes.Length));
        var pathBytes = new byte[Oxp3OwnerLease.PathLength];
        var wide = Encoding.Unicode.GetBytes(path);
        Array.Copy(wide, pathBytes, Math.Min(wide.Length, pathBytes.Length));
        var tokenParts = token.Split(':');
        var admission = tokenParts.Length == 3 && ulong.TryParse(tokenParts[2], out var value) ? value : 0;
        view.Write(Oxp3OwnerLease.SequenceOffset, sequence + 1L);
        view.Write(Oxp3OwnerLease.AdmissionOffset, (long)admission);
        view.Write(Oxp3OwnerLease.MagicOffset, Oxp3OwnerLease.HandoffMagic);
        view.Write(Oxp3OwnerLease.VersionOffset, Oxp3OwnerLease.HandoffVersion);
        view.Write(Oxp3OwnerLease.StateOffset, state);
        view.Write(Oxp3OwnerLease.HeartbeatOffset, Environment.TickCount64);
        view.WriteArray(Oxp3OwnerLease.TokenOffset, tokenBytes, 0, tokenBytes.Length);
        view.WriteArray(Oxp3OwnerLease.PathOffset, pathBytes, 0, pathBytes.Length);
        view.Write(Oxp3OwnerLease.SequenceOffset, sequence + 2L);
    }
    private static void RunOneXSerialDiscovery()
    {
        var cases = new List<string>();
        void Selected(string name, object?[] rows, string expectedPort)
        {
            Check(OneXLightingSession.SelectSerialPort(rows, "CH340") == expectedPort, name);
            cases.Add(name);
        }
        string Failed(string name, object?[] rows, int matchedPortsCount, string descriptor = "CH340", string? displayDescriptor = null)
        {
            try { OneXLightingSession.SelectSerialPort(rows, descriptor); }
            catch (InvalidOperationException error)
            {
                var reason = matchedPortsCount == 0 ? "OneX lighting serial port not found" : "OneX lighting serial port ambiguous";
                Check(error.Message.StartsWith(reason + "; ", StringComparison.Ordinal), name + " failure preserved");
                Check(error.Message.Contains("expectedDescriptor=\"" + (displayDescriptor ?? descriptor) + "\";", StringComparison.Ordinal), name + " expected descriptor");
                Check(error.Message.Contains("; rowCount=" + rows.Length + ";", StringComparison.Ordinal), name + " row count");
                Check(error.Message.Contains("; matchedPortsCount=" + matchedPortsCount + ";", StringComparison.Ordinal), name + " unique matched ports");
                Check(error.Message.EndsWith("; discovery=" + (rows.Length == 0 ? "empty-or-query-failed" : "rows-observed"), StringComparison.Ordinal), name + " query uncertainty");
                cases.Add(name);
                return error.Message;
            }
            throw new Exception(name + " was accepted");
        }
        void Bounded(string message)
        {
            const string key = "; observedNames=";
            var start = message.IndexOf(key, StringComparison.Ordinal);
            var end = message.IndexOf("; discovery=", StringComparison.Ordinal);
            Check(start >= 0 && end > start + key.Length, "observed names context present");
            var names = JsonSerializer.Deserialize<string[]>(message[(start + key.Length)..end])!;
            Check(names.Length == 8, "observed names capped at eight");
            Check(names.All(name => name.Length == 100), "observed names capped at 100 characters");
            Check(names.Select((name, index) => name.StartsWith($"row-{index:D2}:", StringComparison.Ordinal)).All(value => value), "first eight observed rows retained");
            Check(!message.Any(c => char.IsControl(c) || char.IsSurrogate(c) || c is '\u2028' or '\u2029' || char.GetUnicodeCategory(c) == System.Globalization.UnicodeCategory.Format), "diagnostic has no control or formatting characters");
            Check(names.All(name => !name.Contains('"') && !name.Contains('\\')), "observed delimiters sanitized");
            Check(message.Length < 1200, "exception context bounded");
        }
        Selected("single-port", [new { Name = "USB-SERIAL CH340 (COM3)" }], "COM3");
        Selected("duplicate-same-com", [new { Name = "USB-SERIAL CH340 (COM3)" }, new { Name = "CH340 (COM3)" }, new { Name = "Generic (COM9)" }], "COM3");
        Selected("descriptor-contains-retained", [new { Name = "prefixCH340suffix (COM24)" }], "COM24");
        Selected("first-com-match-retained", [new { Name = "CH340 (COM6) (COM7)" }], "COM6");
        Selected("null-name-with-valid-port", [null, new { Name = (string?)null }, new object(), new { Name = 42 }, new { Name = "CH340 (COM10)" }], "COM10");
        var laterPortRows = Enumerable.Range(0, 8).Select(index => (object?)new { Name = "Generic row " + index }).Append(new { Name = "CH340 (COM42)" }).ToArray();
        Selected("port-after-observation-cap", laterPortRows, "COM42");
        Failed("zero-rows-empty-or-query-failed", [], 0);
        Failed("descriptor-without-com", [new { Name = "USB-SERIAL CH340" }], 0);
        Failed("missing-descriptor", [new { Name = "USB-SERIAL (COM3)" }], 0);
        Failed("wrong-chip-rejected", [new { Name = "USB-SERIAL CH341 (COM3)" }, new { Name = "USB-SERIAL CH9102 (COM4)" }], 0);
        Failed("generic-port-rejected", [new { Name = "USB Serial Device (COM3)" }, new { Name = "Communications Port (COM4)" }], 0);
        Failed("descriptor-ordinal-case", [new { Name = "USB-SERIAL ch340 (COM3)" }], 0);
        Failed("com-regex-case", [new { Name = "CH340 (com3)" }], 0);
        foreach (var invalidPort in new[] { "COM0", "COM03", "COM", "XCOM3", "COM3X", "COM3_" })
            Failed("com-regex-reject-" + invalidPort, [new { Name = "CH340 (" + invalidPort + ")" }], 0);
        Failed("distinct-com-ambiguity", [new { Name = "CH340 (COM3)" }, new { Name = "CH340 (COM4)" }, new { Name = "CH340 duplicate (COM3)" }], 2);
        var nullNames = Failed("null-name-rejected", [null, new { Name = (string?)null }], 0);
        Check(nullNames.Contains("observedNames=[\"<null>\", \"<null>\"]", StringComparison.Ordinal), "null names have safe context");
        var unsafeName = "name\r\n\t\0\u001b\u007f\u0085\"\\\u2028\u2029\u202e" + new string('n', 160);
        var unmatchedRows = Enumerable.Range(0, 12).Select(index => (object?)new { Name = $"row-{index:D2}:" + unsafeName }).ToArray();
        Bounded(Failed("bounded-not-found-context", unmatchedRows, 0));
        var ambiguousRows = Enumerable.Range(0, 12).Select(index => (object?)new { Name = $"row-{index:D2}:CH340 (COM{index % 2 + 3}) " + unsafeName }).ToArray();
        Bounded(Failed("bounded-ambiguous-context", ambiguousRows, 2));
        const string unsafeDescriptorPrefix = "\r\n\t\0\u001b\u2028\u2029\u202e\"\\";
        var displayDescriptor = new string('?', unsafeDescriptorPrefix.Length) + new string('D', 100 - unsafeDescriptorPrefix.Length);
        var descriptorMessage = Failed("bounded-descriptor-context", [], 0, unsafeDescriptorPrefix + new string('D', 160), displayDescriptor);
        Check(!descriptorMessage.Any(char.IsControl) && descriptorMessage.Length < 300, "expected descriptor sanitized and bounded");
        // Current field failure: localized Name, underlying USB identity unknown.
        // Even a plausible USB ID is observation only, not permission to write.
        var genericHardware = new { Name = "USB 串行设备 (COM3)", DeviceId = @"USB\VID_1A86&PID_7523\FIELD", VID = "1A86", PID = "7523" };
        var hardwareMessage = Failed("generic-name-hardware-observed-still-rejected", [genericHardware], 0);
        JsonElement[] Hardware(string message) {
            Check(message.Contains("; observedHardware=", StringComparison.Ordinal), "ordinary serial failure retains hardware identity evidence");
            const string key = "; observedHardware=";
            var start = message.IndexOf(key, StringComparison.Ordinal) + key.Length;
            var end = message.IndexOf("; observedNames=", StringComparison.Ordinal);
            return JsonSerializer.Deserialize<JsonElement[]>(message[start..end])!;
        }
        var hardware = Hardware(hardwareMessage);
        Check(hardware.Length == 1 && hardware[0].GetProperty("row").GetInt32() == 1, "hardware evidence stays paired with enumerated row");
        Check(hardware[0].GetProperty("deviceId").GetString() == genericHardware.DeviceId && hardware[0].GetProperty("vid").GetString() == "1A86" && hardware[0].GetProperty("pid").GetString() == "7523", "same-row USB identity survives normal error text");
        Check(hardware[0].GetProperty("protocolAdmitted").GetBoolean() == false, "hardware inventory never claims protocol admission");
        var largeHardwareRows = Enumerable.Range(0, 12).Select(index => (object?)new { Name = "generic " + index, DeviceId = "row" + index + unsafeName, VID = unsafeName, PID = unsafeName }).ToArray();
        var boundedHardwareMessage = Failed("bounded-hardware-evidence", largeHardwareRows, 0);
        var boundedHardware = Hardware(boundedHardwareMessage);
        Check(boundedHardware.Length == 8, "hardware context shares the original eight-row observation ceiling");
        Check(boundedHardware.Select((row, index) => row.GetProperty("row").GetInt32() == index + 1).All(x => x), "hardware diagnostics preserve row order");
        Check(boundedHardware.All(row => new[]{"deviceId", "vid", "pid"}.All(key => row.GetProperty(key).GetString()!.Length <= 100)), "each hardware field shares the existing 100-character ceiling");
        Check(!boundedHardwareMessage.Any(char.IsControl) && boundedHardwareMessage.Length < 5000, "hardware error context remains one bounded line");
        Selected("matched-name-hardware-evidence-does-not-change-selection", [genericHardware, new { Name = "CH340 (COM8)", DeviceId = "unknown", VID = "", PID = "" }], "COM8");
        Console.WriteLine(JsonSerializer.Serialize(new { ok = true, scope = "onex-serial-discovery", cases, checks, productionSelector = true, hardwareWrites = false, HCAssemblyLoaded = false, defaultSuiteRun = false }));
    }
    public static void Main(string[] args)
    {
        if (args.Length == 1 && args[0] == "--apex-hid-v2") { ApexLightingTests.Run(); return; }
        if (args.Length != 0)
        {
            if (args.Length != 1 || args[0] != "--onex-serial-discovery")
                throw new ArgumentException("Unsupported LightSetterSelfTest arguments");
            RunOneXSerialDiscovery();
            return;
        }
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
        var oxp3Transport=new FakeOxp3Transport();var waits=new List<int>();var oxp3=new Oxp3LightingWriter(oxp3Transport,waits.Add);
        Check(oxp3.ApplyColor(new Oxp3Rgb(10,20,30),new Oxp3Rgb(40,50,60),false,50),"OXP3 solid color succeeds");
        Check(oxp3Transport.Reports.Count==8,"OXP3 solid sends eight source writes");
        Check(oxp3.Operations.SequenceEqual(new[]{
            "brightness:gkey:50:3","color:logo:40,50,60:False","brightness:logo:50:3","color:gkey:40,50,60:False",
            "color:left:10,20,30:False","color:right:10,20,30:False","brightness:left:50:3","brightness:right:50:3"}),"OXP3 source order");
        Check(waits.SequenceEqual(new[]{300,300,300,150,300,300,600,600,600,600}),"OXP3 exact waits");
        Check(oxp3Transport.Reports.All(r=>r.Length==65&&r[0]==0&&r[1]==0xB8&&r[2]==0x3F&&r[3]==1&&r[63]==0x3F&&r[64]==0xB8),"OXP3 report framing");
        var breathTransport=new FakeOxp3Transport();var breath=new Oxp3LightingWriter(breathTransport,_=>{ });
        Check(breath.ApplyColor(new Oxp3Rgb(1,2,3),new Oxp3Rgb(4,5,6),true,80),"OXP3 breathing color succeeds");
        Check(breath.Operations.Where(o=>o.StartsWith("color:logo")||o.StartsWith("color:gkey")).All(o=>o.EndsWith(":True"))&&breath.Operations.Where(o=>o.StartsWith("color:left")||o.StartsWith("color:right")).All(o=>o.EndsWith(":False")),"OXP3 second set breathes while sticks stay solid");
        var offTransport=new FakeOxp3Transport();var offWriter=new Oxp3LightingWriter(offTransport,_=>{ });
        Check(offWriter.ApplyOff()&&offTransport.Reports.Count==4,"OXP3 off uses four brightness-zero writes");
        Check(offWriter.Operations.SequenceEqual(new[]{"brightness:logo:0:1","brightness:gkey:0:1","brightness:left:0:1","brightness:right:0:1"}),"OXP3 off order and zero level");
        var zeroTransport=new FakeOxp3Transport();var zeroWriter=new Oxp3LightingWriter(zeroTransport,_=>{ });
        Check(zeroWriter.ApplyBrightness(0,new Oxp3Rgb(9,9,9),new Oxp3Rgb(9,9,9))&&zeroTransport.Reports.Count==4&&!zeroWriter.Operations.Any(o=>o.StartsWith("color")),"OXP3 brightness zero cannot re-light via color");
        var classicTransport=new FakeOxp3Transport();var classic=new Oxp3LightingWriter(classicTransport,_=>{ });
        Check(classic.ApplyPreset(1000,new Oxp3Rgb(4,5,6),50)&&classicTransport.Reports.Count==8,"OXP3 classic preset sends eight writes");
        Check(classic.Operations.SequenceEqual(new[]{
            "brightness:gkey:50:3","color:logo:4,5,6:False","brightness:logo:50:3","color:gkey:4,5,6:False",
            "color:left:183,48,0:False","color:right:183,48,0:False","brightness:left:50:3","brightness:right:50:3"}),"OXP3 classic preset color and order");
        var presetTransport=new FakeOxp3Transport();var presetWriter=new Oxp3LightingWriter(presetTransport,_=>{ });
        Check(presetWriter.ApplyPreset(13,new Oxp3Rgb(7,8,9),50)&&presetWriter.Operations.SequenceEqual(new[]{
            "brightness:gkey:50:3","color:logo:7,8,9:False","brightness:logo:50:3","color:gkey:7,8,9:False",
            "preset:left:13","preset:right:13","brightness:left:50:3","brightness:right:50:3"}),"OXP3 firmware preset order");
        var failedTransport=new FakeOxp3Transport{FailAt=5};var failedWriter=new Oxp3LightingWriter(failedTransport,_=>{ });
        Check(!failedWriter.ApplyColor(new Oxp3Rgb(1,2,3),new Oxp3Rgb(4,5,6),false,50)&&failedWriter.FailedWrites==1&&failedTransport.Reports.Count==8,"OXP3 short write is preserved after remaining source attempts");
        RejectDirect(typeof(IOException),()=>new Oxp3LightingWriter(new FakeOxp3Transport{IsConnected=false},_=>{ }).ApplyColor(new Oxp3Rgb(1,2,3),new Oxp3Rgb(4,5,6),false,50),"disconnected OXP3 HID is rejected before writes");
        var command=Oxp3LightingWriter.BuildV1Command(0xB8,0x01,new byte[]{0xFD,0x05,0x02,1,0x05,4});var report=Oxp3LightingWriter.WithReportID(command);
        Check(command.Length==64&&command[0]==0xB8&&command[1]==0x3F&&command[2]==1&&command[62]==0x3F&&command[63]==0xB8&&report.Length==65&&report[0]==0,"OXP3 v1 framing exact");
        Check(Oxp3OwnerLease.PathsMatch("test-oxp3-path","TEST-OXP3-PATH")&&!Oxp3OwnerLease.PathsMatch("test-oxp3-path","other-path"),"OXP3 owner interface path exact");
        bool? directCancelResult=null;int? directCancelError=null;int? ownerCancelCalls=null;bool? ownerCancelResult=null;int? ownerCancelError=null;int? deadlineCancelCalls=null;bool? deadlineCancelResult=null;int? deadlineCancelError=null;
        int[]? directCancelOutcomes=null,ownerCancelOutcomes=null,deadlineCancelOutcomes=null;
        bool? directIssuedIncomplete=null,directTokenCancelledAtIssue=null,directCancelledWhileIncomplete=null;
        bool? ownerIssuedIncomplete=null,ownerTokenCancelledAtIssue=null,ownerCancelledWhileIncomplete=null;
        bool? deadlineIssuedIncomplete=null,deadlineTokenCancelledAtIssue=null,deadlineCancelledWhileIncomplete=null;
        using(var handoffFile=MemoryMappedFile.CreateOrOpen(Oxp3OwnerLease.HandoffRecordName,Oxp3OwnerLease.HandoffRecordSize,MemoryMappedFileAccess.ReadWrite))
        using(var handoffView=handoffFile.CreateViewAccessor())
        using(var ownerEvent=new EventWaitHandle(false,EventResetMode.ManualReset,Oxp3OwnerLease.EventNameFor("7:9:1")))
        {
            PublishHandoff(handoffView,"7:9:1","test-oxp3-path",Oxp3OwnerLease.StateActive,2);
            using(var ownerLease=Oxp3OwnerLease.Acquire("test-oxp3-path","7:9:1"))
            {
                Check(ownerLease.IsCurrent,"OXP3 exact generation, endpoint and heartbeat are admitted");
                ownerLease.ValidateInterfacePath("TEST-OXP3-PATH");
                RejectDirect(typeof(InvalidOperationException),()=>ownerLease.ValidateInterfacePath("other-path"),"OXP3 owner path mismatch rejected");
                ownerEvent.Set();
                Check(ownerLease.Token.WaitHandle.WaitOne(2000)&&ownerLease.WasRevoked,"OXP3 owner event revokes the live sequence token");
                RejectDirect(typeof(OperationCanceledException),ownerLease.EnsureCurrent,"revoked OXP3 lease fails the next write check");
            }
            PublishHandoff(handoffView,"","test-oxp3-path",Oxp3OwnerLease.StateIdle,4);
            RejectDirect(typeof(IOException),()=>Oxp3OwnerLease.Acquire("test-oxp3-path","7:9:1"),"retired OXP3 handoff is not admitted");
            RejectDirect(typeof(ArgumentException),()=>Oxp3OwnerLease.Acquire("","7:9:1"),"OXP3 owner token is mandatory");
            RejectDirect(typeof(ArgumentException),()=>Oxp3OwnerLease.Acquire("test-oxp3-path","abc"),"malformed OXP3 owner token rejected before open");
            RejectDirect(typeof(ArgumentException),()=>Oxp3OwnerLease.Acquire("test-oxp3-path","007:9:1"),"non-canonical OXP3 owner token rejected before open");
            PublishHandoff(handoffView,"8:10:2","test-oxp3-path",Oxp3OwnerLease.StateActive,6);
            using(var newEvent=new EventWaitHandle(false,EventResetMode.ManualReset,Oxp3OwnerLease.EventNameFor("8:10:2")))
            using(var newLease=Oxp3OwnerLease.Acquire("test-oxp3-path","8:10:2"))
            {
                ownerEvent.Set();
                Check(!newLease.WasRevoked&&newLease.IsCurrent,"reused revocation object never revives the current owner");
                RejectDirect(typeof(IOException),()=>Oxp3OwnerLease.Acquire("test-oxp3-path","7:9:1"),"old generation cannot join the newer handoff");
                var opened=0;
                RejectDirect(typeof(IOException),()=>Program.ExecuteOxp3LightingWith("color","solid",10,20,30,40,50,60,50,75,null,5000,"test-oxp3-path","7:9:1",(path,_)=>{opened++;return new FakeOxp3Transport{DevicePath=path!};}),"stale generation rejected before device open");
                Check(opened==0,"stale generation performs zero device opens");
                RejectDirect(typeof(IOException),()=>Program.ExecuteOxp3LightingWith("color","solid",10,20,30,40,50,60,50,75,null,5000,"other-path","8:10:2",(path,_)=>{opened++;return new FakeOxp3Transport{DevicePath=path!};}),"wrong endpoint rejected before device open");
                Check(opened==0,"wrong endpoint performs zero device opens");
                var slices=0;var sliceTransport=new FakeOxp3Transport{DevicePath="test-oxp3-path"};
                var sliceWriter=new Oxp3LightingWriter(sliceTransport,_=>{if(++slices==8)PublishHandoff(handoffView,"","test-oxp3-path",Oxp3OwnerLease.StateIdle,8);},default,newLease.EnsureCurrent);
                RejectDirect(typeof(OperationCanceledException),()=>sliceWriter.ApplyColor(new Oxp3Rgb(1,2,3),new Oxp3Rgb(4,5,6),false,50),"handoff change stops later non-return writes");
                Check(sliceTransport.Reports.Count==1,"handoff change after the first wait permits only the first write");
                PublishHandoff(handoffView,"8:10:2","test-oxp3-path",Oxp3OwnerLease.StateActive,10);
                Check(newLease.IsCurrent,"republished matching handoff is current again");
            }
            PublishHandoff(handoffView,"7:9:2","test-oxp3-path",Oxp3OwnerLease.StateActive,12);
            using(var sameDeviceEvent=new EventWaitHandle(false,EventResetMode.ManualReset,Oxp3OwnerLease.EventNameFor("7:9:2")))
            using(var sameDeviceLease=Oxp3OwnerLease.Acquire("test-oxp3-path","7:9:2"))
            {
                ownerEvent.Set();
                Check(!sameDeviceLease.WasRevoked&&sameDeviceLease.IsCurrent,"same-device second admission has its own event and record");
                RejectDirect(typeof(IOException),()=>Oxp3OwnerLease.Acquire("test-oxp3-path","7:9:1"),"old admission token cannot rejoin after same-device return");
                Check(!Oxp3OwnerLease.PathsMatch("7:9:1","7:9:2"),"admission tokens differ on the same device");
            }
            var canceled=new CancellationTokenSource();canceled.Cancel();
            RejectDirect(typeof(OperationCanceledException),()=>Oxp3WindowsHidTransport.Open("test-oxp3-path",canceled.Token),"canceled open performs no device enumeration");
            RejectDirect(typeof(OperationCanceledException),()=>Oxp3OwnerLease.Acquire("test-oxp3-path","7:9:2",canceled.Token),"canceled admission performs no handoff open");
            PublishHandoff(handoffView,"7:9:3","test-oxp3-path",Oxp3OwnerLease.StateActive,14);
            using(var pendingEvent=new EventWaitHandle(false,EventResetMode.ManualReset,Oxp3OwnerLease.EventNameFor("7:9:3")))
            using(var pendingLease=Oxp3OwnerLease.Acquire("test-oxp3-path","7:9:3"))
            using(var operation=CancellationTokenSource.CreateLinkedTokenSource(pendingLease.Token))
            {
                var blocking=new BlockingOxp3Transport();
                var pendingWriter=new Oxp3LightingWriter(blocking,_=>{},operation.Token,pendingLease.EnsureCurrent);
                var pendingTask=System.Threading.Tasks.Task.Run(()=>{try{pendingWriter.ApplyColor(new Oxp3Rgb(1,2,3),new Oxp3Rgb(4,5,6),false,50);return false;}catch(OperationCanceledException){return true;}});
                Check(blocking.WriteEntered.Wait(2000),"owner pending IO entered");
                pendingEvent.Set();
                var taskDone=pendingTask.Wait(3000);var pendingCanceled=false;var pendingError="";
                try{if(taskDone)pendingCanceled=pendingTask.Result;}catch(Exception ex){pendingError=ex.GetType().Name+":"+ex.Message;}
                Check(pendingLease.WasRevoked&&taskDone&&pendingCanceled,"owning-lease revocation reaches pending IO "+pendingError);
                Check(blocking.Reports==1,"pending IO receives exactly one write");
            }
            PublishHandoff(handoffView,"7:9:4","test-oxp3-path",Oxp3OwnerLease.StateActive,16);
            using(var programEvent=new EventWaitHandle(false,EventResetMode.ManualReset,Oxp3OwnerLease.EventNameFor("7:9:4")))
            {
                var blocking=new BlockingOxp3Transport();
                var programTask=System.Threading.Tasks.Task.Run(()=>Program.ExecuteOxp3LightingWith("color","solid",10,20,30,40,50,60,50,75,null,5000,"test-oxp3-path","7:9:4",(path,token)=>blocking));
                Check(blocking.WriteEntered.Wait(2000),"production Program pending IO entered");
                programEvent.Set();
                var done=programTask.Wait(3000);var programReceipt=done?programTask.Result:null;
                Check(done&&programReceipt!=null&&(string?)programReceipt["note"]=="lighting-owner-revoked","production Program lease token withdraws pending IO");
            }
            PublishHandoff(handoffView,"7:9:6","test-oxp3-path",Oxp3OwnerLease.StateActive,18);
            using(var openEvent=new EventWaitHandle(false,EventResetMode.ManualReset,Oxp3OwnerLease.EventNameFor("7:9:6")))
            {
                var openEntered=new ManualResetEventSlim();
                var openTask=System.Threading.Tasks.Task.Run(()=>{try{Program.ExecuteOxp3LightingWith("color","solid",10,20,30,40,50,60,50,75,null,5000,"test-oxp3-path","7:9:6",(path,token)=>{openEntered.Set();token.WaitHandle.WaitOne();throw new OperationCanceledException(token);});return false;}catch(OperationCanceledException){return true;}});
                Check(openEntered.Wait(2000),"production Program open entered");
                openEvent.Set();
                Check(openTask.Wait(3000)&&openTask.Result,"production Program lease token cancels open");
            }
            PublishHandoff(handoffView,"7:9:7","test-oxp3-path",Oxp3OwnerLease.StateActive,20);
            using(var openDeadlineEvent=new EventWaitHandle(false,EventResetMode.ManualReset,Oxp3OwnerLease.EventNameFor("7:9:7")))
            {
                var openEntered=new ManualResetEventSlim();
                var openTask=System.Threading.Tasks.Task.Run(()=>{try{Program.ExecuteOxp3LightingWith("color","solid",10,20,30,40,50,60,50,75,null,1000,"test-oxp3-path","7:9:7",(path,token)=>{openEntered.Set();token.WaitHandle.WaitOne();throw new OperationCanceledException(token);});return false;}catch(OperationCanceledException){return true;}});
                Check(openEntered.Wait(2000),"production Program deadline open entered");
                Check(openTask.Wait(3000)&&openTask.Result,"production Program deadline cancels open");
            }
            PublishHandoff(handoffView,"7:9:5","test-oxp3-path",Oxp3OwnerLease.StateActive,18);
            using(var deadlineEvent=new EventWaitHandle(false,EventResetMode.ManualReset,Oxp3OwnerLease.EventNameFor("7:9:5")))
            {
                var blocking=new BlockingOxp3Transport();
                var deadlineTask=System.Threading.Tasks.Task.Run(()=>Program.ExecuteOxp3LightingWith("color","solid",10,20,30,40,50,60,50,75,null,1000,"test-oxp3-path","7:9:5",(path,token)=>blocking));
                Check(blocking.WriteEntered.Wait(2000),"production Program deadline pending IO entered");
                var done=deadlineTask.Wait(3000);var deadlineReceipt=done?deadlineTask.Result:null;
                Check(done&&deadlineReceipt!=null&&(string?)deadlineReceipt["note"]=="lighting-sequence-timeout","production Program deadline expires pending IO");
            }
            var directPipe=@"\\.\pipe\ymcc-oxp3-pipe-" + Guid.NewGuid().ToString("N");
            using(var directServer=PipeNative.CreateNamedPipe(directPipe,0x00000001,0,255,1,1,0,IntPtr.Zero))
            {
                Check(!directServer.IsInvalid,"direct cancel named pipe server created");
                var connectTask=System.Threading.Tasks.Task.Run(()=>PipeNative.ConnectNamedPipe(directServer,IntPtr.Zero));
                var directHandle=File.OpenHandle(directPipe,FileMode.Open,FileAccess.Write,FileShare.ReadWrite,FileOptions.Asynchronous);
                Check(connectTask.Wait(2000),"direct cancel pipe client connected");
                var entered=new ManualResetEventSlim();var issued=new ManualResetEventSlim();var entries=0;
                var directTransport=Oxp3WindowsHidTransport.ForTest(directHandle,"test-oxp3-path",FileAccess.Write,()=>{Interlocked.Increment(ref entries);entered.Set();},()=>issued.Set());
                var writeTask=System.Threading.Tasks.Task.Run(()=>{try{directTransport.Write(new byte[65],CancellationToken.None);return "completed";}catch(Exception ex){return ex.GetType().Name;}});
                Check(entered.Wait(2000),"actual write entered before direct CancelIoEx");
                Check(issued.Wait(2000),"actual write issued before direct CancelIoEx");
                Check(!writeTask.IsCompleted,"actual write is pending before direct CancelIoEx");
                directTransport.CancelPendingIo();
                directCancelResult=directTransport.AnyCancelIoSucceeded;directCancelError=directTransport.LastCancelIoError;directCancelOutcomes=directTransport.CancelIoOutcomes.ToArray();
                directIssuedIncomplete=directTransport.LastWriteIssuedIncomplete;directTokenCancelledAtIssue=directTransport.LastWriteTokenCancelledAtIssue;directCancelledWhileIncomplete=directTransport.LastWriteCancelledWhileIncomplete;
                Check(directTransport.CancelIoCalls>=1&&directTransport.AnyCancelIoSucceeded,"direct CancelIoEx cancels the pending OS write");
                Check(directIssuedIncomplete==true&&directTokenCancelledAtIssue==false,"direct write was incomplete at issue with an uncancelled token");
                Check(writeTask.Wait(2000)&&writeTask.Result!="completed","canceled OS write returns an error");
                directTransport.Dispose();
                Check(!directTransport.IsConnected,"direct canceled transport handle cleanup");
                Check(entries==1,"direct cancel permits no later ordinary write");
                RejectDirect(typeof(ObjectDisposedException),()=>directTransport.Write(new byte[65],CancellationToken.None),"direct canceled transport refuses later ordinary write");
            }
            var pipeName=@"\\.\pipe\ymcc-oxp3-pipe-" + Guid.NewGuid().ToString("N");
            using(var ioServer=PipeNative.CreateNamedPipe(pipeName,0x00000001,0,255,1,1,0,IntPtr.Zero))
            {
                Check(!ioServer.IsInvalid,"actual IO named pipe server created");
                var connectTask=System.Threading.Tasks.Task.Run(()=>PipeNative.ConnectNamedPipe(ioServer,IntPtr.Zero));
                var ioHandle=File.OpenHandle(pipeName,FileMode.Open,FileAccess.Write,FileShare.ReadWrite,FileOptions.Asynchronous);
                Check(connectTask.Wait(2000),"actual IO pipe client connected");
                PublishHandoff(handoffView,"7:9:8","test-oxp3-path",Oxp3OwnerLease.StateActive,22);
                using(var ioEvent=new EventWaitHandle(false,EventResetMode.ManualReset,Oxp3OwnerLease.EventNameFor("7:9:8")))
                {
                    var writeEntered=new ManualResetEventSlim();var writeIssued=new ManualResetEventSlim();var writeEntries=0;Oxp3WindowsHidTransport? actualTransport=null;
                    var programTask=System.Threading.Tasks.Task.Run(()=>Program.ExecuteOxp3LightingWith("color","solid",10,20,30,40,50,60,50,75,null,5000,"test-oxp3-path","7:9:8",(path,token)=>{
                        actualTransport=Oxp3WindowsHidTransport.ForTest(ioHandle,"test-oxp3-path",FileAccess.Write,()=>{Interlocked.Increment(ref writeEntries);writeEntered.Set();},()=>writeIssued.Set());
                        return actualTransport;
                    }));
                    Check(writeEntered.Wait(2000),"actual transport write entered before owner cancellation");
                    Check(writeIssued.Wait(2000),"actual transport write issued before owner cancellation");
                    Check(!programTask.IsCompleted,"actual transport write is pending at owner cancellation");
                    ioEvent.Set();
                    var done=programTask.Wait(3000);var ioReceipt=done?programTask.Result:null;
                    Check(done&&ioReceipt!=null&&(string?)ioReceipt["note"]=="lighting-owner-revoked","owner event cancels actual pending write through Program");
                    Check(actualTransport!=null&&!actualTransport.IsConnected,"actual transport handle closed by Program");
                    ownerCancelCalls=actualTransport?.CancelIoCalls;ownerCancelResult=actualTransport?.AnyCancelIoSucceeded;ownerCancelError=actualTransport?.LastCancelIoError;ownerCancelOutcomes=actualTransport?.CancelIoOutcomes.ToArray();
                    ownerIssuedIncomplete=actualTransport?.LastWriteIssuedIncomplete;ownerTokenCancelledAtIssue=actualTransport?.LastWriteTokenCancelledAtIssue;ownerCancelledWhileIncomplete=actualTransport?.LastWriteCancelledWhileIncomplete;
                    Check(ownerIssuedIncomplete==true&&ownerTokenCancelledAtIssue==false&&ownerCancelledWhileIncomplete==true,"owner cancellation hit the same IO while incomplete with an uncancelled token");
                    Check(ownerCancelCalls>=1&&(ownerCancelResult==true||ownerCancelError==1168),"Program owner cancellation records the terminal CancelIoEx outcome (err="+ownerCancelError+")");
                    Check(writeEntries==1,"cancellation after actual IO entry permits no later ordinary write");
                    RejectDirect(typeof(ObjectDisposedException),()=>actualTransport!.Write(new byte[65],CancellationToken.None),"actual transport refuses later ordinary write after Program cleanup");
                }
            }
            var deadlinePipe=@"\\.\pipe\ymcc-oxp3-pipe-" + Guid.NewGuid().ToString("N");
            using(var deadlineServer=PipeNative.CreateNamedPipe(deadlinePipe,0x00000001,0,255,1,1,0,IntPtr.Zero))
            {
                Check(!deadlineServer.IsInvalid,"actual deadline pipe server created");
                var connectTask=System.Threading.Tasks.Task.Run(()=>PipeNative.ConnectNamedPipe(deadlineServer,IntPtr.Zero));
                var deadlineHandle=File.OpenHandle(deadlinePipe,FileMode.Open,FileAccess.Write,FileShare.ReadWrite,FileOptions.Asynchronous);
                Check(connectTask.Wait(2000),"actual deadline pipe client connected");
                PublishHandoff(handoffView,"7:9:9","test-oxp3-path",Oxp3OwnerLease.StateActive,24);
                using(var deadlineIoEvent=new EventWaitHandle(false,EventResetMode.ManualReset,Oxp3OwnerLease.EventNameFor("7:9:9")))
                {
                var writeEntered=new ManualResetEventSlim();var writeIssued=new ManualResetEventSlim();var writeEntries=0;Oxp3WindowsHidTransport? actualTransport=null;
                var programTask=System.Threading.Tasks.Task.Run(()=>Program.ExecuteOxp3LightingWith("color","solid",10,20,30,40,50,60,50,75,null,1500,"test-oxp3-path","7:9:9",(path,token)=>{
                    actualTransport=Oxp3WindowsHidTransport.ForTest(deadlineHandle,"test-oxp3-path",FileAccess.Write,()=>{Interlocked.Increment(ref writeEntries);writeEntered.Set();},()=>writeIssued.Set());
                    return actualTransport;
                }));
                if(!writeEntered.Wait(2000))Console.Error.WriteLine($"DIAG deadline task done={programTask.IsCompleted} status={programTask.Status} ex={programTask.Exception?.InnerException?.GetType().Name}:{programTask.Exception?.InnerException?.Message}");
                Check(writeEntered.Wait(0),"actual transport write entered before deadline cancellation");
                Check(writeIssued.Wait(2000),"actual transport write issued before deadline cancellation");
                Check(!programTask.IsCompleted,"actual transport write is pending at deadline cancellation");
                var done=programTask.Wait(6000);var deadlineIoReceipt=done?programTask.Result:null;
                Check(done&&deadlineIoReceipt!=null&&(string?)deadlineIoReceipt["note"]=="lighting-sequence-timeout","request deadline cancels actual pending write through Program");
                Check(actualTransport!=null&&!actualTransport.IsConnected,"actual deadline transport handle closed by Program");
                deadlineCancelCalls=actualTransport?.CancelIoCalls;deadlineCancelResult=actualTransport?.AnyCancelIoSucceeded;deadlineCancelError=actualTransport?.LastCancelIoError;deadlineCancelOutcomes=actualTransport?.CancelIoOutcomes.ToArray();
                deadlineIssuedIncomplete=actualTransport?.LastWriteIssuedIncomplete;deadlineTokenCancelledAtIssue=actualTransport?.LastWriteTokenCancelledAtIssue;deadlineCancelledWhileIncomplete=actualTransport?.LastWriteCancelledWhileIncomplete;
                Check(deadlineIssuedIncomplete==true&&deadlineTokenCancelledAtIssue==false&&deadlineCancelledWhileIncomplete==true,"deadline cancellation hit the same IO while incomplete with an uncancelled token");
                Check(deadlineCancelCalls>=1&&(deadlineCancelResult==true||deadlineCancelError==1168),"Program deadline cancellation records the terminal CancelIoEx outcome (err="+deadlineCancelError+")");
                Check(writeEntries==1,"deadline cancellation after actual IO entry permits no later ordinary write");
                RejectDirect(typeof(ObjectDisposedException),()=>actualTransport!.Write(new byte[65],CancellationToken.None),"actual deadline transport refuses later ordinary write");
                }
            }
        }
        var cancelToken=new CancellationTokenSource();var waitCount=0;var cancelTransport=new FakeOxp3Transport();
        var cancelWriter=new Oxp3LightingWriter(cancelTransport,delay=>{if(++waitCount==2)cancelToken.Cancel();},cancelToken.Token);
        RejectDirect(typeof(OperationCanceledException),()=>cancelWriter.ApplyColor(new Oxp3Rgb(1,2,3),new Oxp3Rgb(4,5,6),false,50),"cancelled sequence stops before later writes");
        Check(cancelTransport.Reports.Count==1,"cancellation after first wait permits only the first write");
        var alreadyCancelled=new CancellationTokenSource();alreadyCancelled.Cancel();
        var cancelledReceipt=Program.ExecuteOxp3Admitted("color","solid",10,20,30,40,50,60,50,75,null,new FakeOxp3Transport(),alreadyCancelled.Token);
        Check((string?)cancelledReceipt["note"]=="lighting-sequence-timeout"&&(int?)cancelledReceipt["queuedWriteTasks"]==0&&(bool?)cancelledReceipt["queuedWritesCompleted"]==false,"cancelled OXP3 sequence reports timeout without writes");
        var receiptTransport=new FakeOxp3Transport();var receipt=Program.ExecuteOxp3Admitted("color","solid",10,20,30,40,50,60,50,75,null,receiptTransport);
        Check((string?)receipt["note"]=="ok"&&(string?)receipt["deviceClass"]=="HandheldCompanion.Devices.OneXPlayer3"&&(string?)receipt["capabilityId"]=="ymcc.device.OneXPlayer3"&&(string?)receipt["capabilityOperation"]=="lighting.color.solid","OXP3 receipt identity and operation");
        Check((int?)receipt["queuedWriteTasks"]==8&&(bool?)receipt["queuedWritesCompleted"]==true&&(bool?)receipt["queuedWriteManaged"]==false&&(bool?)receipt["physicalEffectVerified"]==false,"OXP3 receipt write inventory");
        var offReceipt=Program.ExecuteOxp3Admitted("off",null,null,null,null,null,null,null,null,null,null,new FakeOxp3Transport());
        Check((string?)offReceipt["note"]=="ok"&&(int?)offReceipt["queuedWriteTasks"]==4,"OXP3 off receipt");
        var presetReceipt=Program.ExecuteOxp3Admitted("preset",null,10,20,30,40,50,60,50,75,"LEDPreset_OneXPlayerX1_Preset09",new FakeOxp3Transport());
        Check((string?)presetReceipt["note"]=="ok"&&(int?)presetReceipt["queuedWriteTasks"]==8,"OXP3 classic preset receipt");
        RejectDirect(typeof(NotSupportedException),()=>Program.ExecuteOxp3Admitted("preset",null,10,20,30,40,50,60,50,75,"LEDPreset_OneXPlayerX1_Preset08",new FakeOxp3Transport()),"OXP3 missing preset rejected before transport");
        RejectDirect(typeof(NotSupportedException),()=>Program.ExecuteOxp3Admitted("status",null,null,null,null,null,null,null,null,null,null,new FakeOxp3Transport()),"OXP3 status rejected before transport");
        var failReceipt=Program.ExecuteOxp3Admitted("color","solid",10,20,30,40,50,60,50,75,null,new FakeOxp3Transport{FailAt=5});
        Check((string?)failReceipt["note"]=="setLedColor-false"&&(bool?)failReceipt["queuedWritesCompleted"]==false&&(int?)failReceipt["queuedWriteFailures"]==1,"OXP3 short-write receipt is not success");
        var originalChecks = checks;
        var superJoyDependencyChecks = SuperJoyProtocolTests.Run();
        checks += superJoyDependencyChecks;
        var superJoyWindowsTransportChecks = SuperJoyWindowsTransportTests.Run();
        checks += superJoyWindowsTransportChecks;
        var superJoyCombinedConsumerChecks = AyaneoSuperJoyConsumerTests.Run();
        checks += superJoyCombinedConsumerChecks;
        Console.WriteLine(JsonSerializer.Serialize(new { ok = true, checks, superJoyCombinedConsumerChecks, originalChecks, superJoyDependencyChecks, superJoyWindowsTransportChecks, superJoyRuntimeRouteAdmitted = true, hardwareWrites = false, HCAssemblyLoaded = false, actualPinnedHCQueue = true, oxp3PureHid = true, directCancelResult, directCancelError, directCancelOutcomes, directIssuedIncomplete, directTokenCancelledAtIssue, directCancelledWhileIncomplete, ownerCancelCalls, ownerCancelResult, ownerCancelError, ownerCancelOutcomes, ownerIssuedIncomplete, ownerTokenCancelledAtIssue, ownerCancelledWhileIncomplete, deadlineCancelCalls, deadlineCancelResult, deadlineCancelError, deadlineCancelOutcomes, deadlineIssuedIncomplete, deadlineTokenCancelledAtIssue, deadlineCancelledWhileIncomplete }));
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

internal static class PipeNative
{
    [System.Runtime.InteropServices.DllImport("kernel32.dll", EntryPoint="CreateNamedPipeW", CharSet=System.Runtime.InteropServices.CharSet.Unicode, SetLastError=true)]
    internal static extern SafeFileHandle CreateNamedPipe(string name,uint openMode,uint pipeMode,uint maxInstances,uint outBuffer,uint inBuffer,uint timeout,IntPtr security);
    [System.Runtime.InteropServices.DllImport("kernel32.dll", SetLastError=true)]
    [return: System.Runtime.InteropServices.MarshalAs(System.Runtime.InteropServices.UnmanagedType.Bool)]
    internal static extern bool ConnectNamedPipe(SafeFileHandle handle,IntPtr overlapped);
}

public sealed class BlockingOxp3Transport : IOxp3HidTransport
{
    public ManualResetEventSlim WriteEntered {get;} = new(false);
    public int Reports {get; private set;}
    public string DevicePath => "test-oxp3-path";
    public bool IsConnected => true;
    public bool Write(byte[] report, CancellationToken token)
    {
        Reports++;
        WriteEntered.Set();
        if (token.WaitHandle.WaitOne(5000)) throw new OperationCanceledException(token);
        return true;
    }
    public void CancelPendingIo() { }
    public void Dispose() { }
}

public sealed class FakeOxp3Transport : IOxp3HidTransport
{
    public bool IsConnected {get; set;} = true;
    public string DevicePath {get; set;} = "test-oxp3-path";
    public int FailAt {get; set;} = -1;
    public int CancelCalls {get; private set;}
    public List<byte[]> Reports {get;} = [];
    public bool Write(byte[] report, CancellationToken token)
    {
        if (token.IsCancellationRequested) throw new OperationCanceledException(token);
        Reports.Add(report);
        return FailAt < 0 || Reports.Count != FailAt;
    }
    public void CancelPendingIo() => CancelCalls++;
    public void Dispose() { }
}

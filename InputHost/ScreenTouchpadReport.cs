using System.Buffers.Binary;
using System.Reflection;
using System.Text.Json;
using HIDMaestro;

internal static partial class Program
{
    private readonly record struct ScreenPad(bool Active, bool Click, short X, short Y, ushort Pressure);
    // Single Host submitter; reuse the data-only report for every submission.
    private static readonly byte[] ScreenPadReportBuffer = new byte[64];
    private static uint _screenPadPacketCounter;
    private static bool _screenPadOwnsReport;
    private static readonly YeManInputHost.Diagnostics.SteamDeckPadSubmissionEvidence ScreenPadSubmissionEvidence = new();
    // SDK 2.0 uses DeviceClock instead of the old idle CTS. Raw report
    // sequencing still has one writer: stop that clock through the reviewed
    // barrier before taking its last encoded counter, for both FRAME and OFF.
    private static readonly FieldInfo? ScreenPadLastEncoded = typeof(HMController).GetField("_extendedReportBuffer",BindingFlags.Instance|BindingFlags.NonPublic);
    private static uint TakeScreenPadReportOwnership(HMController controller, bool allowMissingIdleThread = false)
    {
        if(!YeManInputHost.Diagnostics.HostControllerIdleCtsRelease.TryStopInputClock(controller,out var detail))
            throw new InvalidOperationException("steamdeck-idle-handoff:"+detail);
        if(ScreenPadLastEncoded?.GetValue(controller) is byte[] prior && prior.Length==64)
            return BinaryPrimitives.ReadUInt32LittleEndian(prior.AsSpan(4));
        // A partial prepare without an allocated report can be released after
        // the same clock barrier; ordinary publication still needs its report.
        if(allowMissingIdleThread)return 0;
        throw new InvalidOperationException("locked-steamdeck-report-contract-missing");
    }
    // Use the same checked handoff before terminal neutral. SDK Dispose ignores
    // its own Join timeout, so it must not be the first idle-writer barrier.
    private static void StopSteamDeckIdleForRelease(HMController controller)
    {
        if (_screenPadOwnsReport) return;
        _screenPadPacketCounter = TakeScreenPadReportOwnership(controller, allowMissingIdleThread: true);
        _screenPadOwnsReport = true;
    }
    private static void SubmitSteamDeckWithScreenPads(HMController controller,HMProfile profile,CommandEnvelope envelope, bool observePadData = false, bool isNeutralCommand = false,
        HostSession? signalSession = null, CommandEnvelope? signalCause = null, string signalReason = "live-frame")
    {
        if(!_screenPadOwnsReport)
        {
            _screenPadPacketCounter=TakeScreenPadReportOwnership(controller);
            _screenPadOwnsReport=true;
        }
        var report = BuildSteamDeckScreenPadReport(profile,envelope,unchecked(++_screenPadPacketCounter),ScreenPadReportBuffer);
        // Observe the FINAL reused SD buffer only after the existing ownership barrier and encoder.
        // No diagnostic owns the clock/buffer or changes the packet counter/SDK arguments.
        var signalAttempt = signalSession is null ? default : signalSession.BeginVirtualSignalEvidenceSafely(
            controller, signalCause, envelope, signalReason, _screenPadPacketCounter, report, null);
        try { controller.SubmitRawReport(report); }
        catch (Exception ex)
        {
            signalSession?.CompleteVirtualSignalEvidenceSafely(signalAttempt, ex);
            throw;
        }
        signalSession?.CompleteVirtualSignalEvidenceSafely(signalAttempt, null);
        var left = envelope.ScreenPadLeft; var right = envelope.ScreenPadRight;
        ScreenPadSubmissionEvidence.Record(observePadData, report,
            new(left.Active,left.Click,left.X,left.Y,left.Pressure),
            new(right.Active,right.Click,right.X,right.Y,right.Pressure),
            envelope.Sequence,envelope.SampleSequence,envelope.MonotonicTick,isNeutralCommand);
    }
    private static readonly (HMButton Button, int Bit)[] DeckButtonBits =
    {
        (HMButton.RightBumper,2),(HMButton.LeftBumper,3),(HMButton.Y,4),
        (HMButton.Circle,5),(HMButton.Square,6),(HMButton.Cross,7),
        (HMButton.Back,12),(HMButton.Guide,13),(HMButton.Start,14),
        (HMButton.LeftPaddle2,15),(HMButton.RightPaddle2,16),
        (HMButton.LeftPaddle,41),(HMButton.RightPaddle,42),
        (HMButton.LeftStick,22),(HMButton.RightStick,26)
    };
    private static bool TryReadScreenPads(JsonElement e, out ScreenPad left, out ScreenPad right)
    {
        left=right=default;
        if(e.ValueKind!=JsonValueKind.Object) return false;
        foreach(var field in e.EnumerateObject()) if(field.Name is not ("left" or "right")) return false;
        return e.TryGetProperty("left",out var l) && e.TryGetProperty("right",out var r) &&
            TryReadScreenPad(l,out left) && TryReadScreenPad(r,out right);
    }
    private static bool TryReadScreenPad(JsonElement e,out ScreenPad p)
    {
        p=default;if(e.ValueKind!=JsonValueKind.Object) return false;
        foreach(var field in e.EnumerateObject())
            if(field.Name is not ("active" or "click" or "x" or "y" or "pressure")) return false;
        if(!e.TryGetProperty("active",out var a) || a.ValueKind is not (JsonValueKind.True or JsonValueKind.False) ||
           !e.TryGetProperty("click",out var c) || c.ValueKind is not (JsonValueKind.True or JsonValueKind.False) ||
           !e.TryGetProperty("x",out var x) || x.ValueKind!=JsonValueKind.Number || !x.TryGetInt32(out var xv) || xv<short.MinValue || xv>short.MaxValue ||
           !e.TryGetProperty("y",out var y) || y.ValueKind!=JsonValueKind.Number || !y.TryGetInt32(out var yv) || yv<short.MinValue || yv>short.MaxValue ||
           !e.TryGetProperty("pressure",out var pressure) || pressure.ValueKind!=JsonValueKind.Number || !pressure.TryGetInt32(out var pv) || pv<0 || pv>32767)
            return false;
        if(!a.GetBoolean()) return !c.GetBoolean() && xv==0 && yv==0 && pv==0;
        p=new(true,c.GetBoolean(),(short)xv,(short)yv,(ushort)pv);return true;
    }
    private static byte[] BuildSteamDeckScreenPadReport(HMProfile profile,CommandEnvelope e,uint counter,byte[]? buffer=null)
    {
        if(profile.InputReportSize!=64) throw new InvalidOperationException("steam-deck-wire-size-mismatch");
        var data=buffer ?? new byte[64];Array.Clear(data);
        data[0]=1;data[2]=9;data[3]=64;
        BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(4),counter);
        // Keep all existing stick/trigger/IMU/button semantics; only pad fields are new.
        var fields=BuildSteamDeckStateFields(profile,e);
        ulong buttons=0;
        foreach(var map in DeckButtonBits) if((fields.Buttons & map.Button)!=0) buttons|=1UL<<map.Bit;
        // SDK 1.12 issue #64: digital trigger bits follow the quantized trigger byte.
        if(TriggerToByte(fields.Rz)>0) buttons|=1;
        if(TriggerToByte(fields.Z)>0) buttons|=2;
        if(fields.Hat is HMHat.North or HMHat.NorthEast or HMHat.NorthWest) buttons|=1UL<<8;
        if(fields.Hat is HMHat.East or HMHat.NorthEast or HMHat.SouthEast) buttons|=1UL<<9;
        if(fields.Hat is HMHat.West or HMHat.NorthWest or HMHat.SouthWest) buttons|=1UL<<10;
        if(fields.Hat is HMHat.South or HMHat.SouthEast or HMHat.SouthWest) buttons|=1UL<<11;
        static void Write16(byte[] d,int offset,short v)=>BinaryPrimitives.WriteInt16LittleEndian(d.AsSpan(offset),v);
        void Pad(ScreenPad pad,int offset,int pressureOffset,int touchBit,int clickBit)
        {
            if(!pad.Active) return;
            buttons|=1UL<<touchBit;if(pad.Click) buttons|=1UL<<clickBit;
            Write16(data,offset,pad.X);Write16(data,offset+2,pad.Y);
            BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(pressureOffset),pad.Pressure);
        }
        Pad(e.ScreenPadLeft,16,56,19,17);Pad(e.ScreenPadRight,20,58,20,18);
        if((e.ScreenButtons&1)!=0)buttons|=1UL<<13; // Steam
        if((e.ScreenButtons&2)!=0)buttons|=1UL<<50; // Quick Access
        if((e.ScreenButtons&4)!=0)buttons|=1UL<<41; // L4
        if((e.ScreenButtons&8)!=0)buttons|=1UL<<15; // L5
        if((e.ScreenButtons&16)!=0)buttons|=1UL<<42; // R4
        if((e.ScreenButtons&32)!=0)buttons|=1UL<<16; // R5
        BinaryPrimitives.WriteUInt64LittleEndian(data.AsSpan(8),buttons);
        Write16(data,24,fields.AccelX);Write16(data,26,fields.AccelY);Write16(data,28,fields.AccelZ);
        Write16(data,30,fields.GyroPitch);Write16(data,32,fields.GyroYaw);Write16(data,34,fields.GyroRoll);
        // HC SteamDeckTarget ImuOffset+12..18: the existing raw report path
        // carries the admitted w/x/y/z quaternion without a new SDK API.
        if(e.ImuAdmitted && e.ImuQuaternionPresent) {
            Write16(data,36,QuantizeMotionAxis(e.ImuQuatW,short.MaxValue));
            Write16(data,38,QuantizeMotionAxis(e.ImuQuatX,short.MaxValue));
            Write16(data,40,QuantizeMotionAxis(e.ImuQuatY,short.MaxValue));
            Write16(data,42,QuantizeMotionAxis(e.ImuQuatZ,short.MaxValue));
        }
        // SDK 2.0 WireArith.Linear quantizes the clamped unit value in
        // double precision before adding the signed minimum. Rounding a
        // centered float product differs by one at half-step boundaries.
        static short Stick(float v,bool y) {
            double unit=float.IsNaN(v)?0.5:Math.Clamp((double)v,0.0,1.0);
            if(y)unit=1.0-unit;
            return (short)(-32767+(int)Math.Round(unit*65534.0));
        }
        static ushort Trigger(float v)=>(ushort)Math.Round(
            (float.IsNaN(v)?0.0:Math.Clamp((double)v,0.0,1.0))*32767.0);
        Write16(data,48,Stick(fields.X,false));Write16(data,50,Stick(fields.Y,true));
        Write16(data,52,Stick(fields.Rx,false));Write16(data,54,Stick(fields.Ry,true));
        BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(44),Trigger(fields.Z));
        BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(46),Trigger(fields.Rz));
        return data;
    }
    // Sony hid-playstation common data: points at 32/36, click buttons[2] bit 1.
    // Current Edge raw-report path; never a mouse replacement.
    private const int DualSenseTouchWidth=1920,DualSenseTouchHeight=1080;
    private static void WriteDualSenseScreenPads(byte[] data,CommandEnvelope e)
    {
        WriteDualSenseContact(data,32,e.ScreenPadsPresent?e.ScreenPadLeft:default,1);
        WriteDualSenseContact(data,36,e.ScreenPadsPresent?e.ScreenPadRight:default,2);
        if(e.ScreenPadsPresent && (e.ScreenPadLeft.Click || e.ScreenPadRight.Click))data[9]|=0x02;
    }
    private static void WriteDualSenseContact(byte[] data,int offset,ScreenPad p,byte id) =>
        WriteSonyContact(data,offset,p,id,DualSenseTouchHeight);
    private static void WriteSonyContact(byte[] data,int offset,ScreenPad p,byte id,int height)
    {
        data[offset]=(byte)(id|(p.Active?0:0x80));
        if(!p.Active){data[offset+1]=data[offset+2]=data[offset+3]=0;return;}
        int x=(int)Math.Round((Math.Clamp((int)p.X,-32767,32767)+32767.0)*(DualSenseTouchWidth-1)/65534.0);
        int y=(int)Math.Round((32767.0-Math.Clamp((int)p.Y,-32767,32767))*(height-1)/65534.0);
        data[offset+1]=(byte)x;data[offset+2]=(byte)(((x>>8)&0x0F)|((y&0x0F)<<4));data[offset+3]=(byte)(y>>4);
    }
    private static (int X,int Y) ReadDualSenseContact(byte[] data,int offset) =>
        (data[offset+1]|((data[offset+2]&15)<<8),(data[offset+2]>>4)|(data[offset+3]<<4));
    private static bool RunEdgeScreenPadSelfTest()
    {
        using var context=new SdkDataOnlyProfiles();context.LoadDefaultProfiles();int count=0;
        foreach(var persona in new[]{"dualsense-edge"})
        {
            var profile=context.GetProfile(persona);if(profile is null)return false;
            var e=new CommandEnvelope("SUBMIT_FRAME","selftest",1,"selftest",1,1,"target","owner",
                persona,persona,"selftest-nonce",1,LockedCoreSha256){Buttons=0x0400|0x0100,LtRaw=.5f,RtRaw=1f,ImuAdmitted=true,
                ImuGyroX=100,ImuGyroY=200,ImuGyroZ=300,ImuAccelX=1,ImuAccelY=2,ImuAccelZ=3};
            var baseline=BuildDualSenseDataOnlyReport(profile,e,5,0xC0);var random=new Random(20261007);
            for(int i=0;i<512;i++)
            {
                var left=new ScreenPad(true,i%3==0,(short)random.Next(-32768,32768),(short)random.Next(-32768,32768),8192);
                var right=new ScreenPad(i%2==0,i%2==0&&i%5==0,(short)random.Next(-32768,32768),(short)random.Next(-32768,32768),8192);
                var wire=BuildDualSenseDataOnlyReport(profile,e with{ScreenPadsPresent=true,ScreenPadLeft=left,ScreenPadRight=right},5,0xC0);
                if(wire.Length!=63 || wire[32]!=1 || wire[36]!=(right.Active?2:0x82) || (wire[9]&2)!=((left.Click||right.Click)?2:0))return false;
                for(int n=0;n<63;n++)if(n!=9 && (n<32||n>39) && wire[n]!=baseline[n])return false;
                if((wire[9]&~2)!=(baseline[9]&~2))return false;
                foreach(var (pad,offset) in new[]{(left,32),(right,36)})
                {
                    var (x,y)=ReadDualSenseContact(wire,offset);if(x<0||x>=1920||y<0||y>=1080)return false;
                    if(pad.Active && (Math.Abs(x-(Math.Clamp((int)pad.X,-32767,32767)+32767.0)*1919/65534)>0.51 ||
                        Math.Abs(y-(32767.0-Math.Clamp((int)pad.Y,-32767,32767))*1079/65534)>0.51))return false;
                }
                count++;
            }
            var corners=BuildDualSenseDataOnlyReport(profile,e with{ScreenPadsPresent=true,
                ScreenPadLeft=new(true,false,-32767,32767,8192),ScreenPadRight=new(true,true,32767,-32767,32767)},6);
            if(ReadDualSenseContact(corners,32)!=(0,0) || ReadDualSenseContact(corners,36)!=(1919,1079) || (corners[9]&2)==0)return false;
            if(baseline[32]!=0x81 || baseline[36]!=0x82 || (baseline[9]&2)!=0)return false;
        }
        Console.WriteLine($"dualsense-screenpad: OK {count} frames; packed coordinates, two contacts on one surface, click/release, buttons/IMU/Edge paddles unchanged; no hardware touched");return true;
    }
    private static bool RunNativeDualSenseFixtureTest(JsonElement root,SdkDataOnlyProfiles context)
    {
        var messages=root.GetProperty("dualSenseMessages").EnumerateArray().Select(v=>v.GetString()!).ToArray();
        if(messages.Length!=3)return false;
        for(int n=0;n<messages.Length;n++)
        {
            if(!TryParseEnvelope(messages[n],out var e,out var error)||e is null){Console.WriteLine("native-ds-screenpad: parser FAIL "+error);return false;}
            var persona="dualsense-edge";var i=n;
            if(e.Persona!=persona || e.ScreenPadsPresent!=(i<2))return false;
            var profile=context.GetProfile(persona);if(profile is null)return false;
            var wire=BuildDualSenseDataOnlyReport(profile,e,n+1,MapDsEdgePaddles(e.BackButtons));
            if(wire[32]!=(i==0?1:0x81) || wire[36]!=(i<2?2:0x82) || (wire[9]&2)!=(i==0?2:0))return false;
            if(i==0 && ReadDualSenseContact(wire,32)!=(598,153))return false;
            if(i<2 && ReadDualSenseContact(wire,36)!=(1646,743))return false;
        }
        Console.WriteLine("native-ds-screenpad: OK actual native assembler -> pipe -> strict Host parser -> Edge HID coordinates/click -> release (3 frames)");return true;
    }
    private static bool RunNativePsDualFixtureTest(JsonElement root,SdkDataOnlyProfiles context)
    {
        var messages=root.GetProperty("psDualMessages").EnumerateArray().Select(v=>v.GetString()!).ToArray();
        if(messages.Length!=3)return false;
        for(int n=0;n<messages.Length;n++) {
            if(!TryParseEnvelope(messages[n],out var e,out var error)||e is null){Console.WriteLine("ps-dual parser FAIL "+error);return false;}
            if(e.Persona!="dualsense-edge")return false;
            var phase=n%3;
            var profile=context.GetProfile("dualsense-edge");if(profile is null)return false;
            var wire=BuildDualSenseDataOnlyReport(profile,e,n+1);
            const int lo=32,ro=36,button=9;
            if(wire[lo]!=(phase<2?1:0x81)||wire[ro]!=(phase<2?2:0x82)||(wire[button]&2)!=(phase==1?2:0))return false;
            if((wire[button]&1)!=(phase<2?1:0))return false;
            if(phase<2) {
                var (lx,ly)=ReadDualSenseContact(wire,lo);var (rx,ry)=ReadDualSenseContact(wire,ro);
                if(lx>959||rx<960||rx>1919||ly<0||ry>=1080)return false;
                if(e.ScreenPadLeft.X>=0||e.ScreenPadRight.X<=0)return false;
            }
        }
        Console.WriteLine("native-PS-dual: OK 3 native half-map -> BUS -> pipe -> strict Host parser -> Edge native HID frames; guide/shared click/release, no hardware touched");return true;
    }
    private static bool RunNativeScreenPadFixtureTest(string path)
    {
        using var doc=JsonDocument.Parse(File.ReadAllText(path));
        if(!doc.RootElement.GetProperty("ok").GetBoolean()) return false;
        var messages=doc.RootElement.GetProperty("messages").EnumerateArray().Select(v=>v.GetString()!).ToArray();
        if(messages.Length!=3) return false;
        using var context=new SdkDataOnlyProfiles();context.LoadDefaultProfiles();
        var profile=context.GetProfile("steam-deck-composite");if(profile is null) return false;
        for(int i=0;i<3;i++)
        {
            if(!TryParseEnvelope(messages[i],out var e,out var error) || e is null)
            {Console.WriteLine($"native-screenpads: FAIL parser {i} {error}");return false;}
            if(e.ScreenPadsPresent!=(i<2) || e.ScreenPadLeft.Active!=(i==0) || e.ScreenPadRight.Active!=(i<2)) return false;
            if(i==0 && e.ScreenPadLeft!=new ScreenPad(true,true,-12345,23456,32767)) return false;
            if(i<2 && e.ScreenPadRight!=new ScreenPad(true,false,23456,-12345,8192)) return false;
            var wire=BuildSteamDeckScreenPadReport(profile,e,(uint)i+1);
            var buttons=BinaryPrimitives.ReadUInt64LittleEndian(wire.AsSpan(8));
            if(((buttons>>19)&1)!=(i==0?1UL:0UL) || ((buttons>>20)&1)!=(i<2?1UL:0UL)) return false;
        }
        var bad=messages[0].Replace("\"x\":-12345","\"x\":\"bad\"");
        if(TryParseEnvelope(bad,out _,out _)) return false;
        Console.WriteLine("native-screenpads: OK actual native assembler -> optimized pipe JSON -> production Host parser -> independent HID contacts -> release (3 frames)");
        return RunNativeDualSenseFixtureTest(doc.RootElement,context) && RunNativePsDualFixtureTest(doc.RootElement,context) && RunNativeScreenButtonFixtureTest(doc.RootElement,context);
    }
    private static bool RunSteamDeckScreenPadSelfTest()
    {
        using var context=new SdkDataOnlyProfiles();context.LoadDefaultProfiles();
        var profile=context.GetProfile("steam-deck-composite");if(profile is null) return false;
        var e=new CommandEnvelope("SUBMIT_FRAME","selftest",1,"selftest",1,1,"target","owner",
            "steamdeck","steam-deck-composite","selftest-nonce",1,LockedCoreSha256);
        // Read-only stock codec oracle: never instantiate a controller or touch any driver.
        var assembly=typeof(HMProfile).Assembly;
        var inner=typeof(HMProfile).GetFields(BindingFlags.Instance|BindingFlags.NonPublic)
            .Single(f=>f.FieldType.FullName=="HIDMaestro.Internal.ControllerProfile").GetValue(profile)!;
        var spec=inner.GetType().GetProperty("ExtendedReport")!.GetValue(inner)!;
        var codec=assembly.GetType("HIDMaestro.Internal.VendorBlobCodec")!;
        var encoder=Activator.CreateInstance(assembly.GetType("HIDMaestro.Internal.VendorBlobCodec+EncoderState")!,true)!;
        var encode=codec.GetMethod("EncodeInput",BindingFlags.Static|BindingFlags.Public|BindingFlags.NonPublic)!;
        var random=new Random(20261007);var golden=new byte[64];
        for(uint i=1;i<=512;i++)
        {
            float Axis()=>(float)(random.NextDouble()*2-1);
            var test=e with {Buttons=random.Next(65536),BackButtons=random.Next(4),Lx=Axis(),Ly=Axis(),Rx=Axis(),Ry=Axis(),
                LtRaw=(float)random.NextDouble(),RtRaw=(float)random.NextDouble(),ImuAdmitted=(i&1)!=0,ImuQuaternionPresent=(i&2)!=0,
                ImuAccelX=Axis(),ImuAccelY=Axis(),ImuAccelZ=Axis(),ImuGyroX=Axis()*100,ImuGyroY=Axis()*100,ImuGyroZ=Axis()*100};
            var st=BuildSteamDeckState(profile,test);var stAxes=st.Axes!;Array.Clear(golden);
            encode.Invoke(null,new object[]{spec,st,stAxes[HMAxis.X],stAxes[HMAxis.Y],stAxes[HMAxis.Rx],stAxes[HMAxis.Ry],
                stAxes[HMAxis.Z],stAxes[HMAxis.Rz],golden,encoder});
            // The SDK state has no quaternion properties. Preserve its full
            // 64-byte baseline and apply the independently specified raw-only delta.
            static short Q(double v) => (short)Math.Clamp(Math.Truncate(v*32767.0),-32768.0,32767.0);
            var q=test.ImuAdmitted && test.ImuQuaternionPresent?new[]{test.ImuQuatW,test.ImuQuatX,test.ImuQuatY,test.ImuQuatZ}:new double[4];
            for(int k=0;k<4;k++)BinaryPrimitives.WriteInt16LittleEndian(golden.AsSpan(36+k*2),Q(q[k]));
            var actual=BuildSteamDeckScreenPadReport(profile,test,i);
            if(!golden.AsSpan().SequenceEqual(actual))
            {
                Console.WriteLine($"screenpads: FAIL stock-codec-parity frame={i} expected={Convert.ToHexString(golden)} actual={Convert.ToHexString(actual)}");
                return false;
            }
        }
        var contacts=e with {ScreenPadsPresent=true,ScreenPadLeft=new(true,true,-32767,32767,32767),
            ScreenPadRight=new(true,true,32767,-32767,8192)};
        var bytes=BuildSteamDeckScreenPadReport(profile,contacts,513);
        var mask=BinaryPrimitives.ReadUInt64LittleEndian(bytes.AsSpan(8));
        if((mask & (15UL<<17))!=(15UL<<17) || BinaryPrimitives.ReadInt16LittleEndian(bytes.AsSpan(16))!=-32767 ||
            BinaryPrimitives.ReadInt16LittleEndian(bytes.AsSpan(22))!=-32767 ||
            BinaryPrimitives.ReadUInt16LittleEndian(bytes.AsSpan(56))!=32767 ||
            BinaryPrimitives.ReadUInt16LittleEndian(bytes.AsSpan(58))!=8192) return false;
        bytes=BuildSteamDeckScreenPadReport(profile,e,514);
        if(BinaryPrimitives.ReadUInt64LittleEndian(bytes.AsSpan(8))!=0 || bytes.AsSpan(16,8).ContainsAnyExcept((byte)0) ||
            bytes.AsSpan(56,4).ContainsAnyExcept((byte)0)) return false;
        using var good=JsonDocument.Parse("{\"left\":{\"active\":true,\"click\":false,\"x\":-32768,\"y\":32767,\"pressure\":8192},\"right\":{\"active\":false,\"click\":false,\"x\":0,\"y\":0,\"pressure\":0}}");
        if(!TryReadScreenPads(good.RootElement,out _,out _)) return false;
        foreach(var bad in new[]{"{}","[]",good.RootElement.GetRawText().Replace("-32768","-32769"),
            good.RootElement.GetRawText().Replace("-32768","\"bad\""),
            good.RootElement.GetRawText().Replace("\"x\":0","\"x\":1"),
            good.RootElement.GetRawText().Replace("\"pressure\":8192","\"pressure\":32768")})
        {using var doc=JsonDocument.Parse(bad);if(TryReadScreenPads(doc.RootElement,out _,out _)) return false;}
        // No SDK constructor or device clock is started in this report test.
        // The separate clock-barrier fixture exercises live BCL-thread joins,
        // timeouts and self-join; this case covers the partial no-writer handoff
        // and reads the counter only after the SAME production barrier returns.
        var fake=(HMController)System.Runtime.CompilerServices.RuntimeHelpers.GetUninitializedObject(typeof(HMController));
        var idleBuffer=new byte[64];BinaryPrimitives.WriteUInt32LittleEndian(idleBuffer.AsSpan(4),12345);
        ScreenPadLastEncoded!.SetValue(fake,idleBuffer);
        if(TakeScreenPadReportOwnership(fake,true)!=12345)return false;
        Console.WriteLine("screenpads: OK 512 stock-codec parity frames + independent coordinates/click/pressure + release + strict parser + no-writer counter continuity; clock barrier has separate actual-consumer fixture; no hardware touched");
        return true;
    }
}

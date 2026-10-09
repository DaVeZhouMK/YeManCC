using System.Buffers.Binary;
using System.Reflection;
using System.Text.Json;
using HIDMaestro;

internal static partial class Program
{
    private readonly record struct ScreenPad(bool Active, bool Click, short X, short Y, ushort Pressure);
    // Single Host submitter; reuse the data-only report instead of allocating 250 arrays/s.
    private static readonly byte[] ScreenPadReportBuffer = new byte[64];
    private static uint _screenPadPacketCounter;
    private static bool _screenPadOwnsReport;
    // Locked SDK 1.7.0's SubmitRawReport does NOT refresh its idle-pump state.
    // Without a one-time ownership handoff it republishes stale neutral frames
    // every 4ms over live touches. Pin this seam to the already SHA-locked DLL;
    // no SDK replacement, new timer or additional producer is introduced.
    private static readonly FieldInfo? ScreenPadIdleCts = typeof(HMController).GetField("_idleCts",BindingFlags.Instance|BindingFlags.NonPublic);
    private static readonly FieldInfo? ScreenPadIdleThread = typeof(HMController).GetField("_idleThread",BindingFlags.Instance|BindingFlags.NonPublic);
    private static readonly FieldInfo? ScreenPadLastEncoded = typeof(HMController).GetField("_extendedReportBuffer",BindingFlags.Instance|BindingFlags.NonPublic);
    private static uint TakeScreenPadReportOwnership(HMController controller)
    {
        if(ScreenPadIdleCts?.GetValue(controller) is not CancellationTokenSource cts ||
           ScreenPadIdleThread is null || ScreenPadLastEncoded?.GetValue(controller) is not byte[] prior || prior.Length!=64)
            throw new InvalidOperationException("locked-steamdeck-idle-contract-missing");
        cts.Cancel();
        if(ScreenPadIdleThread.GetValue(controller) is Thread thread && thread.IsAlive && !thread.Join(100))
            throw new InvalidOperationException("steamdeck-idle-handoff-timeout");
        return BinaryPrimitives.ReadUInt32LittleEndian(prior.AsSpan(4));
    }
    private static void SubmitSteamDeckWithScreenPads(HMController controller,HMProfile profile,CommandEnvelope envelope)
    {
        if((envelope.ScreenPadsPresent || envelope.ScreenButtons!=0) && !_screenPadOwnsReport)
        {
            _screenPadPacketCounter=TakeScreenPadReportOwnership(controller);
            _screenPadOwnsReport=true;
        }
        if(_screenPadOwnsReport)
            controller.SubmitRawReport(BuildSteamDeckScreenPadReport(profile,envelope,unchecked(++_screenPadPacketCounter),ScreenPadReportBuffer));
        else controller.SubmitState(BuildSteamDeckState(profile,envelope));
    }
    private static readonly (HMButton Button, int Bit)[] DeckButtonBits =
    {
        (HMButton.RightBumper,2),(HMButton.LeftBumper,3),(HMButton.Y,4),
        (HMButton.Circle,5),(HMButton.Square,6),(HMButton.Cross,7),
        (HMButton.Back,12),(HMButton.Guide,13),(HMButton.Start,14),
        (HMButton.LeftPaddle,15),(HMButton.RightPaddle,16),
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
        var state=BuildSteamDeckState(profile,e);
        var axes=state.Axes ?? throw new InvalidOperationException("steam-deck-axes-missing");
        ulong buttons=0;
        foreach(var map in DeckButtonBits) if((state.Buttons & map.Button)!=0) buttons|=1UL<<map.Bit;
        if(axes[HMAxis.Rz]>0) buttons|=1;
        if(axes[HMAxis.Z]>0) buttons|=2;
        if(state.Hat is HMHat.North or HMHat.NorthEast or HMHat.NorthWest) buttons|=1UL<<8;
        if(state.Hat is HMHat.East or HMHat.NorthEast or HMHat.SouthEast) buttons|=1UL<<9;
        if(state.Hat is HMHat.West or HMHat.NorthWest or HMHat.SouthWest) buttons|=1UL<<10;
        if(state.Hat is HMHat.South or HMHat.SouthEast or HMHat.SouthWest) buttons|=1UL<<11;
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
        Write16(data,24,state.AccelX);Write16(data,26,state.AccelY);Write16(data,28,state.AccelZ);
        Write16(data,30,state.GyroPitch);Write16(data,32,state.GyroYaw);Write16(data,34,state.GyroRoll);
        static short Stick(float v,bool y)=> (short)Math.Clamp((int)Math.Round(
            (Math.Clamp(v,0f,1f)-0.5f)*(y?-1f:1f)*2f*32767f),-32767,32767);
        Write16(data,48,Stick(axes[HMAxis.X],false));Write16(data,50,Stick(axes[HMAxis.Y],true));
        Write16(data,52,Stick(axes[HMAxis.Rx],false));Write16(data,54,Stick(axes[HMAxis.Ry],true));
        BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(44),(ushort)Math.Round(Math.Clamp(e.LtRaw,0f,1f)*32767f));
        BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(46),(ushort)Math.Round(Math.Clamp(e.RtRaw,0f,1f)*32767f));
        return data;
    }
    // Sony hid-playstation common data: points at 32/36, click buttons[2] bit 1.
    // Existing DualSense/Edge raw-report path; never a mouse replacement.
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
    // Sony-authored hid-playstation: DS4 has a 1920x942 surface and a USB
    // touch packet count/timestamp before two contact records. Keep two IDs
    // even for a fully released packet so consumers cannot retain stale touches.
    private const int Ds4TouchHeight=942;
    private static void WriteDs4ScreenPads(byte[] data,CommandEnvelope e,long counter)
    {
        data[32]=1;data[33]=(byte)counter;
        WriteSonyContact(data,34,e.ScreenPadsPresent?e.ScreenPadLeft:default,1,Ds4TouchHeight);
        WriteSonyContact(data,38,e.ScreenPadsPresent?e.ScreenPadRight:default,2,Ds4TouchHeight);
        if(e.ScreenPadsPresent && (e.ScreenPadLeft.Click || e.ScreenPadRight.Click))data[6]|=0x02;
    }
    private static bool RunDs4ScreenPadSelfTest()
    {
        using var context=new HMContext();context.LoadDefaultProfiles();var profile=context.GetProfile("dualshock-4-v2");
        if(profile is null)return false;
        var e=new CommandEnvelope("SUBMIT_FRAME","ds4pad",1,"selftest",1,1,"target","owner",
            "dualshock4","dualshock-4-v2","nonce",1,LockedCoreSha256){Buttons=0x0100,LtRaw=.25f,ImuAdmitted=true,
            ImuGyroX=123,ImuAccelZ=1,ScreenButtons=1};
        var baseline=BuildDs4V2DataOnlyReport(profile,e,5);var random=new Random(20261008);
        for(int n=0;n<512;n++) {
            var left=new ScreenPad(true,n%3==0,(short)random.Next(-32767,-16),(short)random.Next(-32767,32768),8192);
            var right=new ScreenPad(n%2==0,n%2==0 && n%5==0,(short)random.Next(17,32768),(short)random.Next(-32767,32768),8192);
            var wire=BuildDs4V2DataOnlyReport(profile,e with{ScreenPadsPresent=true,ScreenPadLeft=left,ScreenPadRight=right},5);
            if(wire[32]!=1 || wire[33]!=5 || wire[34]!=1 || wire[38]!=(right.Active?2:0x82) || (wire[6]&2)!=((left.Click||right.Click)?2:0))return false;
            for(int i=0;i<63;i++)if(i!=6 && (i<34||i>41) && wire[i]!=baseline[i])return false;
            if((wire[6]&~2)!=(baseline[6]&~2))return false;
            var (lx,ly)=ReadDualSenseContact(wire,34);var (rx,ry)=ReadDualSenseContact(wire,38);
            if(lx<0||lx>959||ly<0||ly>=942||right.Active&&(rx<960||rx>=1920||ry<0||ry>=942))return false;
        }
        var released=BuildDs4V2DataOnlyReport(profile,e,6);
        if(released[32]!=1||released[33]!=6||released[34]!=0x81||released[38]!=0x82||(released[6]&3)!=1)return false;
        Console.WriteLine("ds4-screenpad: OK 512 frames; two half-surface contacts, packed 1920x942 coordinates, shared click, release, PS/IMU/frame counter unchanged; no hardware touched");return true;
    }
    private static (int X,int Y) ReadDualSenseContact(byte[] data,int offset) =>
        (data[offset+1]|((data[offset+2]&15)<<8),(data[offset+2]>>4)|(data[offset+3]<<4));
    private static bool RunDualSenseScreenPadSelfTest()
    {
        using var context=new HMContext();context.LoadDefaultProfiles();int count=0;
        foreach(var persona in new[]{"dualsense","dualsense-edge"})
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
    private static bool RunNativeDualSenseFixtureTest(JsonElement root,HMContext context)
    {
        var messages=root.GetProperty("dualSenseMessages").EnumerateArray().Select(v=>v.GetString()!).ToArray();
        if(messages.Length!=6)return false;
        for(int n=0;n<messages.Length;n++)
        {
            if(!TryParseEnvelope(messages[n],out var e,out var error)||e is null){Console.WriteLine("native-ds-screenpad: parser FAIL "+error);return false;}
            var persona=n<3?"dualsense":"dualsense-edge";var i=n%3;
            if(e.Persona!=persona || e.ScreenPadsPresent!=(i<2))return false;
            var profile=context.GetProfile(persona);if(profile is null)return false;
            var wire=BuildDualSenseDataOnlyReport(profile,e,n+1,MapDsEdgePaddles(e.BackButtons));
            if(wire[32]!=(i==0?1:0x81) || wire[36]!=(i<2?2:0x82) || (wire[9]&2)!=(i==0?2:0))return false;
            if(i==0 && ReadDualSenseContact(wire,32)!=(598,153))return false;
            if(i<2 && ReadDualSenseContact(wire,36)!=(1646,743))return false;
        }
        Console.WriteLine("native-ds-screenpad: OK actual native assembler -> pipe -> strict Host parser -> DualSense/Edge HID coordinates/click -> release (6 frames)");return true;
    }
    private static bool RunNativePsDualFixtureTest(JsonElement root,HMContext context)
    {
        var messages=root.GetProperty("psDualMessages").EnumerateArray().Select(v=>v.GetString()!).ToArray();
        if(messages.Length!=9)return false;
        for(int n=0;n<9;n++) {
            if(!TryParseEnvelope(messages[n],out var e,out var error)||e is null){Console.WriteLine("ps-dual parser FAIL "+error);return false;}
            var phase=n%3;var isDs4=e.Persona=="dualshock4";
            var profile=context.GetProfile(isDs4?"dualshock-4-v2":e.Persona);if(profile is null)return false;
            var wire=isDs4?BuildDs4V2DataOnlyReport(profile,e,n+1):BuildDualSenseDataOnlyReport(profile,e,n+1);
            var lo=isDs4?34:32;var ro=isDs4?38:36;var button=isDs4?6:9;
            if(wire[lo]!=(phase<2?1:0x81)||wire[ro]!=(phase<2?2:0x82)||(wire[button]&2)!=(phase==1?2:0))return false;
            if((wire[button]&1)!=(phase<2?1:0))return false;
            if(phase<2) {
                var (lx,ly)=ReadDualSenseContact(wire,lo);var (rx,ry)=ReadDualSenseContact(wire,ro);
                if(lx>959||rx<960||rx>1919||ly<0||ry>=(isDs4?942:1080))return false;
                if(e.ScreenPadLeft.X>=0||e.ScreenPadRight.X<=0)return false;
            }
        }
        Console.WriteLine("native-PS-dual: OK 9 native half-map -> BUS -> pipe -> strict Host parser -> PS5/Edge/PS4 native HID frames; guide/shared click/release, no hardware touched");return true;
    }
    private static bool RunNativeScreenPadFixtureTest(string path)
    {
        using var doc=JsonDocument.Parse(File.ReadAllText(path));
        if(!doc.RootElement.GetProperty("ok").GetBoolean()) return false;
        var messages=doc.RootElement.GetProperty("messages").EnumerateArray().Select(v=>v.GetString()!).ToArray();
        if(messages.Length!=3) return false;
        using var context=new HMContext();context.LoadDefaultProfiles();
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
        using var context=new HMContext();context.LoadDefaultProfiles();
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
                LtRaw=(float)random.NextDouble(),RtRaw=(float)random.NextDouble(),ImuAdmitted=(i&1)!=0,
                ImuAccelX=Axis(),ImuAccelY=Axis(),ImuAccelZ=Axis(),ImuGyroX=Axis()*100,ImuGyroY=Axis()*100,ImuGyroZ=Axis()*100};
            var st=BuildSteamDeckState(profile,test);var stAxes=st.Axes!;Array.Clear(golden);
            encode.Invoke(null,new object[]{spec,st,stAxes[HMAxis.X],stAxes[HMAxis.Y],stAxes[HMAxis.Rx],stAxes[HMAxis.Ry],
                stAxes[HMAxis.Z],stAxes[HMAxis.Rz],golden,encoder});
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
        // A real BCL thread models the stock idle producer without constructing
        // any HIDMaestro device. Verify cancellation, join, and counter continuity.
        var fake=(HMController)System.Runtime.CompilerServices.RuntimeHelpers.GetUninitializedObject(typeof(HMController));
        using var idleCts=new CancellationTokenSource();var idleBuffer=new byte[64];
        BinaryPrimitives.WriteUInt32LittleEndian(idleBuffer.AsSpan(4),12345);
        int writes=0;var idleThread=new Thread(()=> {while(!idleCts.Token.WaitHandle.WaitOne(1)) Interlocked.Increment(ref writes);});
        ScreenPadIdleCts!.SetValue(fake,idleCts);ScreenPadIdleThread!.SetValue(fake,idleThread);ScreenPadLastEncoded!.SetValue(fake,idleBuffer);
        idleThread.Start();
        if(TakeScreenPadReportOwnership(fake)!=12345 || idleThread.IsAlive) return false;
        int stable=Volatile.Read(ref writes);Thread.Sleep(12);if(Volatile.Read(ref writes)!=stable) return false;
        Console.WriteLine("screenpads: OK 512 stock-codec parity frames + independent coordinates/click/pressure + release + strict parser + idle-writer handoff/counter continuity; no hardware touched");
        return true;
    }
}

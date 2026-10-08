using System.Buffers.Binary;
using System.Text.Json;
using HIDMaestro;
internal static partial class Program
{
    // Bits: Guide, QuickAccess/Mute, L4/LFn, L5/LB, R4/RFn, R5/RB.
    private static bool TryReadScreenButtons(JsonElement value,string persona,out int bits)
    {
        bits=0;
        if(value.ValueKind!=JsonValueKind.Number || !value.TryGetInt32(out var n) || n<0)return false;
        var allowed=persona switch {"steamdeck" or "dualsense-edge"=>63,"dualsense"=>3,"elite" or "xbox360" or "dualshock4"=>1,_=>-1};
        if(allowed<0 || (n&~allowed)!=0)return false;
        bits=n;return true;
    }
    private static bool ScreenButtonTestFailure(string name){Console.Error.WriteLine("screen-buttons failed: "+name);return false;}
    private static bool RunScreenButtonsSelfTest()
    {
        using var context=new HMContext();context.LoadDefaultProfiles();int cases=0;
        var deck=context.GetProfile("steam-deck-composite");var ps=context.GetProfile("dualsense-edge");var xbox=context.GetProfile("xbox-360-wired");
        if(deck is null || ps is null || xbox is null)return ScreenButtonTestFailure("check-line-19");
        CommandEnvelope E(string persona,int bits)=>new("SUBMIT_FRAME","buttons",1,"buttons",1,1,"target","owner",persona,persona,"nonce",1,new string('a',64)) {ScreenButtons=bits};
        var positions=new[]{13,50,41,15,42,16};var dsBits=new[]{1,4,16,64,32,128};
        for(int i=0;i<6;i++) {
            var d=BuildSteamDeckScreenPadReport(deck,E("steamdeck",1<<i),1);
            if(BinaryPrimitives.ReadUInt64LittleEndian(d.AsSpan(8))!=(1UL<<positions[i]))return ScreenButtonTestFailure("check-line-24");
            var p=BuildDualSenseDataOnlyReport(ps,E("dualsense-edge",1<<i),1);
            if(p[9]!=dsBits[i])return ScreenButtonTestFailure("check-line-26");
            var released=BuildDualSenseDataOnlyReport(ps,E("dualsense-edge",0),1);
            if(released[9]!=0)return ScreenButtonTestFailure("check-line-28");
            cases+=3;
        }
        for(int mask=0;mask<64;mask++) {
            ulong expected=0;int ds=0;
            for(int bit=0;bit<6;bit++)if((mask&(1<<bit))!=0){expected|=1UL<<positions[bit];ds|=dsBits[bit];}
            var e=E("steamdeck",mask) with {Buttons=0x1000,BackButtons=3,ScreenPadsPresent=true,ScreenPadLeft=new(true,true,1234,2345,123),ScreenPadRight=new(true,false,2345,1234,456)};
            var d=BuildSteamDeckScreenPadReport(deck,e,7);
            expected|=(1UL<<7)|(1UL<<15)|(1UL<<16)|(1UL<<19)|(1UL<<20)|(1UL<<17);
            if(BinaryPrimitives.ReadUInt64LittleEndian(d.AsSpan(8))!=expected)return ScreenButtonTestFailure("check-line-37");
            var pe=E("dualsense-edge",mask) with {Buttons=0x1000,ScreenPadsPresent=true,ScreenPadLeft=new(true,true,1234,2345,123)};
            var p=BuildDualSenseDataOnlyReport(ps,pe,7,0x80);
            if(p[9]!=(ds|0x82) || (p[7]&0x20)==0)return ScreenButtonTestFailure("check-line-40");
            if(ReadDualSenseContact(p,32)!=ReadDualSenseContact(BuildDualSenseDataOnlyReport(ps,pe with {ScreenButtons=0},7),32))return ScreenButtonTestFailure("check-line-41");
            cases+=3;
        }
        if((BuildXbox360State(xbox,E("xbox360",1)).Buttons&HMButton.Guide)==0 || (BuildXbox360State(xbox,E("xbox360",0)).Buttons&HMButton.Guide)!=0)return ScreenButtonTestFailure("check-line-44");
        foreach(var persona in new[]{"steamdeck","dualsense-edge","dualsense","xbox360","elite","dualshock4"})foreach(var raw in new[]{"0","1","2","3","4","63","64","-1","1.5","true","\"1\""}) {
            using var doc=JsonDocument.Parse(raw);var valid=TryReadScreenButtons(doc.RootElement,persona,out _);
            var integer=int.TryParse(raw,out var n);var allowed=persona switch {"steamdeck" or "dualsense-edge"=>63,"dualsense"=>3,"xbox360" or "elite" or "dualshock4"=>1,_=>-1};
            if(valid!=(integer && allowed>=0 && n>=0 && (n&~allowed)==0))return ScreenButtonTestFailure("check-line-48");cases++;
        }
        Console.WriteLine($"screen-buttons: OK {cases+2} report/parser checks; Deck Steam/QAM/L4/L5/R4/R5, PS/Mute/Fn/Back, Xbox Guide, release and touch coexistence; no hardware touched");
        return true;
    }
}

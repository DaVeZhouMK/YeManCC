using System.Buffers.Binary;
using System.Text.Json;
using HIDMaestro;
internal static partial class Program
{
    private static bool RunNativeScreenButtonFixtureTest(JsonElement root,HMContext context)
    {
        if(!root.TryGetProperty("screenButtonMessages",out var fixtures))return false;
        int count=0;var deck=context.GetProfile("steam-deck-composite");var edge=context.GetProfile("dualsense-edge");var ps=context.GetProfile("dualsense");var xbox=context.GetProfile("xbox-elite-v2");
        if(deck is null || edge is null || ps is null || xbox is null)return false;
        var positions=new[]{13,50,41,15,42,16};var sony=new[]{1,4,16,64,32,128};
        foreach(var fixture in fixtures.EnumerateArray()) {
            var mask=fixture.GetProperty("mask").GetInt32();var message=fixture.GetProperty("message").GetString()!;
            if(!TryParseEnvelope(message,out var e,out var error) || e is null || e.ScreenButtons!=mask) {Console.Error.WriteLine("native-screen-buttons parser failed: "+error);return false;}
            if(e.Persona=="steamdeck") {
                var d=BuildSteamDeckScreenPadReport(deck,e,1);ulong expected=0;
                for(int i=0;i<6;i++)if((mask&(1<<i))!=0)expected|=1UL<<positions[i];
                if(e.ScreenPadsPresent)expected|=(1UL<<19)|(1UL<<17);
                if(BinaryPrimitives.ReadUInt64LittleEndian(d.AsSpan(8))!=expected)return false;
            } else if(e.Persona is "dualsense" or "dualsense-edge") {
                var d=BuildDualSenseDataOnlyReport(e.Persona=="dualsense"?ps:edge,e,1);int expected=0;
                for(int i=0;i<6;i++)if((mask&(1<<i))!=0)expected|=sony[i];
                if(e.ScreenPadsPresent)expected|=2;
                if(d[9]!=expected)return false;
            } else if(e.Persona=="elite") {
                if(((BuildXbox360State(xbox,e).Buttons&HMButton.Guide)!=0)!=(mask==1))return false;
            } else return false;
            if(mask==63 && TryParseEnvelope(message.Replace("\"screenButtons\":63","\"screenButtons\":64"),out _,out _))return false;
            count++;
        }
        Console.WriteLine($"native-screen-buttons: OK {count} native event -> BUS snapshot -> optimized pipe -> strict Host parser -> Deck/PS5/Xbox report fixtures; release and touch coexistence");
        return count==134;
    }
}

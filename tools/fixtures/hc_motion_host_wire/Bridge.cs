using System.Reflection;
using System.Buffers.Binary;
using System.Text.Json;
using HIDMaestro;
internal static partial class Program {
    // Inert SDK data models only, matching existing repo fixtures. Never create
    // HMContext, HMController, target, transport, service or device interface.
    private static readonly Dictionary<string,HMProfile> MotionWireProfiles=new();
    private static byte[] MotionWireDeckProfileBytes(){
        var assembly=typeof(HMProfile).Assembly;
        var name=assembly.GetManifestResourceNames().Single(n=>n.EndsWith("steam-deck-composite.json",StringComparison.Ordinal));
        using var stream=assembly.GetManifestResourceStream(name)!;using var bytes=new MemoryStream();stream.CopyTo(bytes);return bytes.ToArray();
    }
    private static HMProfile MotionWireProfile(string id) {
        if(MotionWireProfiles.TryGetValue(id,out var prior))return prior;
        var type=typeof(HMProfile).Assembly.GetType("HIDMaestro.Internal.ControllerProfile",true)!;
        object inner;
        if(id=="steam-deck-composite"){
            var optionsType=typeof(HMProfile).Assembly.GetType("HIDMaestro.Internal.HMLayoutJsonOptions",true)!;
            var options=(JsonSerializerOptions)optionsType.GetField("Default",BindingFlags.Public|BindingFlags.Static)!.GetValue(null)!;
            inner=JsonSerializer.Deserialize(MotionWireDeckProfileBytes(),type,options)!;
        }
        else {inner=Activator.CreateInstance(type)!;type.GetProperty("Name")!.SetValue(inner,id);type.GetProperty("Id")!.SetValue(inner,id);type.GetProperty("InputReportSize")!.SetValue(inner,64);}
        var profile=(HMProfile)typeof(HMProfile).GetConstructor(BindingFlags.NonPublic|BindingFlags.Instance,null,new[]{type},null)!.Invoke(new[]{inner});
        MotionWireProfiles[id]=profile;return profile;
    }
    internal static void MotionWireProfileReceipt(string output){
        var bytes=MotionWireDeckProfileBytes();using var json=JsonDocument.Parse(bytes);var root=json.RootElement;
        var defaults=root.TryGetProperty("inputDefaults",out var d)?d.GetRawText():"absent";
        File.WriteAllText(output,JsonSerializer.Serialize(new{resourceSha256=Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(bytes)).ToLowerInvariant(),resourceBytes=bytes.Length,id=root.GetProperty("id").GetString(),inputReportSize=root.GetProperty("inputReportSize").GetInt32(),inputDefaults=defaults,reportId=root.GetProperty("extendedReport").GetProperty("reportId").GetString(),contextCreated=false,controllerCreated=false}));
    }
    internal static bool MotionWireRawParity()=>RunSteamDeckScreenPadSelfTestInert();
    internal static bool MotionWireSonyParity()=>RunEdgeScreenPadSelfTestInert();
    private static bool MotionWireQuaternionPresent(object e)=>e.GetType().GetProperty("ImuQuaternionPresent")?.GetValue(e) is true;
    internal static (bool parsed,string error,double[] axes,double[] quaternion,short[] state,byte[] report,int back,bool quaternionPresent) MotionWire(string wire,long counter){
        if(!TryParseEnvelope(wire,out var e,out var error)||e is null)return (false,error,[],[],[],[],0,false);
        var p=MotionWireProfile(e.ProfileIdentity);byte[] report;short[] state=[];
        switch(e.Persona){
        case "steamdeck":
            var sdk=BuildSteamDeckState(p,e);
            state=[sdk.AccelX,sdk.AccelY,sdk.AccelZ,sdk.GyroPitch,sdk.GyroYaw,sdk.GyroRoll];
            report=BuildSteamDeckScreenPadReport(p,e,(uint)counter);break;
        case "dualsense-edge":report=BuildDualSenseDataOnlyReport(p,e,counter,MapDsEdgePaddles(e.BackButtons));break;
        default:throw new InvalidOperationException(e.Persona);
        }
        return (true,"",[e.ImuGyroX,e.ImuGyroY,e.ImuGyroZ,e.ImuAccelX,e.ImuAccelY,e.ImuAccelZ],[e.ImuQuatW,e.ImuQuatX,e.ImuQuatY,e.ImuQuatZ],state,report,e.BackButtons,MotionWireQuaternionPresent(e));
    }
}

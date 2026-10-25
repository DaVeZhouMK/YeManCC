using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
internal static class ParserFixture
{
    static int cases,checks,accepted,rejected;
    static readonly List<object> Observations=new();
    static void Check(bool value,string message) {checks++;if(!value)throw new InvalidOperationException(message);}
    static JsonObject Frame(string persona="steamdeck",string? profile=null) {
        var f=new JsonObject { ["protocolVersion"]=1,["command"]="SUBMIT_FRAME",["requestId"]="dead-source-cleanup",["sequence"]=2,["runId"]="offline",["epoch"]=1,["powerGeneration"]=1,["targetId"]="target",["ownerIdentity"]="owner",["persona"]=persona,["profileIdentity"]=profile??(persona switch {"steamdeck"=>"steam-deck-composite","elite"=>"xbox-elite-v2",_=>persona}),["startupNonce"]="nonce",["configRevision"]=1,["configHash"]=new string('a',64),["buttons"]=0,["lx"]=0,["ly"]=0,["rx"]=0,["ry"]=0,["lt"]=.25,["rt"]=.75,["sampleSequence"]=1,["monotonicTick"]=1234,["motionPairProofId"]="pair" };
        f["normalizedInputHash"]=Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes("0|00000000|00000000|00000000|00000000|3E800000|3F400000")));return f;
    }
    static JsonElement Observe(string name,string text,bool? expected=null) {
        var b=BaselineParser.Snapshot(text);var a=CandidateParser.Snapshot(text);cases++;
        Check(b==a,"before/after result differs: "+name);using var j=JsonDocument.Parse(a);var root=j.RootElement.Clone();bool ok=root.GetProperty("accepted").GetBoolean();if(ok)accepted++;else rejected++;
        if(expected.HasValue)Check(ok==expected,"acceptance changed from current contract: "+name+" "+a);
        Observations.Add(new {name,input=text,result=root});return root;
    }
    static int Main(string[] args) {
        foreach(var persona in new[]{"steamdeck","dualsense-edge","elite"}) {
            Observe(persona+"-neutral",Frame(persona).ToJsonString(),true);
            foreach(int mask in Enumerable.Range(0,16)) {
                var f=Frame(persona);var q=new JsonObject();string[] keys=["w","x","y","z"];double[] values=[.5,-.25,.75,-1];
                for(int k=0;k<4;k++)if((mask&(1<<k))!=0)q[keys[k]]=values[k];
                f["imu"]=new JsonObject {["admitted"]=true,["quaternion"]=q,["gyroDps"]=new JsonObject {["x"]=1,["y"]=-2,["z"]=3},["accelG"]=new JsonObject {["x"]=-4,["y"]=5,["z"]=-6}};
                var result=Observe(persona+"-quaternion-"+mask,f.ToJsonString(),true);var e=result.GetProperty("envelope");
                Check(e.GetProperty("ImuQuatW").GetDouble()==(mask==15?.5:1),"current complete-quaternion guard preserved");
            }
            foreach(int mask in Enumerable.Range(0,64)) {var f=Frame(persona);f["screenButtons"]=mask;Observe(persona+"-screen-buttons-"+mask,f.ToJsonString(),persona!="elite"||mask<=1);}
            foreach(var prop in new[]{"protocolVersion","command","sequence","persona","profileIdentity","normalizedInputHash","lx","ltRaw","imu","screenPads"})foreach(var token in new[]{"null","[]","{}","true","\"bad\"","-1","1e400"}) {
                var f=Frame(persona);f[prop]=JsonNode.Parse(token);Observe(persona+"-invalid-"+prop+"-"+token,f.ToJsonString());
            }
        }
        foreach(var persona in new[]{"xbox360","dualshock4","dualsense","unknown"})Observe("unsupported-"+persona,Frame(persona).ToJsonString(),false);
        Observe("wrong-profile",Frame("steamdeck","steamdeck").ToJsonString(),false);
        foreach(var text in new[]{"", "{", "null", "[]", "{}", "{\"protocolVersion\":1,\"protocolVersion\":2}",new string('x',16*1024+1)})Observe("malformed",text,false);
        Check(accepted>50 && rejected>50,"both acceptance paths exercised");
        bool sdkLoaded=AppDomain.CurrentDomain.GetAssemblies().Any(x=>x.GetName().Name?.StartsWith("HIDMaestro",StringComparison.Ordinal)==true);Check(!sdkLoaded,"SDK must not load");
        var resultObject=new {pass=true,cases,checks,accepted,rejected,sdkLoaded,productsDevicesRun=false};File.WriteAllText(args[0],JsonSerializer.Serialize(resultObject,new JsonSerializerOptions {WriteIndented=true}));File.WriteAllText(Path.ChangeExtension(args[0],"observations.json"),JsonSerializer.Serialize(Observations));Console.WriteLine(JsonSerializer.Serialize(resultObject));return 0;
    }
}

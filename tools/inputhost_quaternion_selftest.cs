using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
internal sealed record Observation(bool Accepted,string Error,double[]? Quaternion,double[]? Axes,bool Admitted,float LtRaw,float RtRaw);
internal static class QuaternionFixture {
 private static int checks,cases;
 private static void Check(bool v,string name){checks++;if(!v)throw new InvalidOperationException(name);}
 private static JsonObject Frame(string persona="steamdeck") {
  var f=new JsonObject{["protocolVersion"]=1,["command"]="SUBMIT_FRAME",["requestId"]="quat-regression",["sequence"]=2,["runId"]="offline",["epoch"]=1,["powerGeneration"]=1,["targetId"]="target",["ownerIdentity"]="owner",["persona"]=persona,["profileIdentity"]=persona,["startupNonce"]="nonce",["configRevision"]=1,["configHash"]=new string('a',64),["buttons"]=0,["lx"]=0,["ly"]=0,["rx"]=0,["ry"]=0,["lt"]=.25f,["rt"]=.75f,["sampleSequence"]=1,["monotonicTick"]=1234,["motionPairProofId"]="pair"};
  f["normalizedInputHash"]=Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes("0|00000000|00000000|00000000|00000000|3E800000|3F400000")));return f;
 }
 private static void ParseCase(JsonObject f,double[] expected,bool admitted=false,double[]? axes=null) {
  var r=ActualParser.Parse(f.ToJsonString());cases++;Check(r.Accepted,"valid frame accepted");Check(r.Quaternion is not null&&r.Quaternion.SequenceEqual(expected),"named w/x/y/z values preserved");Check(r.Admitted==admitted,"admission preserved");Check(r.Axes is not null&&r.Axes.SequenceEqual(axes??new double[6]),"gyro and accel preserved");Check(r.LtRaw==.25f&&r.RtRaw==.75f,"missing raw trigger fallback preserved");
 }
 private static int Main(string[] args) {
  try {
   foreach(var persona in new[]{"steamdeck","xbox360","elite","dualsense","dualsense-edge","dualshock4"})foreach(var mask in Enumerable.Range(0,16)) {
    var f=Frame(persona);var q=new JsonObject();var names=new[]{"w","x","y","z"};double[] values=[.5,-.25,.75,-1];double[] expected=[1,0,0,0];
    for(var i=0;i<4;i++)if((mask&(1<<i))!=0){q[names[i]]=values[i];expected[i]=values[i];}
    f["imu"]=new JsonObject{["admitted"]=true,["quaternion"]=q,["gyroDps"]=new JsonObject{["x"]=1,["y"]=-2,["z"]=3},["accelG"]=new JsonObject{["x"]=-4,["y"]=5,["z"]=-6}};
    ParseCase(f,expected,true,[1,-2,3,-4,5,-6]);
   }
   ParseCase(Frame(),[1,0,0,0]);
   foreach(var token in new[]{"null","[]","true","4","\"text\"","{}"}) {var f=Frame();f["imu"]=new JsonObject{["quaternion"]=JsonNode.Parse(token)};ParseCase(f,[1,0,0,0]);}
   foreach(var name in new[]{"w","x","y","z"})foreach(var token in new[]{"null","[]","false","\"bad\"","{}"}) {var f=Frame();var q=new JsonObject{[name]=JsonNode.Parse(token)};f["imu"]=new JsonObject{["quaternion"]=q};ParseCase(f,[1,0,0,0]);}
   foreach(var q in new[]{"{\"w\":1,\"x\":0,\"y\":0,\"z\":0}","{\"w\":0,\"x\":1,\"y\":0,\"z\":0}"}){var f=Frame();f["imu"]=new JsonObject{["quaternion"]=JsonNode.Parse(q)};ParseCase(f,q.Contains("\"w\":1")?[1,0,0,0]:[0,1,0,0]);}
   var invalid=Frame();invalid["ltRaw"]="bad";var rejected=ActualParser.Parse(invalid.ToJsonString());cases++;Check(!rejected.Accepted&&rejected.Error=="invalid-frame","R50 raw validation retained");
   var sdkLoaded=AppDomain.CurrentDomain.GetAssemblies().Any(a=>a.GetName().Name?.StartsWith("HIDMaestro",StringComparison.Ordinal)==true);Check(!sdkLoaded,"no vendor SDK loaded");
   var result=new{pass=true,cases,checks,fullActualParser=true,sdkLoaded};File.WriteAllText(args[0],JsonSerializer.Serialize(result,new JsonSerializerOptions{WriteIndented=true}));Console.WriteLine(JsonSerializer.Serialize(result));return 0;
  }catch(Exception ex){Console.Error.WriteLine(ex);return 1;}
 }
}

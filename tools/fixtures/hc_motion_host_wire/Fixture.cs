using System.Buffers.Binary;
using System.Text.Json;
internal static class MotionWireFixture {
 static long checks,failures,rows;static readonly Dictionary<string,long> groups=new();static readonly List<object> errors=[];static string active="";
 static void Check(bool ok,string group,string detail=""){checks++;if(ok)return;failures++;groups[group]=groups.GetValueOrDefault(group)+1;if(groups[group]<=5)errors.Add(new{active,group,detail});}
 static bool Same(double a,double b)=>double.IsFinite(a)&&double.IsFinite(b)&&Math.Abs(a-b)<=1e-8*(1+Math.Abs(b));
 static int Main(string[] args){try{
 Program.MotionWireProfileReceipt(Path.ChangeExtension(args[1],"profile.json"));Check(Program.MotionWireRawParity(),"stock-sdk-report-parity-with-quaternion-delta");Check(Program.MotionWireSonyParity(),"sony-touchpad-regression");
 foreach(var line in File.ReadLines(args[0])){var row=JsonDocument.Parse(line).RootElement;active=row.GetProperty("name").GetString()!;rows++;
 (bool parsed,string error,double[] axes,double[] quaternion,short[] state,byte[] report,int back,bool quaternionPresent) result;
 try{result=Program.MotionWire(row.GetProperty("wire").GetString()!,rows);}catch(OverflowException){Check(false,"numeric-overflow-exception");continue;}
 Check(result.parsed,"host-parser");if(!result.parsed)continue;
 var mapped=row.GetProperty("expectedMapped");for(int i=0;i<6;i++)Check(Same(row.GetProperty("actualMapped")[i].GetDouble(),mapped[i].GetDouble()),"actual-model-matrix");
 var expected=row.GetProperty("expectedAxes");for(int i=0;i<6;i++)Check(Same(result.axes[i],expected[i].GetDouble()),"native-json-host-six-axis");
 Check(result.quaternionPresent==row.GetProperty("expectedQuaternionPresent").GetBoolean(),"quaternion-schema-presence");
 var q=row.GetProperty("expectedQuaternion");for(int i=0;i<4;i++)Check(Same(result.quaternion[i],q[i].GetDouble()),"native-json-host-quaternion",$"component={i} actual={result.quaternion[i]:R} expected={q[i].GetDouble():R}");
 var words=row.GetProperty("expectedWords");string persona=row.GetProperty("persona").GetString()!;
 int[] offsets=persona=="steamdeck"?[24,26,28,30,32,34]:[15,17,19,21,23,25];
 for(int i=0;i<6;i++){var value=BinaryPrimitives.ReadInt16LittleEndian(result.report.AsSpan(offsets[i],2));Check(value==words[i].GetInt16(),"signed-motion-wire",$"offset={offsets[i]} actual={value} expected={words[i].GetInt16()}");if(persona=="steamdeck")Check(result.state[i]==words[i].GetInt16(),"sdk-state-and-raw-codec");}
 bool neutral=row.GetProperty("neutral").GetBoolean();Check(result.back==(neutral?0:3),"back-buttons-survive-bridge");
 if(persona=="steamdeck"){var qw=row.GetProperty("expectedQuaternionWords");for(int i=0;i<4;i++){var actual=BinaryPrimitives.ReadInt16LittleEndian(result.report.AsSpan(36+i*2,2));Check(actual==qw[i].GetInt16(),"steamdeck-raw-quaternion-wire",$"component={i} actual={actual} expected={qw[i].GetInt16()}");}var b=BinaryPrimitives.ReadUInt64LittleEndian(result.report.AsSpan(8,8));Check((b&((1UL<<15)|(1UL<<16)))==(neutral?0UL:(1UL<<15)|(1UL<<16)),"steamdeck-rear-output-unchanged");}
 if(persona=="dualsense-edge")Check((result.report[9]&0xc0)==(neutral?0:0xc0),"edge-rear-output-unchanged");
 }
 var output=new{status=failures==0?"pass":"fail",checks,failureCount=failures,failureGroups=groups,errors,rows,actualHostParser=true,actualReportBuilders=true,actualSdkStateType=true,sdkProfile="actual embedded SteamDeck profile; inert model constructors for Edge; real stock encoder parity with explicit quaternion delta",hostMainExecuted=false,controllerOrContextCreated=false,hardwareIo=false,virtualDeviceCreated=false,gameConsumption=false};
 File.WriteAllText(args[1],JsonSerializer.Serialize(output,new JsonSerializerOptions{WriteIndented=true}));Console.WriteLine(JsonSerializer.Serialize(new{output.status,checks,failures,rows}));return failures==0?0:1;
 }catch(Exception e){Console.Error.WriteLine(e);return 2;}}
}

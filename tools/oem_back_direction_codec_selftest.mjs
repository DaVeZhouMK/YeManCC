// Execute actual Host direction/Steam Deck raw-report code using symbolic SDK types only.
// No HMContext, virtual target, driver, device API, transport or production process is present.
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
if(!process.argv[2]||!process.argv[3])throw Error('Usage: node tools/oem_back_direction_codec_selftest.mjs <out> <compiled-native-matrix.json>');
const out=path.resolve(process.argv[2]);fs.mkdirSync(out,{recursive:true});
const host=fs.readFileSync(path.join(root,'InputHost/Program.cs'),'utf8'),pads=fs.readFileSync(path.join(root,'InputHost/ScreenTouchpadReport.cs'),'utf8');
function method(s,name){const re=new RegExp('^    private static [^\\n;]*\\b'+name+'\\(','m'),m=re.exec(s);if(!m)throw Error('Missing '+name);const nextBrace=s.indexOf('{',m.index),arrow=s.indexOf('=>',m.index);if(arrow>=0&&arrow<nextBrace)return s.slice(m.index,s.indexOf(';',arrow)+1);return s.slice(m.index,s.indexOf('\n    }',m.index)+6);}
const bits=pads.match(/    private static readonly \(HMButton Button, int Bit\)\[\] DeckButtonBits =[\s\S]*?\n    \};/)?.[0];if(!bits)throw Error('Missing actual DeckButtonBits');
const constants=host.match(/    private const int DsEdgePaddleLeft[^]*?    private static int MapDsEdgePaddles/)?.[0].replace(/    private static int MapDsEdgePaddles$/,'');if(!constants)throw Error('Missing Edge constants');
const definitions=[method(host,'Xbox360StickAxis'),method(host,'BuildSteamDeckStateFields'),method(host,'QuantizeMotionAxis'),bits,method(pads,'BuildSteamDeckScreenPadReport'),constants,method(host,'MapDsEdgePaddles')];
if(/\b(?:Submit|CreateContext|CreateController|OpenDevice)\w*\(/.test(definitions.join('\n')))throw Error('Unexpected live operation in codec extraction');
const matrix=JSON.parse(fs.readFileSync(process.argv[3],'utf8'));const cases=[];const seen=new Set;for(const m of matrix.models){if(seen.has(m.deviceClass))continue;seen.add(m.deviceClass);for(const k of m.keys)for(const r of k.keyboardRules)if(k.backIndex>0)cases.push({model:m.deviceClass,keyId:k.keyId,catalog:k.backIndex,nativeOutputBits:r.outputBackBits});}
fs.writeFileSync(path.join(out,'native-direction-cases.json'),JSON.stringify(cases,null,2));
const source=String.raw`
using System.Buffers.Binary;using System.Text.Json;using HIDMaestro;
namespace HIDMaestro {
 [Flags]internal enum HMButton:ulong {None=0,RightBumper=1UL<<0,LeftBumper=1UL<<1,Y=1UL<<2,Circle=1UL<<3,Square=1UL<<4,Cross=1UL<<5,Back=1UL<<6,Guide=1UL<<7,Start=1UL<<8,LeftPaddle=1UL<<9,RightPaddle=1UL<<10,LeftStick=1UL<<11,RightStick=1UL<<12}
 internal enum HMHat{None,North,NorthEast,East,SouthEast,South,SouthWest,West,NorthWest}
 internal sealed class HMProfile{public int InputReportSize{get;init;}=64;}
}
internal static partial class Program {
 private readonly record struct ScreenPad(bool Active,bool Click,short X,short Y,ushort Pressure);
 private sealed record CommandEnvelope {
  public int Buttons{get;init;}public int BackButtons{get;init;}public int ScreenButtons{get;init;}
  public float Lx{get;init;}public float Ly{get;init;}public float Rx{get;init;}public float Ry{get;init;}public float LtRaw{get;init;}public float RtRaw{get;init;}
  public bool ImuAdmitted{get;init;}public double ImuAccelX{get;init;}public double ImuAccelY{get;init;}public double ImuAccelZ{get;init;}public double ImuGyroX{get;init;}public double ImuGyroY{get;init;}public double ImuGyroZ{get;init;}
  public bool ImuQuaternionPresent{get;init;}
  public double ImuQuatW{get;init;}=1;public double ImuQuatX{get;init;}public double ImuQuatY{get;init;}public double ImuQuatZ{get;init;}
  public ScreenPad ScreenPadLeft{get;init;}public ScreenPad ScreenPadRight{get;init;}
 }
`+definitions.join('\n\n')+String.raw`
 public static void Main(){unsigned_placeholder}
}
`;
const main=String.raw`
 int checks=0;void Check(bool ok,string name){++checks;if(!ok)throw new InvalidOperationException(name);}
 var profile=new HMProfile();var buffer=Enumerable.Repeat((byte)255,64).ToArray();
 const ulong paddles=(1UL<<15)|(1UL<<16);uint counter=0;
 for(int round=0;round<100;round++)foreach(int value in new[]{0,1,0,2,0,3,0}){
  var e=new CommandEnvelope{BackButtons=value};var fields=BuildSteamDeckStateFields(profile,e);
  Check(((fields.Buttons&HMButton.LeftPaddle)!=0)==((value&1)!=0),"bit0 -> LeftPaddle");
  Check(((fields.Buttons&HMButton.RightPaddle)!=0)==((value&2)!=0),"bit1 -> RightPaddle");
  var report=BuildSteamDeckScreenPadReport(profile,e,++counter,buffer);var wire=BinaryPrimitives.ReadUInt64LittleEndian(report.AsSpan(8));
  ulong expected=((value&1)!=0?1UL<<15:0)|((value&2)!=0?1UL<<16:0);
  Check((wire&paddles)==expected,"real Steam Deck raw codec left/right bits and release");
  Check(MapDsEdgePaddles(value)==(((value&1)!=0?0x40:0)|((value&2)!=0?0x80:0)),"actual DS Edge left/right mapping");
 }
 var cases=JsonDocument.Parse(File.ReadAllText("native-direction-cases.json"));var output=new List<object>();
 foreach(var entry in cases.RootElement.EnumerateArray()){
  int bits=entry.GetProperty("nativeOutputBits").GetInt32();var e=new CommandEnvelope{BackButtons=bits};
  var report=BuildSteamDeckScreenPadReport(profile,e,++counter,buffer);ulong wire=BinaryPrimitives.ReadUInt64LittleEndian(report.AsSpan(8));
  bool left=(wire&(1UL<<15))!=0,right=(wire&(1UL<<16))!=0;
  Check(left==((bits&1)!=0)&&right==((bits&2)!=0),"native-produced key bits retained by actual Host raw codec");
  output.Add(new{model=entry.GetProperty("model").GetString(),keyId=entry.GetProperty("keyId").GetString(),catalogBackIndex=entry.GetProperty("catalog").GetInt32(),nativeBackBits=bits,hostDeckLeftPaddle=left,hostDeckRightPaddle=right,hostEdgeBits=MapDsEdgePaddles(bits),physicalLeftRightProven=false,SteamConsumptionProven=false});
 }
 File.WriteAllText("native-to-host-direction.json",JsonSerializer.Serialize(output,new JsonSerializerOptions{WriteIndented=true}));
 Console.WriteLine(JsonSerializer.Serialize(new{ok=true,checks,nativeDirectionCases=output.Count,actualHostMethods=true,SDKTypes="symbolic fakes; no SDK loaded",hardwareIo=false,scope="native emitted bits through actual Host fields, Steam Deck raw codec and Edge mapping; no SDK SubmitState encode or game/Steam consumption"}));
`;
fs.writeFileSync(path.join(out,'CodecTest.cs'),source.replace('unsigned_placeholder',main));fs.copyFileSync(path.join(root,'InputHost/SteamDeckStateFields.cs'),path.join(out,'SteamDeckStateFields.cs'));
fs.writeFileSync(path.join(out,'CodecTest.csproj'),'<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable></PropertyGroup></Project>');
const sha=x=>crypto.createHash('sha256').update(x).digest('hex');fs.writeFileSync(path.join(out,'source-binding.json'),JSON.stringify({schema:1,hostProgramSha256:sha(host),screenPadReportSha256:sha(pads),extractedMethodsSha256:sha(definitions.join('\n\n')),nativeCasesSha256:sha(JSON.stringify(cases)),hardwareIo:false},null,2));console.log('Generated actual Host codec harness; native cases='+cases.length);
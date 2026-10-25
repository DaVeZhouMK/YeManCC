using System.Reflection;
using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Reflection.PortableExecutable;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.RegularExpressions;

// Read ECMA-335 metadata/IL as bytes only. Never load the inspected assembly,
// execute its constructors/factory, resolve its dependencies or open a device.
if (args.Length != 4 && args.Length != 5) throw new ArgumentException("Usage: <HC dll> <native-matrix.json> <LightSetter Program.cs> <out.json>");
var dll=Path.GetFullPath(args[0]);var image=File.ReadAllBytes(dll);var assemblyHash=Convert.ToHexString(SHA256.HashData(image)).ToLowerInvariant();
var helper=File.ReadAllText(args[2]);var expected=Regex.Match(helper,@"ExpectedHcSha256\s*=\s*""([a-fA-F0-9]{64})""").Groups[1].Value;
if(string.IsNullOrEmpty(expected)||!assemblyHash.Equals(expected,StringComparison.OrdinalIgnoreCase))throw new InvalidOperationException("Inspected HC is not the helper's pinned assembly");
using var stream=new MemoryStream(image,writable:false);using var pe=new PEReader(stream);var md=pe.GetMetadataReader();
string TypeName(TypeDefinitionHandle handle){var t=md.GetTypeDefinition(handle);var declaring=t.GetDeclaringType();var name=md.GetString(t.Name);var ns=md.GetString(t.Namespace);return !declaring.IsNil?TypeName(declaring)+"+"+name:string.IsNullOrEmpty(ns)?name:ns+"."+name;}
var types=md.TypeDefinitions.ToDictionary(TypeName);
var expectedTypes=args.Length==5?JsonSerializer.Deserialize<Dictionary<string,string>>(File.ReadAllText(args[4]))!:new Dictionary<string,string>();
var ledType=types.Single(kv=>kv.Key.EndsWith("+LEDLevel")).Value;var ledConstants=new Dictionary<string,int>();
foreach(var handle in md.GetTypeDefinition(ledType).GetFields()) {var field=md.GetFieldDefinition(handle);var value=field.GetDefaultValue();if(value.IsNil)continue;var constant=md.GetConstant(value);if(constant.TypeCode!=ConstantTypeCode.Int32)throw new InvalidOperationException("Unsupported LED enum storage");ledConstants[md.GetString(field.Name)]=md.GetBlobReader(constant.Value).ReadInt32();}
var ledNames=new Dictionary<string,string>{{"solid","SolidColor"},{"breathing","Breathing"},{"rainbow","Rainbow"},{"wave","Wave"},{"wheel","Wheel"},{"gradient","Gradient"},{"ambilight","Ambilight"},{"preset","LEDPreset"}};
var helperValues=Regex.Matches(helper,@"\[""(solid|breathing|rainbow|wave|wheel|gradient|ambilight|preset)""\]\s*=\s*(\d+)").ToDictionary(m=>m.Groups[1].Value,m=>int.Parse(m.Groups[2].Value));
var helperLedLevelsMatchRuntime=ledNames.All(kv=>helperValues.TryGetValue(kv.Key,out var number)&&ledConstants.TryGetValue(kv.Value,out var actual)&&number==actual);

object? Method(TypeDefinitionHandle start,string name){var seen=new HashSet<TypeDefinitionHandle>();var current=start;
 while(!current.IsNil&&seen.Add(current)){
  var type=md.GetTypeDefinition(current);var methods=type.GetMethods().Where(h=>md.GetString(md.GetMethodDefinition(h).Name)==name).ToArray();
  if(methods.Length>1)throw new InvalidOperationException("Ambiguous metadata method: "+TypeName(current)+"."+name);
  if(methods.Length==1){var handle=methods[0];var method=md.GetMethodDefinition(handle);var sig=md.GetBlobReader(method.Signature);var header=sig.ReadSignatureHeader();if(header.IsGeneric)sig.ReadCompressedInteger();var count=sig.ReadCompressedInteger();var returnByte=sig.ReadByte();
   var il=method.RelativeVirtualAddress==0?Array.Empty<byte>():pe.GetMethodBody(method.RelativeVirtualAddress).GetILBytes()??Array.Empty<byte>();
   string? constant=il.AsSpan().SequenceEqual(new byte[]{0x16,0x2a})?"false":il.AsSpan().SequenceEqual(new byte[]{0x17,0x2a})?"true":null;
   return new {owner=TypeName(current),method=name,token=MetadataTokens.GetToken(handle),parameterCount=count,returnTypeFirstByte=returnByte,returnsBoolean=returnByte==0x02,
    isPublic=(method.Attributes&MethodAttributes.MemberAccessMask)==MethodAttributes.Public,isVirtual=(method.Attributes&MethodAttributes.Virtual)!=0,
    signatureHex=Convert.ToHexString(md.GetBlobBytes(method.Signature)),ILBytes=il.Length,ILSha256=Convert.ToHexString(SHA256.HashData(il)).ToLowerInvariant(),constantBooleanBody=constant};
  }
  current=type.BaseType.Kind==HandleKind.TypeDefinition?(TypeDefinitionHandle)type.BaseType:default;
 }return null;
}
using var matrix=JsonDocument.Parse(File.ReadAllText(args[1]));var rows=new List<object>();var missing=new List<string>();var seenClasses=new HashSet<string>();int lightClasses=0,declaredLightingClasses=0;
foreach(var model in matrix.RootElement.GetProperty("models").EnumerateArray()){
 string name=model.GetProperty("deviceClass").GetString()!;if(!seenClasses.Add(name))continue;if(model.GetProperty("lightingDeclared").GetBoolean())declaredLightingClasses++;
 var runtimeTypeName=expectedTypes.GetValueOrDefault(name,name);if(!types.TryGetValue(runtimeTypeName,out var type)){missing.Add(name+" => "+runtimeTypeName);continue;}var chain=new List<string>();var cur=type;
 while(!cur.IsNil){chain.Add(TypeName(cur));var parent=md.GetTypeDefinition(cur).BaseType;cur=parent.Kind==HandleKind.TypeDefinition?(TypeDefinitionHandle)parent:default;}
 bool light=model.GetProperty("lightingDeclared").GetBoolean();if(light)lightClasses++;
 var callbacks=new Dictionary<string,object?>();foreach(var method in new[]{"SetLedColor","SetLedBrightness","SetLedStatus","SetLEDPreset"})callbacks[method]=Method(type,method);
 rows.Add(new{deviceClass=name,runtimeTypeName,normalizedIdentityAlias=name!=runtimeTypeName,lightingDeclared=light,classChain=chain,callbacks,hardwareReadinessVerified=false,hardwareEffectVerified=false});
}
var report=new{schema=1,assemblyPath=dll,assemblySha256=assemblyHash,matchesLightSetterPin=true,helperLedLevelsMatchRuntime,ledLevelConstants=ledConstants,assemblyName=md.GetString(md.GetAssemblyDefinition().Name),deviceClasses=seenClasses.Count,declaredLightingClasses,metadataMatchedLightingClasses=lightClasses,missingTypes=missing,availableDeviceTypes=types.Keys.Where(n=>n.StartsWith("HandheldCompanion.Devices.")&&!n.Contains('+')).Order().ToArray(),classes=rows,
 HCAssemblyLoaded=false,inspectedCodeExecuted=false,hardwareIo=false,scope="actual shipped pinned DLL metadata/IL ownership only; no device factory, constructors, runtime callback dispatch or physical writes"};
Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(args[3]))!);File.WriteAllText(args[3],JsonSerializer.Serialize(report,new JsonSerializerOptions{WriteIndented=true}));
if(AppDomain.CurrentDomain.GetAssemblies().Any(a=>a.GetName().Name=="HandheldCompanion"))throw new InvalidOperationException("Inspected HC was unexpectedly loaded");
Console.WriteLine(JsonSerializer.Serialize(new{ok=missing.Count==0&&helperLedLevelsMatchRuntime,classes=seenClasses.Count,declaredLightingClasses,metadataMatchedLightingClasses=lightClasses,missingTypes=missing,HCAssemblyLoaded=false,hardwareIo=false}));
return missing.Count==0&&helperLedLevelsMatchRuntime?0:1;

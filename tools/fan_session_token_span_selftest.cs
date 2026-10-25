// Actual compiled credential comparator parity; no Host Main/listener/HC/device or CPU benchmark.
using System.IO;
using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
internal delegate bool TokenComparer(string supplied, ReadOnlySpan<byte> expected);
internal static class Checks {
 static readonly Dictionary<short,OpCode> Codes=typeof(OpCodes).GetFields(BindingFlags.Public|BindingFlags.Static).Where(f=>f.FieldType==typeof(OpCode)).Select(f=>(OpCode)f.GetValue(null)!).GroupBy(x=>x.Value).ToDictionary(x=>x.Key,x=>x.First());
 static readonly List<object> Results=[];static int failed;
 static void Check(bool ok,string name){Results.Add(new{name,ok});if(!ok)failed++;Console.WriteLine((ok?"PASS ":"FAIL ")+name);}
 static (List<string> Calls,int Stack,int ByteArrays) Inspect(MethodInfo method){var bytes=method.GetMethodBody()!.GetILAsByteArray()!;var calls=new List<string>();int at=0,stack=0,arrays=0;
  while(at<bytes.Length){short id=bytes[at++];if(id==0xfe)id=unchecked((short)(0xfe00|bytes[at++]));var op=Codes[id];if(op==OpCodes.Localloc)stack++;
   if(op==OpCodes.Newarr&&method.Module.ResolveType(BitConverter.ToInt32(bytes,at))==typeof(byte))arrays++;
   if(op.OperandType==OperandType.InlineMethod){var target=method.Module.ResolveMethod(BitConverter.ToInt32(bytes,at))!;calls.Add((target.DeclaringType?.FullName??"")+"."+target.Name+"("+string.Join(",",target.GetParameters().Select(p=>p.ParameterType.ToString()))+")");}
   at+=op.OperandType switch{OperandType.InlineNone=>0,OperandType.ShortInlineBrTarget or OperandType.ShortInlineI or OperandType.ShortInlineVar=>1,OperandType.InlineVar=>2,OperandType.InlineI8 or OperandType.InlineR=>8,OperandType.InlineSwitch=>4+4*BitConverter.ToInt32(bytes,at),_=>4};
  }return(calls,stack,arrays);
 }
 public static int Main(string[] args){try{
  string dll=Path.GetFullPath(args[0]);var assembly=AssemblyLoadContext.Default.LoadFromAssemblyPath(dll);var type=assembly.GetType("YeManFanHost.LocalFanHostServer",true)!;var flags=BindingFlags.NonPublic|BindingFlags.Public|BindingFlags.Static|BindingFlags.Instance;
  var method=type.GetMethod("SessionTokenMatches",flags)??throw new InvalidOperationException("current production comparator missing");var compare=method.CreateDelegate<TokenComparer>();
  var values=new List<(string Name,string Value)>();foreach(int n in new[]{0,1,32,64,128,255,256,257,1024,4096})values.Add(("ascii-"+n,new string('a',n)));
  values.AddRange(new[]{("latin1-254",new string('\u00e9',127)),("latin1-256",new string('\u00e9',128)),("latin1-258",new string('\u00e9',129)),("bmp-255",new string('\u0800',85)),("bmp-256",new string('\u0800',85)+"a"),("bmp-257",new string('\u0800',85)+"ab"),("supplementary-256",string.Concat(Enumerable.Repeat("\ud83d\ude00",64))),("supplementary-257",string.Concat(Enumerable.Repeat("\ud83d\ude00",64))+"x"),("unpaired-surrogate-255",new string('\ud800',85)),("unpaired-surrogate-258",new string('\ud800',86)),("embedded-null","aa\0bb")});
  foreach(var (name,value) in values){var expected=Encoding.UTF8.GetBytes(value);foreach(var (suffix,supplied) in new[]{("same",value),("suffix",value+"!"),("other",new string('b',value.Length))}){
    bool legacy=CryptographicOperations.FixedTimeEquals(Encoding.UTF8.GetBytes(supplied),expected);bool actual=compare(supplied,expected);Check(actual==legacy,name+":"+suffix+":same-result-as-original-byte-comparison");
  }}
  Check(!compare("A",Encoding.UTF8.GetBytes("a")),"case-sensitive comparison preserved");
  Check(!compare("e\u0301",Encoding.UTF8.GetBytes("\u00e9")),"no Unicode normalization introduced");
  Check(compare("\ud801",Encoding.UTF8.GetBytes("\ud800"))==CryptographicOperations.FixedTimeEquals(Encoding.UTF8.GetBytes("\ud801"),Encoding.UTF8.GetBytes("\ud800")),"existing UTF8 replacement fallback semantics preserved");
  var shape=Inspect(method);var caller=Inspect(type.GetMethod("IsAuthorizedRequest",flags)!);
  Check(caller.Calls.Count(x=>x.Contains(".SessionTokenMatches("))==1,"real request authenticator calls this compiled comparator");
  Check(shape.Stack==1&&shape.ByteArrays==1,"compiled comparator has stack storage and a heap fallback");
  Check(shape.Calls.Count(x=>x.Contains("CryptographicOperations.FixedTimeEquals("))==1,"compiled comparator retains one fixed-time byte comparison");
  Check(shape.Calls.Any(x=>x.Contains("Encoding.GetBytes(")&&x.Contains("ReadOnlySpan")&&x.Contains("Span")),"compiled encoding writes into the provided span");
  Check(!AppDomain.CurrentDomain.GetAssemblies().Any(x=>x.GetName().Name=="HandheldCompanion"),"no HC assembly loaded");
  var result=new{ok=failed==0,checks=Results.Count,failed,results=Results,comparatorIl=new{stackInstructions=shape.Stack,byteArrayFallbackInstructions=shape.ByteArrays,calls=shape.Calls},hostSha256=Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(dll))).ToLowerInvariant(),scope="actual compiled private comparator, original byte-comparison oracle and read-only IL; synthetic strings only, not HTTP header encoding proof; no Host Main/HC/device/CPU or allocation timing benchmark"};
  var json=JsonSerializer.Serialize(result);File.WriteAllText(args[1],json);Console.WriteLine(json);return failed==0?0:1;
 }catch(Exception e){Console.Error.WriteLine(e);return 2;}}
}

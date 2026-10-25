using System;using System.IO;using System.Linq;using System.Reflection;using System.Text.Json;
static class SdkFreeToolEntry
{
 [STAThread]
 static int Main(string[] args)
 {
  if(args.Length<4){Console.Error.WriteLine("TOOL_LAUNCH_POLICY_REQUIRED");return 252;}PinnedPreEntryResolver? resolver=null;int code=250;string? failure=null;object? business=null;
  try {
   if(ScopedLoadAuthority.Hash(args[1])!=args[2])throw new InvalidOperationException("AUTHORITY_POLICY_SHA_MISMATCH");
   var policy=JsonSerializer.Deserialize<ScopedEntryPolicy>(File.ReadAllText(args[1]))!;if(policy.metadata.Select(p=>Path.GetFullPath(p.path)).Distinct(StringComparer.OrdinalIgnoreCase).Count()!=policy.metadata.Length)throw new InvalidOperationException("AUTHORITY_DUPLICATE_METADATA_PATH");resolver=new PinnedPreEntryResolver(policy);resolver.Prepare();
   if(args[0]=="validate"){code=0;}else if(args[0]=="run"){
    resolver.Attach();
    // Existing independent producer-deps binding remains before target loading.
    // Synthetic cases do not use these two producer identities.
    TraceEntryDependencyContext.Binding? binding=null;
    if(new[]{"OneXFanCoexistence.dll","JoinedFactoryProbe.dll"}.Contains(Path.GetFileName(policy.entry))){var deps=policy.entry[..^4]+".deps.json";var pin=policy.metadata.Single(p=>Path.GetFullPath(p.path)==Path.GetFullPath(deps));binding=TraceEntryDependencyContext.Bind(policy.entry,pin.sha256,policy.ownedRoot);}
    var entry=resolver.LoadApprovedPath(policy.entry);business=entry.EntryPoint!.Invoke(null,new object[]{args.Skip(4).ToArray()});code=(int)business!;resolver.VerifyAfter();if(binding is not null)TraceEntryDependencyContext.Verify(binding);
   }else throw new InvalidOperationException("AUTHORITY_MODE_INVALID");
  }catch(Exception e){failure=e.ToString();code=250;}
  var loaded=AppDomain.CurrentDomain.GetAssemblies().Where(a=>!a.IsDynamic&&!string.IsNullOrEmpty(a.Location)).Select(a=>new{path=a.Location,identity=a.FullName,sha256=ScopedLoadAuthority.Hash(a.Location)}).ToArray();
  File.WriteAllText(args[3],JsonSerializer.Serialize(new{exitCode=code,failure,events=resolver?.Authority.Events,actualLoadedManaged=loaded,productLoaded=loaded.Any(a=>new[]{"YeManFanHost","HandheldCompanion","Microsoft.Windows.SDK.NET","WinRT.Runtime"}.Contains(new AssemblyName(a.identity!).Name)),osNativeGraphClaimed=false},new JsonSerializerOptions{WriteIndented=true}));return code;
 }
}

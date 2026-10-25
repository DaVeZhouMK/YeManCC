using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;
using System.Collections;
using System.Collections.Generic;

static class DefaultPreloadMetadata
{
 const string ApprovedAuthoritySha="64ad04c2085e208d6e27aaf6f4c15d2a5ad35b58a4076cec73e3b6b407c1900b";
 const string ApprovedAuthorityIdentity="ScopedLoadAuthority, Version=0.0.0.0, Culture=neutral, PublicKeyToken=null";
 static readonly string[] TargetNames={"MTA","STA","OneXFanCoexistence","ConstructorIoPorts","YeManFanHost","YeManFanBackend","YeManLightSetter","HandheldCompanion","Microsoft.Windows.SDK.NET","WinRT.Runtime","JoinedFactoryProbe","BootstrapFixture","Mono.Cecil","Newtonsoft.Json"};
 public sealed record LoadedRow(string identity,string? path,string? sha256,bool dynamic,bool target);
 static string Hash(string p)=>Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(p))).ToLowerInvariant();
 static bool Same(string a,string b)=>string.Equals(Path.GetFullPath(a),Path.GetFullPath(b),StringComparison.OrdinalIgnoreCase);
 static LoadedRow Row(Assembly a){var p=a.IsDynamic?null:a.Location;return new(a.FullName??"",string.IsNullOrEmpty(p)?null:p,string.IsNullOrEmpty(p)?null:Hash(p),a.IsDynamic,TargetNames.Contains(a.GetName().Name));}
 static LoadedRow[] Snapshot()=>AppDomain.CurrentDomain.GetAssemblies().Select(Row).OrderBy(r=>r.identity).ThenBy(r=>r.path).ToArray();
 static void Require(bool value,string reason){if(!value)throw new InvalidOperationException(reason);}
 static string Failure(Exception e)=>(e is TargetInvocationException t&&t.InnerException is not null?t.InnerException:e).Message;
 static int Main(string[] args)
 {
  if(args.Length!=6||args[0]!="check-default-metadata"){Console.Error.WriteLine("PRELOAD_METADATA_EXACT_MODE_AND_SIX_ARGUMENTS_REQUIRED");return 252;}
  Require(Path.IsPathFullyQualified(args[5])&&!File.Exists(args[5]),"PRELOAD_METADATA_FRESH_OWNED_RECEIPT_REQUIRED");
  Hash(typeof(object).Assembly.Location);JsonSerializer.Serialize(new LoadedRow("warmup",null,null,false,false));
  var loads=new List<LoadedRow>();var observeErrors=new List<string>();object gate=new();
  AppDomain.CurrentDomain.AssemblyLoad+=(_,e)=>{try{var row=Row(e.LoadedAssembly);lock(gate)loads.Add(row);}catch(Exception error){lock(gate)observeErrors.Add(error.ToString());}};
  var before=Snapshot();var tpa=AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES") as string;
  var rows=new List<object>();int rejectedPins=0;object? authority=null;FieldInfo? eventsField=null;string? failure=null;bool prepared=false;string? reflectionMethod=null;
  try
  {
   Require(Hash(args[1])==args[2],"PRELOAD_METADATA_POLICY_SHA_MISMATCH");
   Require(args[4]==ApprovedAuthoritySha&&Hash(args[3])==ApprovedAuthoritySha,"PRELOAD_METADATA_AUTHORITY_NOT_REVIEWED_SHA");
   Require(AssemblyName.GetAssemblyName(args[3]).FullName==ApprovedAuthorityIdentity,"PRELOAD_METADATA_AUTHORITY_IDENTITY");
   foreach(var cached in AssemblyLoadContext.Default.Assemblies.Where(a=>a.GetName().Name=="ScopedLoadAuthority"))
    Require(!cached.IsDynamic&&Same(cached.Location,args[3])&&cached.FullName==ApprovedAuthorityIdentity&&Hash(cached.Location)==ApprovedAuthoritySha,"PRELOAD_METADATA_AUTHORITY_CACHE_CONFLICT");
   Require(tpa is not null,"PRELOAD_METADATA_TPA_UNAVAILABLE");
   foreach(var candidate in tpa!.Split(Path.PathSeparator,StringSplitOptions.RemoveEmptyEntries).Where(p=>string.Equals(Path.GetFileNameWithoutExtension(p),"ScopedLoadAuthority",StringComparison.OrdinalIgnoreCase)))
    Require(Same(candidate,args[3])&&Hash(candidate)==ApprovedAuthoritySha,"PRELOAD_METADATA_AUTHORITY_TPA_PATH_CONFLICT");
   // The one permitted managed image load is this exact reviewed own helper.
   // Policy paths and identities never select an image for Assembly.Load.
   var helper=AssemblyLoadContext.Default.LoadFromAssemblyPath(Path.GetFullPath(args[3]));
   Require(helper.FullName==ApprovedAuthorityIdentity&&Same(helper.Location,args[3])&&AssemblyLoadContext.GetLoadContext(helper)==AssemblyLoadContext.Default&&Hash(helper.Location)==ApprovedAuthoritySha,"PRELOAD_METADATA_AUTHORITY_CLR_RESULT");
   var policyType=helper.GetType("ScopedEntryPolicy",true)!;var pinType=helper.GetType("AuthorityAssembly",true)!;var authorityType=helper.GetType("ScopedLoadAuthority",true)!;
   var policy=JsonSerializer.Deserialize(File.ReadAllText(args[1]),policyType)!;
   authority=Activator.CreateInstance(authorityType,new[]{policy})!;
   eventsField=authorityType.GetField("Events",BindingFlags.Instance|BindingFlags.Public)!;
   var prepare=authorityType.GetMethod("Prepare",BindingFlags.Instance|BindingFlags.Public,null,Type.EmptyTypes,null)!;
   Require(prepare.DeclaringType==authorityType&&prepare.ReturnType==typeof(void),"PRELOAD_METADATA_PREPARE_METHOD_SHAPE");
   prepare.Invoke(authority,null);prepared=true;
   var review=authorityType.GetMethod("ReviewPreloadBinding",BindingFlags.Instance|BindingFlags.NonPublic,null,new[]{typeof(AssemblyLoadContext),pinType},null)!;
   Require(review.DeclaringType==authorityType&&review.ReturnType==typeof(void)&&!review.IsStatic,"PRELOAD_METADATA_REVIEW_METHOD_SHAPE");reflectionMethod=review.ToString();
   var contexts=(IEnumerable)policyType.GetProperty("contexts")!.GetValue(policy)!;
   var defaultContext=contexts.Cast<object>().Single(c=>string.Equals((string?)c.GetType().GetProperty("id")!.GetValue(c),"Default",StringComparison.Ordinal));
   var pins=((IEnumerable)defaultContext.GetType().GetProperty("managed")!.GetValue(defaultContext)!).Cast<object>().ToArray();
   foreach(var pin in pins)
   {
    Require(pin.GetType()==pinType,"PRELOAD_METADATA_PIN_TYPE");
    var events=(List<string>)eventsField.GetValue(authority)!;var start=events.Count;string? rejection=null;
    try{review.Invoke(authority,new[]{(object)AssemblyLoadContext.Default,pin});}catch(Exception e){rejection=Failure(e);}
    if(rejection is not null)rejectedPins++;rows.Add(new{pinJson=JsonSerializer.Serialize(pin,pinType),admitted=rejection is null,rejection,events=events.Skip(start).ToArray()});
   }
  }
  catch(Exception e){failure=Failure(e);}
  var after=Snapshot();LoadedRow[] observed;string[] errors;lock(gate){observed=loads.ToArray();errors=observeErrors.ToArray();}
  bool target=before.Concat(after).Concat(observed).Any(r=>r.target);bool rejected=failure is not null||rejectedPins!=0;
  int code=target||errors.Length!=0?2:rejected?3:0;
  File.WriteAllText(args[5],JsonSerializer.Serialize(new{schemaVersion=1,status=code==0?"PASS_DEFAULT_PRELOAD_METADATA":code==3?"REJECTED_DEFAULT_PRELOAD_METADATA":"FAIL_OBSERVATION",exitCode=code,failure,metadataPrepared=prepared,reflectionMethod,reviewedAuthorityPath=args[3],reviewedAuthoritySha256=ApprovedAuthoritySha,policyPath=args[1],policySha256=args[2],defaultPinCount=rows.Count,rows,authorityEvents=authority is null?null:eventsField?.GetValue(authority),actualLoadedManagedBefore=before,actualLoadedManagedAfter=after,assemblyLoadEvents=observed,observerErrors=errors,targetAssembliesObserved=target,tpaPaths=tpa?.Split(Path.PathSeparator,StringSplitOptions.RemoveEmptyEntries),attachExecuted=false,loadPinExecuted=false,producerExecuted=false,osNativeGraphClaimed=false},new JsonSerializerOptions{WriteIndented=true}));
  return code;
 }
}

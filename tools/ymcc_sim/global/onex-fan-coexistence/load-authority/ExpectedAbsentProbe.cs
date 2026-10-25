using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.Loader;
using System.Collections.Generic;

// Test-only expected-absence experiment. This is not a managed-code sandbox.
// No request API gets a general unknown-name fallback from this contract.
public sealed partial class ScopedLoadAuthority
{
 sealed class ExpectedAbsentProbe(string name,int thread)
 {
  public readonly string Name=name;
  public readonly int Thread=thread;
  public int GuardVisits;
 }
 readonly object absentProbeGate=new();
 readonly HashSet<string> usedAbsentProbeNames=new(StringComparer.OrdinalIgnoreCase);
 ExpectedAbsentProbe? activeAbsentProbe;

 static bool BareProbeIdentity(AssemblyName request)=>request.Version is null&&
  string.IsNullOrEmpty(request.CultureName)&&(request.GetPublicKeyToken()?.Length??0)==0&&
  request.Flags==AssemblyNameFlags.None&&request.ContentType==AssemblyContentType.Default;
 static bool FactoryProbeName(string? name)=>name is not null&&
  name.StartsWith("YMCCFactoryMissing",StringComparison.Ordinal)&&name.Length==18+32&&
  Guid.TryParseExact(name[18..],"N",out var value)&&value.ToString("N")==name[18..];
 static bool SameSimpleName(string? a,string? b)=>string.Equals(a,b,StringComparison.OrdinalIgnoreCase);

 HashSet<string> ExpectedProbeRoots()
 {
  var roots=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
  void Add(string root){if(!Path.IsPathFullyQualified(root))throw Refuse("AUTHORITY_ABSENT_PROBE_ROOT_NOT_ABSOLUTE");roots.Add(Full(root));}
  Add(AppContext.BaseDirectory);Add(Path.GetDirectoryName(Full(policy.entry))!);
  foreach(var context in policy.contexts)
  {
   foreach(var pin in context.managed)Add(Path.GetDirectoryName(Full(pin.path))!);
   if(context.kind=="custom")
   {
    var separator=context.id.LastIndexOf('|');
    if(separator<0)throw Refuse("AUTHORITY_ABSENT_PROBE_UNDECLARED_CONTEXT_ROOT");
    Add(context.id[(separator+1)..]);
   }
  }
  foreach(var key in new[]{"APP_PATHS","APP_NI_PATHS","PROBING_DIRECTORIES"})
  {
   var data=AppContext.GetData(key);if(data is null)continue;
   if(data is not string text)throw Refuse("AUTHORITY_ABSENT_PROBE_PROBING_DATA_SHAPE");
   foreach(var root in text.Split(Path.PathSeparator,StringSplitOptions.RemoveEmptyEntries))Add(root);
  }
  return roots;
 }
 void VerifyExpectedAbsence(string name)
 {
  if(AppDomain.CurrentDomain.GetAssemblies().Any(a=>SameSimpleName(a.GetName().Name,name)))
   throw Refuse("AUTHORITY_ABSENT_PROBE_ALREADY_LOADED");
  foreach(var context in policy.contexts)
   if(context.managed.Any(p=>SameSimpleName(new AssemblyName(p.identity).Name,name))||
    context.requests.Any(r=>SameSimpleName(new AssemblyName(r.requestedIdentity).Name,name)))
    throw Refuse("AUTHORITY_ABSENT_PROBE_POLICY_CANDIDATE");
  if(AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES") is not string tpa)
   throw Refuse("AUTHORITY_ABSENT_PROBE_TPA_UNAVAILABLE");
  if(tpa.Split(Path.PathSeparator,StringSplitOptions.RemoveEmptyEntries).Any(p=>
   SameSimpleName(Path.GetFileNameWithoutExtension(p),name)||
   SameSimpleName(Path.GetFileNameWithoutExtension(p),name+".ni")))
   throw Refuse("AUTHORITY_ABSENT_PROBE_TPA_CANDIDATE");
  foreach(var root in ExpectedProbeRoots())
  {
   if(Directory.Exists(root))Plain(root);
   foreach(var suffix in new[]{".dll",".exe",".ni.dll",".ni.exe"})
    if(File.Exists(Path.Combine(root,name+suffix)))throw Refuse("AUTHORITY_ABSENT_PROBE_PROBING_CANDIDATE");
  }
 }

 public Assembly LoadExpectedAbsentProbe(AssemblyLoadContext context,AssemblyName request)
 {
  // The wrapper fixes the actual CLR receiver. A delegate cannot substitute a
  // different context or insert another load between admission and this call.
  if(!prepared||!attached)throw Refuse("AUTHORITY_ABSENT_PROBE_GUARD_NOT_ATTACHED");
  if(!ReferenceEquals(context,AssemblyLoadContext.Default))throw Refuse("AUTHORITY_ABSENT_PROBE_CONTEXT_MISMATCH");
  if(!FactoryProbeName(request.Name)||!BareProbeIdentity(request))throw Refuse("AUTHORITY_ABSENT_PROBE_IDENTITY_SHAPE");
  var probe=new ExpectedAbsentProbe(request.Name!,Environment.CurrentManagedThreadId);
  lock(absentProbeGate)
  {
   if(activeAbsentProbe is not null)throw Refuse("AUTHORITY_ABSENT_PROBE_ACTIVE");
   if(usedAbsentProbeNames.Contains(probe.Name))throw Refuse("AUTHORITY_ABSENT_PROBE_REUSED");
   VerifyExpectedAbsence(probe.Name);
   usedAbsentProbeNames.Add(probe.Name);activeAbsentProbe=probe;
   Events.Add("absent-probe-register:Default:"+probe.Name+":"+probe.Thread);
  }
  try
  {
   VerifyExpectedAbsence(probe.Name);
   Events.Add("absent-probe-before-clr:Default:"+probe.Name);
   try
   {
    var unexpected=context.LoadFromAssemblyName(request);
    throw Refuse("AUTHORITY_ABSENT_PROBE_UNEXPECTED_ASSEMBLY");
   }
   catch(FileNotFoundException error)
   {
    // Only the exact failed bind can complete this contract. Do not turn an
    // unrelated dependency failure into an expected successful experiment.
    var missing=error.FileName is null?null:new AssemblyName(error.FileName);
    if(error.InnerException is not null||probe.GuardVisits!=1||missing is null||!SameSimpleName(missing.Name,probe.Name)||!BareProbeIdentity(missing))
     throw Refuse("AUTHORITY_ABSENT_PROBE_COMPLETION_MISMATCH");
    VerifyExpectedAbsence(probe.Name);
    Events.Add("absent-probe-file-not-found:Default:"+probe.Name);
    throw;
   }
  }
  finally
  {
   lock(absentProbeGate)
   {
    activeAbsentProbe=null;
    Events.Add("absent-probe-consumed:Default:"+probe.Name);
   }
  }
 }
 bool PermitExpectedAbsentFallback(AssemblyLoadContext context,AssemblyName request)
 {
  lock(absentProbeGate)
  {
   var probe=activeAbsentProbe;
   if(probe is null||!SameSimpleName(request.Name,probe.Name))return false;
   if(!ReferenceEquals(context,AssemblyLoadContext.Default))throw Refuse("AUTHORITY_ABSENT_PROBE_CONTEXT_MISMATCH");
   if(probe.Thread!=Environment.CurrentManagedThreadId)throw Refuse("AUTHORITY_ABSENT_PROBE_THREAD_MISMATCH");
   if(request.Name!=probe.Name||!BareProbeIdentity(request))throw Refuse("AUTHORITY_ABSENT_PROBE_REQUEST_IDENTITY_MISMATCH");
   if(probe.GuardVisits!=0)throw Refuse("AUTHORITY_ABSENT_PROBE_REUSED");
   VerifyExpectedAbsence(probe.Name);probe.GuardVisits++;
   Events.Add("absent-probe-guard-observed:Default:"+probe.Name);
   return true;
  }
 }
 Assembly? ResolveExpectedAbsentOrReject(AssemblyLoadContext context,AssemblyName request)=>
  PermitExpectedAbsentFallback(context,request)?null:ResolveOrReject(context,request);
}

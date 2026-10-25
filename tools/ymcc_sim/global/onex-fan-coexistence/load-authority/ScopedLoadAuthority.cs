using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Collections.Generic;
using System.Text.Json;

public sealed record AuthorityFile(string path,string sha256);
public sealed record AuthorityAssembly(string path,string sha256,string identity,string role);
public sealed record AuthorityRequest(string requestedIdentity,string targetContext,string targetPath);
public sealed record AuthorityContext(string id,string kind,string? registrationType,AuthorityAssembly? registrationAssembly,AuthorityAssembly[] managed,AuthorityRequest[] requests);
public sealed record ExactInteropAlias(string requestedIdentity,string targetContext,AuthorityAssembly provider,string[] requestingContexts);
// The five-argument primary constructor stays ABI compatible with schema2 callers.
public sealed record ScopedEntryPolicy(int schemaVersion,string ownedRoot,string entry,AuthorityContext[] contexts,AuthorityFile[] metadata)
{
 public ExactInteropAlias[]? exactAliases {get;init;}
}

// This is a call-site authority, not a CLR sandbox. Reviewed callers must use its
// path/request APIs and must not make an independent raw CLR load afterwards.
public sealed partial class ScopedLoadAuthority
{
 readonly ScopedEntryPolicy policy;
 readonly Dictionary<string,AuthorityContext> scopes=new(StringComparer.Ordinal);
 readonly Dictionary<string,Dictionary<string,AuthorityAssembly>> paths=new(StringComparer.Ordinal);
 readonly Dictionary<string,Dictionary<string,AuthorityRequest>> requests=new(StringComparer.Ordinal);
 readonly Dictionary<AssemblyLoadContext,string> registered=new(ReferenceEqualityComparer.Instance);
 public readonly List<string> Events=new();
 bool prepared,attached;
 public ScopedLoadAuthority(ScopedEntryPolicy p){policy=p;}
 public ScopedEntryPolicy Policy=>policy;
 public static string Hash(string file)=>Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(file))).ToLowerInvariant();
 static string Full(string p)=>Path.GetFullPath(p);
 static bool Same(string a,string b)=>string.Equals(Full(a),Full(b),StringComparison.OrdinalIgnoreCase);
 static string Identity(AssemblyName n)=>n.FullName??throw new InvalidOperationException("AUTHORITY_INCOMPLETE_IDENTITY");
 static void Plain(string file){for(var p=Full(file);p is not null;p=Path.GetDirectoryName(p))if((File.GetAttributes(p)&FileAttributes.ReparsePoint)!=0)throw new InvalidOperationException("AUTHORITY_PATH_FORWARDING");}
 static void Verify(AuthorityAssembly pin){Plain(pin.path);if(Hash(pin.path)!=pin.sha256)throw new InvalidOperationException("AUTHORITY_SHA_MISMATCH");if(AssemblyName.GetAssemblyName(pin.path).FullName!=pin.identity)throw new InvalidOperationException("AUTHORITY_DISK_IDENTITY_MISMATCH");}
 InvalidOperationException Refuse(string reason){Events.Add("reject:"+reason);return new InvalidOperationException(reason);}
 string Scope(AssemblyLoadContext context){if(!prepared)throw Refuse("AUTHORITY_NOT_PREPARED");if(!registered.TryGetValue(context,out var id))throw Refuse("AUTHORITY_CONTEXT_NOT_REGISTERED");return id;}
 public void Prepare()
 {
  if(prepared||(policy.schemaVersion!=2&&policy.schemaVersion!=3&&policy.schemaVersion!=4&&policy.schemaVersion!=5&&policy.schemaVersion!=6))throw Refuse("AUTHORITY_POLICY_SHAPE");
  foreach(var c in policy.contexts){if(!scopes.TryAdd(c.id,c)||!new[]{"default","custom"}.Contains(c.kind))throw Refuse("AUTHORITY_CONTEXT_SHAPE");var map=new Dictionary<string,AuthorityAssembly>(StringComparer.OrdinalIgnoreCase);var names=new HashSet<string>(StringComparer.OrdinalIgnoreCase);foreach(var p in c.managed){Verify(p);if(!map.TryAdd(Full(p.path),p)||!names.Add(new AssemblyName(p.identity).Name!))throw Refuse("AUTHORITY_DUPLICATE_CONTEXT_PIN");}paths.Add(c.id,map);if(c.kind=="custom"){if(c.registrationAssembly is null||string.IsNullOrEmpty(c.registrationType))throw Refuse("AUTHORITY_REGISTRATION_SHAPE");Verify(c.registrationAssembly);}var rm=new Dictionary<string,AuthorityRequest>(StringComparer.Ordinal);foreach(var request in c.requests)if(!rm.TryAdd(request.requestedIdentity,request))throw Refuse("AUTHORITY_DUPLICATE_REQUEST");requests.Add(c.id,rm);}
  if(!scopes.TryGetValue("Default",out var d)||d.kind!="default"||scopes.Values.Count(c=>c.kind=="default")!=1)throw Refuse("AUTHORITY_DEFAULT_SHAPE");
  ValidateAliasDeclaration();
  foreach(var c in policy.contexts)foreach(var b in c.requests){if(b.targetContext!=c.id&&b.targetContext!="Default")throw Refuse("AUTHORITY_CROSS_CUSTOM_EXPORT_FORBIDDEN");if(!paths.TryGetValue(b.targetContext,out var target)||!target.TryGetValue(Full(b.targetPath),out var p))throw Refuse("AUTHORITY_REQUEST_BINDING_IDENTITY_OR_PATH");ValidateRequestBinding(c.id,b.targetContext,b.targetPath,b.requestedIdentity,p,"AUTHORITY_REQUEST_BINDING_IDENTITY_OR_PATH");}
  foreach(var p in policy.metadata){Plain(p.path);if(Hash(p.path)!=p.sha256)throw Refuse("AUTHORITY_METADATA_SHA_MISMATCH");}
  if(!paths["Default"].ContainsKey(Full(policy.entry)))throw Refuse("AUTHORITY_ENTRY_PATH_NOT_APPROVED");
  // Target bytes are inspected as metadata only. Infrastructure/frameworks can
  // already exist; a non-infrastructure target must not predate the authority.
  foreach(var p in policy.contexts.SelectMany(c=>c.managed).Where(p=>p.role=="target"))if(AppDomain.CurrentDomain.GetAssemblies().Any(a=>!a.IsDynamic&&a.GetName().Name==new AssemblyName(p.identity).Name))throw Refuse("AUTHORITY_TARGET_ALREADY_LOADED");
  registered.Add(AssemblyLoadContext.Default,"Default");prepared=true;Events.Add("prepare-metadata-only");
 }
 public void RegisterKnownContext(string id,AssemblyLoadContext context)
 {
  if(!prepared)throw Refuse("AUTHORITY_NOT_PREPARED");if(context==AssemblyLoadContext.Default||!scopes.TryGetValue(id,out var c)||c.kind!="custom")throw Refuse("AUTHORITY_CONTEXT_KIND_MISMATCH");
  var a=context.GetType().Assembly;var pin=c.registrationAssembly!;if(context.GetType().FullName!=c.registrationType||a.FullName!=pin.identity||a.IsDynamic||!Same(a.Location,pin.path))throw Refuse("AUTHORITY_CONTEXT_TYPE_OR_ASSEMBLY_MISMATCH");Verify(pin);
  if(!registered.TryAdd(context,id))throw Refuse("AUTHORITY_CONTEXT_ALREADY_REGISTERED");Events.Add("register:"+id+":"+context.GetType().FullName);
 }
 // This is one reviewed identity translation, never version-range unification.
 const string InteropAliasProducer="OneXFanCoexistence, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null";
 const string Interop8="System.Runtime.InteropServices, Version=8.0.0.0, Culture=neutral, PublicKeyToken=b03f5f7f11d50a3a";
 const string Interop10="System.Runtime.InteropServices, Version=10.0.0.0, Culture=neutral, PublicKeyToken=b03f5f7f11d50a3a";
 const string Interop10Path=@"C:\Program Files\dotnet\shared\Microsoft.NETCore.App\10.0.11\System.Runtime.InteropServices.dll";
 const string Interop10Sha="9d0621f1e573bd20525287533bea24ec7824813eada7820a0c82252ede072ab9";
 bool IsExactInteropProvider(AuthorityAssembly p)=>p.identity==Interop10&&p.sha256==Interop10Sha&&p.role=="framework"&&Path.IsPathFullyQualified(p.path)&&Same(p.path,Interop10Path);
 bool IsAliasScope(AuthorityContext c)
 {
  if(c.id=="Default")return c.kind=="default";
  if(c.kind!="custom"||c.registrationAssembly is null||!paths["Default"].TryGetValue(Full(policy.entry),out var entry)||entry.identity!=InteropAliasProducer||c.registrationAssembly.identity!=entry.identity||c.registrationAssembly.sha256!=entry.sha256||!Same(c.registrationAssembly.path,entry.path))return false;
  if(c.id=="ConstructorContext")return c.registrationType=="Harness+ConstructorContext";
  const string prefix="Harness.ProductContext|";
  if(c.registrationType!="Harness+ProductContext"||!c.id.StartsWith(prefix,StringComparison.Ordinal))return false;
  var root=c.id[prefix.Length..];
  return Path.IsPathFullyQualified(root)&&root==Path.TrimEndingDirectorySeparator(Full(root)).ToUpperInvariant()&&c.managed.All(p=>string.Equals(Path.GetDirectoryName(Full(p.path)),root,StringComparison.OrdinalIgnoreCase));
 }
 void ValidateAliasDeclaration()
 {
  if(policy.schemaVersion==6){ValidateJoinedDefaultVersionPairs();return;}
  if(policy.schemaVersion==5){ValidateFrameworkVersionPairs();return;}
  if(policy.schemaVersion==4){ValidateFrameworkAliasTable();return;}
  if(policy.schemaVersion==2){if(policy.exactAliases is not null)throw Refuse("AUTHORITY_ALIAS_SCHEMA3_REQUIRED");return;}
  if(policy.exactAliases is null||policy.exactAliases.Length!=1)throw Refuse("AUTHORITY_ALIAS_EXACTLY_ONE_REQUIRED");
  var a=policy.exactAliases[0];
  if(a is null||a.requestedIdentity!=Interop8||a.targetContext!="Default"||a.provider is null||!IsExactInteropProvider(a.provider))throw Refuse("AUTHORITY_ALIAS_DECLARATION_MISMATCH");
  if(policy.contexts.Length!=3||a.requestingContexts is null||a.requestingContexts.Length!=3||a.requestingContexts.Distinct(StringComparer.Ordinal).Count()!=3||!policy.contexts.All(c=>IsAliasScope(c)&&a.requestingContexts.Contains(c.id,StringComparer.Ordinal))||!scopes.ContainsKey("ConstructorContext")||policy.contexts.Count(c=>c.registrationType=="Harness+ProductContext")!=1)throw Refuse("AUTHORITY_ALIAS_CONTEXT_SET_MISMATCH");
  if(!paths["Default"].TryGetValue(Full(a.provider.path),out var pin)||!IsExactInteropProvider(pin))throw Refuse("AUTHORITY_ALIAS_PROVIDER_NOT_DEFAULT_PIN");
  foreach(var c in policy.contexts)
  {
   if(!requests[c.id].TryGetValue(Interop8,out var request)||request.targetContext!="Default"||!Path.IsPathFullyQualified(request.targetPath)||!Same(request.targetPath,Interop10Path))throw Refuse("AUTHORITY_ALIAS_EXACT_ROUTE_REQUIRED");
   if(c.id!="Default"&&c.managed.Any(p=>new AssemblyName(p.identity).Name=="System.Runtime.InteropServices"))throw Refuse("AUTHORITY_ALIAS_CUSTOM_PROVIDER_FORBIDDEN");
  }
 }
 // Schema4 is an explicit bounded table over already pinned Default frameworks.
 const string FrameworkAliasRoot=@"C:\Program Files\dotnet\shared\Microsoft.NETCore.App\10.0.11";
 bool IsExactFrameworkAliasProvider(ExactInteropAlias alias)
 {
  if(alias is null||alias.provider is null||alias.targetContext!="Default"||string.IsNullOrEmpty(alias.requestedIdentity))return false;
  var p=alias.provider;
  if(p.role!="framework"||!Path.IsPathFullyQualified(p.path)||p.sha256 is null||p.sha256.Length!=64||p.sha256.Any(c=>!((c>='0'&&c<='9')||(c>='a'&&c<='f'))))return false;
  try
  {
   var provider=new AssemblyName(p.identity);var requested=new AssemblyName(alias.requestedIdentity);
   if(provider.FullName!=p.identity||requested.FullName!=alias.requestedIdentity||provider.Version!=new Version(10,0,0,0)||requested.Version!=new Version(8,0,0,0)||string.IsNullOrEmpty(provider.Name)||provider.Name.IndexOfAny(Path.GetInvalidFileNameChars())>=0)return false;
   // Changing Version on a parsed complete provider identity retains its exact
   // name/culture/token and flags. No simple-name or version-range matching.
   provider.Version=new Version(8,0,0,0);
   if(provider.FullName!=alias.requestedIdentity)return false;
   return string.Equals(Path.GetDirectoryName(Full(p.path)),Full(FrameworkAliasRoot),StringComparison.OrdinalIgnoreCase)&&Same(p.path,Path.Combine(FrameworkAliasRoot,provider.Name+".dll"));
  }
  catch(ArgumentException){return false;}
  catch(FileLoadException){return false;}
 }
 void ValidateFrameworkAliasTable()
 {
  var aliases=policy.exactAliases;
  if(aliases is null||aliases.Length<1||aliases.Length>16)throw Refuse("AUTHORITY_FRAMEWORK_ALIAS_TABLE_SIZE");
  if(policy.contexts.Length!=3||!policy.contexts.All(IsAliasScope)||!scopes.ContainsKey("ConstructorContext")||policy.contexts.Count(c=>c.registrationType=="Harness+ProductContext")!=1)throw Refuse("AUTHORITY_ALIAS_CONTEXT_SET_MISMATCH");
  var names=new HashSet<string>(StringComparer.Ordinal);var providerIdentities=new HashSet<string>(StringComparer.Ordinal);var providerPaths=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
  foreach(var alias in aliases)
  {
   if(!IsExactFrameworkAliasProvider(alias))throw Refuse("AUTHORITY_FRAMEWORK_ALIAS_DECLARATION_MISMATCH");
   if(!names.Add(alias.requestedIdentity)||!providerIdentities.Add(alias.provider.identity)||!providerPaths.Add(Full(alias.provider.path)))throw Refuse("AUTHORITY_FRAMEWORK_ALIAS_DUPLICATE");
   if(alias.requestingContexts is null||alias.requestingContexts.Length!=3||alias.requestingContexts.Distinct(StringComparer.Ordinal).Count()!=3||!policy.contexts.All(c=>alias.requestingContexts.Contains(c.id,StringComparer.Ordinal)))throw Refuse("AUTHORITY_ALIAS_CONTEXT_SET_MISMATCH");
   if(!paths["Default"].TryGetValue(Full(alias.provider.path),out var pin)||pin.identity!=alias.provider.identity||pin.sha256!=alias.provider.sha256||pin.role!="framework"||!Same(pin.path,alias.provider.path))throw Refuse("AUTHORITY_FRAMEWORK_ALIAS_PROVIDER_NOT_DEFAULT_PIN");
   foreach(var context in policy.contexts)
   {
    if(!requests[context.id].TryGetValue(alias.requestedIdentity,out var request)||request.targetContext!="Default"||!Path.IsPathFullyQualified(request.targetPath)||!Same(request.targetPath,alias.provider.path))throw Refuse("AUTHORITY_ALIAS_EXACT_ROUTE_REQUIRED");
    if(context.id!="Default"&&context.managed.Any(p=>string.Equals(new AssemblyName(p.identity).Name,new AssemblyName(alias.provider.identity).Name,StringComparison.OrdinalIgnoreCase)))throw Refuse("AUTHORITY_ALIAS_CUSTOM_PROVIDER_FORBIDDEN");
   }
  }
 }
 void ValidateFrameworkAliasBinding(string requester,string target,string path,string requested,AuthorityAssembly pin,string refusal)
 {
  ValidateFrameworkAliasTable();
  var alias=policy.exactAliases!.SingleOrDefault(a=>a.requestedIdentity==requested);
  if(alias is null||target!="Default"||!alias.requestingContexts.Contains(requester,StringComparer.Ordinal)||!Path.IsPathFullyQualified(path)||!Same(path,alias.provider.path)||pin.identity!=alias.provider.identity||pin.sha256!=alias.provider.sha256||pin.role!="framework"||!Same(pin.path,alias.provider.path))throw Refuse(refusal);
 }
 // Schema5 accepts only explicitly declared complete lower-version pairs.
 bool SameFrameworkProviderPin(AuthorityAssembly a,AuthorityAssembly b)=>a.identity==b.identity&&a.sha256==b.sha256&&a.role==b.role&&Same(a.path,b.path);
 bool IsExactFrameworkVersionPair(ExactInteropAlias alias)
 {
  if(alias is null||alias.provider is null||alias.targetContext!="Default"||string.IsNullOrEmpty(alias.requestedIdentity))return false;
  var p=alias.provider;
  if(p.role!="framework"||!Path.IsPathFullyQualified(p.path)||p.sha256 is null||p.sha256.Length!=64||p.sha256.Any(c=>!((c>='0'&&c<='9')||(c>='a'&&c<='f'))))return false;
  try
  {
   var provider=new AssemblyName(p.identity);var requested=new AssemblyName(alias.requestedIdentity);var version=requested.Version;
   bool exactNetstandardFacade=p.identity=="netstandard, Version=2.1.0.0, Culture=neutral, PublicKeyToken=cc7b13ffcd2ddd51"&&alias.requestedIdentity=="netstandard, Version=2.0.0.0, Culture=neutral, PublicKeyToken=cc7b13ffcd2ddd51"&&p.sha256=="ce09cef0d758196f2467f320d840c03202453efe9fb1920dddd24ea8277bbbb3"&&Same(p.path,Path.Combine(FrameworkAliasRoot,"netstandard.dll"));
   if(provider.FullName!=p.identity||requested.FullName!=alias.requestedIdentity||(string.Equals(provider.Name,"netstandard",StringComparison.OrdinalIgnoreCase)?!exactNetstandardFacade:provider.Version!=new Version(10,0,0,0))||version is null||version.Major<0||version.Minor<0||version.Build<0||version.Revision<0||version.Major>65535||version.Minor>65535||version.Build>65535||version.Revision>65535||version.CompareTo(provider.Version)>=0||string.IsNullOrEmpty(provider.Name)||provider.Name.IndexOfAny(Path.GetInvalidFileNameChars())>=0)return false;
   // Version ordering only bounds a declaration. Resolution below still looks
   // up requested.FullName exactly; it never searches a version interval.
   provider.Version=version;
   if(provider.FullName!=alias.requestedIdentity)return false;
   return string.Equals(Path.GetDirectoryName(Full(p.path)),Full(FrameworkAliasRoot),StringComparison.OrdinalIgnoreCase)&&Same(p.path,Path.Combine(FrameworkAliasRoot,provider.Name+".dll"));
  }
  catch(ArgumentException){return false;}
  catch(FileLoadException){return false;}
 }
 void ValidateFrameworkVersionPairs()
 {
  var aliases=policy.exactAliases;
  if(aliases is null||aliases.Length<1||aliases.Length>16)throw Refuse("AUTHORITY_FRAMEWORK_ALIAS_TABLE_SIZE");
  if(policy.contexts.Length!=3||!policy.contexts.All(IsAliasScope)||!scopes.ContainsKey("ConstructorContext")||policy.contexts.Count(c=>c.registrationType=="Harness+ProductContext")!=1)throw Refuse("AUTHORITY_ALIAS_CONTEXT_SET_MISMATCH");
  var requestsSeen=new HashSet<string>(StringComparer.Ordinal);var providers=new Dictionary<string,AuthorityAssembly>(StringComparer.Ordinal);var providerPaths=new Dictionary<string,AuthorityAssembly>(StringComparer.OrdinalIgnoreCase);
  foreach(var alias in aliases)
  {
   if(!IsExactFrameworkVersionPair(alias))throw Refuse("AUTHORITY_FRAMEWORK_VERSION_PAIR_DECLARATION_MISMATCH");
   if(!requestsSeen.Add(alias.requestedIdentity))throw Refuse("AUTHORITY_FRAMEWORK_VERSION_PAIR_DUPLICATE_REQUEST");
   if((providers.TryGetValue(alias.provider.identity,out var sameIdentity)&&!SameFrameworkProviderPin(sameIdentity,alias.provider))||(providerPaths.TryGetValue(Full(alias.provider.path),out var samePath)&&!SameFrameworkProviderPin(samePath,alias.provider)))throw Refuse("AUTHORITY_FRAMEWORK_VERSION_PAIR_PROVIDER_CONFLICT");
   providers[alias.provider.identity]=alias.provider;providerPaths[Full(alias.provider.path)]=alias.provider;
   if(alias.requestingContexts is null||alias.requestingContexts.Length!=3||alias.requestingContexts.Distinct(StringComparer.Ordinal).Count()!=3||!policy.contexts.All(c=>alias.requestingContexts.Contains(c.id,StringComparer.Ordinal)))throw Refuse("AUTHORITY_ALIAS_CONTEXT_SET_MISMATCH");
   if(!paths["Default"].TryGetValue(Full(alias.provider.path),out var pin)||!SameFrameworkProviderPin(pin,alias.provider)||pin.role!="framework")throw Refuse("AUTHORITY_FRAMEWORK_ALIAS_PROVIDER_NOT_DEFAULT_PIN");
   foreach(var context in policy.contexts)
   {
    if(!requests[context.id].TryGetValue(alias.requestedIdentity,out var request)||request.targetContext!="Default"||!Path.IsPathFullyQualified(request.targetPath)||!Same(request.targetPath,alias.provider.path))throw Refuse("AUTHORITY_ALIAS_EXACT_ROUTE_REQUIRED");
    if(context.id!="Default"&&context.managed.Any(p=>string.Equals(new AssemblyName(p.identity).Name,new AssemblyName(alias.provider.identity).Name,StringComparison.OrdinalIgnoreCase)))throw Refuse("AUTHORITY_ALIAS_CUSTOM_PROVIDER_FORBIDDEN");
   }
  }
 }
 void ValidateFrameworkVersionPairBinding(string requester,string target,string path,string requested,AuthorityAssembly pin,string refusal)
 {
  ValidateFrameworkVersionPairs();
  var alias=policy.exactAliases!.SingleOrDefault(a=>a.requestedIdentity==requested);
  if(alias is null||target!="Default"||!alias.requestingContexts.Contains(requester,StringComparer.Ordinal)||!Path.IsPathFullyQualified(path)||!Same(path,alias.provider.path)||!SameFrameworkProviderPin(pin,alias.provider)||pin.role!="framework")throw Refuse(refusal);
 }
 // Schema6 is a separate joined producer profile with only the real Default ALC.
 void ValidateJoinedDefaultVersionPairs()
 {
  var aliases=policy.exactAliases;
  if(aliases is null||aliases.Length<1||aliases.Length>16)throw Refuse("AUTHORITY_FRAMEWORK_ALIAS_TABLE_SIZE");
  if(policy.contexts.Length!=1||policy.contexts[0].id!="Default"||policy.contexts[0].kind!="default"||policy.contexts[0].registrationType is not null||policy.contexts[0].registrationAssembly is not null||!paths["Default"].TryGetValue(Full(policy.entry),out var entry)||entry.identity!="JoinedFactoryProbe, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null"||entry.role!="tool")throw Refuse("AUTHORITY_JOINED_DEFAULT_POLICY_SHAPE");
  var requestsSeen=new HashSet<string>(StringComparer.Ordinal);var providers=new Dictionary<string,AuthorityAssembly>(StringComparer.Ordinal);var providerPaths=new Dictionary<string,AuthorityAssembly>(StringComparer.OrdinalIgnoreCase);
  bool configuredSinkSeen=false;
  foreach(var alias in aliases)
  {
   if(IsConfiguredFileSinkAlias(alias)){if(configuredSinkSeen||!requestsSeen.Add(alias.requestedIdentity))throw Refuse("AUTHORITY_CONFIGURED_SINK_DUPLICATE");configuredSinkSeen=true;ValidateConfiguredFileSinkAlias(alias);continue;}
   if(!IsExactFrameworkVersionPair(alias))throw Refuse("AUTHORITY_FRAMEWORK_VERSION_PAIR_DECLARATION_MISMATCH");
   if(!requestsSeen.Add(alias.requestedIdentity))throw Refuse("AUTHORITY_FRAMEWORK_VERSION_PAIR_DUPLICATE_REQUEST");
   if((providers.TryGetValue(alias.provider.identity,out var sameIdentity)&&!SameFrameworkProviderPin(sameIdentity,alias.provider))||(providerPaths.TryGetValue(Full(alias.provider.path),out var samePath)&&!SameFrameworkProviderPin(samePath,alias.provider)))throw Refuse("AUTHORITY_FRAMEWORK_VERSION_PAIR_PROVIDER_CONFLICT");
   providers[alias.provider.identity]=alias.provider;providerPaths[Full(alias.provider.path)]=alias.provider;
   if(alias.requestingContexts is null||alias.requestingContexts.Length!=1||alias.requestingContexts[0]!="Default")throw Refuse("AUTHORITY_JOINED_DEFAULT_CONTEXT_SET_MISMATCH");
   if(!paths["Default"].TryGetValue(Full(alias.provider.path),out var pin)||!SameFrameworkProviderPin(pin,alias.provider)||pin.role!="framework")throw Refuse("AUTHORITY_FRAMEWORK_ALIAS_PROVIDER_NOT_DEFAULT_PIN");
   if(!requests["Default"].TryGetValue(alias.requestedIdentity,out var request)||request.targetContext!="Default"||!Path.IsPathFullyQualified(request.targetPath)||!Same(request.targetPath,alias.provider.path))throw Refuse("AUTHORITY_ALIAS_EXACT_ROUTE_REQUIRED");
  }
 }
 void ValidateJoinedDefaultVersionPairBinding(string requester,string target,string path,string requested,AuthorityAssembly pin,string refusal)
 {
  ValidateJoinedDefaultVersionPairs();
  if(requester!="Default"||target!="Default"||!Path.IsPathFullyQualified(path)||!paths["Default"].TryGetValue(Full(path),out var approved)||!SameFrameworkProviderPin(approved,pin))throw Refuse(refusal);
  if(requested==pin.identity)return; // Existing exact target/infrastructure pins stay exact.
  var alias=policy.exactAliases!.SingleOrDefault(a=>a.requestedIdentity==requested);
  if(alias is null||!Same(path,alias.provider.path)||!SameFrameworkProviderPin(pin,alias.provider)||(pin.role!="framework"&&!IsConfiguredFileSinkAlias(alias)))throw Refuse(refusal);
 }
 // Both preparation and requested-path admission use this same full-pin check.
 void ValidateRequestBinding(string requester,string target,string path,string requested,AuthorityAssembly pin,string refusal)
 {
  if(policy.schemaVersion==6){ValidateJoinedDefaultVersionPairBinding(requester,target,path,requested,pin,refusal);return;}
  if(requested==pin.identity)return; // Original schema2 exact identity behavior.
  if(policy.schemaVersion==5){ValidateFrameworkVersionPairBinding(requester,target,path,requested,pin,refusal);return;}
  if(policy.schemaVersion==4){ValidateFrameworkAliasBinding(requester,target,path,requested,pin,refusal);return;}
  if(policy.schemaVersion!=3)throw Refuse(refusal);
  ValidateAliasDeclaration();
  var a=policy.exactAliases![0];
  if(requested!=Interop8||target!="Default"||!a.requestingContexts.Contains(requester,StringComparer.Ordinal)||!Path.IsPathFullyQualified(path)||!Same(path,Interop10Path)||!IsExactInteropProvider(pin))throw Refuse(refusal);
 }
 AuthorityAssembly PathPin(AssemblyLoadContext context,string path,AssemblyName? requested,string? requester=null)
 {
  var scope=Scope(context);if(!paths[scope].TryGetValue(Full(path),out var p))throw Refuse("AUTHORITY_PATH_NOT_APPROVED_IN_CONTEXT");if(requested is not null)ValidateRequestBinding(requester??scope,scope,path,Identity(requested),p,"AUTHORITY_REQUEST_IDENTITY_MISMATCH");return p;
 }
 public Assembly LoadPath(AssemblyLoadContext context,string path)=>LoadPin(context,PathPin(context,path,null));
 public Assembly LoadRequestedPath(AssemblyLoadContext context,AssemblyName requested,string path)
 {
  var scope=Scope(context);if(!requests[scope].TryGetValue(Identity(requested),out var binding))throw Refuse("AUTHORITY_REQUEST_NOT_APPROVED");if(binding.targetContext!=scope||!Same(binding.targetPath,path))throw Refuse("AUTHORITY_REQUEST_CONTEXT_OR_PATH_MISMATCH");return LoadPin(context,PathPin(context,path,requested,scope));
 }
 public Assembly ResolveOrReject(AssemblyLoadContext context,AssemblyName requested)
 {
  var scope=Scope(context);if(policy.schemaVersion==6)Events.Add("resolve-request-exact:"+scope+":"+Identity(requested));if(!requests[scope].TryGetValue(Identity(requested),out var binding))throw Refuse("AUTHORITY_REQUEST_NOT_APPROVED");
  var target=binding.targetContext==scope?context:AssemblyLoadContext.Default;return LoadPin(target,PathPin(target,binding.targetPath,requested,scope));
 }
 public Assembly RequireExistingExport(AssemblyLoadContext requester,AssemblyName requested,Assembly candidate)
 {
  var scope=Scope(requester);if(!requests[scope].TryGetValue(Identity(requested),out var b)||b.targetContext!="Default"||candidate.IsDynamic||!Same(candidate.Location,b.targetPath)||(policy.schemaVersion==2&&candidate.FullName!=b.requestedIdentity)||AssemblyLoadContext.GetLoadContext(candidate)!=AssemblyLoadContext.Default)throw Refuse("AUTHORITY_EXISTING_EXPORT_NOT_APPROVED");var p=PathPin(AssemblyLoadContext.Default,b.targetPath,requested,scope);if(candidate.FullName!=p.identity)throw Refuse("AUTHORITY_EXISTING_EXPORT_NOT_APPROVED");Verify(p);Events.Add("existing-export-verified:"+scope+":"+p.identity);return candidate;
 }
 void ReviewPreloadBinding(AssemblyLoadContext context,AuthorityAssembly pin)
 {
  var simple=new AssemblyName(pin.identity).Name!;
  // Review the receiving ALC only. Equal simple names never merge ALC owners.
  foreach(var cached in context.Assemblies.Where(a=>string.Equals(a.GetName().Name,simple,StringComparison.OrdinalIgnoreCase)))
  {
   if(cached.IsDynamic||string.IsNullOrEmpty(cached.Location)||AssemblyLoadContext.GetLoadContext(cached)!=context)
    throw Refuse("AUTHORITY_CACHE_IDENTITY_OR_PATH_CONFLICT");
   Plain(cached.Location);
   using var handle=new FileStream(Full(cached.Location),FileMode.Open,FileAccess.Read,FileShare.Read);
   var sha=Convert.ToHexString(SHA256.HashData(handle)).ToLowerInvariant();
   var disk=AssemblyName.GetAssemblyName(cached.Location).FullName;
   Events.Add("preload-cache-candidate:"+cached.FullName+":"+sha+":"+Full(cached.Location));
   if(cached.FullName!=pin.identity||disk!=pin.identity||!Same(cached.Location,pin.path)||sha!=pin.sha256)
    throw Refuse("AUTHORITY_CACHE_IDENTITY_OR_PATH_CONFLICT");
  }
  if(!ReferenceEquals(context,AssemblyLoadContext.Default))return;
  if(AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES") is not string tpa)
   throw Refuse("AUTHORITY_DEFAULT_TPA_UNAVAILABLE");
  // File names select possible runtime binding slots; they never approve bytes.
  // Inspect metadata/hash only for those slots and the exact requested path.
  var candidates=tpa.Split(Path.PathSeparator,StringSplitOptions.RemoveEmptyEntries)
   .Where(p=>string.Equals(Path.GetFileNameWithoutExtension(p),simple,StringComparison.OrdinalIgnoreCase)||
    string.Equals(Path.GetFileNameWithoutExtension(p),simple+".ni",StringComparison.OrdinalIgnoreCase)||Same(p,pin.path))
   .Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
  foreach(var path in candidates)
  {
   if(!Path.IsPathFullyQualified(path))throw Refuse("AUTHORITY_DEFAULT_TPA_PATH_NOT_ABSOLUTE");
   Plain(path);
   using var handle=new FileStream(Full(path),FileMode.Open,FileAccess.Read,FileShare.Read);
   var sha=Convert.ToHexString(SHA256.HashData(handle)).ToLowerInvariant();
   var disk=AssemblyName.GetAssemblyName(path);
   Events.Add("preload-tpa-candidate:"+disk.FullName+":"+sha+":"+Full(path));
   if(!string.Equals(disk.Name,simple,StringComparison.OrdinalIgnoreCase)||disk.FullName!=pin.identity)
    throw Refuse("AUTHORITY_DEFAULT_TPA_IDENTITY_CONFLICT");
   if(!Same(path,pin.path))throw Refuse("AUTHORITY_DEFAULT_TPA_PATH_CONFLICT");
   if(sha!=pin.sha256)throw Refuse("AUTHORITY_DEFAULT_TPA_SHA_CONFLICT");
  }
  Events.Add("default-tpa-preload-reviewed:"+pin.identity+":"+candidates.Length);
 }
 Assembly LoadPin(AssemblyLoadContext context,AuthorityAssembly pin)
 {
  var scope=Scope(context);Plain(pin.path);
  // Hold a non-writable/non-deletable read handle from verification through CLR
  // loading, preserving Assembly.Location (LoadFromStream would change Guard).
  using var bytes=new FileStream(Full(pin.path),FileMode.Open,FileAccess.Read,FileShare.Read);
  var digest=Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();if(digest!=pin.sha256)throw Refuse("AUTHORITY_SHA_MISMATCH");if(AssemblyName.GetAssemblyName(pin.path).FullName!=pin.identity)throw Refuse("AUTHORITY_DISK_IDENTITY_MISMATCH");
  ReviewPreloadBinding(context,pin);
  Events.Add("gate-verified-before-clr-load:"+scope+":"+pin.identity+":"+Full(pin.path));
  var result=context.LoadFromAssemblyPath(Full(pin.path));
  if(result.FullName!=pin.identity||!Same(result.Location,pin.path)||AssemblyLoadContext.GetLoadContext(result)!=context)throw Refuse("AUTHORITY_CLR_RESULT_MISMATCH");
  bytes.Position=0;if(Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant()!=pin.sha256)throw Refuse("AUTHORITY_LOADED_FILE_CHANGED");return result;
 }
 public void RequireProducerEntry(Assembly entry)
 {
  if(!prepared||!attached)throw Refuse("AUTHORITY_PRODUCER_NOT_PREPARED_AND_ATTACHED");
  if(entry.IsDynamic||string.IsNullOrEmpty(entry.Location)||!Same(entry.Location,policy.entry)||AssemblyLoadContext.GetLoadContext(entry)!=AssemblyLoadContext.Default)throw Refuse("AUTHORITY_PRODUCER_ENTRY_CONTEXT_OR_PATH");
  var pin=PathPin(AssemblyLoadContext.Default,entry.Location,entry.GetName());Verify(pin);
  Events.Add("producer-entry-approved-before-business:"+entry.FullName+":"+Full(entry.Location));
 }
 public void AttachDefaultFallback()
 {
  if(!prepared||attached)throw Refuse("AUTHORITY_ATTACH_ORDER");AssemblyLoadContext.Default.Resolving+=ResolveDefault;attached=true;Events.Add("default-fallback-additional-guard-only");
 }
 Assembly? ResolveDefault(AssemblyLoadContext c,AssemblyName n)=>ResolveExpectedAbsentOrReject(c,n);
 public void VerifyAfter(){foreach(var p in policy.contexts.SelectMany(c=>c.managed))Verify(p);foreach(var p in policy.metadata)if(Hash(p.path)!=p.sha256)throw Refuse("AUTHORITY_METADATA_CHANGED");Events.Add("after-hashes-verified");}

 // One reviewed configured File sink request; no name search, target-version range or directory fallback.
 const string ConfiguredFileSinkRequest="Serilog.Sinks.File, Culture=neutral, PublicKeyToken=null";
 const string ConfiguredFileSinkPlainRequest="Serilog.Sinks.File";
 const string ConfiguredFileSinkProvider="Serilog.Sinks.File, Version=7.0.0.0, Culture=neutral, PublicKeyToken=24c2f752a8e58a10";
 bool IsConfiguredFileSinkAlias(ExactInteropAlias a)=>a is not null&&(a.requestedIdentity==ConfiguredFileSinkRequest||a.requestedIdentity==ConfiguredFileSinkPlainRequest)&&a.targetContext=="Default"&&a.provider is not null&&a.provider.identity==ConfiguredFileSinkProvider&&a.provider.role=="target";
 void ValidateConfiguredFileSinkAlias(ExactInteropAlias alias)
 {
  if(!IsConfiguredFileSinkAlias(alias)||policy.schemaVersion!=6||alias.requestingContexts is null||alias.requestingContexts.Length!=1||alias.requestingContexts[0]!="Default")throw Refuse("AUTHORITY_CONFIGURED_SINK_SHAPE");
  if(!Path.IsPathFullyQualified(alias.provider.path)||!paths["Default"].TryGetValue(Full(alias.provider.path),out var pin)||!SameFrameworkProviderPin(pin,alias.provider)||pin.role!="target")throw Refuse("AUTHORITY_CONFIGURED_SINK_PROVIDER_NOT_DEFAULT_PIN");
  if(!requests["Default"].TryGetValue(alias.requestedIdentity,out var request)||request.targetContext!="Default"||!Path.IsPathFullyQualified(request.targetPath)||!Same(request.targetPath,alias.provider.path))throw Refuse("AUTHORITY_CONFIGURED_SINK_EXACT_ROUTE_REQUIRED");
  if(!Path.IsPathFullyQualified(policy.ownedRoot))throw Refuse("AUTHORITY_CONFIGURED_SINK_CONFIG_PIN_REQUIRED");
  var configPath=Path.Combine(Full(policy.ownedRoot),"inputs","YeManFanHost.json");AuthorityFile? config=null;
  foreach(var p in policy.metadata)if(Same(p.path,configPath)){if(config is not null)throw Refuse("AUTHORITY_CONFIGURED_SINK_CONFIG_PIN_REQUIRED");config=p;}
  if(config is null)throw Refuse("AUTHORITY_CONFIGURED_SINK_CONFIG_PIN_REQUIRED");Plain(configPath);if(Hash(configPath)!=config.sha256)throw Refuse("AUTHORITY_CONFIGURED_SINK_CONFIG_SHA_MISMATCH");
  try
  {
   using var document=JsonDocument.Parse(File.ReadAllText(configPath));var r=document.RootElement;
   if(r.ValueKind!=JsonValueKind.Object||!r.TryGetProperty("Serilog",out var serilog)||serilog.ValueKind!=JsonValueKind.Object||!serilog.TryGetProperty("Using",out var uses)||uses.ValueKind!=JsonValueKind.Array||uses.GetArrayLength()!=1||uses[0].ValueKind!=JsonValueKind.String||uses[0].GetString()!="Serilog.Sinks.File"||!serilog.TryGetProperty("WriteTo",out var sinks)||sinks.ValueKind!=JsonValueKind.Array||sinks.GetArrayLength()!=1||sinks[0].ValueKind!=JsonValueKind.Object||!sinks[0].TryGetProperty("Name",out var name)||name.ValueKind!=JsonValueKind.String||name.GetString()!="File")throw Refuse("AUTHORITY_CONFIGURED_SINK_CONFIG_NOT_EXACT_FILE");
  }
  catch(JsonException){throw Refuse("AUTHORITY_CONFIGURED_SINK_CONFIG_INVALID");}
 }
}
public static class ToolLoadAuthority
{
 static ScopedLoadAuthority? current;
 public static ScopedLoadAuthority Current=>current??throw new InvalidOperationException("AUTHORITY_NOT_BOUND_BEFORE_TOOL_ENTRY");
 public static void Bind(ScopedLoadAuthority a){if(current is not null)throw new InvalidOperationException("AUTHORITY_ALREADY_BOUND");current=a;}
}

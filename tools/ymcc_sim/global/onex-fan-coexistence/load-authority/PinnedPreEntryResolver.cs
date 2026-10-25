using System.Reflection;using System.Runtime.Loader;
// Compatibility-shaped launcher facade. Primary path/request gates do the work;
// the Default.Resolving subscription remains only an extra fallback guard.
public sealed class PinnedPreEntryResolver
{
 readonly ScopedLoadAuthority authority;
 public PinnedPreEntryResolver(ScopedEntryPolicy policy){authority=new ScopedLoadAuthority(policy);}
 public void Prepare()=>authority.Prepare();
 public void Attach(){ToolLoadAuthority.Bind(authority);authority.AttachDefaultFallback();}
 public Assembly LoadApprovedPath(string path)=>authority.LoadPath(AssemblyLoadContext.Default,path);
 public Assembly LoadDefaultRequest(AssemblyName request)=>authority.ResolveOrReject(AssemblyLoadContext.Default,request);
 public void VerifyAfter()=>authority.VerifyAfter();
 public ScopedLoadAuthority Authority=>authority;
}

using System;
using System.Linq;
using System.Reflection;
using System.Runtime.Loader;

// Explicit tool calls only. This does not intercept Default static/TPA binding.
public static class ToolDefaultLoads
{
 public static Assembly Path(string path)=>ToolLoadAuthority.Current.LoadPath(AssemblyLoadContext.Default,path);
 public static Assembly Request(AssemblyName requested)=>ToolLoadAuthority.Current.ResolveOrReject(AssemblyLoadContext.Default,requested);
 public static Assembly ApprovedSimpleName(string name)
 {
  // A simple-name call must select exactly one policy-bound COMPLETE identity.
  // Never pass the partial name to the CLR or infer its version from loaded code.
  var current=ToolLoadAuthority.Current;
  var matches=current.Policy.contexts.Single(c=>c.id=="Default").requests
   .Where(r=>new AssemblyName(r.requestedIdentity).Name==name).ToArray();
  if(matches.Length!=1)throw new InvalidOperationException("AUTHORITY_SIMPLE_NAME_NOT_UNIQUE_OR_APPROVED");
  return current.ResolveOrReject(AssemblyLoadContext.Default,new AssemblyName(matches[0].requestedIdentity));
 }
}

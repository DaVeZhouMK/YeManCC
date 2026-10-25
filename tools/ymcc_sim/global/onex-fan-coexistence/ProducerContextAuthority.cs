using System;
using System.IO;
using System.Runtime.Loader;
// The scope is the producer type AND canonical input root, never the ALC name.
public static class ProducerContextAuthority
{
 public static string ScopeId(string producerKind,string root)=>producerKind+"|"+Path.TrimEndingDirectorySeparator(Path.GetFullPath(root)).ToUpperInvariant();
 public static void Register(string producerKind,string root,AssemblyLoadContext context)=>ToolLoadAuthority.Current.RegisterKnownContext(ScopeId(producerKind,root),context);
}

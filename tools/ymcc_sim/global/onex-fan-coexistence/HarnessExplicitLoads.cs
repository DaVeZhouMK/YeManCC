using System.Reflection;

static partial class Harness
{
 static Assembly LoadBootstrapAssembly(string path){var assembly=ToolDefaultLoads.Path(path);Deny(assembly);return assembly;}
 static Assembly LoadLockedNewtonsoft()=>ToolDefaultLoads.ApprovedSimpleName("Newtonsoft.Json");
}

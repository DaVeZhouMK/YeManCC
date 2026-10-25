using System.Reflection;

public static partial class MsiLifecycleIoPorts
{
 static Assembly LoadHidAssembly(AssemblyName requested)=>ToolDefaultLoads.Request(requested);
}

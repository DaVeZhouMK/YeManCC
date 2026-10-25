using System;
using System.Reflection;
using System.Runtime.CompilerServices;
static class JoinedFactoryEntry
{
 [STAThread] static int Main(string[] args)
 {
  try { ToolLoadAuthority.Current.RequireProducerEntry(Assembly.GetExecutingAssembly()); }
  catch(Exception error){Console.Error.WriteLine("TOOL_ENTRY_POLICY_REQUIRED:"+error.Message);return 253;}
  return Invoke(args);
 }
 [MethodImpl(MethodImplOptions.NoInlining)] static int Invoke(string[] args)=>JoinedFactoryProbe.Main(args);
}

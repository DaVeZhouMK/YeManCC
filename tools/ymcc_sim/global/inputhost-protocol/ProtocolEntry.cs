using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text.Json;
internal static class InputHostProtocolEntry
{
    private static int _nativeAttempts;
    private static readonly List<string> _prohibitedAssemblies = new();
    internal static int OriginalFixture(string resultPath)
    {
        var entry=typeof(OptionalTriggerFixture).GetMethod("Main",BindingFlags.Static|BindingFlags.NonPublic)??throw new MissingMethodException("Original fixture Main");
        return (int)(entry.Invoke(null,new object[]{new[]{resultPath}})??2);
    }
    internal static int Run(string resultPath,Func<int> body)
    {
        if(!Path.IsPathFullyQualified(resultPath)) {Console.Error.WriteLine("PROTOCOL_RESULT_PATH_REQUIRED");return 2;}
        NativeLibrary.SetDllImportResolver(Assembly.GetExecutingAssembly(),(name,assembly,path)=> { _nativeAttempts++; throw new DllNotFoundException("INPUTHOST_FIXTURE_NATIVE_IMPORT_DENIED:"+name); });
        AppDomain.CurrentDomain.AssemblyLoad += (_,e)=> { var name=e.LoadedAssembly.GetName().Name??"";if(name.StartsWith("HIDMaestro",StringComparison.Ordinal)||name.StartsWith("HandheldCompanion",StringComparison.Ordinal)||name.StartsWith("Nefarius",StringComparison.Ordinal)||name=="YeManInputHost")_prohibitedAssemblies.Add(name);};
        var code=2;
        try { code=body(); }
        catch(Exception error) {Console.Error.WriteLine(error is TargetInvocationException target ? target.InnerException : error);code=1;}
        finally {System.IO.File.WriteAllText(Path.Combine(Path.GetDirectoryName(resultPath)!,"boundary-report.json"),JsonSerializer.Serialize(new {nativeAttempts=_nativeAttempts,prohibitedAssemblies=_prohibitedAssemblies,benchmarkInvoked=false,hostEntryInvoked=false,sdkOrHardwareInvoked=false},new JsonSerializerOptions{WriteIndented=true}));}
        return _nativeAttempts==0&&_prohibitedAssemblies.Count==0?code:3;
    }
    private static int Main(string[] args)=>args.Length==1?Run(args[0],()=>OriginalFixture(args[0])):2;
}

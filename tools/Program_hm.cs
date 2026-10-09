using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
class Program
{
    static void Main()
    {
        var path = @"C:\SOFT\YeMan\PowerControl\feature-assets\virtual-gamepad\HIDMaestro.Core.dll";
        var dir = Path.GetDirectoryName(path);
        var paths = new[] { dir, RuntimeEnvironment.GetRuntimeDirectory() };
        var resolver = new PathAssemblyResolver(paths.SelectMany(p => Directory.Exists(p) ? Directory.GetFiles(p, "*.dll") : Array.Empty<string>()));
        using var mlc = new MetadataLoadContext(resolver);
        var asm = mlc.LoadFromAssemblyPath(path);
        Console.WriteLine("== Profile types ==");
        foreach (var t in asm.GetTypes().Where(t => t.FullName != null && (t.FullName.Contains("HMProfile") || t.FullName.Contains("ProfileBuilder"))).OrderBy(t=>t.FullName))
            Console.WriteLine(t.FullName + " [public=" + t.IsPublic + "]");
        Console.WriteLine("== Methods with 'Profile' in name on context types ==");
        foreach (var t in asm.GetTypes().Where(t => t.FullName != null && (t.FullName.Contains("HIDMaestroContext") || t.FullName.Contains("Context"))))
            foreach (var m in t.GetMethods(BindingFlags.Public|BindingFlags.Instance|BindingFlags.Static).Where(m=>m.Name.Contains("Profile")||m.Name.Contains("Create")||m.Name.Contains("Install")))
                Console.WriteLine(t.Name + "." + m.Name + "(" + string.Join(",", m.GetParameters().Select(p=>p.ParameterType.Name+" "+p.Name)) + ")");
    }
}

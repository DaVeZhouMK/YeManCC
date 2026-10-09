using System.Reflection;
using System.Runtime.Loader;
using System.Text.Json;

if (args.Length != 1) throw new ArgumentException("HIDMaestro.Core.dll path required");
var path = Path.GetFullPath(args[0]);
var root = Path.GetDirectoryName(path) ?? throw new InvalidOperationException("assembly directory missing");
AssemblyLoadContext.Default.Resolving += (_, name) => {
    var dependency = Path.Combine(root, name.Name + ".dll");
    return File.Exists(dependency) ? AssemblyLoadContext.Default.LoadFromAssemblyPath(dependency) : null;
};

var result = new Dictionary<string, object?> {
    ["assemblyPath"] = path,
    ["constructorsInvoked"] = false,
    ["staticMethodsInvoked"] = false,
    ["systemMutation"] = false,
};
try {
    var assembly = AssemblyLoadContext.Default.LoadFromAssemblyPath(path);
    var types = assembly.GetTypes()
        .Where(type => type.IsPublic)
        .OrderBy(type => type.FullName)
        .Select(type => new {
            type = type.FullName,
            constructors = type.GetConstructors(BindingFlags.Public | BindingFlags.Instance).Select(ctor => ctor.ToString()).ToArray(),
            properties = type.GetProperties(BindingFlags.Public | BindingFlags.Instance | BindingFlags.Static | BindingFlags.DeclaredOnly)
                .Select(property => property.ToString())
                .ToArray(),
            events = type.GetEvents(BindingFlags.Public | BindingFlags.Instance | BindingFlags.Static | BindingFlags.DeclaredOnly)
                .Select(@event => @event.ToString())
                .ToArray(),
            methods = type.GetMethods(BindingFlags.Public | BindingFlags.Instance | BindingFlags.Static | BindingFlags.DeclaredOnly)
                .Where(method => !method.IsSpecialName)
                .Select(method => new { signature = method.ToString(), isStatic = method.IsStatic })
                .ToArray(),
        }).ToArray();
    result["assembly"] = new { load = "succeeded", fullName = assembly.FullName, types };
} catch (ReflectionTypeLoadException ex) {
    result["assembly"] = new { load = "partial", loaderExceptions = ex.LoaderExceptions.Where(error => error is not null).Select(error => error!.ToString()).ToArray() };
} catch (Exception ex) {
    result["assembly"] = new { load = "failed", exception = ex.ToString() };
}
Console.WriteLine(JsonSerializer.Serialize(result, new JsonSerializerOptions { WriteIndented = true }));

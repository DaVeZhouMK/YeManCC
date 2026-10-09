using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Text.Json;

if (args.Length != 1) throw new ArgumentException("HIDMaestro.Core.dll path required");
var path = Path.GetFullPath(args[0]);
var root = Path.GetDirectoryName(path)!;
AssemblyLoadContext.Default.Resolving += (_, name) =>
{
    var dependency = Path.Combine(root, name.Name + ".dll");
    return File.Exists(dependency) ? AssemblyLoadContext.Default.LoadFromAssemblyPath(dependency) : null;
};
var assembly = AssemblyLoadContext.Default.LoadFromAssemblyPath(path);
var type = assembly.GetType("HIDMaestro.HMGamepadState") ?? throw new InvalidOperationException("HMGamepadState missing");
var fields = type.GetFields(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static)
    .Select(f => new { name = f.Name, type = f.FieldType.FullName, isPublic = f.IsPublic, isStatic = f.IsStatic, offset = TryOffset(f) })
    .ToArray();
var helper = assembly.GetType("HIDMaestro.HMGamepadStateHelpers");
var methods = helper?.GetMethods(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static)
    .Where(m => !m.IsSpecialName).Select(m => m.ToString()).ToArray() ?? Array.Empty<string>();
var enums = new[] { "HIDMaestro.HMButton", "HIDMaestro.HMHat", "HIDMaestro.HMAxis" }.Select(name => new { name, values = Enum.GetValues(assembly.GetType(name)!).Cast<object>().Select(value => new { name = value.ToString(), value = Convert.ToInt64(value) }) });
var controllerFields = assembly.GetType("HIDMaestro.HMController")!.GetFields(BindingFlags.NonPublic | BindingFlags.Public | BindingFlags.Instance).Select(field => new { field.Name, type = field.FieldType.FullName });
var controllerType = assembly.GetType("HIDMaestro.HMController")!;
var submitMethods = controllerType.GetMethods(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance)
    .Where(m => m.Name is "SubmitState" or "SubmitRawReport")
    .Select(m => new { name = m.Name, signature = m.ToString(), il = TryIl(m) })
    .ToArray();
var contextType = assembly.GetType("HIDMaestro.HMContext")!;
var contextMethods = contextType.GetMethods(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static | BindingFlags.Instance)
    .Where(m => !m.IsSpecialName)
    .Select(m => new { name = m.Name, isStatic = m.IsStatic, signature = m.ToString(), il = TryIl(m) })
    .ToArray();
var context = Activator.CreateInstance(contextType)!;
context.GetType().GetMethod("LoadDefaultProfiles")!.Invoke(context, null);
var contextProperties = context.GetType().GetProperties(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance)
    .Where(p => p.GetIndexParameters().Length == 0)
    .Select(p =>
    {
        try { return new { name = p.Name, type = p.PropertyType.FullName, value = p.GetValue(context) }; }
        catch (Exception ex) { return new { name = p.Name, type = p.PropertyType.FullName, value = (object?)$"<error:{ex.GetType().Name}>" }; }
    }).ToArray();
var profile = (context.GetType().GetMethod("GetProfile")!.Invoke(context, new object[] { "xbox-360-wired" }))!;
var standardAxesMethod = helper!.GetMethod("StandardAxes")!;
var standardAxes = new
{
    neutral = standardAxesMethod.Invoke(null, new[] { profile, 0f, 0f, 0f, 0f, 0f, 0f }),
    positive = standardAxesMethod.Invoke(null, new[] { profile, 1f, 1f, 1f, 1f, 1f, 1f }),
    negative = standardAxesMethod.Invoke(null, new[] { profile, -1f, -1f, -1f, -1f, -1f, -1f }),
};
var profileProperties = profile.GetType().GetProperties(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance)
    .Where(p => p.GetIndexParameters().Length == 0)
    .Select(p =>
    {
        try { return new { name = p.Name, type = p.PropertyType.FullName, value = p.GetValue(profile) }; }
        catch (Exception ex) { return new { name = p.Name, type = p.PropertyType.FullName, value = (object?)$"<error:{ex.GetType().Name}>" }; }
    }).ToArray();
Console.WriteLine(JsonSerializer.Serialize(new { assembly = assembly.FullName, type = type.FullName, size = TrySize(type), fields, helper = helper.FullName, methods, enums, contextProperties, contextMethods, controllerFields, submitMethods, profileProperties, standardAxes }, new JsonSerializerOptions { WriteIndented = true }));

static int? TrySize(Type type) { try { return Marshal.SizeOf(type); } catch { return null; } }
static int? TryOffset(FieldInfo field) { try { return (int)Marshal.OffsetOf(field.DeclaringType!, field.Name); } catch { return null; } }
static string? TryIl(MethodInfo method)
{
    try
    {
        var body = method.GetMethodBody();
        return body is null ? null : Convert.ToHexString(body.GetILAsByteArray() ?? Array.Empty<byte>());
    }
    catch { return null; }
}

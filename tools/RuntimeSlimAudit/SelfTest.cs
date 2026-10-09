using Mono.Cecil;
using Mono.Cecil.Cil;
using System.Diagnostics;
using System.Reflection;
using System.Text.Json;

internal static class AuditSelfTest
{
    internal static int Run(string output)
    {
        string root = Path.GetFullPath(output);
        if (Directory.Exists(root)) throw new IOException("Selftest output must be new; existing evidence is never overwritten.");
        Directory.CreateDirectory(root);
        string fixtures = Path.Combine(root, "fixtures"); Directory.CreateDirectory(fixtures);
        using (var sdk = AssemblyDefinition.CreateAssembly(new AssemblyNameDefinition("Microsoft.Windows.SDK.NET", new Version(1, 0)), "Microsoft.Windows.SDK.NET", ModuleKind.Dll))
        {
            sdk.MainModule.Types.Add(new TypeDefinition("Windows.Foundation", "Fixture", Mono.Cecil.TypeAttributes.Public, sdk.MainModule.TypeSystem.Object));
            sdk.Write(Path.Combine(fixtures, "Microsoft.Windows.SDK.NET.dll"));
        }
        using (var consumer = AssemblyDefinition.CreateAssembly(new AssemblyNameDefinition("Consumer", new Version(1, 0)), "Consumer", ModuleKind.Dll))
        {
            var m = consumer.MainModule;
            var scope = new AssemblyNameReference("Microsoft.Windows.SDK.NET", new Version(1, 0)); m.AssemblyReferences.Add(scope);
            var type = new TypeDefinition("Test", "FixtureConsumer", Mono.Cecil.TypeAttributes.Public, m.TypeSystem.Object); m.Types.Add(type);
            type.Fields.Add(new FieldDefinition("Projection", Mono.Cecil.FieldAttributes.Public, new TypeReference("Windows.Foundation", "Fixture", m, scope)));
            var dll = new ModuleReference("libVIIPER.dll"); m.ModuleReferences.Add(dll);
            type.Methods.Add(new MethodDefinition("Native", Mono.Cecil.MethodAttributes.Public | Mono.Cecil.MethodAttributes.Static | Mono.Cecil.MethodAttributes.PInvokeImpl, m.TypeSystem.Int32)
            { PInvokeInfo = new PInvokeInfo(PInvokeAttributes.CallConvCdecl, "fixture_entry", dll), ImplAttributes = Mono.Cecil.MethodImplAttributes.PreserveSig });
            var literal = new MethodDefinition("DynamicEvidence", Mono.Cecil.MethodAttributes.Public | Mono.Cecil.MethodAttributes.Static, m.TypeSystem.Void); type.Methods.Add(literal);
            var il = literal.Body.GetILProcessor(); il.Emit(OpCodes.Ldstr, "libVIIPER.dll"); il.Emit(OpCodes.Pop); il.Emit(OpCodes.Ret);
            consumer.Write(Path.Combine(fixtures, "Consumer.dll"));
        }
        string json = Path.Combine(root, "audit.json"), xml = Path.Combine(root, "roots.xml");
        var start = new ProcessStartInfo("dotnet") { UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true, RedirectStandardError = true };
        foreach (string arg in new[] { typeof(AuditSelfTest).Assembly.Location, fixtures, json, xml }) start.ArgumentList.Add(arg);
        using var process = Process.Start(start)!;
        var stdout = process.StandardOutput.ReadToEndAsync(); var stderr = process.StandardError.ReadToEndAsync(); process.WaitForExit();
        File.WriteAllText(Path.Combine(root, "child.log"), stdout.Result + stderr.Result);
        if (process.ExitCode != 0) return process.ExitCode;
        using var doc = JsonDocument.Parse(File.ReadAllText(json)); var r = doc.RootElement;
        var edges = r.GetProperty("edges").EnumerateArray().ToArray();
        var checks = new Dictionary<string, bool> {
            ["metadata_only"] = !r.GetProperty("targetCodeExecuted").GetBoolean(),
            ["both_fixture_files"] = r.GetProperty("files").GetArrayLength() == 2,
            ["no_parse_error"] = r.GetProperty("errors").GetArrayLength() == 0,
            ["sdk_external_type"] = edges.Any(e => e.GetProperty("Assembly").GetString() == "Microsoft.Windows.SDK.NET" && e.GetProperty("Consumer").GetString() == "Consumer.dll"),
            ["sdk_root_xml"] = File.ReadAllText(xml).Contains("Windows.Foundation.Fixture"),
            ["native_import"] = edges.Any(e => e.GetProperty("Kind").GetString() == "PINVOKE" && e.GetProperty("Target").GetString() == "fixture_entry"),
            ["literal_not_call_proof"] = edges.Any(e => e.GetProperty("Kind").GetString() == "STRING_NOT_CALL_PROOF")
        };
        File.WriteAllText(Path.Combine(root, "result.json"), JsonSerializer.Serialize(new { checks, targetCodeExecuted = false, deviceAccessed = false }, new JsonSerializerOptions { WriteIndented = true }));
        Console.WriteLine($"AUDIT_SELFTEST checks={checks.Count} failed={checks.Count(c => !c.Value)} NOT_DEVICE_PASS");
        return checks.All(c => c.Value) ? 0 : 3;
    }
}

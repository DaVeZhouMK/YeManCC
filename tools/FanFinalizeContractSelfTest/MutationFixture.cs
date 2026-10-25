using System.IO;
using Mono.Cecil;
using Mono.Cecil.Cil;

internal static class MutationFixture
{
    internal static void WriteWrongTerminalState(string input, string output)
    {
        if (File.Exists(output)) throw new IOException("Negative fixture output already exists");
        var resolver = new DefaultAssemblyResolver();
        resolver.AddSearchDirectory(Path.GetDirectoryName(Path.GetFullPath(input))!);
        using var assembly = AssemblyDefinition.ReadAssembly(input, new ReaderParameters { AssemblyResolver = resolver });
        var engine = assembly.MainModule.Types.Single(t => t.FullName == "YeManFanHost.FanHostEngine");
        var method = engine.Methods.Single(m => m.Name == "MarkAbnormalFinalizeTerminal");
        var literal = method.Body.Instructions.Where(i => i.OpCode == OpCodes.Ldstr && (string)i.Operand == "FaultLocked").ToArray();
        if (literal.Length != 1) throw new InvalidOperationException("Exact terminal-state mutation site missing or ambiguous");
        literal[0].Operand = "Ready";
        assembly.Write(output);
    }
}

// Pure PE/metadata/IL inspection of one locked type. The target DLL is read as
// bytes only: never Assembly.Load, SDK object construction or SDK execution.
using System.Collections.Immutable;
using System.Reflection;
using System.Reflection.Emit;
using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Reflection.PortableExecutable;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

internal static class PrimaryIdentityMetadata
{
    private const string LockedHash = "8FE7BDB21EC497591F527B7B87C4BC7B41B45C35A06CC211A7A0DCE48838ED2C";
    private const string TargetType = "HIDMaestro.HMController";
    private sealed class Signatures : ISignatureTypeProvider<string, object?>
    {
        internal static string Definition(MetadataReader r, TypeDefinitionHandle h)
        {
            var t = r.GetTypeDefinition(h); var owner = t.GetDeclaringType();
            return owner.IsNil ? Join(r.GetString(t.Namespace), r.GetString(t.Name)) : Definition(r, owner) + "+" + r.GetString(t.Name);
        }
        internal static string Reference(MetadataReader r, TypeReferenceHandle h)
        {
            var t = r.GetTypeReference(h);
            return t.ResolutionScope.Kind == HandleKind.TypeReference ? Reference(r, (TypeReferenceHandle)t.ResolutionScope) + "+" + r.GetString(t.Name) : Join(r.GetString(t.Namespace), r.GetString(t.Name));
        }
        private static string Join(string ns, string name) => ns.Length == 0 ? name : ns + "." + name;
        public string GetArrayType(string e, ArrayShape s) => e + "[" + new string(',', s.Rank - 1) + "]";
        public string GetByReferenceType(string e) => e + "&";
        public string GetFunctionPointerType(MethodSignature<string> s) => "fn(" + string.Join(",", s.ParameterTypes) + ")->" + s.ReturnType;
        public string GetGenericInstantiation(string t, ImmutableArray<string> a) => t + "<" + string.Join(",", a) + ">";
        public string GetGenericMethodParameter(object? c, int i) => "!!" + i;
        public string GetGenericTypeParameter(object? c, int i) => "!" + i;
        public string GetModifiedType(string m, string u, bool required) => u + (required ? " modreq(" : " modopt(") + m + ")";
        public string GetPinnedType(string e) => e + " pinned";
        public string GetPointerType(string e) => e + "*";
        public string GetPrimitiveType(PrimitiveTypeCode c) => c switch {
            PrimitiveTypeCode.Int32 => "System.Int32", PrimitiveTypeCode.String => "System.String",
            PrimitiveTypeCode.Void => "System.Void", PrimitiveTypeCode.Boolean => "System.Boolean",
            PrimitiveTypeCode.Object => "System.Object", PrimitiveTypeCode.IntPtr => "System.IntPtr",
            _ => "System." + c
        };
        public string GetSZArrayType(string e) => e + "[]";
        public string GetTypeFromDefinition(MetadataReader r, TypeDefinitionHandle h, byte raw) => Definition(r, h);
        public string GetTypeFromReference(MetadataReader r, TypeReferenceHandle h, byte raw) => Reference(r, h);
        public string GetTypeFromSpecification(MetadataReader r, object? c, TypeSpecificationHandle h, byte raw) => r.GetTypeSpecification(h).DecodeSignature(this, c);
    }
    private static readonly Signatures Signature = new();
    // Reflection here reads only BCL OpCodes constants to decode IL. It never
    // loads the target assembly or reflects any SDK type/object.
    private static readonly Dictionary<ushort, OpCode> Codes = typeof(OpCodes).GetFields(BindingFlags.Public | BindingFlags.Static)
        .Select(f => (OpCode)f.GetValue(null)!).ToDictionary(op => unchecked((ushort)op.Value));
    private sealed record Instruction(int Offset, string OpCode, string Bytes, object? Operand, int? Token, string? Resolved);
    private static string EntityName(MetadataReader r, EntityHandle h) => h.Kind switch {
        HandleKind.TypeDefinition => Signatures.Definition(r, (TypeDefinitionHandle)h),
        HandleKind.TypeReference => Signatures.Reference(r, (TypeReferenceHandle)h),
        HandleKind.TypeSpecification => r.GetTypeSpecification((TypeSpecificationHandle)h).DecodeSignature(Signature, null),
        HandleKind.FieldDefinition => FieldName(r, (FieldDefinitionHandle)h),
        HandleKind.MethodDefinition => MethodName(r, (MethodDefinitionHandle)h),
        HandleKind.MemberReference => MemberName(r, (MemberReferenceHandle)h),
        HandleKind.MethodSpecification => EntityName(r, r.GetMethodSpecification((MethodSpecificationHandle)h).Method),
        _ => h.Kind + ":" + MetadataTokens.GetToken(h).ToString("X8")
    };
    private static string FieldName(MetadataReader r, FieldDefinitionHandle h)
    { var f = r.GetFieldDefinition(h); return Signatures.Definition(r, f.GetDeclaringType()) + "::" + r.GetString(f.Name); }
    private static string MethodName(MetadataReader r, MethodDefinitionHandle h)
    { var m = r.GetMethodDefinition(h); return Signatures.Definition(r, m.GetDeclaringType()) + "::" + r.GetString(m.Name); }
    private static string MemberName(MetadataReader r, MemberReferenceHandle h)
    { var m = r.GetMemberReference(h); return EntityName(r, m.Parent) + "::" + r.GetString(m.Name); }
    private static object[] Attributes(MetadataReader r, CustomAttributeHandleCollection attrs) => attrs.Select(h => {
        var a = r.GetCustomAttribute(h);
        return (object)new { constructor = EntityName(r, a.Constructor), valueBlobHex = Convert.ToHexString(r.GetBlobBytes(a.Value)) };
    }).ToArray();
    private static List<Instruction> Decode(MetadataReader r, byte[] bytes)
    {
        var instructions = new List<Instruction>(); var p = 0;
        void Need(int n) { if (n < 0 || p > bytes.Length - n) throw new BadImageFormatException("Truncated IL operand."); }
        while (p < bytes.Length)
        {
            var start = p; ushort code = bytes[p++];
            if (code == 0xFE) { Need(1); code = (ushort)(0xFE00 | bytes[p++]); }
            if (!Codes.TryGetValue(code, out var op)) throw new BadImageFormatException("Unknown IL opcode.");
            object? operand = null; int? token = null; string? resolved = null; var n = 0;
            switch (op.OperandType)
            {
                case OperandType.InlineNone: break;
                case OperandType.ShortInlineI: Need(1); n = 1; operand = (sbyte)bytes[p]; break;
                case OperandType.ShortInlineVar: Need(1); n = 1; operand = bytes[p]; break;
                case OperandType.InlineVar: Need(2); n = 2; operand = BitConverter.ToUInt16(bytes, p); break;
                case OperandType.InlineI: Need(4); n = 4; operand = BitConverter.ToInt32(bytes, p); break;
                case OperandType.InlineI8: Need(8); n = 8; operand = BitConverter.ToInt64(bytes, p); break;
                case OperandType.ShortInlineR: Need(4); n = 4; operand = BitConverter.ToSingle(bytes, p); break;
                case OperandType.InlineR: Need(8); n = 8; operand = BitConverter.ToDouble(bytes, p); break;
                case OperandType.ShortInlineBrTarget: Need(1); n = 1; operand = "IL_" + (p + 1 + (sbyte)bytes[p]).ToString("X4"); break;
                case OperandType.InlineBrTarget: Need(4); n = 4; operand = "IL_" + (p + 4 + BitConverter.ToInt32(bytes, p)).ToString("X4"); break;
                case OperandType.InlineSwitch:
                    Need(4); var count = BitConverter.ToInt32(bytes, p);
                    if (count < 0 || count > 1024) throw new BadImageFormatException("Switch exceeds bounded review.");
                    n = checked(4 + count * 4); Need(n);
                    operand = Enumerable.Range(0, count).Select(i => "IL_" + (p + n + BitConverter.ToInt32(bytes, p + 4 + i * 4)).ToString("X4")).ToArray(); break;
                case OperandType.InlineField: case OperandType.InlineMethod: case OperandType.InlineType:
                case OperandType.InlineTok: case OperandType.InlineSig: case OperandType.InlineString:
                    Need(4); n = 4; token = BitConverter.ToInt32(bytes, p); operand = "0x" + token.Value.ToString("X8");
                    resolved = op.OperandType == OperandType.InlineString
                        ? "user-string:" + r.GetUserString(MetadataTokens.UserStringHandle(token.Value & 0x00FFFFFF))
                        : EntityName(r, MetadataTokens.EntityHandle(token.Value)); break;
                default: throw new BadImageFormatException("Unsupported IL operand form.");
            }
            p += n; instructions.Add(new(start, op.Name!, Convert.ToHexString(bytes.AsSpan(start, p - start)), operand, token, resolved));
            if (instructions.Count > 4096) throw new BadImageFormatException("Method exceeds bounded instruction limit.");
        }
        return instructions;
    }
    private static object MethodEvidence(MetadataReader r, PEReader pe, MethodDefinitionHandle h)
    {
        var m = r.GetMethodDefinition(h); var sig = m.DecodeSignature(Signature, null);
        if (m.RelativeVirtualAddress == 0) throw new BadImageFormatException("Expected managed method body.");
        var body = pe.GetMethodBody(m.RelativeVirtualAddress); var il = body.GetILBytes() ?? throw new BadImageFormatException("Missing IL bytes.");
        if (il.Length > 65536) throw new BadImageFormatException("Target method exceeds review bound.");
        return new {
            token = "0x" + MetadataTokens.GetToken(h).ToString("X8"), name = r.GetString(m.Name), declaringType = Signatures.Definition(r, m.GetDeclaringType()),
            attributes = m.Attributes.ToString(), attributesHex = "0x" + ((int)m.Attributes).ToString("X4"), isStatic = m.Attributes.HasFlag(MethodAttributes.Static),
            returnType = sig.ReturnType, parameterTypes = sig.ParameterTypes,
            parameters = m.GetParameters().Select(p => { var item = r.GetParameter(p); return new { sequence = item.SequenceNumber, name = r.GetString(item.Name), attributes = item.Attributes.ToString() }; }).ToArray(),
            rva = "0x" + m.RelativeVirtualAddress.ToString("X8"), body.MaxStack, body.LocalVariablesInitialized,
            localSignatureToken = body.LocalSignature.IsNil ? null : "0x" + MetadataTokens.GetToken(body.LocalSignature).ToString("X8"),
            ilLength = il.Length, ilSha256 = Convert.ToHexString(SHA256.HashData(il)), ilHex = Convert.ToHexString(il), instructions = Decode(r, il)
        };
    }
    private static int? Argument(Instruction instruction) => instruction.OpCode switch {
        "ldarg.0" => 0, "ldarg.1" => 1, "ldarg.2" => 2, "ldarg.3" => 3,
        "ldarg" or "ldarg.s" => Convert.ToInt32(instruction.Operand), _ => null
    };
    private static int Main(string[] args)
    {
        if (args.Length != 2) { Console.Error.WriteLine("usage: metadata-tool locked-core.dll output.json"); return 2; }
        try
        {
            var path = Path.GetFullPath(args[0]); var bytes = File.ReadAllBytes(path);
            var hash = Convert.ToHexString(SHA256.HashData(bytes));
            if (bytes.Length != 63_076_352 || hash != LockedHash) throw new InvalidOperationException("DLL is not the requested locked 2.0.0 8fe7bdb2 binary.");
            using var stream = new MemoryStream(bytes, writable: false);
            using var pe = new PEReader(stream); var r = pe.GetMetadataReader();
            var matches = r.TypeDefinitions.Where(h => Signatures.Definition(r, h) == TargetType).ToArray();
            if (matches.Length != 1) throw new BadImageFormatException("Expected one HMController definition.");
            var typeHandle = matches[0]; var type = r.GetTypeDefinition(typeHandle);
            var properties = type.GetProperties().Where(h => r.GetString(r.GetPropertyDefinition(h).Name) is "Index" or "InstanceId").ToArray();
            var fieldHandles = type.GetFields().Where(h => r.GetString(r.GetFieldDefinition(h).Name) is "<Index>k__BackingField" or "<InstanceId>k__BackingField").ToArray();
            if (properties.Length != 2 || fieldHandles.Length != 2) throw new BadImageFormatException("Target auto-property/field shape differs.");
            var fieldTokens = fieldHandles.Select(h => MetadataTokens.GetToken(h)).ToHashSet();
            var fields = fieldHandles.Select(h => {
                var f = r.GetFieldDefinition(h);
                return new { name = r.GetString(f.Name), token = "0x" + MetadataTokens.GetToken(h).ToString("X8"),
                    type = f.DecodeSignature(Signature, null), declaringType = Signatures.Definition(r, f.GetDeclaringType()),
                    visibility = (f.Attributes & FieldAttributes.FieldAccessMask).ToString(), isStatic = f.Attributes.HasFlag(FieldAttributes.Static), isInstance = !f.Attributes.HasFlag(FieldAttributes.Static),
                    initOnly = f.Attributes.HasFlag(FieldAttributes.InitOnly), attributes = f.Attributes.ToString(), attributesHex = "0x" + ((int)f.Attributes).ToString("X4"),
                    signatureBlobHex = Convert.ToHexString(r.GetBlobBytes(f.Signature)), customAttributes = Attributes(r, f.GetCustomAttributes()) };
            }).ToArray();
            var checks = new List<object>(); var checkFailed = false;
            void Check(string name, bool passed) { checks.Add(new { name, passed }); if (!passed) checkFailed = true; }
            foreach (var field in fields) Check(field.name + " is private readonly instance field on exact HMController", field.visibility == "Private" && field.isInstance && field.initOnly && field.declaringType == TargetType);
            var propertyEvidence = properties.Select(h => {
                var p = r.GetPropertyDefinition(h); var name = r.GetString(p.Name); var access = p.GetAccessors();
                if (access.Getter.IsNil) throw new BadImageFormatException("Expected getter.");
                var getter = r.GetMethodDefinition(access.Getter); var il = pe.GetMethodBody(getter.RelativeVirtualAddress).GetILBytes()!;
                var instructions = Decode(r, il); var fieldName = "<" + name + ">k__BackingField";
                var corresponding = fieldHandles.Single(f => r.GetString(r.GetFieldDefinition(f).Name) == fieldName);
                Check(name + " getter is exactly ldarg.0 / ldfld / ret", instructions.Count == 3 && instructions[0].OpCode == "ldarg.0" && instructions[1].OpCode == "ldfld" && instructions[1].Token == MetadataTokens.GetToken(corresponding) && instructions[2].OpCode == "ret");
                Check(name + " has no setter and internal instance getter", access.Setter.IsNil && (getter.Attributes & MethodAttributes.MemberAccessMask) == MethodAttributes.Assembly && !getter.Attributes.HasFlag(MethodAttributes.Static));
                return new { name, token = "0x" + MetadataTokens.GetToken(h).ToString("X8"), type = p.DecodeSignature(Signature, null).ReturnType,
                    setterPresent = !access.Setter.IsNil, getter = MethodEvidence(r, pe, access.Getter), customAttributes = Attributes(r, p.GetCustomAttributes()) };
            }).ToArray();
            var constructors = type.GetMethods().Where(h => r.GetString(r.GetMethodDefinition(h).Name) == ".ctor").ToArray();
            if (constructors.Length == 0 || constructors.Length > 8) throw new BadImageFormatException("Constructor count exceeds narrow review.");
            var writes = new List<object>(); var writtenTokens = new HashSet<int>();
            foreach (var h in constructors)
            {
                var m = r.GetMethodDefinition(h); var signature = m.DecodeSignature(Signature, null);
                var instructions = Decode(r, pe.GetMethodBody(m.RelativeVirtualAddress).GetILBytes()!);
                for (var i = 0; i < instructions.Count; ++i)
                {
                    var instruction = instructions[i];
                    if (instruction.OpCode != "stfld" || instruction.Token is not int token || !fieldTokens.Contains(token)) continue;
                    var inputArgument = i > 0 ? Argument(instructions[i - 1]) : null;
                    var receiverArgument = i > 1 ? Argument(instructions[i - 2]) : null;
                    var parameterName = inputArgument.HasValue ? m.GetParameters().Select(p => r.GetParameter(p)).Where(p => p.SequenceNumber == inputArgument.Value).Select(p => r.GetString(p.Name)).SingleOrDefault() : null;
                    writtenTokens.Add(token);
                    writes.Add(new { constructorToken = "0x" + MetadataTokens.GetToken(h).ToString("X8"), field = instruction.Resolved,
                        offset = "IL_" + instruction.Offset.ToString("X4"), bytes = instruction.Bytes,
                        receiverArgument, valueArgument = inputArgument, parameterName,
                        parameterType = inputArgument is > 0 ? signature.ParameterTypes[inputArgument.Value - 1] : null,
                        localInstructionWindow = instructions.Skip(Math.Max(0, i - 2)).Take(Math.Min(i, 2) + 1).ToArray() });
                    Check(instruction.Resolved + " constructor store comes directly from named argument", receiverArgument == 0 && parameterName == (instruction.Resolved!.Contains("<Index>", StringComparison.Ordinal) ? "index" : "instanceId"));
                }
            }
            Check("constructor IL writes both target backing fields", fieldTokens.SetEquals(writtenTokens));
            Check("target field types are Int32 and String", fields.Single(f => f.name == "<Index>k__BackingField").type == "System.Int32" && fields.Single(f => f.name == "<InstanceId>k__BackingField").type == "System.String");
            var report = new { passed = !checkFailed, binary = new { path, sha256 = hash, bytes = bytes.Length,
                    assembly = r.GetString(r.GetAssemblyDefinition().Name), version = r.GetAssemblyDefinition().Version.ToString(), mvid = r.GetGuid(r.GetModuleDefinition().Mvid).ToString() },
                targetType = new { name = TargetType, token = "0x" + MetadataTokens.GetToken(typeHandle).ToString("X8"), attributes = type.Attributes.ToString() },
                fields, properties = propertyEvidence, constructors = constructors.Select(h => MethodEvidence(r, pe, h)).ToArray(), constructorWrites = writes, checks,
                scope = "PEReader/System.Reflection.Metadata for only HMController target properties, fields, constructors and IL. BCL OpCodes constants decode IL; no SDK object reflection or SDK execution.",
                sdkAssemblyLoaded = false, sdkObjectsConstructed = 0, sdkObjectFieldReads = 0, sdkMethodsExecuted = 0, liveDeviceApis = 0, reDecompilerExecuted = false };
            File.WriteAllText(args[1], JsonSerializer.Serialize(report, new JsonSerializerOptions { WriteIndented = true, PropertyNamingPolicy = JsonNamingPolicy.CamelCase }), new UTF8Encoding(false));
            Console.WriteLine($"HMCONTROLLER_PRIMARY_METADATA passed={!checkFailed} fields={fields.Length} properties={properties.Length} constructors={constructors.Length} checks={checks.Count} hash={hash}");
            return checkFailed ? 1 : 0;
        }
        catch (Exception ex) { Console.Error.WriteLine(ex); return 2; }
    }
}
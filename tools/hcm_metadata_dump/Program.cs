// HC 发行版 DLL 陀螺仪类型元数据导出（反编译级：类型/字段+默认常量/属性/方法名+参数个数/枚举字面量）。
// 零外部依赖：System.Reflection.Metadata + PEReader（dotnet 共享框架自带）。
// 用法: hcm-metadata-dump <HandheldCompanion.dll> <输出.txt>
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;
using System.Text;

string dll = args.Length > 0 ? args[0] : throw new ArgumentException("dll");
string outp = args.Length > 1 ? args[1] : "hc-metadata.txt";

using var fs = File.OpenRead(dll);
using var pe = new PEReader(fs);
var md = pe.GetMetadataReader();
var sb = new StringBuilder();

bool IsGyro(string full)
{
    return full.Contains("Motion") || full.Contains("Gyro") || full.Contains("Sensor")
        || full.Contains("Inclination") || full.Contains("InputUtils") || full.Contains("Profile")
        || full.Contains("Layout") || full.Contains("Steering") || full.Contains("Axis")
        || full.Contains("IMU") || full.Contains("Controller");
}

int dumped = 0;
foreach (var th in md.TypeDefinitions)
{
    var td = md.GetTypeDefinition(th);
    var name = md.GetString(td.Name);
    if (name.StartsWith("<")) continue;
    var ns = md.GetString(td.Namespace);
    var full = string.IsNullOrEmpty(ns) ? name : ns + "." + name;
    if (!IsGyro(full)) continue;

    sb.AppendLine($"TYPE {full}");
    // 基类
    var baseHandle = td.BaseType;
    if (!baseHandle.IsNil)
    {
        try
        {
            if (baseHandle.Kind == HandleKind.TypeDefinition)
                sb.AppendLine($"  BASE {md.GetString(md.GetTypeDefinition((TypeDefinitionHandle)baseHandle).Name)}");
            else if (baseHandle.Kind == HandleKind.TypeReference)
                sb.AppendLine($"  BASE {md.GetString(md.GetTypeReference((TypeReferenceHandle)baseHandle).Name)}");
        }
        catch { }
    }
    // 枚举字面量：存在 value__ 字段即为枚举（C# 编译标记，无 TypeAttributes.Enum）
    var fieldDefs = td.GetFields().Select(fh => md.GetFieldDefinition(fh)).ToArray();
    bool isEnum = fieldDefs.Any(f => md.GetString(f.Name) == "value__");
    if (isEnum)
    {
        sb.AppendLine("  ENUM");
        foreach (var f in fieldDefs)
        {
            var fn = md.GetString(f.Name);
            if (fn == "value__") continue;
            sb.AppendLine($"    {fn} = {ReadConstant(md, f.GetDefaultValue())}");
        }
    }
    // 字段 + 默认常量
    foreach (var f in fieldDefs)
    {
        var fn = md.GetString(f.Name);
        if (fn == "value__") continue;
        var def = f.GetDefaultValue();
        string c = def.IsNil ? "" : " = " + ReadConstant(md, def);
        sb.AppendLine($"  FIELD {fn}{c}");
    }
    // 属性
    foreach (var ph in td.GetProperties())
    {
        var p = md.GetPropertyDefinition(ph);
        sb.AppendLine($"  PROP {md.GetString(p.Name)}");
    }
    // 方法名 + 参数个数
    foreach (var mh in td.GetMethods())
    {
        var m = md.GetMethodDefinition(mh);
        var mn = md.GetString(m.Name);
        if (mn.StartsWith("get_") || mn.StartsWith("set_") || mn.StartsWith("add_") || mn.StartsWith("remove_")) continue;
        if (mn.StartsWith("op_")) continue;
        int pcount = 0;
        var sig = m.Signature;
        if (!sig.IsNil)
        {
            var br = md.GetBlobReader(sig);
            br.ReadSignatureHeader();
            if (br.ReadCompressedInteger() == 0x10) br.ReadCompressedInteger(); // generic param count
            pcount = br.ReadCompressedInteger();
        }
        sb.AppendLine($"  METHOD {mn}({pcount})");
    }
    dumped++;
}
sb.Insert(0, $"HC_METADATA_DUMP types={dumped} dll={dll}\n");
File.WriteAllText(outp, sb.ToString(), Encoding.UTF8);
Console.WriteLine($"dumped {dumped} types -> {outp}");

static object ReadConstant(MetadataReader md, ConstantHandle h)
{
    if (h.IsNil) return "";
    var c = md.GetConstant(h);
    var br = md.GetBlobReader(c.Value);
    return c.TypeCode switch
    {
        ConstantTypeCode.Int32 => br.ReadInt32(),
        ConstantTypeCode.UInt32 => br.ReadUInt32(),
        ConstantTypeCode.Int64 => br.ReadInt64(),
        ConstantTypeCode.UInt64 => br.ReadUInt64(),
        ConstantTypeCode.Single => br.ReadSingle(),
        ConstantTypeCode.Double => br.ReadDouble(),
        ConstantTypeCode.String => br.ReadUTF8(br.Length) ?? "",
        ConstantTypeCode.Boolean => br.ReadBoolean(),
        ConstantTypeCode.Char => br.ReadChar(),
        ConstantTypeCode.NullReference => "null",
        ConstantTypeCode.Int16 => br.ReadInt16(),
        ConstantTypeCode.UInt16 => br.ReadUInt16(),
        ConstantTypeCode.SByte => br.ReadSByte(),
        ConstantTypeCode.Byte => br.ReadByte(),
        _ => "?"
    };
}

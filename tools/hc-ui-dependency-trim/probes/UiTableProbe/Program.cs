using System.IO;
using System.Reflection;
using System.Security.Cryptography;
using System.Text.Json;
using System.Windows;
using System.Windows.Media;

if(args.Length!=4||args[0] is not "capture" and not "compare")throw new ArgumentException("capture|compare runtime policy-or-baseline report");
string mode=args[0],root=Path.GetFullPath(args[1]),source=Path.GetFullPath(args[2]),report=Path.GetFullPath(args[3]);
var values=new List<TableValue>();var loads=new List<object>();int native=0;int hcCount=0;Exception? failure=null;
var worker=new Thread(()=>
{
    try
    {
        if(Application.Current!=null)throw new InvalidOperationException("Unexpected WPF Application");
        using var manifest=JsonDocument.Parse(File.ReadAllText(Path.Combine(root,"HandheldCompanion.runtime.json")));
        foreach(var file in manifest.RootElement.GetProperty("files").EnumerateArray())
            if(!Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(Path.Combine(root,file.GetProperty("path").GetString()!)))).Equals(file.GetProperty("sha256").GetString(),StringComparison.OrdinalIgnoreCase))throw new InvalidDataException("Manifest mismatch");
        using var input=JsonDocument.Parse(File.ReadAllText(source));
        TableValue[] expected=mode=="compare"?JsonSerializer.Deserialize<TableValue[]>(input.RootElement.GetProperty("Values").GetRawText())!:[];
        var fields=mode=="capture"?input.RootElement.GetProperty("Tables").EnumerateArray().SelectMany(t=>t.GetProperty("KeptInitializers").EnumerateArray().Select(f=>new FieldName(t.GetProperty("Type").GetString()!,f.GetProperty("Field").GetString()!.Split("::")[1]))).ToArray():expected.Select(v=>new FieldName(v.Type,v.Field)).ToArray();
        if(fields.Length!=74)throw new InvalidDataException("Reviewed field count changed");
        var context=new StrictContext(root,"538ea1f68f629a04",loads,()=>native++,null);
        var hc=context.LoadFromAssemblyPath(Path.Combine(root,"HandheldCompanion.dll"));context.BlockNative(hc);hcCount=hc.GetTypes().Length;if(hcCount!=1768)throw new InvalidDataException("HC types changed");
        var modern=context.LoadFromAssemblyName(new AssemblyName("iNKORE.UI.WPF.Modern, Version=0.10.2.0, Culture=neutral, PublicKeyToken=538ea1f68f629a04"));context.BlockNative(modern);
        foreach(var f in fields)
        {
            var type=modern.GetType(f.Type,true)!;var field=type.GetField(f.Name,BindingFlags.Public|BindingFlags.Static)!;
            if(field==null||!field.IsInitOnly)throw new InvalidDataException("Missing immutable field: "+f.Type+"::"+f.Name);
            object value=field.GetValue(null)!;
            TableValue actual;
            if(value is string literal)actual=new(type.FullName!,field.Name,"string",literal,null);
            else
            {
                string glyph=(string)value.GetType().GetProperty("Glyph")!.GetValue(value)!;
                var font=(FontFamily)value.GetType().GetProperty("FontFamily")!.GetValue(value)!;
                actual=new(type.FullName!,field.Name,"glyph-font",glyph,font.Source);
            }
            if(mode=="compare"&&!expected.Contains(actual))throw new InvalidDataException("Actual table value differs: "+actual.Type+"::"+actual.Field);
            values.Add(actual);
        }
        if(mode=="compare")foreach(var group in fields.GroupBy(f=>f.Type))if(modern.GetType(group.Key,true)!.GetFields(BindingFlags.Public|BindingFlags.Static).Length!=group.Count())throw new InvalidDataException("Candidate retained field set changed");
        if(Application.Current!=null||native!=0)throw new InvalidOperationException("Unexpected Application/native work");
    }
    catch(Exception e){failure=e;}
});worker.SetApartmentState(ApartmentState.STA);worker.Start();worker.Join();
var result=new{Status=failure==null?"passed":"failed",Mode=mode,Source=source,SourceSha256=Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(source))).ToLowerInvariant(),HcGetTypes=hcCount,Values=values,Count=values.Count,ResolvedLoads=loads,NativeAttempts=native,ApplicationCreated=false,WindowOrControlInstanceCreated=false,RenderingOrFontMeasurementRequested=false,Exception=failure?.ToString()};
File.WriteAllText(report,JsonSerializer.Serialize(result,new JsonSerializerOptions{WriteIndented=true}));Console.WriteLine(JsonSerializer.Serialize(new{result.Status,result.Mode,result.Count,result.NativeAttempts}));if(failure!=null){Console.Error.WriteLine(failure);Environment.ExitCode=1;}
sealed record FieldName(string Type,string Name);
sealed record TableValue(string Type,string Field,string Kind,string Value,string? FontFamilySource);

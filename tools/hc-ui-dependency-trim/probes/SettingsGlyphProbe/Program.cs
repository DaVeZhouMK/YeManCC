using System.IO;
using System.Reflection;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;
using System.Windows;

if(args.Length!=3)throw new ArgumentException("runtime report expected-codepoint");
string root=Path.GetFullPath(args[0]),report=Path.GetFullPath(args[1]);int codePoint=int.Parse(args[2]);
string expected=char.ConvertFromUtf32(codePoint);var loads=new List<object>();var values=new List<object>();int native=0;Exception? failure=null;int hcCount=0;
var worker=new Thread(()=>
{
    try
    {
        if(Application.Current!=null)throw new InvalidOperationException("Unexpected WPF Application");
        using var manifest=JsonDocument.Parse(File.ReadAllText(Path.Combine(root,"HandheldCompanion.runtime.json")));
        foreach(var file in manifest.RootElement.GetProperty("files").EnumerateArray())
            if(!Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(Path.Combine(root,file.GetProperty("path").GetString()!)))).Equals(file.GetProperty("sha256").GetString(),StringComparison.OrdinalIgnoreCase))throw new InvalidDataException("Manifest mismatch");
        var context=new StrictContext(root,"538ea1f68f629a04",loads,()=>native++,null);
        var hc=context.LoadFromAssemblyPath(Path.Combine(root,"HandheldCompanion.dll"));context.BlockNative(hc);hcCount=hc.GetTypes().Length;if(hcCount!=1768)throw new InvalidDataException("HC type count changed");
        var modern=context.LoadFromAssemblyName(new AssemblyName("iNKORE.UI.WPF.Modern, Version=0.10.2.0, Culture=neutral, PublicKeyToken=538ea1f68f629a04"));context.BlockNative(modern);
        var controls=context.LoadFromAssemblyName(new AssemblyName("iNKORE.UI.WPF.Modern.Controls, Version=0.10.2.0, Culture=neutral, PublicKeyToken=538ea1f68f629a04"));context.BlockNative(controls);
        var icons=modern.GetType("iNKORE.UI.WPF.Modern.Common.IconKeys.FluentSystemIcons");
        if(icons!=null)
        {
            var data=icons.GetField("ChevronRight_16_Regular",BindingFlags.Public|BindingFlags.Static)!.GetValue(null)!;
            string glyph=(string)data.GetType().GetProperty("Glyph")!.GetValue(data)!;
            if(glyph!=expected)throw new InvalidDataException("Original icon glyph differs from exact fold");
            values.Add(new{Source="Original FluentSystemIcons field actual initialization and Glyph getter",Value=glyph,MatchesExpected=true});
        }
        else values.Add(new{Source="FluentSystemIcons type is eliminated in folded candidate",Eliminated=true});
        var settings=controls.GetType("iNKORE.UI.WPF.Modern.Controls.SettingsCard",true)!;
        var property=(DependencyProperty)settings.GetField("ActionIconProperty",BindingFlags.Public|BindingFlags.Static)!.GetValue(null)!;
        var actual=property.GetMetadata(settings).DefaultValue as string;
        if(actual!=expected)throw new InvalidDataException("Actual SettingsCard ActionIcon default differs");
        values.Add(new{Source="Actual SettingsCard static constructor and DependencyProperty metadata",Value=actual,MatchesExpected=true});
        if(Application.Current!=null||native!=0)throw new InvalidOperationException("Unexpected Application/native work");
    }
    catch(Exception error){failure=error;}
});worker.SetApartmentState(ApartmentState.STA);worker.Start();worker.Join();
var result=new{Status=failure==null?"passed":"failed",CodePoint=codePoint,ExpectedGlyph=expected,HcGetTypes=hcCount,Values=values,ResolvedLoads=loads,NativeAttempts=native,ApplicationCreated=false,WindowOrControlInstanceCreated=false,DeviceConstructorsExecuted=false,RenderingOrFontMeasurementRequested=false,Exception=failure?.ToString()};
File.WriteAllText(report,JsonSerializer.Serialize(result,new JsonSerializerOptions{WriteIndented=true}));Console.WriteLine(JsonSerializer.Serialize(new{result.Status,result.HcGetTypes,result.NativeAttempts}));if(failure!=null){Console.Error.WriteLine(failure);Environment.ExitCode=1;}

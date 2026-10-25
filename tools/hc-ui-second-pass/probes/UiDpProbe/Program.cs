using System.IO;
using System.Reflection;
using System.Text.Json;
using System.Windows;
using System.Windows.Media;
using System.Threading;

string root=Path.GetFullPath(args[0]),report=Path.GetFullPath(args[1]);var cases=new List<object>();var maps=new List<object>();var loads=new List<object>();int denied=0;Exception? failure=null;
var worker=new Thread(()=>{try{
    var context=new StrictContext(root,"538ea1f68f629a04",loads,()=>denied++,null);
    var modern=context.LoadFromAssemblyName(AssemblyName.GetAssemblyName(Path.Combine(root,"iNKORE.UI.WPF.Modern.dll")));
    var controls=context.LoadFromAssemblyName(AssemblyName.GetAssemblyName(Path.Combine(root,"iNKORE.UI.WPF.Modern.Controls.dll")));
    if(args.Length<3||args[2]!="properties-only")foreach(var a in new[]{modern,controls})foreach(var t in a.GetTypes().Where(t=>!t.ContainsGenericParameters))foreach(var it in t.GetInterfaces()){
        var map=t.GetInterfaceMap(it);if(map.InterfaceMethods.Length!=map.TargetMethods.Length)throw new InvalidDataException("Interface map mismatch");
        maps.Add(new{Type=t.FullName,Interface=it.FullName,Methods=map.InterfaceMethods.Select(m=>m.ToString()).ToArray(),Targets=map.TargetMethods.Select(m=>m.DeclaringType!.FullName+":"+m).ToArray()});
    }
    foreach(string name in new[]{"iNKORE.UI.WPF.Modern.Controls.FontIcon","iNKORE.UI.WPF.Modern.Common.FontIconSource"}){
        var t=modern.GetType(name,true)!;var instance=(DependencyObject)Activator.CreateInstance(t)!;
        foreach(var item in new (string Name,object Value)[]{("Glyph","\uF2AF"),("FontSize",19.0),("FontStyle",FontStyles.Italic),("FontWeight",FontWeights.Bold)}){
            var property=t.GetProperty(item.Name)!;var field=t.GetField(item.Name+"Property",BindingFlags.Static|BindingFlags.Public|BindingFlags.FlattenHierarchy)!;var dp=(DependencyProperty)field.GetValue(null)!;
            var metadata=dp.GetMetadata(t);string[] callbacks=metadata.PropertyChangedCallback?.GetInvocationList().Select(d=>d.Method.DeclaringType!.FullName+":"+d.Method).ToArray()??[];
            property.SetValue(instance,item.Value);object? got=property.GetValue(instance);if(!Equals(item.Value,got))throw new InvalidDataException("DP getter/setter roundtrip failed");
            string? actual=t.GetProperty("ActualGlyph")?.GetValue(instance) as string;if(item.Name=="Glyph"&&actual!=null&&actual!="\uF2AF")throw new InvalidDataException("Glyph callback result differs");
            cases.Add(new{Type=name,Property=item.Name,Expected=item.Value.ToString(),Actual=got?.ToString(),Callbacks=callbacks,ActualGlyph=actual,DependencyPropertyName=dp.Name});
            instance.ClearValue(dp);
        }
        if(instance is Visual v&&PresentationSource.FromVisual(v)!=null)throw new InvalidDataException("Visual attached");
    }
    if(Application.Current!=null||denied!=0)throw new InvalidDataException("Unexpected Application/native import");
}catch(Exception e){failure=e;}});worker.SetApartmentState(ApartmentState.STA);worker.Start();worker.Join();
var result=new{Status=failure==null?"passed":"failed",Cases=cases,InterfaceMaps=maps,InterfaceMapCount=maps.Count,Exception=failure?.ToString(),NativeImportsDenied=denied,WindowCreated=false,ApplicationCreated=false,TemplateApplied=false,MeasureOrRenderRequested=false,DeviceConstructed=false};
File.WriteAllText(report,JsonSerializer.Serialize(result,new JsonSerializerOptions{WriteIndented=true}));Console.WriteLine(JsonSerializer.Serialize(new{result.Status,CaseCount=cases.Count,result.InterfaceMapCount,result.NativeImportsDenied}));if(failure!=null)Environment.ExitCode=1;


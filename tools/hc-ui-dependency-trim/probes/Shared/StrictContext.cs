using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
sealed class StrictContext:AssemblyLoadContext
{
    readonly string root,expected;readonly string? denyTarget;readonly HashSet<string> framework;readonly List<object> loads;readonly Action denied;readonly HashSet<Assembly> blocked=[];
    public StrictContext(string root,string expected,List<object> loads,Action denied,string? denyTarget):base("modern-headless-"+Guid.NewGuid().ToString("N"),false)
    {
        this.root=root;this.expected=expected;this.loads=loads;this.denied=denied;this.denyTarget=denyTarget;
        framework=((string?)AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES")??"").Split(Path.PathSeparator).Where(p=>p.StartsWith(RuntimeEnvironment.GetRuntimeDirectory(),StringComparison.OrdinalIgnoreCase)||p.Contains(Path.DirectorySeparatorChar+"Microsoft.WindowsDesktop.App"+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase)).Select(p=>Path.GetFileNameWithoutExtension(p)!).ToHashSet(StringComparer.OrdinalIgnoreCase);
    }
    protected override Assembly? Load(AssemblyName requested)
    {
        if(requested.Name!=null&&framework.Contains(requested.Name))return Default.LoadFromAssemblyName(requested);
        string name=requested.Name??throw new FileLoadException("Missing name");if(name!=Path.GetFileName(name)||name.Contains(':'))throw new FileLoadException("Invalid dependency name");
        if(name==denyTarget)throw new FileLoadException("UI_NEGATIVE_DENIED:"+name);
        string path=Path.Combine(root,name+".dll");if(!File.Exists(path))throw new FileNotFoundException("Missing frozen dependency: "+name,path);
        var declared=AssemblyName.GetAssemblyName(path);
        if(name is "iNKORE.UI.WPF.Modern" or "iNKORE.UI.WPF.Modern.Controls")
        {
            string requestedToken=Convert.ToHexString(requested.GetPublicKeyToken()??[]).ToLowerInvariant(),actualToken=Convert.ToHexString(declared.GetPublicKeyToken()??[]).ToLowerInvariant();
            if(requestedToken!=expected||actualToken!=expected||requested.Version!=declared.Version)throw new FileLoadException("Selected target identity mismatch: "+requested.FullName+" -> "+declared.FullName);
        }
        var a=LoadFromAssemblyPath(path);BlockNative(a);loads.Add(new{Requested=requested.FullName,Resolved=a.GetName().FullName,Path=path});return a;
    }
    public void BlockNative(Assembly a)
    {
        if(blocked.Add(a))NativeLibrary.SetDllImportResolver(a,(name,_,_)=>{denied();throw new DllNotFoundException("Native import blocked by headless probe: "+name);});
    }
    protected override IntPtr LoadUnmanagedDll(string name){denied();throw new DllNotFoundException("Native import blocked by headless probe: "+name);}
}



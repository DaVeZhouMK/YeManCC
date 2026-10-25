using System;
using System.IO;
using System.Reflection;
using System.Runtime.Loader;
using System.Runtime.InteropServices;

static partial class Harness
{
    sealed class ProductContext : AssemblyLoadContext
    {
        readonly string root;
        public ProductContext(string root):base(true) { this.root=root; ProducerContextAuthority.Register("Harness.ProductContext",root,this); }
        protected override Assembly? Load(AssemblyName name)
        {
            var port=ExistingFixtureAssembly();
            if (name.Name == port.GetName().Name) return ToolLoadAuthority.Current.RequireExistingExport(this,name,port);
            var file = Path.Combine(root, name.Name + ".dll");
            if (!File.Exists(file)) return ToolLoadAuthority.Current.ResolveOrReject(this,name);
            var assembly = ToolLoadAuthority.Current.LoadRequestedPath(this,name,file); Deny(assembly); return assembly;
        }
    }
    static void Deny(Assembly assembly) => NativeLibrary.SetDllImportResolver(assembly, (name, _, _) =>
    { NativeAttempts++; throw new InvalidOperationException("NATIVE_IO_DENIED:" + name); });
    static Assembly Load(string file)
    {
        var ctx = new ProductContext(Path.GetDirectoryName(file)!);
        OwnedProductContexts.Add(ctx); // Keep collectible contexts alive until this owned fixture process exits.
        var assembly = ToolLoadAuthority.Current.LoadPath(ctx,Path.GetFullPath(file)); Deny(assembly); return assembly;
    }
}

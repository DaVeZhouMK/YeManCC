using System;
using System.IO;
using System.Reflection;
using System.Runtime.Loader;
using System.Runtime.InteropServices;

static partial class OldWriteProbe
{
 sealed class Context:AssemblyLoadContext
 {
  readonly string root;
  public Context(string root):base(true){this.root=root;ProducerContextAuthority.Register("OldWriteProbe.Context",root,this);}
  protected override Assembly? Load(AssemblyName name)
  {
   var port=ExistingFixtureAssembly();
   if(name.Name==port.GetName().Name)return ToolLoadAuthority.Current.RequireExistingExport(this,name,port);
   var p=Path.Combine(root,name.Name+".dll");if(!File.Exists(p))return ToolLoadAuthority.Current.ResolveOrReject(this,name);
   var a=ToolLoadAuthority.Current.LoadRequestedPath(this,name,p);Deny(a);return a;
  }
 }
 static void Deny(Assembly a)=>NativeLibrary.SetDllImportResolver(a,(n,_,_)=>{NativeAttempts++;throw new InvalidOperationException("NATIVE_DENIED:"+n);});
 static Assembly Load(string p){var c=new Context(Path.GetDirectoryName(p)!);var a=ToolLoadAuthority.Current.LoadPath(c,Path.GetFullPath(p));Deny(a);return a;}
}

using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;

namespace YeMan.Setup {
 public static class WebAssets {
  public const string ProductName="YeManCC 野蛮系统控制中心";
  static string loaderFolder;
  static IntPtr loader;
  static readonly object sdkGate=new object();static bool sdkConfigured;
  public static void Initialize() {
   AppDomain.CurrentDomain.AssemblyResolve += (sender,args) => {
    string name=new AssemblyName(args.Name).Name;
    if(name=="Microsoft.Web.WebView2.Core")return Assembly.Load(Bytes("wv.core"));
    if(name=="Microsoft.Web.WebView2.WinForms")return Assembly.Load(Bytes("wv.forms"));
    return null;
   };
  }
  public static byte[] Bytes(string name) {
   using(var s=Assembly.GetExecutingAssembly().GetManifestResourceStream(name)) {
    if(s==null)throw new IOException("设置中心缺少界面资源："+name);
    using(var output=new MemoryStream()){s.CopyTo(output);return output.ToArray();}
   }
  }
  public static string Html {get{return Encoding.UTF8.GetString(Bytes("setup.html")).TrimStart('\ufeff').Replace("__YMCC_APP_ICON__","data:image/png;base64,"+Convert.ToBase64String(Bytes("app.png")));}}
  public static string HtmlHash {get{using(var s=new MemoryStream(Bytes("setup.html")))return SafeFiles.Hash(s);}}
  public static string IconHash {get{using(var s=new MemoryStream(Bytes("app.icon")))return SafeFiles.Hash(s);}}
  public static System.Drawing.Icon CreateIcon() {
   using(var stream=new MemoryStream(Bytes("app.icon")))using(var icon=new System.Drawing.Icon(stream))return (System.Drawing.Icon)icon.Clone();
  }
  [System.Runtime.CompilerServices.MethodImpl(System.Runtime.CompilerServices.MethodImplOptions.NoInlining)]
  public static void PrepareRuntimeSdk() {
   lock(sdkGate){if(sdkConfigured)return;Microsoft.Web.WebView2.Core.CoreWebView2Environment.SetLoaderDllFolderPath(LoaderFolder());sdkConfigured=true;}
  }
  [System.Runtime.CompilerServices.MethodImpl(System.Runtime.CompilerServices.MethodImplOptions.NoInlining)]
  public static bool RuntimeAvailable() {
   PrepareRuntimeSdk();
   try {Version version;return Version.TryParse(Microsoft.Web.WebView2.Core.CoreWebView2Environment.GetAvailableBrowserVersionString(),out version)&&version>=new Version(123,0,0,0);}
   catch(Microsoft.Web.WebView2.Core.WebView2RuntimeNotFoundException){return false;}
  }
  [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]static extern IntPtr LoadLibraryEx(string path,IntPtr reserved,uint flags);
  public static string LoaderFolder() {
   if(loaderFolder!=null)return loaderFolder;
   string parent=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"YeManCC-Setup","host");
   SafeFiles.NoLinks(parent);Directory.CreateDirectory(parent);
   string folder=Path.Combine(parent,Guid.NewGuid().ToString("N"));Directory.CreateDirectory(folder);
   string path=Path.Combine(folder,"WebView2Loader.dll");
   using(var output=new FileStream(path,FileMode.CreateNew,FileAccess.Write,FileShare.None)){byte[] bytes=Bytes("wv.loader");output.Write(bytes,0,bytes.Length);}
   using(var locked=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.Read)) {
    using(var source=new MemoryStream(Bytes("wv.loader")))if(SafeFiles.Hash(locked)!=SafeFiles.Hash(source))throw new IOException("界面加载器校验失败");
    loader=LoadLibraryEx(path,IntPtr.Zero,0x00000100|0x00001000);
    if(loader==IntPtr.Zero)throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error(),"无法加载本地 WebView2 界面组件");
   }
   loaderFolder=folder;return loaderFolder;
  }
 }
}

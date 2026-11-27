using System;
using System.IO;
using System.Text;
using System.Windows.Forms;
namespace YeMan.Setup {
 static class Program {
  [STAThread] static int Main(string[] args) {
   AppContext.SetSwitch("Switch.System.IO.UseLegacyPathHandling",false);AppContext.SetSwitch("Switch.System.IO.BlockLongPaths",false);Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);WebAssets.Initialize();
   string errorPath=null;
   try {
    if(args.Length==2&&(args[0]=="--diagnose"||args[0]=="--diagnose-advanced-steam")) {
     string directory=SafeFiles.Full(args[1]);if(Directory.Exists(directory)||File.Exists(directory))throw new IOException("检测报告目录必须为新目录");
     SafeFiles.NoLinks(directory);Directory.CreateDirectory(directory);errorPath=Path.Combine(directory,"ERROR.json");
     bool advancedSteam=args[0]=="--diagnose-advanced-steam";
     if(advancedSteam)Json.Save(Path.Combine(directory,"advanced-steam-runtime.json"),RuntimeProtectionProbe.DiagnosticSnapshot());
     else Json.Save(Path.Combine(directory,"detection.json"),WebSetupForm.DetectionSnapshot(Plan.InstallRoot));
     Json.Save(Path.Combine(directory,"scope.json"),new{Utc=DateTime.UtcNow.ToString("o"),Mode=advancedSteam?"READ_ONLY_ADVANCED_STEAM_PATHS":"READ_ONLY_INSTALLATION_DETECTION",SystemActions="NOT_RUN",ProductProcessesStarted=0,SecurityChanges=0,FullCompatibilityAcceptance=false});return 0;
    }
    if(args.Length==2&&args[0]=="--validate") {string root=SafeFiles.Full(args[1]);var b=Resources.Unpack(root,s=>{});Json.Save(Path.Combine(root,"validation.json"),new {Status="LOCAL_PAYLOAD_VERIFIED",b.Version,b.CoreVersion,WebViewRuntimeBundled=false,WebViewBootstrapperBundled=true,NetworkPolicy="OFFLINE_BY_DEFAULT_WEBVIEW2_OPT_IN",FirstInstallOnly=false,LocalUpdate=true,UiSha256=WebAssets.HtmlHash,SystemActions="NOT_RUN"});return 0;}
    Plan plan=new Plan();bool apply=false,native=false,preview=args.Length==1&&(args[0]=="--preview"||args[0]=="--preview-offline");string capture=null;
    if(preview)native=args[0]=="--preview-offline";
    if(args.Length==2&&args[0]=="--capture-ui") {preview=true;capture=SafeFiles.Full(args[1]);if(Directory.Exists(capture))throw new IOException("截图证据目录必须为新目录");SafeFiles.NoLinks(capture);Directory.CreateDirectory(capture);errorPath=Path.Combine(capture,"ERROR.json");}
    else if(args.Length==2&&(args[0]=="--apply"||args[0]=="--apply-native")){plan=Json.Read<Plan>(Encoding.UTF8.GetString(Convert.FromBase64String(args[1])));Plan.Validate(plan);if(!Native.IsAdmin)throw new IOException("没有获得安装权限");apply=true;native=args[0]=="--apply-native";}
    else if(args.Length!=0&&!preview)throw new IOException("不支持的设置中心参数");
    if(!apply)plan.Root=Plan.InstallRoot;
    bool webReady=!native&&Runtimes.Observe("WebView2",Runtimes.WebView).ready==true;
    if(!webReady&&capture!=null)throw new IOException("缺少 WebView2，H5 截图未执行");
    if(webReady){int code=RunWeb(plan,preview,apply,capture);if(code!=2||capture!=null)return code;}
    using(var fallback=new BootstrapForm(plan,preview,apply)){Application.Run(fallback);return 0;}
   }catch(Exception e){if(errorPath!=null)Json.Save(errorPath,new{Error=e.ToString(),SystemActions="NOT_RUN"});else MessageBox.Show(e.Message,WebAssets.ProductName,MessageBoxButtons.OK,MessageBoxIcon.Error);return 1;}
  }
  [System.Runtime.CompilerServices.MethodImpl(System.Runtime.CompilerServices.MethodImplOptions.NoInlining)]
  static int RunWeb(Plan plan,bool preview,bool apply,string capture) {using(var form=new WebSetupForm(plan,preview,apply,capture)){Application.Run(form);return form.CaptureExitCode;}}
 }
}

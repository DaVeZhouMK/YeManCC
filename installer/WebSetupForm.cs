using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using Microsoft.Web.WebView2.Core;
using Microsoft.Web.WebView2.WinForms;

namespace YeMan.Setup {
 public sealed class WebSetupForm : Form {
  readonly Plan plan;readonly bool preview,applyOnLoad;readonly string capture;
  readonly WebView2 browser=new WebView2();readonly Timer padTimer=new Timer();
  bool ready,running,applyClaimed;ushort previous;int lastAxis;DateTime repeat;
  string trustedDocument;
  int engineInvocations;
  public int CaptureExitCode;
  void Trace(string text){if(capture!=null)File.AppendAllText(Path.Combine(capture,"TRACE.log"),DateTime.UtcNow.ToString("o")+" "+text+Environment.NewLine);}
  public WebSetupForm(Plan selected,bool readOnly,bool apply,string captureDirectory) {
   plan=selected;preview=readOnly;applyOnLoad=apply;capture=captureDirectory;
   Text=WebAssets.ProductName;Icon=WebAssets.CreateIcon();
   FormBorderStyle=FormBorderStyle.None;StartPosition=FormStartPosition.CenterScreen;ClientSize=new Size(820,580);MinimumSize=new Size(720,540);BackColor=Color.FromArgb(247,249,252);AutoScaleMode=AutoScaleMode.None;
   browser.Dock=DockStyle.Fill;browser.DefaultBackgroundColor=BackColor;Controls.Add(browser);
   Shown+=async(s,e)=>await Initialize();FormClosing+=(s,e)=>{if(running)e.Cancel=true;};
   FormClosed+=(s,e)=>{padTimer.Stop();padTimer.Dispose();browser.Dispose();};padTimer.Interval=40;padTimer.Tick+=(s,e)=>Pad();
  }
  async Task Initialize() {
   try {
    Trace("loading embedded WebView2 SDK");
    WebAssets.PrepareRuntimeSdk();
    string parent=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"YeManCC-Setup","web-profile");
    SafeFiles.NoLinks(parent);Directory.CreateDirectory(parent);
    var environment=await CoreWebView2Environment.CreateAsync(null,Path.Combine(parent,Guid.NewGuid().ToString("N")),new CoreWebView2EnvironmentOptions());
    Trace("environment created");
    await browser.EnsureCoreWebView2Async(environment);
    Trace("controller ready");
    var core=browser.CoreWebView2;
    core.Settings.AreDefaultContextMenusEnabled=false;core.Settings.AreDevToolsEnabled=false;core.Settings.AreHostObjectsAllowed=false;core.Settings.IsStatusBarEnabled=false;core.Settings.AreBrowserAcceleratorKeysEnabled=false;
    core.NavigationStarting+=(s,e)=>{Trace("navigation URI="+e.Uri.Substring(0,Math.Min(80,e.Uri.Length)));if(e.Uri!="about:blank"&&e.Uri!=trustedDocument)e.Cancel=true;};
    core.NewWindowRequested+=(s,e)=>e.Handled=true;core.DownloadStarting+=(s,e)=>e.Cancel=true;core.PermissionRequested+=(s,e)=>e.State=CoreWebView2PermissionState.Deny;
    core.AddWebResourceRequestedFilter("*",CoreWebView2WebResourceContext.All);
    core.WebResourceRequested+=(s,e)=>{if(!e.Request.Uri.StartsWith("about:",StringComparison.OrdinalIgnoreCase)&&!e.Request.Uri.StartsWith("data:",StringComparison.OrdinalIgnoreCase))e.Response=environment.CreateWebResourceResponse(new MemoryStream(),403,"Local only","");};
    core.WebMessageReceived+=Message;
    core.NavigationCompleted+=async(s,e)=>{Trace("navigation completed "+e.IsSuccess+" "+e.WebErrorStatus);if(capture!=null&&!ready){await Task.Delay(300);string status=await core.ExecuteScriptAsync("JSON.stringify({ready:!!window.YMCCSetup,chrome:!!window.chrome?.webview,text:document.body.innerText.slice(0,120)})");Trace("document "+status);}};
    string html="<!doctype html><html lang='zh-CN'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><meta http-equiv='Content-Security-Policy' content=\"default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src data:; connect-src 'none'; frame-src 'none'; form-action 'none'; base-uri 'none'\"><style>html,body{margin:0;padding:0;background:transparent}body{overflow:hidden}</style></head><body>"+WebAssets.Html+"</body></html>";
    trustedDocument="data:text/html;charset=utf-8;base64,"+Convert.ToBase64String(Encoding.UTF8.GetBytes(html));
    core.NavigateToString(html);
    await Task.Delay(15000);
    if(!IsDisposed&&!ready){CaptureExitCode=1;Trace("UI handshake timeout");if(capture!=null){Json.Save(Path.Combine(capture,"ERROR.json"),new{Error="UI handshake timed out",SystemActions="NOT_RUN"});Close();}else MessageBox.Show(this,"本地界面未完成初始化，请重新打开设置中心。","界面未就绪",MessageBoxButtons.OK,MessageBoxIcon.Information);}
   }catch(Exception e){CaptureExitCode=1;if(capture!=null){Json.Save(Path.Combine(capture,"ERROR.json"),new{Error=e.ToString()});Close();}else MessageBox.Show(this,e.Message,"无法打开本地设置界面",MessageBoxButtons.OK,MessageBoxIcon.Information);}
  }
  object Scan(string root){return DetectionSnapshot(root);}
  internal static object DetectionSnapshot(string root) {
   var installation=Engine.AssessInstall();
   var system=Native.SystemSupport();if(!system.Allowed){installation.Allowed=false;installation.Error=system.Error;}
   return new {localVersion=BuildInfo.ProgramVersion,hwVersion=BuildInfo.HwVersion,root=root,installedVersion=installation.InstalledVersion,installation=installation,
    runtimes=new[]{new RuntimeObservation{name=system.Label,ready=system.Allowed,error=system.Error},Runtimes.Observe("WebView2 Runtime · x64",Runtimes.WebView),Runtimes.Observe(".NET 10 桌面运行时 · x64",()=>Runtimes.DesktopVersion()>=new Version(10,0,12)),Runtimes.Observe("VC++ v14 · x64",()=>Runtimes.VC("x64")),Runtimes.Observe("VC++ v14 · x86",()=>Runtimes.VC("x86")),Runtimes.Observe("HidHide 手柄驱动",()=>new HidHidePlatform().Observe().Ready)}};
  }
  void Send(object data) {if(!IsDisposed&&browser.CoreWebView2!=null)browser.CoreWebView2.PostWebMessageAsJson(Json.Write(data));}
  void Notice(string text){Send(new{type="notice",message=text});}
  static string Value(Dictionary<string,object> d,string key){object v;return d.TryGetValue(key,out v)?Convert.ToString(v):"";}
  async void Message(object sender,CoreWebView2WebMessageReceivedEventArgs e) {
   try {
    Trace("message source="+e.Source.Substring(0,Math.Min(80,e.Source.Length))+" content="+e.WebMessageAsJson.Substring(0,Math.Min(160,e.WebMessageAsJson.Length)));
    if((e.Source!="about:blank"&&e.Source!=trustedDocument)||e.WebMessageAsJson.Length>32768)return;
    var data=Json.Read<Dictionary<string,object>>(e.WebMessageAsJson);string action=Value(data,"action");
    if(action=="ready") {
     if(ready)return;ready=true;Send(new{type="init",preview=preview,path=plan.Root,shortcut=plan.DesktopShortcut,defender=plan.DefenderExclusions,scan=Scan(Plan.InstallRoot)});if(capture==null)padTimer.Start();
     if(capture!=null){await CaptureUi();return;}if(applyOnLoad)await Apply(plan);return;
    }
    if(!ready)return;
    if(action=="close"){if(!running)Close();return;}
    if(action=="minimize"){WindowState=FormWindowState.Minimized;return;}
    if(action=="maximize"){WindowState=WindowState==FormWindowState.Maximized?FormWindowState.Normal:FormWindowState.Maximized;return;}
    if(action=="drag"){ReleaseCapture();SendMessage(Handle,0xA1,new IntPtr(2),IntPtr.Zero);return;}
    if(running)return;
    if(action=="scan"){Send(new{type="scan",scan=Scan(Plan.InstallRoot)});return;}
    if(action=="browse"||action=="folder-select"){Notice("此版本固定安装到 C:\\SOFT\\YeMan，暂不支持更换位置");return;}
    if(action=="apply") {
     // Native enforcement precedes parsing, elevation, state writes, or engine calls.
     if(preview){Notice("当前为真实界面预览，安装已在程序端禁用。没有安装、下载或修改杀毒设置。");return;}
     if(applyClaimed)return;object p;if(!data.TryGetValue("plan",out p))throw new IOException("缺少准备计划");await Apply(Json.Read<Plan>(Json.Write(p)));
    }
   }catch(Exception ex){Notice(ex.Message);}
  }
  async Task Apply(Plan selected) {
   if(preview)throw new IOException("预览禁止执行安装");
   if(running||applyClaimed)return;Plan.Validate(selected);selected.Root=SafeFiles.Full(selected.Root);
   var assessment=Engine.AssessInstall();if(!assessment.Allowed)throw new IOException(assessment.Error);
   string current=assessment.InstalledVersion;
   if(assessment.Action!="recover"&&(selected.ExpectedInstalledVersion==null||selected.ExpectedInstalledVersion!=current)){Send(new{type="scan",scan=Scan(selected.Root)});throw new IOException("YMCC 版本已变化，请核对显示的版本后再安装");}
   applyClaimed=true;
   if(!Native.IsAdmin) {
    try {string data=Convert.ToBase64String(Encoding.UTF8.GetBytes(Json.Write(selected)));Process.Start(new ProcessStartInfo(System.Reflection.Assembly.GetExecutingAssembly().Location,"--apply "+data){UseShellExecute=true,Verb="runas"});Close();}
    catch {applyClaimed=false;throw;}return;
   }
   running=true;Send(new{type="progress",message="正在核对本地核心资源"});
   Receipt receipt;
   try {engineInvocations++;receipt=await Task.Run(()=>new Engine(text=>{if(!IsDisposed)BeginInvoke((Action)(()=>Send(new{type="progress",message=text})));}).Execute(selected));}
   catch(Exception e){receipt=new Receipt{Root=selected.Root,Phase="failed",Error=e.Message};}
   finally{running=false;}
   if(receipt.Phase=="confirmation-required"){applyClaimed=false;Send(new{type="confirmation",scan=Scan(selected.Root),message="上次安装已恢复，请确认版本后继续"});return;}
   Send(new{type="result",result=receipt});
  }
  void Pad() {
   if(!ready||running||Form.ActiveForm!=this||!ContainsFocus)return;
   Native.PadState p;try{p=Native.Pad();}catch{return;}
   ushort pressed=(ushort)(p.Buttons&~previous);previous=p.Buttons;
   if((pressed&0x1000)!=0)Input("a");else if((pressed&0x2000)!=0)Input("b");else if((pressed&0x4000)!=0)Input("x");
   int axis=(p.Buttons&1)!=0||p.LY>18000?1:(p.Buttons&2)!=0||p.LY<-18000?2:(p.Buttons&4)!=0||p.LX<-18000?3:(p.Buttons&8)!=0||p.LX>18000?4:0;
   if(axis!=0&&(axis!=lastAxis||DateTime.UtcNow>=repeat)){Input(axis==1?"up":axis==2?"down":axis==3?"left":"right");repeat=DateTime.UtcNow.AddMilliseconds(axis==lastAxis?150:380);}lastAxis=axis;
  }
  void Input(string name){browser.CoreWebView2.ExecuteScriptAsync("window.YMCCSetup.input("+Json.Write(name)+")");}
  [DllImport("user32.dll")]static extern bool ReleaseCapture();
  [DllImport("user32.dll")]static extern IntPtr SendMessage(IntPtr window,int message,IntPtr wParam,IntPtr lParam);
  async Task CaptureUi() {
   // Product-owned rendering API: capture the real embedded page, never a mock image.
   try {
    var observations=new List<object>();
    if(Text!=WebAssets.ProductName||Icon==null)throw new IOException("Product title or window icon is missing");
    await Task.Delay(180);await CapturePage("environment-default-820x580.png");
    foreach(int height in new[]{540,580,720,800}) {
     int width=height==540?720:height==580?820:1280;
     ClientSize=new Size(width,height);await Task.Delay(180);
     await browser.CoreWebView2.ExecuteScriptAsync("if(window.YMCCSetup.getState().view!=='home')window.YMCCSetup.back();");await Task.Delay(80);
     await CapturePage("environment-"+width+"x"+height+".png");
     await browser.CoreWebView2.ExecuteScriptAsync("window.YMCCSetup.next();");await Task.Delay(160);
     await CapturePage("confirmation-"+width+"x"+height+".png");
     string state=await browser.CoreWebView2.ExecuteScriptAsync("({ui:window.YMCCSetup.getState(),width:innerWidth,height:innerHeight,overflow:document.documentElement.scrollWidth>innerWidth})");observations.Add(new{height=height,state=Json.Read<object>(state)});
     // A native-side preview gate is exercised through the same bridge as a real click.
     await browser.CoreWebView2.ExecuteScriptAsync("window.chrome.webview.postMessage({action:'apply',plan:{Root:'C:\\\\YMCC-PREVIEW-MUST-NOT-EXIST',Mode:'first'}})");await Task.Delay(100);
     string blocked=await browser.CoreWebView2.ExecuteScriptAsync("document.getElementById('ymcc-notice').textContent");observations.Add(new{height=height,previewGuard=blocked});
     if(!blocked.Contains("预览")||!blocked.Contains("禁用"))throw new IOException("Native preview guard did not report rejection");
     await browser.CoreWebView2.ExecuteScriptAsync("window.YMCCSetup.back();");await Task.Delay(80);
     observations.Add(new{height=height,navigation=Json.Read<object>(await VerifyNavigation(height))});
    }
    ClientSize=new Size(820,580);await Task.Delay(100);
    observations.Add(new{type="SYNTHETIC_INSTALL_CHOICES_LAYOUT_ONLY",states=await CaptureInstallChoices()});
    // Explicit synthetic failure input verifies only the shared result layout, not OS installation.
    Send(new{type="result",result=new Receipt{Phase="blocked-reboot",Root=Plan.InstallRoot,RebootRequired=true,Error="测试输入：运行库要求重启，YMCC 尚未安装完成"}});await Task.Delay(100);
    string pending=await browser.CoreWebView2.ExecuteScriptAsync("({title:document.getElementById('ymcc-title').textContent,phase:window.YMCCSetup.getState().result.Phase,warning:!!document.querySelector('.mc-result-symbol.failed')})");
    var pendingState=Json.Read<Dictionary<string,object>>(pending);
    if(Convert.ToString(pendingState["title"])!="请重启后继续安装"||Convert.ToString(pendingState["phase"])!="blocked-reboot"||!Convert.ToBoolean(pendingState["warning"]))throw new IOException("Pending-reboot layout falsely reports completion");
    await CapturePage("pending-reboot-SYNTHETIC-layout.png");observations.Add(new{type="SYNTHETIC_FAILURE_LAYOUT_ONLY",state=pendingState});
    if(engineInvocations!=0||running||applyClaimed)throw new IOException("Preview invoked an installation path");
    Json.Save(Path.Combine(capture,"RENDER.json"),new{Type="ACTUAL_WEBVIEW2_RENDER",ProductName=Text,IconSha256=WebAssets.IconHash,UiSha256=WebAssets.HtmlHash,ArtifactSha256=SafeFiles.Hash(System.Reflection.Assembly.GetExecutingAssembly().Location),Preview=true,EngineInvocations=engineInvocations,SystemActions="NOT_RUN",NetworkRequests="BLOCKED_BY_HOST_AND_CSP",observations=observations});
   }catch(Exception e){CaptureExitCode=1;Json.Save(Path.Combine(capture,"ERROR.json"),new{Error=e.ToString()});}finally{Close();}
  }
  async Task CapturePage(string name) {
   using(var file=new FileStream(Path.Combine(capture,name),FileMode.CreateNew,FileAccess.Write,FileShare.None)) {
    var pending=browser.CoreWebView2.CapturePreviewAsync(CoreWebView2CapturePreviewImageFormat.Png,file);
    if(await Task.WhenAny(pending,Task.Delay(6000))!=pending)throw new IOException("实际界面截图超时："+name);
    await pending;
   }
  }
  async Task<object> CaptureInstallChoices() {
   string original=await browser.CoreWebView2.ExecuteScriptAsync("window.YMCCSetup.getState().scan");
   var observations=new List<object>();
   await browser.CoreWebView2.ExecuteScriptAsync("(()=>{const s="+original+";const w=s.runtimes.find(r=>r.name.startsWith('WebView2'));w.ready=false;w.error=null;window.YMCCSetup.receive({type:'scan',scan:s});})()");await Task.Delay(100);
   string webviewStatus=await browser.CoreWebView2.ExecuteScriptAsync("[...document.querySelectorAll('.mc-runtime-row')].find(r=>r.textContent.includes('WebView2')).querySelector('.mc-badge').textContent");
   if(Json.Read<string>(webviewStatus)!="联网补齐")throw new IOException("WebView2 缺失提示与联网规则不一致");
   observations.Add(new{type="SYNTHETIC_MISSING_WEBVIEW_DETECTION_ONLY",status=webviewStatus});
   await browser.CoreWebView2.ExecuteScriptAsync("(()=>{const s="+original+";s.runtimes.slice(1).forEach(r=>{r.ready=false;r.error=null;});window.YMCCSetup.receive({type:'scan',scan:s});})()");await Task.Delay(100);
   string missing=await browser.CoreWebView2.ExecuteScriptAsync("({count:document.querySelectorAll('.mc-runtime-row').length,automatic:[...document.querySelectorAll('.mc-badge')].filter(e=>e.textContent==='自动补齐').length,network:[...document.querySelectorAll('.mc-badge')].filter(e=>e.textContent==='联网补齐').length})");
   var missingState=Json.Read<Dictionary<string,object>>(missing);
   if(Convert.ToInt32(missingState["count"])!=6||Convert.ToInt32(missingState["automatic"])!=4||Convert.ToInt32(missingState["network"])!=1)throw new IOException("Compact missing-runtime state lost installation or network information");
   await CapturePage("environment-missing-SYNTHETIC-layout.png");observations.Add(new{type="SYNTHETIC_MISSING_RUNTIME_LAYOUT_ONLY",state=missingState});
   await browser.CoreWebView2.ExecuteScriptAsync("window.YMCCSetup.receive({type:'scan',scan:"+original+"})");
   await browser.CoreWebView2.ExecuteScriptAsync("(()=>{const s="+original+";s.runtimes[0].ready=null;s.runtimes[0].error='测试输入：环境读取权限不足';window.YMCCSetup.receive({type:'scan',scan:s});})()");await Task.Delay(100);
   string failedRuntime=await browser.CoreWebView2.ExecuteScriptAsync("({count:document.querySelectorAll('.mc-runtime-row').length,text:document.querySelector('.mc-runtime-row .mc-badge').textContent,nextDisabled:document.getElementById('ymcc-next').disabled})");
   var failedState=Json.Read<Dictionary<string,object>>(failedRuntime);
   if(Convert.ToInt32(failedState["count"])!=6||Convert.ToString(failedState["text"])!="检测失败"||Convert.ToBoolean(failedState["nextDisabled"]))throw new IOException("环境读取错误使页面失去响应："+failedRuntime);
   await CapturePage("environment-read-error-SYNTHETIC-layout.png");observations.Add(new{type="SYNTHETIC_RUNTIME_READ_ERROR_LAYOUT_ONLY",state=failedState});
   var examples=new[]{
    new {Name="fresh",Version="",Action="install",Allowed=true,Label="安装 YMCC "+BuildInfo.ProgramVersion,Button="开始安装 →"},
    new {Name="older",Version="0.0.38",Action="update",Allowed=true,Label="YMCC 0.0.38 → "+BuildInfo.ProgramVersion+" · 保留设置",Button="更新安装 →"},
    new {Name="same",Version=BuildInfo.ProgramVersion,Action="reinstall",Allowed=true,Label="YMCC "+BuildInfo.ProgramVersion+" · 同版重新安装，保留设置",Button="重新安装 →"},
    new {Name="newer",Version="0.0.40",Action="blocked",Allowed=false,Label="当前 0.0.40 · 包内 "+BuildInfo.ProgramVersion,Button="开始安装 →"},
    new {Name="unknown",Version="unknown",Action="blocked",Allowed=false,Label="当前 版本未知 · 包内 "+BuildInfo.ProgramVersion,Button="开始安装 →"},
    new {Name="recover",Version="unknown",Action="recover",Allowed=true,Label="上次安装未完成 · 恢复后确认版本",Button="恢复并继续 →"}
   };
   foreach(var example in examples) {
    string data=Json.Write(new{installedVersion=example.Version,installation=new InstallAssessment{InstalledVersion=example.Version,PackageVersion=BuildInfo.ProgramVersion,Action=example.Action,Allowed=example.Allowed,Label=example.Label,Error=example.Allowed?null:"测试输入：现有版本不可覆盖"}});
    await browser.CoreWebView2.ExecuteScriptAsync("(()=>{const s=Object.assign("+original+","+data+");window.YMCCSetup.receive({type:'scan',scan:s});if(window.YMCCSetup.getState().view==='home')window.YMCCSetup.next();})()");await Task.Delay(100);
    string raw=await browser.CoreWebView2.ExecuteScriptAsync("({label:document.getElementById('ymcc-version-change').textContent,button:document.getElementById('ymcc-next').textContent,disabled:document.getElementById('ymcc-next').disabled,view:window.YMCCSetup.getState().view})");
    var state=Json.Read<Dictionary<string,object>>(raw);
    if(Convert.ToString(state["label"])!=example.Label.Replace("YMCC","YeManCC")||Convert.ToString(state["button"])!=example.Button||Convert.ToBoolean(state["disabled"])==example.Allowed||Convert.ToString(state["view"])!="review")throw new IOException("安装版本界面分支不正确："+example.Name+" "+raw);
    await CapturePage("installation-"+example.Name+"-SYNTHETIC-layout.png");observations.Add(new{input=example,state=state});
   }
   await browser.CoreWebView2.ExecuteScriptAsync("window.YMCCSetup.receive({type:'progress',message:'测试输入：恢复中'});window.YMCCSetup.receive({type:'confirmation',scan:"+original+",message:'测试输入：恢复后确认版本'})");await Task.Delay(100);
   string restored=await browser.CoreWebView2.ExecuteScriptAsync("({view:window.YMCCSetup.getState().view,logs:window.YMCCSetup.getState().logs.length,result:window.YMCCSetup.getState().result,notice:document.getElementById('ymcc-notice').textContent})");
   var restoredState=Json.Read<Dictionary<string,object>>(restored);
   if(Convert.ToString(restoredState["view"])!="review"||Convert.ToInt32(restoredState["logs"])!=0||restoredState["result"]!=null||Convert.ToString(restoredState["notice"])!="测试输入：恢复后确认版本")throw new IOException("恢复后未回到版本确认界面："+restored);
   await CapturePage("recovery-confirmation-SYNTHETIC-layout.png");observations.Add(new{type="SYNTHETIC_RECOVERY_CONFIRMATION_LAYOUT_ONLY",state=restoredState});return observations;
  }
  async Task<string> VerifyNavigation(int height) {
   Trace("navigation assertions start "+height);
   string script=@"(async()=>{
const app=window.YMCCSetup,root=document.getElementById('ymcc-setup-real'),sleep=()=>new Promise(r=>setTimeout(r,60)),results=[];
const check=(name,ok)=>{results.push({name,pass:!!ok});window.__uiVerificationPartial=results;if(!ok)throw new Error(name);};
check('step 1 displays actual system and five environment results',app.getState().view==='home'&&root.querySelectorAll('.mc-runtime-row').length===6&&/^Windows /.test(app.getState().scan.runtimes[0].name));
check('all environment rows fit the initial viewport',(()=>{const main=root.querySelector('.mc-main').getBoundingClientRect();return [...root.querySelectorAll('.mc-runtime-row')].every(e=>{const b=e.getBoundingClientRect();return b.top>=main.top&&b.bottom<=main.bottom;});})());
check('bundled version is visible in step 1',root.querySelector('#ymcc-package-version').textContent.includes(app.getState().scan.localVersion));
check('product name is shown once in the title bar',root.querySelector('.mc-app-brand strong').textContent==='YeManCC 野蛮系统控制中心');
check('shared product icons render without placeholders',[...root.querySelectorAll('.mc-app-brand img,.mc-version-card img')].length===2&&[...root.querySelectorAll('.mc-app-brand img,.mc-version-card img')].every(e=>e.complete&&e.naturalWidth>0&&e.src.startsWith('data:image/png;base64,')));
check('home contains one action and no redundant headings or badges',root.querySelector('.mc-heading').hidden&&!root.querySelector('.mc-step-line,.mc-kicker,.mc-edition,.mc-preview-tag,#ymcc-footer-note')&&[...root.querySelectorAll('.mc-flow button')].filter(e=>!e.hidden).length===1);
check('home fits without scrolling',(()=>{const main=root.querySelector('.mc-main');return main.scrollHeight<=main.clientHeight+1&&document.documentElement.scrollWidth<=innerWidth;})());
check('step 1 has no purpose reset or online selectors',!root.querySelector('[name=ymcc-purpose],#ymcc-reset,#ymcc-online,select,.mc-custom'));
root.querySelector('#ymcc-next').focus();app.input('right');check('right stops at step 1 visual row edge',document.activeElement.id==='ymcc-next');
app.input('a');await sleep();check('A advances to installation confirmation',app.getState().view==='review');
check('path and shortcut are immediately visible',!!root.querySelector('#ymcc-fixed-path')&&!!root.querySelector('#ymcc-shortcut')&&!root.querySelector('details'));
check('step 2 exposes no update reset or online options',!root.querySelector('[name=ymcc-purpose],#ymcc-reset,#ymcc-online,select,.mc-custom'));
check('installation root is fixed without editable or browse controls',root.querySelector('#ymcc-fixed-path').textContent===app.getState().path&&!root.querySelector('#ymcc-path,[data-action=browse]'));
check('installation options fit without scrolling',(()=>{const main=root.querySelector('.mc-main');return main.scrollHeight<=main.clientHeight+1;})());
const shortcut=root.querySelector('#ymcc-shortcut'),original=shortcut.checked;shortcut.focus();app.input('a');check('A toggles desktop shortcut',app.getState().shortcut===!original);app.input('a');check('A restores desktop shortcut',app.getState().shortcut===original);
app.input('down');check('down reaches Defender option',document.activeElement.id==='ymcc-defender');
const defender=root.querySelector('#ymcc-defender');check('Defender defaults to explicitly visible enabled option',defender.checked&&app.getState().defender);
app.input('a');check('A can opt out of Defender exclusions',!defender.checked&&!app.getState().defender);app.input('a');check('A restores default Defender exclusions',defender.checked&&app.getState().defender);
app.input('right');check('right stops at Defender checkbox visual edge',document.activeElement===defender);
app.input('up');check('up returns to shortcut checkbox',document.activeElement===shortcut);
const valid=e=>!e.disabled&&!e.closest('[hidden]')&&e.getClientRects().length&&(!e.checkVisibility||e.checkVisibility())&&(!e.closest('details')||e.closest('details').open||e.tagName==='SUMMARY');
const all=[...root.querySelectorAll('button,input,summary')].filter(e=>valid(e)&&!e.closest('.mc-window-controls'));
const seen=new Set(),queue=[root.querySelector('#ymcc-next')];while(queue.length){const e=queue.shift();if(seen.has(e))continue;seen.add(e);for(const dir of ['up','down','left','right']){e.focus();app.input(dir);const to=document.activeElement;if(all.includes(to)&&!seen.has(to))queue.push(to);}}
check('all enabled controls reachable by direction',all.every(e=>seen.has(e)));
const main=root.querySelector('.mc-main').getBoundingClientRect();let inside=true;for(const e of all){e.focus();e.scrollIntoView({block:'nearest'});const r=e.getBoundingClientRect();if(e.closest('.mc-main')&&(r.top<main.top-2||r.bottom>main.bottom+2))inside=false;}
check('scrolled focus stays fully visible',inside);app.input('b');await sleep();check('B returns to home',app.getState().view==='home');
check('preview never manufactures completed state',app.getState().result===null&&app.getState().logs.length===0);
return {results,count:results.length,failures:results.filter(r=>!r.pass).length};})()";
   await browser.CoreWebView2.ExecuteScriptAsync("window.__uiVerification=null;window.__uiVerificationPartial=[];"+script+".then(value=>window.__uiVerification=value).catch(error=>window.__uiVerification={error:String(error.stack||error),partial:window.__uiVerificationPartial});");
   for(int i=0;i<60;i++) {
    await Task.Delay(100);string output=await browser.CoreWebView2.ExecuteScriptAsync("window.__uiVerification");
    if(string.IsNullOrWhiteSpace(output)||output=="null"||output=="{}")continue;
    using(var file=new FileStream(Path.Combine(capture,"NAVIGATION-"+height+"-RAW.json"),FileMode.CreateNew,FileAccess.Write,FileShare.None))using(var writer=new StreamWriter(file,new UTF8Encoding(false)))writer.Write(output);
    var result=Json.Read<Dictionary<string,object>>(output);
    if(result==null||result.ContainsKey("error")||!result.ContainsKey("count")||Convert.ToInt32(result["count"])<18||Convert.ToInt32(result["failures"])!=0)throw new IOException("Actual UI navigation failed: "+output);
    Trace("navigation assertions completed "+height);
    return output;
   }
   throw new IOException("Actual UI navigation did not return a bounded result");
  }
 }
}

using System;
using System.Diagnostics;
using System.Drawing;
using System.Reflection;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace YeMan.Setup {
 // Native fallback keeps installation available without a browser runtime.
 public sealed class BootstrapForm : Form {
  readonly Plan plan;readonly bool preview,apply;
  readonly Label title;readonly TextBox message;readonly CheckBox shortcut,defender,web;
  readonly Button next,back;readonly Timer padTimer=new Timer();ushort previous;int page;bool busy;
  public BootstrapForm(Plan selected,bool readOnly,bool elevatedApply) {
   plan=selected;preview=readOnly;apply=elevatedApply;
   Text=WebAssets.ProductName;Icon=WebAssets.CreateIcon();ClientSize=new Size(600,350);FormBorderStyle=FormBorderStyle.FixedDialog;MaximizeBox=false;StartPosition=FormStartPosition.CenterScreen;
   BackColor=Color.FromArgb(247,249,252);ForeColor=Color.FromArgb(25,43,64);Font=new Font("Microsoft YaHei UI",10);
   title=new Label{Location=new Point(28,25),AutoSize=true,Font=new Font("Microsoft YaHei UI",20)};
   message=new TextBox{Location=new Point(30,88),Size=new Size(540,190),ReadOnly=true,Multiline=true,BackColor=BackColor,ForeColor=ForeColor,BorderStyle=BorderStyle.None,ScrollBars=ScrollBars.Vertical,TabStop=false};
   shortcut=new CheckBox{Location=new Point(30,156),AutoSize=true,Text="创建桌面快捷方式",Checked=plan.DesktopShortcut};
   defender=new CheckBox{Location=new Point(30,192),AutoSize=true,Text="添加 Defender 排除项（仅核心文件）",Checked=plan.DefenderExclusions};
   web=new CheckBox{Location=new Point(30,228),AutoSize=true,Text="缺少 WebView2 时联网补装",Checked=plan.DownloadWebView};
   back=new Button{Text="上一步",Location=new Point(328,292),Size=new Size(100,42)};
   next=new Button{Location=new Point(442,292),Size=new Size(128,42),FlatStyle=FlatStyle.Flat,BackColor=Color.FromArgb(8,100,185),ForeColor=Color.White};
   Controls.AddRange(new Control[]{title,message,shortcut,defender,web,back,next});AcceptButton=next;
   next.Click+=async (s,e)=>await Forward();back.Click+=(s,e)=>{page=0;Render();};
   FormClosing+=(s,e)=>{if(busy)e.Cancel=true;};
   padTimer.Interval=45;padTimer.Tick+=(s,e)=>PollPad();
   Shown+=async(s,e)=>{padTimer.Start();if(apply&&!preview)await Install();};
   FormClosed+=(s,e)=>{padTimer.Stop();padTimer.Dispose();};Render();
  }
  void Render() {
   shortcut.Visible=defender.Visible=web.Visible=back.Visible=page==1;
   next.Enabled=page!=2;message.Height=page==1?56:190;
   if(page==0){title.Text="安装 YeManCC "+BuildInfo.ProgramVersion;message.Text="WebView2 当前不可用，可以离线安装。";next.Text="下一步";}
   if(page==1){title.Text="确认安装";message.Text=Plan.InstallRoot+Environment.NewLine+"为所有用户安装，保留设置。";next.Text="开始安装";}
   if(page==2){title.Text="正在安装";message.Text="正在准备文件…";next.Text="正在安装";}
   if(page==3)next.Text="关闭";
   if(next.Enabled)next.Focus();
  }
  void PollPad() {
   if(Form.ActiveForm!=this||busy)return;
   Native.PadState current;try{current=Native.Pad();}catch{return;}ushort pressed=(ushort)(current.Buttons&~previous);previous=current.Buttons;
   if((pressed&0x0005)!=0)SelectNextControl(ActiveControl,false,true,true,true);
   if((pressed&0x000a)!=0)SelectNextControl(ActiveControl,true,true,true,true);
   if((pressed&0x1000)!=0){var button=ActiveControl as Button;if(button!=null)button.PerformClick();else{var check=ActiveControl as CheckBox;if(check!=null)check.Checked=!check.Checked;}}
   if((pressed&0x2000)!=0){if(page==1){page=0;Render();}else Close();}
  }
  async Task Forward() {
   if(busy)return;
   if(page==0){page=1;Render();return;}
   if(page==3){Close();return;}
   if(preview){message.Text="预览模式，不执行安装。";return;}
   plan.DesktopShortcut=shortcut.Checked;plan.DefenderExclusions=defender.Checked;plan.DownloadWebView=web.Checked;
   if(!Native.IsAdmin) {
    try {
     string payload=Convert.ToBase64String(Encoding.UTF8.GetBytes(Json.Write(plan)));
     using(Process.Start(new ProcessStartInfo(Assembly.GetExecutingAssembly().Location,"--apply-native "+payload){UseShellExecute=true,Verb="runas"})){}Close();
    }catch(System.ComponentModel.Win32Exception e){message.Text=e.NativeErrorCode==1223?"未授权管理员权限，可以重试。":e.Message;}
    return;
   }
   await Install();
  }
  async Task Install() {
   busy=true;page=2;Render();Receipt result;
   try{result=await Task.Run(()=>new Engine(text=>{if(!IsDisposed)BeginInvoke((Action)(()=>message.Text=text));}).Execute(plan));}
   catch(Exception e){result=new Receipt{Phase="failed",Root=plan.Root,Error=e.Message};}
   busy=false;page=3;Render();bool ok=result.Phase=="completed"||result.Phase=="reboot-required";
   title.Text=ok?"安装完成":"安装未完成";
   var body=new StringBuilder();body.AppendLine(ok?"YeManCC 程序文件已安装。":result.Error);
   if(ok&&result.RebootRequired)body.AppendLine("请重启后再检查组件。");
   if(ok)foreach(string warning in result.Warnings)body.AppendLine("• "+warning);
   if(result.Defender.Status=="warning"||result.Defender.Status=="cleanup-required"||result.Defender.Status=="unavailable")body.AppendLine(result.Defender.Message);
   body.AppendLine(result.Root);
   if(!string.IsNullOrEmpty(result.Backup))body.AppendLine("记录与备份："+result.Backup);
   message.Text=body.ToString();
  }
 }
}

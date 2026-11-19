using System;
using System.Drawing;
using System.IO;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace YeMan.Setup {
 // This small, OS-provided .NET window is used ONLY if WebView2 is absent.
 // Only the signed small Microsoft bootstrapper is embedded; it downloads the runtime.
 public sealed class BootstrapForm : Form {
  readonly Label message;readonly Button prepare;readonly bool preview;bool busy;readonly Timer padTimer=new Timer();ushort previous;
  public BootstrapForm(bool readOnly) {
   preview=readOnly;Text=WebAssets.ProductName;Icon=WebAssets.CreateIcon();Size=new Size(620,380);MinimumSize=Size;MaximumSize=Size;StartPosition=FormStartPosition.CenterScreen;
   BackColor=Color.FromArgb(247,249,252);ForeColor=Color.FromArgb(25,43,64);Font=new Font("Microsoft YaHei UI",10);
   Controls.Add(new Label{Text="准备界面环境",Location=new Point(32,34),AutoSize=true,Font=new Font("Microsoft YaHei UI",21)});
   message=new Label{Text="将安装 YeManCC "+BuildInfo.ProgramVersion+"。\r\n此电脑缺少 WebView2，联网补齐后自动继续。",Location=new Point(34,100),Size=new Size(540,115)};Controls.Add(message);
   prepare=new Button{Text=preview?"预览模式：未安装环境":"联网补齐并继续",Location=new Point(292,244),Size=new Size(270,46),FlatStyle=FlatStyle.Flat,BackColor=Color.FromArgb(8,121,217),ForeColor=Color.White,Enabled=!preview};Controls.Add(prepare);
   prepare.Click+=async (s,e)=>await Prepare();FormClosing+=(s,e)=>{if(busy)e.Cancel=true;};
   padTimer.Interval=45;padTimer.Tick+=(s,e)=>{if(Form.ActiveForm!=this||busy)return;var p=Native.Pad();ushort pressed=(ushort)(p.Buttons&~previous);previous=p.Buttons;if((pressed&0x1000)!=0&&prepare.Enabled)prepare.PerformClick();if((pressed&0x2000)!=0)Close();};Shown+=(s,e)=>{prepare.Focus();padTimer.Start();};FormClosed+=(s,e)=>{padTimer.Stop();padTimer.Dispose();};
  }
  async Task Prepare() {
   if(preview)return;busy=true;prepare.Enabled=false;
   message.Text="正在核验微软引导程序并联网补齐 WebView2。\r\n完成后自动进入安装向导，请保持网络连接。";
   try {
    string here=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"YeManCC-Setup","bootstrap",Guid.NewGuid().ToString("N"));
    await Task.Run(()=>{if(Runtimes.WebView())return;string path=Resources.UnpackOne(here,Runtimes.WebViewBootstrapper);try{Runtimes.Run(path,"/silent /install",Runtimes.WebView);}finally{SafeFiles.NoLinks(path);if(File.Exists(path))File.Delete(path);}});
    if(!Runtimes.WebView())throw new IOException("补装后仍未检测到所需 WebView2，请重启后重新打开安装器。");
    DialogResult=DialogResult.OK;busy=false;Close();
   }catch(Exception e){message.Text=e.Message+"\r\n请检查网络连接，或先安装 WebView2 后再打开安装器。";prepare.Enabled=true;}finally{busy=false;}
  }
 }
}

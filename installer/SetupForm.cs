using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Reflection;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
namespace YeMan.Setup {
 public sealed class SoftButton : Button {
  public bool Primary; public bool Selected;
  public SoftButton(){FlatStyle=FlatStyle.Flat;FlatAppearance.BorderSize=0;BackColor=Color.FromArgb(35,43,58);ForeColor=Color.White;Font=new Font("Microsoft YaHei UI",11);Cursor=Cursors.Hand;SetStyle(ControlStyles.UserPaint|ControlStyles.OptimizedDoubleBuffer,true);}
  protected override void OnPaint(PaintEventArgs e){e.Graphics.SmoothingMode=SmoothingMode.AntiAlias;Color bg=Primary?Color.FromArgb(91,136,255):Selected?Color.FromArgb(48,72,111):BackColor;using(var b=new SolidBrush(Enabled?bg:Color.FromArgb(42,47,56)))e.Graphics.FillRectangle(b,ClientRectangle);if(Focused||Selected)using(var p=new Pen(Focused?Color.FromArgb(142,191,255):Color.FromArgb(92,130,183),Focused?3:1))e.Graphics.DrawRectangle(p,2,2,Width-5,Height-5);TextRenderer.DrawText(e.Graphics,Text,Font,new Rectangle(16,0,Width-32,Height),Enabled?ForeColor:Color.Gray,TextFormatFlags.Left|TextFormatFlags.VerticalCenter|TextFormatFlags.WordBreak);}
  protected override void OnGotFocus(EventArgs e){base.OnGotFocus(e);Invalidate();}protected override void OnLostFocus(EventArgs e){base.OnLostFocus(e);Invalidate();}
 }
 public sealed partial class SetupForm : Form {
  readonly Plan plan;int page;bool running;bool expanding;
  Panel body;Label title,subtitle,stepLabel;SoftButton back,next;TextBox root,log;CheckBox online,reset,shortcut;
  readonly System.Windows.Forms.Timer padTimer=new System.Windows.Forms.Timer();ushort lastButtons;int lastAxis;DateTime repeat;Receipt receipt;
  public SetupForm(Plan chosen,bool execute){
   plan=chosen??new Plan();Text="YeManCC · 首次设置中心";StartPosition=FormStartPosition.CenterScreen;Size=new Size(1040,720);MinimumSize=new Size(900,660);BackColor=Color.FromArgb(17,22,31);ForeColor=Color.FromArgb(228,235,246);Font=new Font("Microsoft YaHei UI",10);AutoScaleMode=AutoScaleMode.Dpi;
   var rail=new Panel{Dock=DockStyle.Left,Width=220,BackColor=Color.FromArgb(23,30,42)};Controls.Add(rail);
   var brand=MakeLabel("YMCC",28,40,170,48,24);brand.Font=new Font("Segoe UI",24,FontStyle.Bold);rail.Controls.Add(brand);rail.Controls.Add(MakeLabel("野蛮核心控制台",30,95,175,30,11));rail.Controls.Add(MakeLabel("首次设置中心",30,145,175,30,14));
   stepLabel=MakeLabel("",30,230,180,205,12);rail.Controls.Add(stepLabel);
   var help=MakeLabel("手柄 / 键盘 / 触屏\r\nA 确认   B 返回\r\n方向键移动焦点",28,0,185,100,10);help.Dock=DockStyle.Bottom;rail.Controls.Add(help);
   var frame=new Panel{Dock=DockStyle.Fill,Padding=new Padding(32,28,32,22)};Controls.Add(frame);frame.BringToFront();
   var foot=new Panel{Dock=DockStyle.Bottom,Height=66};frame.Controls.Add(foot);
   back=new SoftButton{Text="返回",Size=new Size(100,46),Location=new Point(0,10)};back.Click+=(s,e)=>GoBack();foot.Controls.Add(back);
   next=new SoftButton{Text="下一步  →",Size=new Size(176,46),Primary=true,Anchor=AnchorStyles.Right|AnchorStyles.Top};foot.Controls.Add(next);next.Left=foot.Width-next.Width;foot.Resize+=(s,e)=>next.Left=foot.Width-next.Width;next.Click+=(s,e)=>Forward();
   var head=new Panel{Dock=DockStyle.Top,Height=108};frame.Controls.Add(head);title=MakeLabel("",0,0,680,43,23);title.Anchor=AnchorStyles.Top|AnchorStyles.Left|AnchorStyles.Right;head.Controls.Add(title);subtitle=MakeLabel("",0,53,660,50,10);subtitle.ForeColor=Color.FromArgb(159,177,201);head.Controls.Add(subtitle);
   body=new Panel{Dock=DockStyle.Fill,AutoScroll=true};frame.Controls.Add(body);body.BringToFront();FormClosing+=(s,e)=>{if(running)e.Cancel=true;};
   padTimer.Interval=40;padTimer.Tick+=(s,e)=>PollPad();Shown+=(s,e)=>{padTimer.Start();if(execute){page=2;Render();Start();}else Render();};FormClosed+=(s,e)=>{padTimer.Stop();padTimer.Dispose();};KeyPreview=true;
  }
  static Label MakeLabel(string text,int x,int y,int w,int h,int size){return new Label{Text=text,Location=new Point(x,y),Size=new Size(w,h),Font=new Font("Microsoft YaHei UI",size),ForeColor=Color.FromArgb(228,235,246)};}
  SoftButton AddButton(string text,int y,Action action){var b=new SoftButton{Text=text,Location=new Point(0,y),Size=new Size(Math.Max(440,body.ClientSize.Width-24),70),Anchor=AnchorStyles.Left|AnchorStyles.Top|AnchorStyles.Right};b.Click+=(s,e)=>action();body.Controls.Add(b);return b;}
  void Clear(){while(body.Controls.Count>0){var c=body.Controls[0];body.Controls.Remove(c);c.Dispose();}}
  void SaveOptions(){if(root!=null&&!root.IsDisposed)plan.Root=root.Text;if(online!=null&&!online.IsDisposed)plan.Online=online.Checked;if(reset!=null&&!reset.IsDisposed)plan.ResetSettings=reset.Checked;if(shortcut!=null&&!shortcut.IsDisposed)plan.DesktopShortcut=shortcut.Checked;}
  void Render(){Clear();back.Visible=page==1;next.Enabled=!running;stepLabel.Text=(page==0?"●":"✓")+"  选择用途\r\n\r\n"+(page==1?"●":page>1?"✓":"02")+"  确认准备\r\n\r\n"+(page>=2?"●":"03")+"  自动补齐";
   if(page==0){title.Text="先把使用环境准备好";subtitle.Text="一个核心包，按需补齐。默认沿用本地资源。";next.Text="下一步  →";
    var first=AddButton("首次使用\r\n准备 YMCC 和所需环境",10,()=>{plan.Mode="first";plan.ResetSettings=false;Render();});first.Selected=plan.Mode=="first";
    var fill=AddButton("更新与补齐\r\n保留设置，补齐程序和核心依赖",94,()=>{plan.Mode="fill";plan.ResetSettings=false;Render();});fill.Selected=plan.Mode=="fill";
    var again=AddButton("重新设置\r\n重新检查；恢复默认可在下一步选择",178,()=>{plan.Mode="reset";Render();});again.Selected=plan.Mode=="reset";
    body.Controls.Add(MakeLabel("YMCC 仍为绿色程序。设置中心负责环境与首次准备。",0,279,600,54,10));(plan.Mode=="first"?first:plan.Mode=="fill"?fill:again).Focus();
   }else if(page==1){title.Text="接下来交给设置中心";subtitle.Text="确认后请求 Windows 权限，并自动完成以下准备。";next.Text="开始准备  →";
    body.Controls.Add(MakeLabel("01   YMCC 程序",0,6,600,26,13));body.Controls.Add(MakeLabel(plan.Online?"从官方更新源获取并校验":"本地 0.0.39 优先；保留已安装的较新版本",25,40,610,28,10));
    body.Controls.Add(MakeLabel("02   HWiNFO ＋ RTSS",0,81,600,27,13));body.Controls.Add(MakeLabel("准备核心调度依赖，保留已有配置",25,115,600,28,10));
    body.Controls.Add(MakeLabel("03   基础运行环境",0,156,600,27,13));body.Controls.Add(MakeLabel(Runtimes.Summary(),25,191,610,53,10));
    var custom=AddButton(expanding?"收起自定义  ▴":"自定义  ▾    默认设置可直接继续",254,()=>{SaveOptions();expanding=!expanding;Render();});custom.Height=44;
    if(expanding){body.Controls.Add(MakeLabel("绿色目录（请选专用目录）",0,316,560,25,10));root=new TextBox{Text=plan.Root,Location=new Point(0,350),Size=new Size(390,30),BackColor=Color.FromArgb(30,39,54),ForeColor=Color.White,BorderStyle=BorderStyle.FixedSingle};body.Controls.Add(root);
     var browse=new SoftButton{Text="选择目录",Location=new Point(402,345),Size=new Size(142,40)};body.Controls.Add(browse);browse.Click+=(s,e)=>{using(var dialog=new FolderPicker(root.Text)){padTimer.Stop();if(dialog.ShowDialog(this)==DialogResult.OK)root.Text=dialog.Selected;padTimer.Start();}};
     online=Check("使用在线正式版 YMCC（需要联网）",397,plan.Online);online.CheckedChanged+=(s,e)=>plan.Online=online.Checked;shortcut=Check("创建桌面入口",438,plan.DesktopShortcut);if(plan.Mode=="reset")reset=Check("备份后恢复 YMCC 首选项（不清理游戏资料）",479,plan.ResetSettings);
     body.Controls.Add(MakeLabel("缺少 WebView2 时需要联网。权限与系统防护保持正常。",0,530,590,45,9));
    }else body.Controls.Add(MakeLabel("目录："+plan.Root,0,316,600,56,10));if(expanding)root.Focus();else next.Focus();
   }else if(page==2){title.Text="正在准备，请稍候";subtitle.Text="首次准备可能需要几分钟。进度会在这里持续更新。";next.Text="准备中…";next.Enabled=false;log=new TextBox{Multiline=true,ReadOnly=true,Dock=DockStyle.Fill,ScrollBars=ScrollBars.Vertical,BackColor=Color.FromArgb(23,30,42),ForeColor=Color.FromArgb(207,222,244),BorderStyle=BorderStyle.None,Font=new Font("Microsoft YaHei UI",10),TabStop=false};body.Controls.Add(log);
   }else{bool ok=receipt!=null&&(receipt.Phase=="completed"||receipt.Phase=="reboot-required");title.Text=ok?(receipt.RebootRequired?"已准备好，等待重启":"核心环境已准备好"):"还有一步需要处理";subtitle.Text=ok?"从桌面或绿色目录打开 YMCC，即可继续功能设置。":"处理下方原因后，可重新运行设置中心。";next.Text="关闭";body.Controls.Add(MakeLabel(ok?(receipt.RebootRequired?"部分运行库要求重启。请保存工作后自行重启。":"YMCC、调度文件和基础运行库已完成准备。"):(receipt==null?"执行未完成":receipt.Error),0,10,620,150,12));
    if(receipt!=null){var detail=new TextBox{Text="绿色目录："+receipt.Root+"\r\n记录与备份："+receipt.Backup+"\r\n\r\n调度数据与硬件功能需要进入 YMCC 后确认。",Location=new Point(0,176),Size=new Size(Math.Max(480,body.Width-20),130),Multiline=true,ReadOnly=true,BackColor=BackColor,ForeColor=ForeColor,BorderStyle=BorderStyle.None};body.Controls.Add(detail);}next.Focus();}
  }
  CheckBox Check(string text,int y,bool value){var c=new CheckBox{Text=text,Checked=value,Location=new Point(0,y),Size=new Size(600,34),ForeColor=Color.White};body.Controls.Add(c);return c;}
 }
}

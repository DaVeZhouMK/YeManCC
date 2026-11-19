using System;
using System.IO;
using System.Reflection;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using System.Drawing;
namespace YeMan.Setup {
 public sealed partial class SetupForm {
  void Forward(){if(running)return;if(page==0){page=1;Render();}else if(page==1){SaveOptions();try{Engine.ValidateRoot(plan.Root);plan.Root=SafeFiles.Full(plan.Root);if(plan.ResetSettings&&plan.Mode!="reset")throw new IOException("恢复默认只能从重新设置入口进入");
     if(!Native.IsAdmin){string data=Convert.ToBase64String(Encoding.UTF8.GetBytes(Json.Write(plan)));var start=new System.Diagnostics.ProcessStartInfo(Assembly.GetExecutingAssembly().Location,"--apply "+data){UseShellExecute=true,Verb="runas",WorkingDirectory=Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location)};System.Diagnostics.Process.Start(start);Close();return;}
     page=2;Render();Start();
    }catch(Exception e){MessageBox.Show(this,e.Message,"尚未开始准备",MessageBoxButtons.OK,MessageBoxIcon.Information);}
   }else if(page==3)Close();}
  void GoBack(){if(!running&&page==1){SaveOptions();page=0;Render();}}
  async void Start(){running=true;next.Enabled=false;try{receipt=await Task.Run(()=>new Engine(s=>{if(!IsDisposed)BeginInvoke((Action)(()=>{if(log!=null&&!log.IsDisposed)log.AppendText(s+"\r\n");}));}).Execute(plan));}catch(Exception e){receipt=new Receipt{Phase="failed",Error=e.Message,Root=plan.Root};}finally{running=false;page=3;Render();}}
  protected override bool ProcessCmdKey(ref Message msg,Keys key){if(key==Keys.Escape){GoBack();return true;}if(key==Keys.Up||key==Keys.Down||key==Keys.Left||key==Keys.Right){if(ActiveControl is TextBox&&!((TextBox)ActiveControl).ReadOnly)return base.ProcessCmdKey(ref msg,key);Navigate(key);return true;}return base.ProcessCmdKey(ref msg,key);}
  void Navigate(Keys key){if(running)return;var candidates=new System.Collections.Generic.List<Control>();Gather(this,candidates);Control current=ActiveControl;if(current==null||!candidates.Contains(current)){next.Focus();return;}Point center=current.PointToScreen(new Point(current.Width/2,current.Height/2));Control best=null;double score=double.MaxValue;
   foreach(var c in candidates){if(c==current)continue;Point other=c.PointToScreen(new Point(c.Width/2,c.Height/2));int dx=other.X-center.X,dy=other.Y-center.Y;bool horizontal=key==Keys.Left||key==Keys.Right;int primary=horizontal?dx:dy;if(key==Keys.Left||key==Keys.Up)primary=-primary;if(primary<=0)continue;int cross=Math.Abs(horizontal?dy:dx);if(horizontal&&Math.Abs(dy)>(current.Height+c.Height)/2-2)continue;double s=primary+cross*0.5;if(s<score){score=s;best=c;}}
   if(best!=null){best.Focus();if(SafeDescendant(best,body))body.ScrollControlIntoView(best);}
  }
  static bool SafeDescendant(Control child,Control ancestor){for(var p=child.Parent;p!=null;p=p.Parent)if(p==ancestor)return true;return false;}
  static void Gather(Control c,System.Collections.Generic.List<Control> output){foreach(Control child in c.Controls){if(!child.Visible||!child.Enabled)continue;if(child.CanSelect&&child.TabStop&&(child is Button||child is CheckBox||child is TextBox))output.Add(child);Gather(child,output);}}
  void PollPad(){if(!ContainsFocus||Form.ActiveForm!=this)return;Native.PadState p;try{p=Native.Pad();}catch{return;}HandlePad(p);}
  void HandlePad(Native.PadState p){ushort pressed=(ushort)(p.Buttons&~lastButtons);lastButtons=p.Buttons;
   if((pressed&0x1000)!=0){if(ActiveControl is Button)((Button)ActiveControl).PerformClick();else if(ActiveControl is CheckBox){var c=(CheckBox)ActiveControl;c.Checked=!c.Checked;}}if((pressed&0x2000)!=0)GoBack();
   int axis=(p.Buttons&1)!=0||p.LY>18000?1:(p.Buttons&2)!=0||p.LY<-18000?2:(p.Buttons&4)!=0||p.LX<-18000?3:(p.Buttons&8)!=0||p.LX>18000?4:0;
   if(axis!=0&&(axis!=lastAxis||DateTime.UtcNow>=repeat)){Navigate(axis==1?Keys.Up:axis==2?Keys.Down:axis==3?Keys.Left:Keys.Right);repeat=DateTime.UtcNow.AddMilliseconds(axis!=lastAxis?360:150);}lastAxis=axis;
  }
  public void Snapshot(string path,int width,int height){Size=new Size(width,height);StartPosition=FormStartPosition.Manual;Location=new Point(-20000,-20000);ShowInTaskbar=false;Show();page=1;Render();Application.DoEvents();using(var image=new Bitmap(Width,Height)){DrawToBitmap(image,new Rectangle(0,0,Width,Height));image.Save(path,System.Drawing.Imaging.ImageFormat.Png);}Close();}
 }
}

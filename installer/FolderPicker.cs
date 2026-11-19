using System;
using System.Drawing;
using System.IO;
using System.Windows.Forms;
namespace YeMan.Setup {
 // The optional directory picker remains usable before virtual-controller drivers exist.
 public sealed class FolderPicker : Form {
  readonly ListBox list=new ListBox();readonly Label location=new Label();readonly System.Windows.Forms.Timer timer=new System.Windows.Forms.Timer();
  string folder;public string Selected;ushort previous;int lastDirection;DateTime repeat;
  public FolderPicker(string initial){
   Text="选择绿色目录";Size=new Size(700,520);StartPosition=FormStartPosition.CenterParent;Font=new Font("Microsoft YaHei UI",11);BackColor=Color.FromArgb(23,30,42);ForeColor=Color.White;KeyPreview=true;
   location.Dock=DockStyle.Top;location.Height=65;location.Padding=new Padding(14);Controls.Add(location);
   var footer=new Panel{Dock=DockStyle.Bottom,Height=90};Controls.Add(footer);
   var hint=new Label{Text="A 打开目录 · X 使用此处 · B 返回上层 · 方向键选择",Dock=DockStyle.Top,Height=28};footer.Controls.Add(hint);
   var choose=new SoftButton{Text="使用此目录",Location=new Point(390,34),Size=new Size(265,44),Primary=true};footer.Controls.Add(choose);choose.Click+=(s,e)=>Choose();
   list.Dock=DockStyle.Fill;list.BackColor=Color.FromArgb(32,42,58);list.ForeColor=Color.White;list.BorderStyle=BorderStyle.None;list.ItemHeight=28;Controls.Add(list);list.BringToFront();list.DoubleClick+=(s,e)=>Open();
   folder=Directory.Exists(initial)?initial:Path.GetDirectoryName(initial);if(folder==null||!Directory.Exists(folder))folder="";
   Shown+=(s,e)=>{LoadFolder();list.Focus();timer.Start();};timer.Interval=40;timer.Tick+=(s,e)=>Poll();FormClosed+=(s,e)=>{timer.Stop();timer.Dispose();};
  }
  void LoadFolder(){list.Items.Clear();location.Text=folder.Length==0?"选择磁盘；进入目录后按 X 确认":folder+"\r\n非空目录会使用其中的 YeManCC 子目录";
   if(folder.Length==0){foreach(var drive in DriveInfo.GetDrives())if(drive.IsReady&&drive.DriveType==DriveType.Fixed)list.Items.Add(drive.RootDirectory.FullName);}
   else{list.Items.Add("..  上一级");try{foreach(var p in Directory.GetDirectories(folder)){if((File.GetAttributes(p)&FileAttributes.ReparsePoint)==0)list.Items.Add(Path.GetFileName(p));}}catch(UnauthorizedAccessException){location.Text="此目录不可浏览，请返回上层";}}
   if(list.Items.Count>0)list.SelectedIndex=0;
  }
  void Open(){if(list.SelectedIndex<0)return;if(folder.Length==0)folder=Convert.ToString(list.SelectedItem);else if(list.SelectedIndex==0){Up();return;}else folder=Path.Combine(folder,Convert.ToString(list.SelectedItem));LoadFolder();}
  void Up(){if(folder.Length==0){DialogResult=DialogResult.Cancel;Close();return;}string p=Path.GetDirectoryName(folder.TrimEnd('\\'));folder=p??"";LoadFolder();}
  void Choose(){if(folder.Length==0)return;string candidate=folder;try{if(candidate.TrimEnd('\\').Length<4||(!File.Exists(Path.Combine(candidate,@"YeManCC\YeManCC.exe"))&&Directory.GetFileSystemEntries(candidate).Length>0))candidate=Path.Combine(candidate,"YeManCC");Engine.ValidateRoot(candidate);Selected=candidate;DialogResult=DialogResult.OK;Close();}catch(Exception e){MessageBox.Show(this,e.Message,"选择目录");}}
  protected override bool ProcessCmdKey(ref Message msg,Keys key){if(key==Keys.Escape){Up();return true;}if(key==Keys.Enter){Open();return true;}return base.ProcessCmdKey(ref msg,key);}
  void Poll(){if(Form.ActiveForm!=this)return;Native.PadState p;try{p=Native.Pad();}catch{return;}ushort pressed=(ushort)(p.Buttons&~previous);previous=p.Buttons;if((pressed&0x1000)!=0){if(ActiveControl is SoftButton)Choose();else Open();}if((pressed&0x2000)!=0)Up();if((pressed&0x4000)!=0)Choose();int direction=(p.Buttons&1)!=0||p.LY>18000?-1:(p.Buttons&2)!=0||p.LY<-18000?1:0;if(direction!=0&&(direction!=lastDirection||DateTime.UtcNow>=repeat)){list.SelectedIndex=Math.Max(0,Math.Min(list.Items.Count-1,list.SelectedIndex+direction));repeat=DateTime.UtcNow.AddMilliseconds(direction!=lastDirection?350:140);}lastDirection=direction;}
 }
}
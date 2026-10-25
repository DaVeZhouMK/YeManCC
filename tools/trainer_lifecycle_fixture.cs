using System;using System.IO;using System.Diagnostics;using System.Text;using System.Web.Script.Serialization;using System.Windows.Forms;using System.Collections.Generic;
sealed class TrainerLifecycleFixture:Form {
 [System.Runtime.InteropServices.DllImport("user32.dll")]static extern bool ShowWindow(IntPtr h,int cmd);
 readonly string events,exe;readonly string[] arguments;readonly Timer timer=new Timer();
 TrainerLifecycleFixture(string[] args){exe=Process.GetCurrentProcess().MainModule.FileName;arguments=args;var j=new JavaScriptSerializer();var c=j.Deserialize<Dictionary<string,object>>(File.ReadAllText(exe+".fixture.json"));Text=Convert.ToString(c["title"]);events=Convert.ToString(c["eventRoot"]);Directory.CreateDirectory(events);Width=320;Height=140;ShowInTaskbar=false;StartPosition=FormStartPosition.Manual;Left=-1200;Top=-1200;timer.Interval=200;timer.Tick+=(s,e)=>{if(File.Exists(Path.Combine(events,"close-request")))Close();};timer.Start();FormClosed+=(s,e)=>Save("closed");}
 protected override bool ShowWithoutActivation {get{return true;}}
 protected override CreateParams CreateParams {get{var p=base.CreateParams;p.ExStyle|=0x08000080;return p;}}
 protected override void OnShown(EventArgs e){base.OnShown(e);ShowWindow(Handle,4);Save("opened");}
 void Save(string stage){File.WriteAllText(Path.Combine(events,stage+"-"+Process.GetCurrentProcess().Id+".json"),new JavaScriptSerializer().Serialize(new{stage=stage,pid=Process.GetCurrentProcess().Id,exe=exe,args=arguments,cwd=Environment.CurrentDirectory,title=Text,at=DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()}),new UTF8Encoding(false));}
 [STAThread]static void Main(string[] args){Application.EnableVisualStyles();Application.Run(new TrainerLifecycleFixture(args));}
}

using System;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Windows.Forms;
static class Launcher {
 [STAThread] static void Main(){try{
  string root=Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);string exe=Path.Combine(root,@"YeManCC\YeManCC.exe");
  if(!File.Exists(exe))throw new IOException("找不到 YMCC，请先运行首次设置中心");
  Environment.SetEnvironmentVariable("YEMAN_POWER_CONTROL_DIR",Path.Combine(root,"PowerControl"),EnvironmentVariableTarget.Process);var p=new ProcessStartInfo(exe){UseShellExecute=true,WorkingDirectory=Path.GetDirectoryName(exe)};
  Process.Start(p);
 }catch(Exception e){MessageBox.Show(e.Message,"YeManCC",MessageBoxButtons.OK,MessageBoxIcon.Error);}}
}

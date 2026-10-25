// System-provider read-only observation. No input writes, device changes or listeners.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Text;
namespace YmccLiveConsumer {
 public sealed class ProcessFact {
  public uint ProcessId, SessionId, WaitResult, Error;
  public string CreationFiletime, ImagePath;
  public bool IdentityObserved, AliveObserved, ExitObserved, SessionObserved, ImageObserved, TimesObserved;
  public uint? ExitCode;
 }
 public sealed class SlotFact {
  public uint Slot, Result, PacketNumber;
  public ushort Buttons;
  public byte LeftTrigger, RightTrigger;
  public short LeftX, LeftY, RightX, RightY;
  public string Error;
  public bool ApiSucceeded;
 }
 public static class ReadOnlyNative {
  [StructLayout(LayoutKind.Sequential)] struct FileTime {public uint Low,High;}
  [StructLayout(LayoutKind.Sequential)] struct Gamepad {public ushort Buttons;public byte LT,RT;public short LX,LY,RX,RY;}
  [StructLayout(LayoutKind.Sequential)] struct InputState {public uint Packet;public Gamepad Pad;}
  [DllImport("kernel32.dll",SetLastError=true)] static extern IntPtr OpenProcess(uint access,bool inherit,uint pid);
  [DllImport("kernel32.dll",SetLastError=true)] static extern bool CloseHandle(IntPtr handle);
  [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetProcessTimes(IntPtr handle,out FileTime created,out FileTime exited,out FileTime kernel,out FileTime user);
  [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool QueryFullProcessImageNameW(IntPtr handle,uint flags,StringBuilder text,ref uint size);
  [DllImport("kernel32.dll",SetLastError=true)] static extern bool ProcessIdToSessionId(uint pid,out uint session);
  [DllImport("kernel32.dll",SetLastError=true)] static extern uint WaitForSingleObject(IntPtr handle,uint timeout);
  [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetExitCodeProcess(IntPtr handle,out uint code);
  [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd,out uint pid);
  [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
  [DllImport("xinput1_4.dll",EntryPoint="XInputGetState")] static extern uint GetInput(uint slot,out InputState state);
  public static IntPtr Open(uint pid){return OpenProcess(0x00101000,false,pid);} // query-limited + synchronize, no write rights
  public static void Close(IntPtr h){if(h!=IntPtr.Zero)CloseHandle(h);}
  public static ProcessFact Read(IntPtr h,uint pid){
   var r=new ProcessFact{ProcessId=pid};if(h==IntPtr.Zero){r.Error=(uint)Marshal.GetLastWin32Error();return r;}
   r.WaitResult=WaitForSingleObject(h,0);r.AliveObserved=r.WaitResult==258;r.ExitObserved=r.WaitResult==0;
   uint exit;if(r.ExitObserved&&GetExitCodeProcess(h,out exit))r.ExitCode=exit;
   FileTime c,e,k,u;r.TimesObserved=GetProcessTimes(h,out c,out e,out k,out u);
   if(r.TimesObserved)r.CreationFiletime=(((ulong)c.High<<32)|c.Low).ToString(CultureInfo.InvariantCulture);
   var text=new StringBuilder(32768);uint size=32768;r.ImageObserved=QueryFullProcessImageNameW(h,0,text,ref size);if(r.ImageObserved)r.ImagePath=text.ToString();
   uint session;r.SessionObserved=ProcessIdToSessionId(pid,out session);if(r.SessionObserved)r.SessionId=session;
   r.IdentityObserved=r.TimesObserved&&r.SessionObserved&&r.ImageObserved&&r.AliveObserved&&r.CreationFiletime!="0";
   return r;
  }
  public static uint ForegroundPid(){uint pid;GetWindowThreadProcessId(GetForegroundWindow(),out pid);return pid;}
  public static SlotFact[] Slots(){var rows=new List<SlotFact>();for(uint i=0;i<4;i++){var r=new SlotFact{Slot=i};try{InputState v;r.Result=GetInput(i,out v);r.ApiSucceeded=r.Result==0;if(r.ApiSucceeded){r.PacketNumber=v.Packet;r.Buttons=v.Pad.Buttons;r.LeftTrigger=v.Pad.LT;r.RightTrigger=v.Pad.RT;r.LeftX=v.Pad.LX;r.LeftY=v.Pad.LY;r.RightX=v.Pad.RX;r.RightY=v.Pad.RY;}}catch(Exception e){r.Result=uint.MaxValue;r.Error=e.GetType().Name;}rows.Add(r);}return rows.ToArray();}
 }
}

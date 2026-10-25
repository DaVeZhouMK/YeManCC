using System;
using System.Runtime.InteropServices;
public static class OwnedLosslessCloseWindow {
 delegate IntPtr Procedure(IntPtr window,uint message,IntPtr wparam,IntPtr lparam);
 [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)]struct Class {public uint size,style;public Procedure procedure;public int classBytes,windowBytes;public IntPtr instance,icon,cursor,brush;public string menu,name;public IntPtr smallIcon;}
 [StructLayout(LayoutKind.Sequential)]struct Point{public int x,y;}
 [StructLayout(LayoutKind.Sequential)]struct Message{public IntPtr window;public uint message;public IntPtr wparam,lparam;public uint time;public Point point;public uint extra;}
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern ushort RegisterClassEx(ref Class value);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern IntPtr CreateWindowEx(uint ex,string cls,string title,uint style,int x,int y,int width,int height,IntPtr parent,IntPtr menu,IntPtr instance,IntPtr parameter);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern IntPtr DefWindowProc(IntPtr window,uint message,IntPtr wparam,IntPtr lparam);
 [DllImport("user32.dll")]static extern bool DestroyWindow(IntPtr window);
 [DllImport("user32.dll")]static extern void PostQuitMessage(int exit);
 [DllImport("user32.dll")]static extern int GetMessage(out Message message,IntPtr window,uint minimum,uint maximum);
 [DllImport("user32.dll")]static extern bool TranslateMessage(ref Message message);
 [DllImport("user32.dll")]static extern IntPtr DispatchMessage(ref Message message);
 static Procedure callback;
 static bool ignore;
 public static void Main(string[] args){
  string mode=args.Length>0?args[0]:"hidden";ignore=mode=="ignore";
  callback=delegate(IntPtr window,uint message,IntPtr wparam,IntPtr lparam){if(message==0x10){if(ignore)return IntPtr.Zero;DestroyWindow(window);PostQuitMessage(0);return IntPtr.Zero;}return DefWindowProc(window,message,wparam,lparam);};
  string name=mode=="wrongclass"?"YMCC-owned-decoy":"HwndWrapper[LosslessScaling;;OwnedCloseFixture]";
  Class cls=new Class();cls.size=(uint)Marshal.SizeOf(typeof(Class));cls.procedure=callback;cls.name=name;
  if(RegisterClassEx(ref cls)==0)Environment.Exit(2);
  IntPtr main=CreateWindowEx(0,name,"Lossless Scaling",0,-32000,-32000,80,60,IntPtr.Zero,IntPtr.Zero,IntPtr.Zero,IntPtr.Zero);
  if(main==IntPtr.Zero)Environment.Exit(3);
  if(mode=="ambiguous"&&CreateWindowEx(0,name,"Lossless Scaling",0,-32000,-32000,80,60,IntPtr.Zero,IntPtr.Zero,IntPtr.Zero,IntPtr.Zero)==IntPtr.Zero)Environment.Exit(4);
  Console.WriteLine("READY");Console.Out.Flush();Message msg;while(GetMessage(out msg,IntPtr.Zero,0,0)>0){TranslateMessage(ref msg);DispatchMessage(ref msg);}GC.KeepAlive(callback);
 }
}


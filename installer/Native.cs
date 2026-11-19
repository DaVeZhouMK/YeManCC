using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Principal;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace YeMan.Setup {
 public static class Native {
  [DllImport("wintrust.dll", CharSet=CharSet.Unicode, ExactSpelling=true)] static extern int WinVerifyTrust(IntPtr hwnd, [MarshalAs(UnmanagedType.LPStruct)] Guid action, IntPtr data);
  [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)] struct TrustFile { public uint Size; public IntPtr FilePath; public IntPtr File; public IntPtr Subject; }
  [StructLayout(LayoutKind.Sequential)] struct TrustData { public uint Size; public IntPtr Policy; public IntPtr SIP; public uint UI; public uint Revocation; public uint Choice; public IntPtr FileInfo; public uint StateAction; public IntPtr State; public IntPtr Url; public uint Flags; public uint Context; }
  public static bool IsAdmin { get { return new WindowsPrincipal(WindowsIdentity.GetCurrent()).IsInRole(WindowsBuiltInRole.Administrator); } }
  [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)] struct OsVersion {
   public uint Size,Major,Minor,Build,Platform;[MarshalAs(UnmanagedType.ByValTStr,SizeConst=128)]public string ServicePack;
   public ushort ServicePackMajor,ServicePackMinor,SuiteMask;public byte ProductType,Reserved;
  }
  [StructLayout(LayoutKind.Sequential)] struct SystemInfo {
   public ushort Architecture,Reserved;public uint PageSize;public IntPtr Minimum,Maximum;public UIntPtr ProcessorMask;
   public uint ProcessorCount,ProcessorType,AllocationGranularity;public ushort ProcessorLevel,ProcessorRevision;
  }
  [DllImport("ntdll.dll",CharSet=CharSet.Unicode,ExactSpelling=true)]static extern int RtlGetVersion(ref OsVersion version);
  [DllImport("kernel32.dll")]static extern void GetNativeSystemInfo(out SystemInfo info);
  public static SystemObservation SystemSupport() {
   try {
    var version=new OsVersion{Size=(uint)Marshal.SizeOf(typeof(OsVersion))};SystemInfo info;
    if(RtlGetVersion(ref version)!=0)throw new IOException("无法读取真实 Windows 版本");
    GetNativeSystemInfo(out info);return Runtimes.AssessSystem((int)version.Major,(int)version.Minor,(int)version.Build,info.Architecture,version.ProductType);
   }catch(Exception e){return new SystemObservation{Allowed=false,Label="Windows 系统检测失败",Error=e.Message};}
  }
  public static void RequireSupportedSystem(){var system=SystemSupport();if(!system.Allowed)throw new IOException(system.Error);}
  public static void MicrosoftSignature(string path) {
   PublisherSignature(path,"O=Microsoft Corporation");
  }
  internal static void PublisherSignature(string path,string organization) {
   IntPtr name=IntPtr.Zero, info=IntPtr.Zero, data=IntPtr.Zero;
   try {
    name=Marshal.StringToCoTaskMemUni(Path.GetFullPath(path));
    var f=new TrustFile { Size=(uint)Marshal.SizeOf(typeof(TrustFile)), FilePath=name };
    info=Marshal.AllocHGlobal(Marshal.SizeOf(f)); Marshal.StructureToPtr(f,info,false);
    // Use the OS Authenticode chain policy. Do not suppress trust failures.
    var d=new TrustData { Size=(uint)Marshal.SizeOf(typeof(TrustData)), UI=2, Choice=1, FileInfo=info, Flags=0x1000 };
    data=Marshal.AllocHGlobal(Marshal.SizeOf(d)); Marshal.StructureToPtr(d,data,false);
    int result=WinVerifyTrust(new IntPtr(-1), new Guid("00AAC56B-CD44-11d0-8CC2-00C04FC295EE"),data);
    if(result!=0) throw new IOException("安装文件签名验证失败：0x"+result.ToString("X8"));
    using(var cert=new X509Certificate2(X509Certificate.CreateFromSignedFile(path))) {
     if(!cert.Subject.Contains(organization)) throw new IOException("安装器发布者不符合要求："+organization);
    }
   } finally { if(data!=IntPtr.Zero)Marshal.FreeHGlobal(data);if(info!=IntPtr.Zero)Marshal.FreeHGlobal(info);if(name!=IntPtr.Zero)Marshal.FreeCoTaskMem(name); }
  }
  [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern SafeFileHandle CreateFile(string name,uint access,uint share,IntPtr security,uint creation,uint flags,IntPtr template);
  [DllImport("kernel32.dll",SetLastError=true)] static extern bool DeviceIoControl(SafeFileHandle h,uint control,byte[] input,int inputSize,byte[] output,int outputSize,out int returned,IntPtr overlapped);
  public static void Junction(string link,string target) {
   SafeFiles.NoLinks(Path.GetDirectoryName(link)); SafeFiles.NoLinks(target);
   if(Directory.Exists(link)||File.Exists(link))throw new IOException("兼容目录已存在："+link);
   Directory.CreateDirectory(link); bool ok=false;
   try {
    byte[] sub=Encoding.Unicode.GetBytes(@"\??\"+Path.GetFullPath(target)); byte[] print=Encoding.Unicode.GetBytes(Path.GetFullPath(target));
    byte[] buf=new byte[16+sub.Length+2+print.Length+2];
    using(var w=new BinaryWriter(new MemoryStream(buf))) {
     w.Write((uint)0xA0000003);w.Write((ushort)(buf.Length-8));w.Write((ushort)0);
     w.Write((ushort)0);w.Write((ushort)sub.Length);w.Write((ushort)(sub.Length+2));w.Write((ushort)print.Length);w.Write(sub);w.Write((ushort)0);w.Write(print);w.Write((ushort)0);
    }
    using(var h=CreateFile(link,0x40000000,0,IntPtr.Zero,3,0x02200000,IntPtr.Zero)) {
     int returned;
     if(h.IsInvalid||!DeviceIoControl(h,0x000900A4,buf,buf.Length,null,0,out returned,IntPtr.Zero))throw new IOException("无法建立路径兼容入口",new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error()));
    } ok=true;
   } finally { if(!ok&&Directory.Exists(link))Directory.Delete(link,false); }
  }
  public static string JunctionTarget(string link) {
   if(!Directory.Exists(link)||(File.GetAttributes(link)&FileAttributes.ReparsePoint)==0)return null;
   using(var h=CreateFile(link,0,7,IntPtr.Zero,3,0x02200000,IntPtr.Zero)) {
    byte[] buf=new byte[16384];int returned;
    if(h.IsInvalid||!DeviceIoControl(h,0x000900A8,null,0,buf,buf.Length,out returned,IntPtr.Zero)||returned<16||BitConverter.ToUInt32(buf,0)!=0xA0000003)return null;
    int offset=BitConverter.ToUInt16(buf,8),length=BitConverter.ToUInt16(buf,10);if(16+offset+length>returned)return null;
    string name=Encoding.Unicode.GetString(buf,16+offset,length);if(name.StartsWith(@"\??\"))name=name.Substring(4);return SafeFiles.Full(name);
   }
  }
  public static bool JunctionTo(string link,string target) {string actual=JunctionTarget(link);return actual!=null&&string.Equals(actual,SafeFiles.Full(target),StringComparison.OrdinalIgnoreCase);}
  public static string Quote(string arg) {
   var b=new StringBuilder("\"");int slashes=0;
   foreach(char c in arg){if(c=='\\'){slashes++;continue;}if(c=='\"'){b.Append('\\',slashes*2+1);b.Append(c);}else{b.Append('\\',slashes);b.Append(c);}slashes=0;}
   b.Append('\\',slashes*2);b.Append('"');return b.ToString();
  }
  [StructLayout(LayoutKind.Sequential)] public struct PadState {public uint Packet;public ushort Buttons;public byte LeftTrigger,RightTrigger;public short LX,LY,RX,RY;}
  [DllImport("xinput1_4.dll")] static extern uint XInputGetState(uint index,out PadState state);
  public static PadState Pad() {
   PadState first=new PadState(),stick=new PadState(),p;bool connected=false,moving=false;
   for(uint i=0;i<4;i++) {
    if(XInputGetState(i,out p)!=0)continue;
    if(!connected){first=p;connected=true;}
    if((p.Buttons&0x700f)!=0)return p;
    if(!moving&&(p.LX>18000||p.LX<-18000||p.LY>18000||p.LY<-18000)){stick=p;moving=true;}
   }
   return moving?stick:first;
  }
 }
}

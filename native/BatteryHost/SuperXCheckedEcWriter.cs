using System.Buffers.Binary;
using System.ComponentModel;
using System.Reflection;
using System.Runtime.ExceptionServices;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using LibreHardwareMonitor.PawnIo;
using Microsoft.Win32.SafeHandles;
namespace YeManBatteryHost;

internal interface ISuperXChargeWriter : IDisposable { void WriteLimit(byte value); }
internal interface ISuperXPortSession : IDisposable
{
    void SelectSlot(int slot);
    void WritePort(ushort port,byte value);
}
internal interface ISuperXBusLease : IDisposable { void Enter(); }
internal readonly record struct SuperXIoResult(bool Ok,uint Returned,int Error);
internal interface ISuperXKernelChannel : IDisposable
{
    void Open(string path);
    SuperXIoResult Ioctl(uint code,byte[] input);
}

// Source-exact firmware limit writer. No generic EC address or bypass setter is exposed.
// Its constructor is dormant; the enclosing provider owns it before any external work.
internal sealed class SuperXCheckedEcWriter : ISuperXChargeWriter
{
    readonly object gate=new();
    readonly Func<ISuperXPortSession> createSession;
    readonly Func<ISuperXBusLease> createBus;
    ISuperXPortSession? session;
    ISuperXBusLease? bus;
    bool attempted,stopped;
    internal SuperXCheckedEcWriter(Func<ISuperXPortSession>? createSession=null,Func<ISuperXBusLease>? createBus=null)
    {
        this.createSession=createSession??(()=>new SuperXCheckedLpcSession());
        this.createBus=createBus??(()=>new SuperXIsaBusLease());
    }
    public void WriteLimit(byte value)
    {
        lock(gate)WriteLimitLocked(value);
    }
    void WriteLimitLocked(byte value)
    {
        if(value<75||value>100)throw new IOException("Super X 充电档位须在75至100之间");
        if(stopped||attempted)throw new IOException("Super X 写入终端已停止或已消费，不重放请求");
        attempted=true;
        Exception? failure=null;
        try
        {
            bus=createBus()??throw new IOException("缺少 Super X ISA 通道所有者");
            bus.Enter();
            session=createSession()??throw new IOException("缺少 Super X 端口终端");
            session.SelectSlot(1);
            foreach(byte item in new byte[]{0x87,0x01,0x55,0xaa})session.WritePort(0x4e,item);
            session.WritePort(0x4e,0x2e);session.WritePort(0x4f,0x11);
            session.WritePort(0x4e,0x2f);session.WritePort(0x4f,0x04);
            session.WritePort(0x4e,0x2e);session.WritePort(0x4f,0x10);
            session.WritePort(0x4e,0x2f);session.WritePort(0x4f,0xa3);
            session.WritePort(0x4e,0x2e);session.WritePort(0x4f,0x12);
            session.WritePort(0x4e,0x2f);session.WritePort(0x4f,value);
        }
        catch(Exception error){failure=error;}
        // IT87 at4E has no exit write in the pinned source. Return the mutex on
        // its acquiring thread only after the synchronous calls have returned.
        try{ReleaseBus();}catch(Exception error){failure=failure is null?error:new AggregateException(failure,error);}
        if(failure is not null)ExceptionDispatchInfo.Capture(failure).Throw();
    }
    void ReleaseBus(){if(bus is not null){bus.Dispose();bus=null;}}
    public void Dispose()
    {
        lock(gate)DisposeLocked();
    }
    void DisposeLocked()
    {
        stopped=true;
        Exception? failure=null;
        try{if(session is not null){session.Dispose();session=null;}}catch(Exception error){failure=error;}
        try{ReleaseBus();}catch(Exception error){failure=failure is null?error:new AggregateException(failure,error);}
        if(failure is not null)ExceptionDispatchInfo.Capture(failure).Throw();
    }
}

internal sealed class SuperXIsaBusLease : ISuperXBusLease
{
    Mutex? mutex;
    bool started,owned,stopped;
    int ownerThread;
    public void Enter()
    {
        if(started||stopped)throw new IOException("ISA 通道租约不能重新进入");
        started=true;mutex=new Mutex(false,@"Global\Access_ISABUS.HTP.Method");
        ownerThread=Environment.CurrentManagedThreadId;
        try{owned=mutex.WaitOne(250);}catch(AbandonedMutexException){owned=true;}
        if(!owned)throw new IOException("硬件通道忙，请稍后重试");
    }
    public void Dispose()
    {
        stopped=true;
        if(mutex is null)return;
        if(owned)
        {
            if(ownerThread!=Environment.CurrentManagedThreadId)
                throw new IOException("ISA 通道仍须由原获取线程归还");
            mutex.ReleaseMutex();owned=false;
        }
        mutex.Dispose();mutex=null;
    }
}

// Preserve actual Win32 completion, unlike LpcIo's void wrappers which discard it.
// All kernel work is deferred until the session has retained this object.
internal sealed class SuperXNativeKernelChannel : ISuperXKernelChannel
{
    readonly object gate=new();
    IntPtr raw;
    SafeFileHandle? reference;
    bool opened,stopped,invalidClose;
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
    static extern IntPtr CreateFile(string name,uint access,uint share,IntPtr security,uint creation,uint flags,IntPtr template);
    [DllImport("kernel32.dll",SetLastError=true)]
    [return:MarshalAs(UnmanagedType.Bool)]
    static extern bool DeviceIoControl(SafeFileHandle handle,uint code,byte[] input,uint inputBytes,IntPtr output,uint outputBytes,out uint returned,IntPtr overlapped);
    [DllImport("kernel32.dll",SetLastError=true)]
    [return:MarshalAs(UnmanagedType.Bool)]
    static extern bool CloseHandle(IntPtr handle);
    public void Open(string path)
    {
        lock(gate)
        {
            if(opened||stopped)throw new IOException("驱动句柄不能重新打开");
            if(path!=@"\\?\GLOBALROOT\Device\PawnIO"&&path!=@"\\.\PawnIO")throw new IOException("驱动路径不属于固定接口");
            opened=true;
            // Same access/share/creation/flags as the audited fixed loader.
            raw=CreateFile(path,3,3,IntPtr.Zero,3,128,IntPtr.Zero);
            if(raw==IntPtr.Zero||raw==new IntPtr(-1))
            {
                int error=Marshal.GetLastWin32Error();raw=IntPtr.Zero;
                throw new IOException("PawnIO 通道不可用",new Win32Exception(error));
            }
            // This wrapper does not own the kernel close. A failed CloseHandle
            // must retain the exact native value instead of marking it closed.
            reference=new SafeFileHandle(raw,false);
        }
    }
    public SuperXIoResult Ioctl(uint code,byte[] input)
    {
        lock(gate)
        {
            if(stopped||raw==IntPtr.Zero||reference is null)throw new IOException("驱动句柄已停止或未打开");
            if(code!=SuperXCheckedLpcSession.LoadIoctl&&code!=SuperXCheckedLpcSession.ExecuteIoctl)throw new IOException("驱动命令超出固定接口");
            bool ok=DeviceIoControl(reference,code,input,checked((uint)input.Length),IntPtr.Zero,0,out uint returned,IntPtr.Zero);
            int error=ok?0:Marshal.GetLastWin32Error();return new(ok,returned,error);
        }
    }
    public void Dispose()
    {
        lock(gate)
        {
            stopped=true;
            if(raw==IntPtr.Zero)return;
            if(invalidClose)throw new IOException("原驱动句柄无效，不能确认归还；禁止重用或重新关闭可能已复用的数值");
            if(!CloseHandle(raw))
            {
                int error=Marshal.GetLastWin32Error();invalidClose=error==6;
                throw new IOException("尚未确认归还原驱动句柄",new Win32Exception(error));
            }
            raw=IntPtr.Zero;
            if(reference is not null){reference.SetHandleAsInvalid();reference.Dispose();reference=null;}
        }
    }
}

internal sealed class SuperXCheckedLpcSession : ISuperXPortSession
{
    internal const uint LoadIoctl=unchecked((uint)-1582161788);
    internal const uint ExecuteIoctl=unchecked((uint)-1582161660);
    readonly Func<ISuperXKernelChannel> createKernel;
    readonly Func<byte[]> moduleImage;
    readonly Func<string> devicePath;
    ISuperXKernelChannel? kernel;
    bool started,loaded,faulted,stopped,slotSelected;
    internal SuperXCheckedLpcSession(Func<ISuperXKernelChannel>? createKernel=null,Func<byte[]>? moduleImage=null,Func<string>? devicePath=null)
    {
        this.createKernel=createKernel??(()=>new SuperXNativeKernelChannel());
        this.moduleImage=moduleImage??SuperXFixedLpcModule.ReadImage;
        this.devicePath=devicePath??SuperXFixedLpcModule.DevicePath;
    }
    void EnsureLoaded()
    {
        if(stopped||faulted)throw new IOException("固定 LpcIO 终端已停止或存在未确认调用");
        if(loaded)return;
        if(started)throw new IOException("固定 LpcIO 初始化未完成，不重新打开");
        started=true;
        try
        {
            byte[] image=moduleImage();if(image.Length==0)throw new IOException("固定 LpcIO 模块为空");
            string path=devicePath();
            kernel=createKernel()??throw new IOException("缺少驱动句柄所有者");
            kernel.Open(path);
            Check(kernel.Ioctl(LoadIoctl,image),"加载固定模块");loaded=true;
        }
        catch{faulted=true;throw;}
    }
    static void Check(SuperXIoResult result,string stage)
    {
        if(!result.Ok)throw new IOException("PawnIO 未确认"+stage,new Win32Exception(result.Error));
        if(result.Returned!=0)throw new IOException("PawnIO 返回长度与固定无输出接口不符："+stage);
    }
    void Call(string command,params long[] arguments)
    {
        EnsureLoaded();
        try
        {
            byte[] packet=new byte[32+arguments.Length*8];
            var name=Encoding.ASCII.GetBytes(command);if(name.Length>31)throw new IOException("固定命令名称超长");
            name.CopyTo(packet,0);
            for(int i=0;i<arguments.Length;i++)BinaryPrimitives.WriteInt64LittleEndian(packet.AsSpan(32+i*8,8),arguments[i]);
            Check(kernel!.Ioctl(ExecuteIoctl,packet),command);
        }
        catch{faulted=true;throw;}
    }
    public void SelectSlot(int slot)
    {
        if(slot!=1||slotSelected)throw new IOException("Super X 只允许选择一次固定 LpcIO 槽位1");
        Call("ioctl_select_slot",slot);slotSelected=true;
    }
    public void WritePort(ushort port,byte value)
    {
        if(port!=0x4e&&port!=0x4f)throw new IOException("Super X 端口超出固定接口");
        if(!slotSelected)throw new IOException("未确认选择固定 LpcIO 槽位，禁止端口写入");
        Call("ioctl_pio_outb",port,value);
    }
    public void Dispose()
    {
        stopped=true;loaded=false;
        if(kernel is not null){kernel.Dispose();kernel=null;}
    }
}

internal static class SuperXFixedLpcModule
{
    internal const string LibrarySha256="f7ed30f07ea636c0ddcc5764c15c0a25ae8a0aca02ded77e4af24439955487cf";
    internal const string ModuleSha256="3dcf8b2bc80ff642d97c4608511a818642b5bf315ff53df3df393d043e71d101";
    internal const string Resource="LibreHardwareMonitor.Resources.PawnIo.LpcIO.bin";
    static Assembly FixedAssembly()
    {
        var assembly=typeof(LpcIo).Assembly;
        if(assembly.ManifestModule.ModuleVersionId!=new Guid("85ff0191-4e81-41da-9cb7-d803f3e471c0"))throw new IOException("LpcIO 程序集身份不符");
        if(string.IsNullOrEmpty(assembly.Location))throw new IOException("缺少固定 LpcIO 程序集文件身份");
        using var file=File.OpenRead(assembly.Location);
        if(Convert.ToHexString(SHA256.HashData(file)).ToLowerInvariant()!=LibrarySha256)throw new IOException("固定 LpcIO 程序集哈希不符");
        return assembly;
    }
    internal static byte[] ReadImage()
    {
        var assembly=FixedAssembly();using var stream=assembly.GetManifestResourceStream(Resource)??throw new IOException("缺少固定 LpcIO 模块");
        using var memory=new MemoryStream();stream.CopyTo(memory);var bytes=memory.ToArray();
        if(Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant()!=ModuleSha256)throw new IOException("固定 LpcIO 模块哈希不符");
        return bytes;
    }
    internal static string DevicePath()
    {
        // Resolve the existing loader's exact version predicate; no path fallback
        // follows a failed load, IOCTL, or a response whose effect is unknown.
        var type=FixedAssembly().GetType("LibreHardwareMonitor.PawnIo.PawnIo",true)!;
        var current=type.GetProperty("Version",BindingFlags.Public|BindingFlags.Static)?.GetValue(null) as Version;
        var last=type.GetField("VERSION_LAST",BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Static)?.GetValue(null) as Version;
        if(current is null||last is null)throw new IOException("固定 PawnIO 版本接口不符");
        return current<last?@"\\.\PawnIO":@"\\?\GLOBALROOT\Device\PawnIO";
    }
}

using System;
using System.Collections.Generic;
using System.IO;

public static class OriginalConstructorPorts
{
    public static readonly List<string> Calls=new();
    public sealed record StageFrame(string Stage,int ManagedThreadId,string Apartment,string Context);
    public static readonly System.Collections.Concurrent.ConcurrentQueue<StageFrame> Frames=new();
    public static string Processor="AMD Ryzen 9 395",Product="fixture-product",Model="fixture-model",Manufacturer="fixture-vendor",SystemName="fixture-system",Version="fixture-version",SettingsRoot="";
    public static string Failure="";public static int Failures;public static Action<string>? OnMark;
    public static int Cores=8,BiosMajor=1,BiosMinor=1;
    public static void Mark(string stage){Frames.Enqueue(new(stage,Environment.CurrentManagedThreadId,System.Threading.Thread.CurrentThread.GetApartmentState().ToString(),System.Threading.SynchronizationContext.Current?.GetType().FullName??"none"));Calls.Add(stage);OnMark?.Invoke(stage);if(stage==Failure&&Failures>0){Failures--;throw new IOException("JOINED_PORT_FAILURE:"+stage);}}
    public static object Board(string collection,string query)
    {
        Mark("board:"+collection+":"+query);
        return query switch {"Name"=>Processor,"NumberOfCores"=>Cores,"Product"=>Product,"Model"=>Model,"Manufacturer"=>Manufacturer,"SystemName"=>SystemName,"Version"=>Version,_=>"fixture-value"};
    }
    public static object Setting(string name){Mark("setting:"+name);if(name!="LayoutMode")throw new InvalidOperationException("UNDECLARED_CONSTRUCTOR_SETTING:"+name);return 0;}
    public static bool PhysicalMemory(out ulong kilobytes){Mark("physical-memory-read");kilobytes=32UL*1024*1024;return true;}
    public static string MetadataCacheRoot(){var root=Path.GetFullPath(SettingsRoot);if(!root.Split(Path.DirectorySeparatorChar).Any(p=>p.Equals("Build",StringComparison.OrdinalIgnoreCase)))throw new InvalidOperationException("Owned Build metadata root required");var path=Path.Combine(root,"cache");Directory.CreateDirectory(path);return path;}
    public static int Major(){Mark("bios-version-read");return BiosMajor;}
    public static int Minor()=>BiosMinor;
}

public static class JoinedFactoryPorts
{
    public static string Root="";public static bool UnsupportedSensors;
    public static int ThreadIdentityReads;
    public static int PowerReads;
    public static bool PowerOnline(){System.Threading.Interlocked.Increment(ref PowerReads);OriginalConstructorPorts.Mark("joined.power-read-inert");return true;}
    [System.Runtime.InteropServices.DllImport("kernel32.dll",EntryPoint="GetCurrentThreadId",ExactSpelling=true)]
    private static extern uint GetCurrentThreadId();
    public static uint ReadThreadIdentity(){System.Threading.Interlocked.Increment(ref ThreadIdentityReads);OriginalConstructorPorts.Mark("sta.os-thread-id");return GetCurrentThreadId();}
    public static void Validation(string stage)=>OriginalConstructorPorts.Mark(stage);
    public static string TempRoot(){OriginalConstructorPorts.Mark("runtime.temp-root");var root=Path.GetFullPath(Root);if(!root.Split(Path.DirectorySeparatorChar).Any(p=>p.Equals("Build",StringComparison.OrdinalIgnoreCase)))throw new InvalidOperationException("Owned Build factory root required");return root;}
    public static void Sensors(){OriginalConstructorPorts.Mark("device.sensors");if(UnsupportedSensors)throw new PlatformNotSupportedException("JOINED_UNSUPPORTED_SENSORS");}
    public static void SettingWrite(string name){OriginalConstructorPorts.Mark("setting-write:"+name);throw new InvalidOperationException("JOINED_SETTING_WRITE_REFUSED:"+name);}
}

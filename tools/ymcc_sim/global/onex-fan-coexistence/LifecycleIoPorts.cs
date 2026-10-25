using System.Collections.Generic;
using System.Runtime.CompilerServices;
public static class LifecycleIoPorts
{
    public sealed record IoRow(string Stage,int Transport,ushort Register,byte Value,int Thread);
    public static readonly List<IoRow> Rows=new();
    static readonly Dictionary<object,int> Ids=new(ReferenceEqualityComparer.Instance);
    static readonly HashSet<object> Opened=new(ReferenceEqualityComparer.Instance);
    public static bool InitializeResult=true;public static int InitializeFailures,DisposeFailures,WriteFailures,ExitFailures;
    public static Action<string,object>? OnIo;
    public static int OpenTransportCount=>Opened.Count;
    static int Id(object transport){if(!Ids.TryGetValue(transport,out var id)){id=Ids.Count+1;Ids.Add(transport,id);}return id;}
    static void Record(string stage,object transport,ushort register=0,byte value=0){Rows.Add(new(stage,Id(transport),register,value,Environment.CurrentManagedThreadId));OriginalConstructorPorts.Mark("life."+stage);OnIo?.Invoke(stage,transport);}
    public static void Reset(){Rows.Clear();Ids.Clear();Opened.Clear();InitializeResult=true;InitializeFailures=DisposeFailures=WriteFailures=ExitFailures=0;OnIo=null;}
    public static bool Initialize(object transport){Record("initialize",transport);if(InitializeFailures-->0)throw new System.IO.IOException("LIFE_INIT_FAILED");if(InitializeResult)Opened.Add(transport);return InitializeResult;}
    public static void Dispose(object transport){Record("dispose",transport);if(DisposeFailures-->0)throw new System.IO.IOException("LIFE_DISPOSE_FAILED");Opened.Remove(transport);}
    static void Require(object transport){if(!Opened.Contains(transport))throw new InvalidOperationException("LIFE_TRANSPORT_NOT_OPEN");}
    public static void Enter(object transport,ushort command,ushort data){Require(transport);Record("enter",transport,command,(byte)data);}
    public static void Write(object transport,ushort port,byte value){Require(transport);Record("port-write",transport,port,value);if(WriteFailures-->0)throw new System.IO.IOException("LIFE_PORT_WRITE_FAILED");}
    public static void Exit(object transport,ushort command,ushort data){Require(transport);Record("exit",transport,command,(byte)data);if(ExitFailures-->0)throw new System.IO.IOException("LIFE_PORT_EXIT_FAILED");}
    public static void Forbidden(string stage){OriginalConstructorPorts.Mark("life.forbidden."+stage);throw new InvalidOperationException("LIFE_FORBIDDEN:"+stage);}
}

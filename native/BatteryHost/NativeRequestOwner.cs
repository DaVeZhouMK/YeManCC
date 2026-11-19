using System.Globalization;
using System.Runtime.InteropServices;
using System.Text.Json;
namespace YeManBatteryHost;

// A synchronous native request borrows this callback/context only until Invoke
// returns. InProcessRuntime clears it while still holding operationGate; timers,
// cleanup and later ordinary calls must never retain or call the borrowed pointer.
internal sealed class NativeRequestOwner
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int Callback(nuint context);
    readonly Callback callback;
    readonly nuint context;
    NativeRequestOwner(nuint reader,nuint context){callback=Marshal.GetDelegateForFunctionPointer<Callback>((nint)reader);this.context=context;}
    static nuint Pointer(JsonElement field)
    {
        ulong value=0;
        bool valid=field.ValueKind==JsonValueKind.Number?field.TryGetUInt64(out value):
            field.ValueKind==JsonValueKind.String&&ulong.TryParse(field.GetString(),NumberStyles.None,CultureInfo.InvariantCulture,out value);
        if(!valid||value==0||value>(ulong)nint.MaxValue)throw new IOException("nativeRequestOwner 必须包含有效的进程内指针");
        return (nuint)value;
    }
    internal static NativeRequestOwner? Capture(JsonElement envelope)
    {
        JsonElement? supplied=null;
        foreach(var property in envelope.EnumerateObject())if(property.NameEquals("nativeRequestOwner"))
        {if(supplied is not null)throw new IOException("nativeRequestOwner 不允许重复字段");supplied=property.Value;}
        if(supplied is null)return null;
        if(supplied.Value.ValueKind!=JsonValueKind.Object)throw new IOException("nativeRequestOwner 必须是对象");
        var fields=new Dictionary<string,JsonElement>(StringComparer.Ordinal);
        foreach(var property in supplied.Value.EnumerateObject())
            if(property.Name is not ("reader" or "context")||!fields.TryAdd(property.Name,property.Value))throw new IOException("nativeRequestOwner 字段异常");
        if(fields.Count!=2)throw new IOException("nativeRequestOwner 缺少字段");
        return new NativeRequestOwner(Pointer(fields["reader"]),Pointer(fields["context"]));
    }
    internal bool Current(){try{return callback(context)==1;}catch{return false;}}
    internal void RequireCurrent(){if(!Current())throw new IOException("BOOT_CHARGE_REQUEST_EXPIRED");}
}

using System.Buffers.Binary;
using System.Collections;
using System.Diagnostics;
using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Text;

public static class ManagedProbe
{
    sealed class TargetContext(string root,List<string> attempts) : AssemblyLoadContext(isCollectible:true)
    {
        public readonly List<Assembly> Loaded=[];
        protected override Assembly? Load(AssemblyName name)
        {
            if(!Program.Target(name.Name??""))return null;
            var asm=LoadFromAssemblyPath(Path.Combine(root,name.Name+".dll"));
            NativeLibrary.SetDllImportResolver(asm,(library,_,_)=>{attempts.Add(library);throw new DllNotFoundException("Target native resolution denied: "+library);});
            Loaded.Add(asm);return asm;
        }
        protected override IntPtr LoadUnmanagedDll(string name){attempts.Add(name);throw new DllNotFoundException("Target native resolution denied: "+name);}
        public Assembly Get(string name)=>LoadFromAssemblyName(new AssemblyName(name));
    }
    static HashSet<string> NativeModules()=>Process.GetCurrentProcess().Modules.Cast<ProcessModule>().Select(m=>m.FileName).Where(n=>n!=null).ToHashSet(StringComparer.OrdinalIgnoreCase)!;
    public static ProbeResult Run(string kind,string root)
    {
        var checks=new List<Check>();var attempts=new List<string>();object? data=null;
        var safety=Metadata.Safety(root,kind);
        checks.Add(new("safety",safety.Passed,string.Join("; ",safety.Failures)));
        if(!safety.Passed)return new(kind,root,"failed",checks,new{Safety=safety},[],[]);
        var before=NativeModules();var context=new TargetContext(Path.GetFullPath(root),attempts);
        try
        {
            var core=context.Get("SharpDX");
            data=kind switch{"enum"=>Enum(context.Get("SharpDX.DirectInput"),checks),"format"=>Format(context.Get("SharpDX.DirectInput"),checks),"shadow"=>Shadow(core,context.Get("SharpDX.Direct3D9"),checks),_=>throw new ArgumentException("Unknown probe")};
        }
        catch(Exception e){e=Program.Unwrap(e);checks.Add(new(kind+"-execution",false,e.GetType().Name+": "+e.Message));}
        var managedPaths=context.Loaded.Select(a=>a.Location).ToHashSet(StringComparer.OrdinalIgnoreCase);
        var newNative=NativeModules().Except(before).Where(p=>!managedPaths.Contains(p)).Where(p=>{var n=Path.GetFileName(p);return n.StartsWith("dinput",StringComparison.OrdinalIgnoreCase)||n.StartsWith("d3d",StringComparison.OrdinalIgnoreCase)||n.StartsWith("dxgi",StringComparison.OrdinalIgnoreCase)||n.Contains("SharpDX",StringComparison.OrdinalIgnoreCase);}).ToArray();
        checks.Add(new("no-target-native",attempts.Count==0&&newNative.Length==0,$"DllImport/ALC attempts={attempts.Count}; new target native modules={newNative.Length}"));
        context.Unload();
        return new(kind,root,checks.All(c=>c.Passed)?"passed":"failed",checks,new{Safety=safety,Payload=data},attempts.ToArray(),newNative);
    }
    static object Enum(Assembly input,List<Check> checks)
    {
        var type=input.GetType("SharpDX.DirectInput.EnumDevicesCallback",true)!;
        var native=input.GetType("SharpDX.DirectInput.DeviceInstance+__Native",true)!;
        var callback=Activator.CreateInstance(type,true)!;
        var pointer=(IntPtr)type.GetProperty("NativePointer")!.GetValue(callback)!;
        var field=type.GetField("_callback",BindingFlags.Instance|BindingFlags.NonPublic)!;
        var original=(Delegate)field.GetValue(callback)!;
        var rebound=Marshal.GetDelegateForFunctionPointer(pointer,original.GetType());
        checks.Add(new("enum-binding",pointer!=IntPtr.Zero&&ReferenceEquals(original,rebound),"Actual private callback delegate -> CLR thunk -> same delegate instance"));
        var method=new DynamicMethod("InvokeManagedEnum",typeof(int),[typeof(Delegate),typeof(IntPtr),typeof(IntPtr)],typeof(ManagedProbe).Module,true);
        var il=method.GetILGenerator();il.Emit(OpCodes.Ldarg_0);il.Emit(OpCodes.Castclass,original.GetType());il.Emit(OpCodes.Ldarg_1);il.Emit(OpCodes.Conv_U);il.Emit(OpCodes.Ldarg_2);il.Emit(OpCodes.Callvirt,original.GetType().GetMethod("Invoke")!);il.Emit(OpCodes.Ret);
        var invoke=(Func<Delegate,IntPtr,IntPtr,int>)method.CreateDelegate(typeof(Func<Delegate,IntPtr,IntPtr,int>));
        var sizeMethod=new DynamicMethod("RawSize",typeof(int),Type.EmptyTypes,typeof(ManagedProbe).Module,true);
        var sizeIL=sizeMethod.GetILGenerator();sizeIL.Emit(OpCodes.Sizeof,native);sizeIL.Emit(OpCodes.Ret);
        var size=((Func<int>)sizeMethod.CreateDelegate(typeof(Func<int>)))();
        var offsetValue=Activator.CreateInstance(native)!;
        int ClrOffset(string name)
        {
            var dm=new DynamicMethod("RawOffset",typeof(int),[typeof(object)],typeof(ManagedProbe).Module,true);var gen=dm.GetILGenerator();
            gen.Emit(OpCodes.Ldarg_0);gen.Emit(OpCodes.Unbox,native);gen.Emit(OpCodes.Ldflda,native.GetField(name,BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic)!);gen.Emit(OpCodes.Conv_U);
            gen.Emit(OpCodes.Ldarg_0);gen.Emit(OpCodes.Unbox,native);gen.Emit(OpCodes.Conv_U);gen.Emit(OpCodes.Sub);gen.Emit(OpCodes.Conv_I4);gen.Emit(OpCodes.Ret);
            return ((Func<object,int>)dm.CreateDelegate(typeof(Func<object,int>)))(offsetValue);
        }
        var returns=new List<int>();var records=new List<object>();
        for(var k=0;k<2;k++)
        {
            var bytes=new byte[size];
            int Offset(string name)=>ClrOffset(name);
            void Int(string name,int value)=>BinaryPrimitives.WriteInt32LittleEndian(bytes.AsSpan(Offset(name),4),value);
            var instance=Guid.Parse(k==0?"11111111-2222-3333-4444-555555555555":"aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");
            var product=Guid.Parse("01234567-89ab-cdef-0123-456789abcdef");
            Int("Size",size);instance.ToByteArray().CopyTo(bytes,Offset("InstanceGuid"));product.ToByteArray().CopyTo(bytes,Offset("ProductGuid"));Int("RawType",0x10114);
            Encoding.Unicode.GetBytes("YMCC in-memory instance "+k+"\0").CopyTo(bytes,Offset("InstanceName"));
            Encoding.Unicode.GetBytes("No hardware product "+k+"\0").CopyTo(bytes,Offset("ProductName"));
            BinaryPrimitives.WriteUInt16LittleEndian(bytes.AsSpan(Offset("UsagePage")),1);BinaryPrimitives.WriteUInt16LittleEndian(bytes.AsSpan(Offset("Usage")),5);
            var handle=GCHandle.Alloc(bytes,GCHandleType.Pinned);
            try{GC.Collect();GC.WaitForPendingFinalizers();returns.Add(invoke(rebound,handle.AddrOfPinnedObject(),IntPtr.Zero));}finally{handle.Free();}
            var list=(IList)type.GetProperty("DeviceInstances")!.GetValue(callback)!;
            var value=list[k]!;object? Field(string name)=>value.GetType().GetField(name,BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic)!.GetValue(value);
            bool valid=(Guid)Field("InstanceGuid")! == instance && (Guid)Field("ProductGuid")! == product && (string)Field("InstanceName")! == "YMCC in-memory instance "+k && (string)Field("ProductName")! == "No hardware product "+k && Convert.ToInt32(Field("UsagePage"))==1 && Convert.ToInt32(Field("Usage"))==5 && (int)Field("Size")! == size && (int)Field("RawType")! == 0x10114;
            checks.Add(new("enum-record-"+k,valid,"Synthesized GUID/name/size/type/HID usage decoded by the shipped __MarshalFrom"));
            records.Add(new{InstanceGuid=Field("InstanceGuid"),ProductGuid=Field("ProductGuid"),InstanceName=Field("InstanceName"),ProductName=Field("ProductName"),Size=Field("Size"),RawType=Field("RawType"),UsagePage=Convert.ToInt32(Field("UsagePage")),Usage=Convert.ToInt32(Field("Usage"))});
        }
        checks.Add(new("enum-return",returns.SequenceEqual(new[]{1,1}),"Callback return values: "+string.Join(",",returns)));
        GC.KeepAlive(callback);GC.KeepAlive(rebound);
        return new{NativeStructureBytes=size,MarshalSizeForComparison=Marshal.SizeOf(native),LayoutMechanism="CLR sizeof/ldflda, matching the unsafe callback; Marshal.SizeOf treats default CharSet chars differently",Returns=returns,Records=records,Mechanism="managed delegate Invoke after pointer binding; no unmanaged calli or EnumDevices API"};
    }
    static object Format(Assembly input,List<Check> checks)
    {
        var type=input.GetType("SharpDX.DirectInput.Joystick",true)!;
        var value=RuntimeHelpers.GetUninitializedObject(type);GC.SuppressFinalize(value);
        var generic=type.BaseType!;
        var map=generic.GetField("_mapNameToObjectFormat",BindingFlags.Instance|BindingFlags.NonPublic)!;
        map.SetValue(value,Activator.CreateInstance(map.FieldType));
        object format;
        try{format=generic.GetMethod("GetDataFormat",BindingFlags.Instance|BindingFlags.NonPublic)!.Invoke(value,null)!;}
        catch(Exception e){e=Program.Unwrap(e);checks.Add(new("format-content",false,e.GetType().Name+": "+e.Message));return new{Error=e.Message};}
        var fmtType=format.GetType();var objects=(IEnumerable)fmtType.GetProperty("ObjectsFormat")!.GetValue(format)!;
        var rows=new List<object>();var names=new List<string>();var offsets=new List<int>();
        foreach(var o in objects)
        {
            object Get(string name)=>o!.GetType().GetProperty(name)!.GetValue(o)!;
            names.Add((string)Get("Name"));offsets.Add((int)Get("Offset"));
            rows.Add(new{Name=Get("Name"),Guid=Get("Guid"),Offset=Get("Offset"),TypeFlags=Convert.ToInt32(Get("TypeFlags")),Flags=Convert.ToInt32(Get("Flags")),InstanceNumber=Get("InstanceNumber")});
        }
        var dataSize=(int)fmtType.GetField("DataSize")!.GetValue(format)!;
        var again=generic.GetMethod("GetDataFormat",BindingFlags.Instance|BindingFlags.NonPublic)!.Invoke(value,null);
        checks.Add(new("format-content",dataSize==272&&rows.Count==164&&names.Distinct().Count()==164&&names[0]=="X"&&offsets[0]==0,$"RawJoystickState DataSize={dataSize}; reflected objects={rows.Count}; first={names.FirstOrDefault()}"));
        checks.Add(new("format-cache",ReferenceEquals(format,again),"Real GetDataFormat reflection result is cached on a zero-handle object; device constructors and disposal are skipped"));
        // Payload alone is stable across original/current roots. Safety traces are intentionally excluded from equality.
        return new{DataSize=dataSize,Objects=rows};
    }
    static object Shadow(Assembly core,Assembly d3d,List<Check> checks)
    {
        var include=d3d.GetType("SharpDX.Direct3D9.Include",false);
        var cb=core.GetType("SharpDX.ICallbackable",true)!;
        var toPtr=core.GetType("SharpDX.CppObject",true)!.GetMethods(BindingFlags.Static|BindingFlags.Public).SingleOrDefault(m=>m.Name=="ToCallbackPtr");
        checks.Add(new("shadow-types",include!=null&&toPtr!=null,"Include="+(include!=null)+"; CppObject.ToCallbackPtr="+(toPtr!=null)));
        if(include==null||toPtr==null)return new{Missing="Include or ToCallbackPtr"};
        var adapter=CreateAdapter(include,cb);var instance=Activator.CreateInstance(adapter)!;
        var call=toPtr.MakeGenericMethod(include);
        var pointer=(IntPtr)call.Invoke(null,[instance])!;
        var second=(IntPtr)call.Invoke(null,[instance])!;
        checks.Add(new("shadow-pointer",pointer!=IntPtr.Zero&&pointer==second,"Actual ShadowAttribute.Get -> Activator -> ShadowContainer.Initialize -> CppObjectShadow.Initialize; cached pointer reused"));
        var shadow=cb.GetProperty("Shadow")!.GetValue(instance) as IDisposable;
        if(pointer==IntPtr.Zero||shadow==null)return new{PointerNonzero=false};
        try
        {
            var vtbl=Marshal.ReadIntPtr(pointer);var includeShadow=d3d.GetType("SharpDX.Direct3D9.IncludeShadow",true)!;
            var actualVtbl=includeShadow.GetField("Vtbl",BindingFlags.Static|BindingFlags.NonPublic)!.GetValue(null)!;
            var delegates=(IList)actualVtbl.GetType().BaseType!.GetField("methods",BindingFlags.Instance|BindingFlags.NonPublic)!.GetValue(actualVtbl)!;
            var slots=new List<object>();
            for(var i=0;i<2;i++)
            {
                var d=(Delegate)delegates[i]!;var fp=Marshal.ReadIntPtr(vtbl,i*IntPtr.Size);var rebound=Marshal.GetDelegateForFunctionPointer(fp,d.GetType());
                GC.Collect();GC.WaitForPendingFinalizers();
                bool ok=fp!=IntPtr.Zero&&ReferenceEquals(d,rebound)&&Marshal.GetFunctionPointerForDelegate(d)==fp;
                checks.Add(new("shadow-slot-"+i,ok,"Bound retained "+d.GetType().Name+"; body intentionally not invoked"));
                slots.Add(new{Slot=i,Delegate=d.GetType().FullName,Convention=d.GetType().GetCustomAttribute<UnmanagedFunctionPointerAttribute>()?.CallingConvention.ToString(),PointerStable=ok});
            }
            return new{PointerNonzero=true,CachedPointer=true,Slots=slots,OpenCloseInvoked=false};
        }
        finally{shadow.Dispose();cb.GetProperty("Shadow")!.SetValue(instance,null);GC.SuppressFinalize(instance);}
    }
    static Type CreateAdapter(Type include,Type callback)
    {
        var assembly=AssemblyBuilder.DefineDynamicAssembly(new AssemblyName("SharpDxMemoryCallback_"+Guid.NewGuid().ToString("N")),AssemblyBuilderAccess.RunAndCollect);
        var tb=assembly.DefineDynamicModule("memory").DefineType("MemoryInclude",TypeAttributes.Public|TypeAttributes.Sealed);
        tb.AddInterfaceImplementation(include);tb.DefineDefaultConstructor(MethodAttributes.Public);
        var storage=tb.DefineField("_shadow",typeof(IDisposable),FieldAttributes.Private);
        foreach(var iface in include.GetInterfaces().Append(include).Distinct())foreach(var m in iface.GetMethods(BindingFlags.Public|BindingFlags.Instance|BindingFlags.DeclaredOnly))
        {
            var mb=tb.DefineMethod(m.Name,MethodAttributes.Public|MethodAttributes.Virtual|MethodAttributes.Final|MethodAttributes.HideBySig|MethodAttributes.NewSlot,m.ReturnType,m.GetParameters().Select(p=>p.ParameterType).ToArray());
            var il=mb.GetILGenerator();
            if(m.Name=="get_Shadow"){il.Emit(OpCodes.Ldarg_0);il.Emit(OpCodes.Ldfld,storage);}
            else if(m.Name=="set_Shadow"){il.Emit(OpCodes.Ldarg_0);il.Emit(OpCodes.Ldarg_1);il.Emit(OpCodes.Stfld,storage);}
            else if(m.Name=="Open"){il.Emit(OpCodes.Newobj,typeof(MemoryStream).GetConstructor(Type.EmptyTypes)!);}
            else if(m.ReturnType!=typeof(void))throw new InvalidOperationException("Unreviewed callback adapter method: "+m);
            il.Emit(OpCodes.Ret);tb.DefineMethodOverride(mb,m);
        }
        Program.Require(callback.IsAssignableFrom(include),"Include no longer implements ICallbackable");
        return tb.CreateType()!;
    }
}


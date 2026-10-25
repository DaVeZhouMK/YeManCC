using Mono.Cecil;
using Mono.Cecil.Cil;

internal static partial class Program
{
    // Fixtures are emitted/read as metadata. None are loaded or executed.
    static void SelfTestCom(string output)
    {
        output=Path.GetFullPath(output); if(Directory.Exists(output)) throw new IOException("Selftest evidence must be new"); Directory.CreateDirectory(output);
        string original=Path.Combine(output,"original");Directory.CreateDirectory(original);
        AssemblyDefinition New(string name)=>AssemblyDefinition.CreateAssembly(new AssemblyNameDefinition(name,new Version(1,0)),name,ModuleKind.Dll);
        TypeReference Ref(ModuleDefinition m,string assembly,string type) {var ar=new AssemblyNameReference(assembly,new Version(1,0));if(!m.AssemblyReferences.Any(a=>a.Name==assembly))m.AssemblyReferences.Add(ar);return new TypeReference("Fixture",type,m,ar);}
        MethodDefinition Method(TypeDefinition t,string name,bool abstractMethod=false) {var m=t.Module;var md=new MethodDefinition(name,MethodAttributes.Public|MethodAttributes.Virtual|MethodAttributes.NewSlot|(abstractMethod?MethodAttributes.Abstract:0),m.TypeSystem.Void);t.Methods.Add(md);if(!abstractMethod)md.Body.GetILProcessor().Emit(OpCodes.Ret);return md;}
        using(var a=New("Ancestor")){var t=new TypeDefinition("Fixture","IAncestor",TypeAttributes.Public|TypeAttributes.Interface|TypeAttributes.Abstract);a.MainModule.Types.Add(t);Method(t,"BaseSlot",true);a.Write(Path.Combine(original,"Ancestor.dll"));}
        using(var a=New("Callback")){var m=a.MainModule;var t=new TypeDefinition("Fixture","NativeCallback",TypeAttributes.Public|TypeAttributes.Sealed,m.ImportReference(typeof(MulticastDelegate)));m.Types.Add(t);var ctor=new MethodDefinition(".ctor",MethodAttributes.Public|MethodAttributes.SpecialName|MethodAttributes.RTSpecialName,m.TypeSystem.Void){ImplAttributes=MethodImplAttributes.Runtime};ctor.Parameters.Add(new ParameterDefinition(m.TypeSystem.Object));ctor.Parameters.Add(new ParameterDefinition(m.TypeSystem.IntPtr));t.Methods.Add(ctor);var invoke=new MethodDefinition("Invoke",MethodAttributes.Public|MethodAttributes.Virtual|MethodAttributes.NewSlot,m.TypeSystem.Void){ImplAttributes=MethodImplAttributes.Runtime};invoke.Parameters.Add(new ParameterDefinition(m.TypeSystem.Int32));t.Methods.Add(invoke);a.Write(Path.Combine(original,"Callback.dll"));}
        using(var a=New("Owner")){var m=a.MainModule;var t=new TypeDefinition("Fixture","INative",TypeAttributes.Public|TypeAttributes.Interface|TypeAttributes.Abstract|TypeAttributes.Import);m.Types.Add(t);t.Interfaces.Add(new InterfaceImplementation(Ref(m,"Ancestor","IAncestor")));Method(t,"UnusedNativeSlot",true);Method(t,"UsedNativeSlot",true);var native=new TypeDefinition("Fixture","Native",TypeAttributes.Public,m.TypeSystem.Object);m.Types.Add(native);var pinvoke=new MethodDefinition("Register",MethodAttributes.Public|MethodAttributes.Static|MethodAttributes.PInvokeImpl,m.TypeSystem.Void);pinvoke.Parameters.Add(new ParameterDefinition(Ref(m,"Callback","NativeCallback")));pinvoke.Parameters.Add(new ParameterDefinition("Count",ParameterAttributes.None,m.TypeSystem.Int32));var module=new ModuleReference("fixture-native-do-not-load.dll");m.ModuleReferences.Add(module);pinvoke.PInvokeInfo=new PInvokeInfo(PInvokeAttributes.CallConvCdecl,"register",module);native.Methods.Add(pinvoke);native.Fields.Add(new FieldDefinition("Unrelated",FieldAttributes.Public,Ref(m,"Unrelated","Data")));native.Fields.Add(new FieldDefinition("Primitive",FieldAttributes.Public,m.TypeSystem.Int32));m.AssemblyReferences.Add(new AssemblyNameReference("Unavailable.Contract",new Version(7,0)));a.MainModule.Types.Add(new TypeDefinition("Fixture","UnusedManaged",TypeAttributes.Public,a.MainModule.TypeSystem.Object));a.MainModule.Resources.Add(new EmbeddedResource("fixture-unused.bin",ManifestResourceAttributes.Private,new byte[32768]));a.Write(Path.Combine(original,"Owner.dll"));}
        using(var a=New("Implementation")){var m=a.MainModule;var inherited=new TypeDefinition("Fixture","IDerived",TypeAttributes.Public|TypeAttributes.Interface|TypeAttributes.Abstract);m.Types.Add(inherited);inherited.Interfaces.Add(new InterfaceImplementation(Ref(m,"Owner","INative")));var t=new TypeDefinition("Fixture","Receiver",TypeAttributes.Public,m.TypeSystem.Object);m.Types.Add(t);t.Interfaces.Add(new InterfaceImplementation(inherited));Method(t,"UsedNativeSlot");a.Write(Path.Combine(original,"Implementation.dll"));}
        using(var a=New("Unrelated")){a.MainModule.Types.Add(new TypeDefinition("Fixture","Data",TypeAttributes.Public,a.MainModule.TypeSystem.Object));a.Write(Path.Combine(original,"Unrelated.dll"));}
        using var inventory=InspectCom(original);
        var checks=new Dictionary<string,bool>{["COM_owner_protected"]=inventory.Protected.ContainsKey("Owner"),["unmarked_native_signature_callback_protected"]=inventory.Protected.ContainsKey("Callback"),["unmarked_inherited_interface_and_receiver_protected"]=inventory.Protected.ContainsKey("Implementation"),["unmarked_base_interface_protected"]=inventory.Protected.ContainsKey("Ancestor"),["unrelated_dependency_not_protected"]=!inventory.Protected.ContainsKey("Unrelated"),["missing_dependency_reported"]=inventory.UnavailableDependencies.Any(s=>s.Contains("Unavailable.Contract")),["original_passes"]=CompareCom(original,original).Status=="passed"};
        string Variant(string name){var dir=Path.Combine(output,name);Directory.CreateDirectory(dir);foreach(var p in Directory.GetFiles(original,"*.dll"))File.Copy(p,Path.Combine(dir,Path.GetFileName(p)));return dir;}
        void Mutate(string file,Action<AssemblyDefinition> action){using var ms=new MemoryStream(File.ReadAllBytes(file));using var a=AssemblyDefinition.ReadAssembly(ms);action(a);a.Write(file);}
        var smaller=Variant("safe-derivative");Mutate(Path.Combine(smaller,"Owner.dll"),a=>{a.MainModule.Resources.Clear();a.MainModule.Types.Remove(Types(a.MainModule).Single(t=>t.Name=="UnusedManaged"));});
        checks["smaller_non_ABI_derivative_accepted"]=CompareCom(original,smaller).Status=="passed" && new FileInfo(Path.Combine(smaller,"Owner.dll")).Length<new FileInfo(Path.Combine(original,"Owner.dll")).Length;
        var badField=Variant("bad-field");Mutate(Path.Combine(badField,"Owner.dll"),a=>Types(a.MainModule).Single(t=>t.Name=="Native").Fields.Add(new FieldDefinition("UnexpectedNativeField",FieldAttributes.Public,a.MainModule.TypeSystem.Int64)));checks["changed_native_fields_rejected"]=CompareCom(original,badField).Status=="failed";
        var badEntry=Variant("bad-native-entry");Mutate(Path.Combine(badEntry,"Owner.dll"),a=>Types(a.MainModule).Single(t=>t.Name=="Native").Methods.Single(m=>m.Name=="Register").PInvokeInfo.EntryPoint="WrongEntry");checks["changed_native_import_entry_rejected"]=CompareCom(original,badEntry).Status=="failed";
        var badPacking=Variant("bad-packing");Mutate(Path.Combine(badPacking,"Owner.dll"),a=>Types(a.MainModule).Single(t=>t.Name=="Native").PackingSize=1);checks["changed_native_packing_rejected"]=CompareCom(original,badPacking).Status=="failed";
        var badSlot=Variant("bad-slot");Mutate(Path.Combine(badSlot,"Owner.dll"),a=>{var t=Types(a.MainModule).Single(t=>t.Name=="INative");t.Methods.RemoveAt(0);});var slot=CompareCom(original,badSlot);checks["deleted_unused_COM_slot_rejected"]=slot.Status=="failed";
        var badCallback=Variant("bad-callback");Mutate(Path.Combine(badCallback,"Callback.dll"),a=>Types(a.MainModule).Single(t=>t.Name=="NativeCallback").Methods.Single(m=>m.Name=="Invoke").Parameters[0].ParameterType=a.MainModule.TypeSystem.Int64);var callback=CompareCom(original,badCallback);checks["callback_signature_rejected"]=callback.Status=="failed";
        var badImplementation=Variant("bad-implementation");Mutate(Path.Combine(badImplementation,"Implementation.dll"),a=>Types(a.MainModule).Single(t=>t.Name=="Receiver").Methods.Clear());checks["callback_implementation_rejected"]=CompareCom(original,badImplementation).Status=="failed";
        var missing=Variant("missing-callback");File.Delete(Path.Combine(missing,"Callback.dll"));checks["missing_callback_assembly_rejected"]=CompareCom(original,missing).Status=="failed";
        File.Copy(Path.Combine(original,"Owner.dll"),Path.Combine(badSlot,"Owner.dll"),true);checks["exact_restoration_passes"]=CompareCom(original,badSlot).Status=="passed";
        // The linker may encode the same CLI primitive through the framework
        // facade or its resolved definition. Compare the actual definition;
        // unknown and unrelated assembly scopes must still remain distinct.
        var forwardedParameter=Variant("framework-forwarded-corelib");
        Mutate(Path.Combine(forwardedParameter,"Owner.dll"),a=>{
            // CLI primitives have no per-use TypeRef scope: change the module's
            // actual core-library reference, as the linker does during retargeting.
            var core=(AssemblyNameReference)a.MainModule.TypeSystem.CoreLibrary;
            var target=AssemblyNameReference.Parse(typeof(int).Assembly.FullName!);
            core.Name=target.Name;core.Version=target.Version;core.PublicKeyToken=target.PublicKeyToken;
        });
        checks["framework_forwarded_corelib_assembly_accepted"]=CompareCom(original,forwardedParameter).Status=="passed";
        using(var before=Read(Path.Combine(original,"Owner.dll")))using(var after=Read(Path.Combine(forwardedParameter,"Owner.dll"))){
            var b=Types(before.MainModule).Single(t=>t.Name=="Native");
            var c=Types(after.MainModule).Single(t=>t.Name=="Native");
            var bm=b.Methods.Single(m=>m.Name=="Register");var cm=c.Methods.Single(m=>m.Name=="Register");
            checks["framework_forwarded_parameter_accepted"]=ComMethod(bm)==ComMethod(cm);
            checks["framework_forwarded_field_accepted"]=ComField(b.Fields.Single(f=>f.Name=="Primitive"))==ComField(c.Fields.Single(f=>f.Name=="Primitive"));
            var bt=bm.Parameters[1].ParameterType;var ct=cm.Parameters[1].ParameterType;
            checks["framework_positive_requires_distinct_spelling_same_definition"]=Scope(bt)!=Scope(ct)&&SemanticScope(bt)==SemanticScope(ct)&&bt.Resolve().FullName==ct.Resolve().FullName;
        }
        var changedPrimitive=Variant("bad-framework-primitive");
        Mutate(Path.Combine(changedPrimitive,"Owner.dll"),a=>Types(a.MainModule).Single(t=>t.Name=="Native").Methods.Single(m=>m.Name=="Register").Parameters[1].ParameterType=a.MainModule.ImportReference(typeof(long)));
        checks["framework_forwarded_parameter_type_change_rejected"]=CompareCom(original,changedPrimitive).Status=="failed";
        var changedFieldPrimitive=Variant("bad-framework-field");
        Mutate(Path.Combine(changedFieldPrimitive,"Owner.dll"),a=>Types(a.MainModule).Single(t=>t.Name=="Native").Fields.Single(f=>f.Name=="Primitive").FieldType=a.MainModule.ImportReference(typeof(long)));
        checks["framework_forwarded_field_type_change_rejected"]=CompareCom(original,changedFieldPrimitive).Status=="failed";
        var unknownFramework=Variant("bad-unresolved-framework-scope");
        Mutate(Path.Combine(unknownFramework,"Owner.dll"),a=>Types(a.MainModule).Single(t=>t.Name=="Native").Fields.Single(f=>f.Name=="Unrelated").FieldType=Ref(a.MainModule,"System.UnavailableFixture","Data"));
        checks["unresolved_framework_scope_change_rejected"]=CompareCom(original,unknownFramework).Status=="failed";
        var unrelatedScope=Variant("bad-unrelated-owner");
        using(var a=New("System.UnrelatedFixture")){a.MainModule.Types.Add(new TypeDefinition("Fixture","Data",TypeAttributes.Public,a.MainModule.TypeSystem.Object));a.Write(Path.Combine(unrelatedScope,"System.UnrelatedFixture.dll"));}
        Mutate(Path.Combine(unrelatedScope,"Owner.dll"),a=>Types(a.MainModule).Single(t=>t.Name=="Native").Fields.Single(f=>f.Name=="Unrelated").FieldType=Ref(a.MainModule,"System.UnrelatedFixture","Data"));
        checks["resolved_framework_like_different_owner_rejected"]=CompareCom(original,unrelatedScope).Status=="failed";
        Write(Path.Combine(output,"result.json"),new{status=checks.Values.All(v=>v)?"passed":"failed",checks,slotFailures=slot.Failures,callbackFailures=callback.Failures,protectedAssemblies=inventory.Protected.Keys.Order().ToArray(),unavailableDependencies=inventory.UnavailableDependencies,targetCodeExecuted=false,constructorsExecuted=false,hardwareAccess=false});
        AuditCom(original,Path.Combine(output,"audit.json"),Path.Combine(output,"roots.xml"));Console.WriteLine($"COM SELFTEST {checks.Count} checks; {checks.Count(c=>!c.Value)} failed");if(checks.Values.Any(v=>!v))Environment.ExitCode=1;
    }
}

using System.IO;
using System.Text.Json;
using Mono.Cecil;
using Mono.Cecil.Cil;

static partial class Harness
{
    static int BootstrapStructure(string[] args)
    {
        using var original=AssemblyDefinition.ReadAssembly(args[0]);
        using var fixture=AssemblyDefinition.ReadAssembly(args[1]);
        var aliases=new Dictionary<string,string>(StringComparer.Ordinal);
        var pairs=new List<(MethodDefinition original,MethodDefinition fixture)>();
        MethodDefinition Method(AssemblyDefinition image,string type,string name)=>image.MainModule.GetType(type).Methods.Single(m=>m.Name==name);
        foreach(var name in new[]{"GetCurrent","PullSensors","Initialize"})pairs.Add((Method(original,"HandheldCompanion.Devices.IDevice",name),Method(fixture,"HandheldCompanion.Devices.IDevice",name)));
        foreach(var method in original.MainModule.GetType("HandheldCompanion.MotherboardInfo").Methods)
        {
            var match=fixture.MainModule.GetType(method.DeclaringType.FullName).Methods.Single(m=>m.Name==method.Name&&m.Parameters.Count==method.Parameters.Count);
            pairs.Add((method,match));
        }
        pairs.Add((Method(original,"HandheldCompanion.Utils.CommonUtils","Between"),Method(fixture,"HandheldCompanion.Utils.CommonUtils","Between")));
        // Bind compiler display classes only through the actual delegate site.
        // Never normalize arbitrary names or call/field signatures by a regex.
        for(int index=0;index<pairs.Count;index++)
        {
            var pair=pairs[index];
            var before=pair.original.Body.Instructions.Where(i=>i.OpCode==OpCodes.Ldftn).Select(i=>(MethodReference)i.Operand).ToArray();
            var after=pair.fixture.Body.Instructions.Where(i=>i.OpCode==OpCodes.Ldftn).Select(i=>(MethodReference)i.Operand).ToArray();
            Check(before.Length==after.Length,"Original delegate site count: "+pair.original.FullName);
            for(int i=0;i<before.Length;i++)
            {
                var left=before[i].Resolve();var right=after[i].Resolve();
                Check(left.DeclaringType.DeclaringType?.FullName==pair.original.DeclaringType.FullName&&right.DeclaringType.DeclaringType?.FullName==pair.fixture.DeclaringType.FullName,"Delegate belongs to the exact original declaring algorithm");
                var label=pair.original.DeclaringType.FullName+"/$delegate-owner-"+index+"-"+i;
                if(!aliases.ContainsKey(left.DeclaringType.FullName))aliases[left.DeclaringType.FullName]=label;
                if(!aliases.ContainsKey(right.DeclaringType.FullName))aliases[right.DeclaringType.FullName]=aliases[left.DeclaringType.FullName];
                var fieldsLeft=left.DeclaringType.Fields.Select(f=>f.Name+":"+f.FieldType.FullName).Order().ToArray();
                var fieldsRight=right.DeclaringType.Fields.Select(f=>f.Name+":"+f.FieldType.FullName).Order().ToArray();
                Check(fieldsLeft.SequenceEqual(fieldsRight),"Bound closure captured fields match exactly");
                if(!pairs.Any(p=>p.original.FullName==left.FullName))pairs.Add((left,right));
                foreach(var ctor in left.DeclaringType.Methods.Where(m=>m.IsConstructor))
                    if(!pairs.Any(p=>p.original.FullName==ctor.FullName))pairs.Add((ctor,right.DeclaringType.Methods.Single(m=>m.Name==ctor.Name)));
            }
        }
        string Normalize(string text){foreach(var (key,value)in aliases.OrderByDescending(p=>p.Key.Length))text=text.Replace(key,value,StringComparison.Ordinal);return text;}
        string Canon(MethodDefinition method)
        {
            var body=method.Body;var code=body.Instructions;
            int Index(Instruction? i)=>i is null?-1:code.IndexOf(i);
            string Operand(object? value)=>value switch
            {
                null=>"",Instruction target=>"target:"+Index(target),Instruction[] targets=>"targets:"+string.Join(",",targets.Select(Index)),
                VariableDefinition local=>"local:"+local.Index+":"+Normalize(local.VariableType.FullName),
                ParameterDefinition arg=>"arg:"+arg.Index+":"+Normalize(arg.ParameterType.FullName),
                MemberReference member=>Normalize(member.FullName),
                _=>Convert.ToString(value,System.Globalization.CultureInfo.InvariantCulture)??""
            };
            return JsonSerializer.Serialize(new{signature=Normalize(method.FullName),attributes=(int)method.Attributes,implementation=(int)method.ImplAttributes,body.InitLocals,
                locals=body.Variables.Select(v=>Normalize(v.VariableType.FullName)).ToArray(),
                instructions=code.Select(i=>new{opcode=i.OpCode.OperandType==OperandType.ShortInlineBrTarget?i.OpCode.Code.ToString()[..^2]:i.OpCode.Code.ToString(),operand=Operand(i.Operand)}).ToArray(),
                handlers=body.ExceptionHandlers.Select(h=>new{kind=h.HandlerType.ToString(),type=h.CatchType?.FullName,start=Index(h.TryStart),end=Index(h.TryEnd),handler=Index(h.HandlerStart),handlerEnd=Index(h.HandlerEnd),filter=Index(h.FilterStart)}).ToArray()});
        }
        var rows=pairs.Select(p=>new{method=p.original.FullName,fixtureMethod=p.fixture.FullName,matched=Canon(p.original)==Canon(p.fixture),original=Canon(p.original),fixture=Canon(p.fixture)}).ToArray();
        var failed=rows.Where(r=>!r.matched).ToArray();
        File.WriteAllText(args[2],JsonSerializer.Serialize(new{status=failed.Length==0?"PASS":"FAIL",originalSha256=Hash(args[0]),fixtureSha256=Hash(args[1]),methods=rows,aliases,checks=Checks,exactAlgorithms=failed.Length==0,deviceConstructorBodiesExecuted=false,hardwareIo=false},new JsonSerializerOptions{WriteIndented=true}));
        foreach(var row in failed)Console.WriteLine("BOOTSTRAP_IL_MISMATCH "+row.method);
        return failed.Length==0?0:2;
    }
    static int BindBootstrapOriginal(string[] args)
    {
        BootstrapStructure([args[0],args[1],args[3]+".before.json"]);
        using var report=JsonDocument.Parse(File.ReadAllText(args[3]+".before.json"));
        var compilationDifferences=report.RootElement.GetProperty("methods").EnumerateArray().Where(r=>!r.GetProperty("matched").GetBoolean()).Select(r=>r.GetProperty("method").GetString()!).ToHashSet(StringComparer.Ordinal);
        var admittedCompilationDifferences=new HashSet<string>(StringComparer.Ordinal){"System.Void HandheldCompanion.Devices.IDevice::PullSensors()","System.Object HandheldCompanion.MotherboardInfo::queryCacheValue(System.String,System.String)","System.String HandheldCompanion.MotherboardInfo::ConvertToDateTime(System.String)","System.Boolean HandheldCompanion.MotherboardInfo::Collect()","System.Void HandheldCompanion.MotherboardInfo::writeCache()"};
        Check(compilationDifferences.SetEquals(admittedCompilationDifferences),"Only the five recorded compilation/terminal type differences may precede exact original PE binding");
        using var original=AssemblyDefinition.ReadAssembly(args[0]);using var fixture=AssemblyDefinition.ReadAssembly(args[1]);
        static IEnumerable<TypeDefinition> Types(IEnumerable<TypeDefinition> types)=>types.SelectMany(t=>new[]{t}.Concat(Types(t.NestedTypes)));
        var sourceTypes=Types(original.MainModule.Types).ToDictionary(t=>t.FullName);
        var fixtureTypes=Types(fixture.MainModule.Types).ToDictionary(t=>t.FullName);
        var sourceMethods=sourceTypes.Values.SelectMany(t=>t.Methods).ToDictionary(m=>m.FullName);
        var fixtureMethods=fixtureTypes.Values.SelectMany(t=>t.Methods).ToDictionary(m=>m.FullName);
        var pairs=report.RootElement.GetProperty("methods").EnumerateArray().Select(r=>(source:sourceMethods[r.GetProperty("method").GetString()!],target:fixtureMethods[r.GetProperty("fixtureMethod").GetString()!])).ToArray();
        var owners=new Dictionary<string,TypeDefinition>();
        foreach(var pair in pairs)owners[pair.source.DeclaringType.FullName]=pair.target.DeclaringType;
        TypeReference Type(TypeReference type)=>type switch
        {
            GenericParameter=>type,
            GenericInstanceType generic=>Generic(generic),
            ByReferenceType reference=>new ByReferenceType(Type(reference.ElementType)),
            ArrayType array=>new ArrayType(Type(array.ElementType),array.Rank),
            PointerType pointer=>new PointerType(Type(pointer.ElementType)),
            _=>owners.TryGetValue(type.FullName,out var owner)?owner:fixtureTypes.TryGetValue(type.FullName,out var own)?own:
                type.Scope.Name==original.Name.Name?throw new InvalidOperationException("Unbound original HC type: "+type.FullName):fixture.MainModule.ImportReference(type)
        };
        TypeReference Generic(GenericInstanceType source)
        {
            var target=new GenericInstanceType(Type(source.ElementType));foreach(var arg in source.GenericArguments)target.GenericArguments.Add(Type(arg));return target;
        }
        MethodReference Method(MethodReference source)
        {
            if(source is GenericInstanceMethod generic)
            {
                var target=new GenericInstanceMethod(Method(generic.ElementMethod));foreach(var arg in generic.GenericArguments)target.GenericArguments.Add(Type(arg));return target;
            }
            var declaring=Type(source.DeclaringType);
            if(declaring is TypeDefinition own)
            {
                var match=own.Methods.SingleOrDefault(m=>m.Name==source.Name&&m.Parameters.Select(p=>p.ParameterType.FullName).SequenceEqual(source.Parameters.Select(p=>Type(p.ParameterType).FullName)));
                Check(match is not null,"Original local/terminal member is explicitly declared: "+source.FullName);
                Check(match!.HasThis==source.HasThis&&match.ExplicitThis==source.ExplicitThis&&match.ReturnType.FullName==Type(source.ReturnType).FullName&&match.GenericParameters.Count==source.GenericParameters.Count,"Original rebound member calling convention and return ABI: "+source.FullName);
                return match!;
            }
            var mapped=new MethodReference(source.Name,Type(source.ReturnType),declaring){HasThis=source.HasThis,ExplicitThis=source.ExplicitThis,CallingConvention=source.CallingConvention};
            foreach(var parameterType in source.GenericParameters)mapped.GenericParameters.Add(new GenericParameter(parameterType.Name,mapped));
            foreach(var parameter in source.Parameters)mapped.Parameters.Add(new ParameterDefinition(Type(parameter.ParameterType)));
            return mapped;
        }
        FieldReference Field(FieldReference source)
        {
            var declaring=Type(source.DeclaringType);
            if(declaring is TypeDefinition own){var field=own.Fields.SingleOrDefault(f=>f.Name==source.Name&&f.FieldType.FullName==Type(source.FieldType).FullName);Check(field is not null,"Original captured/device field is declared: "+source.FullName);return field!;}
            return new FieldReference(source.Name,Type(source.FieldType),declaring);
        }
        foreach(var (source,target)in pairs)
        {
            var body=new Mono.Cecil.Cil.MethodBody(target){InitLocals=source.Body.InitLocals,MaxStackSize=source.Body.MaxStackSize};
            foreach(var local in source.Body.Variables)body.Variables.Add(new VariableDefinition(Type(local.VariableType)));
            var instructions=source.Body.Instructions.ToDictionary(i=>i,_=>Instruction.Create(OpCodes.Nop));
            foreach(var originalInstruction in source.Body.Instructions)
            {
                var copy=instructions[originalInstruction];copy.OpCode=originalInstruction.OpCode;
                copy.Operand=originalInstruction.Operand switch
                {
                    null=>null,Instruction branch=>instructions[branch],Instruction[] branches=>branches.Select(i=>instructions[i]).ToArray(),
                    VariableDefinition local=>body.Variables[local.Index],ParameterDefinition parameter=>target.Parameters[parameter.Index],
                    TypeReference type=>Type(type),MethodReference method=>Method(method),FieldReference field=>Field(field),
                    string or int or long or float or double or byte or sbyte=>originalInstruction.Operand,
                    _=>throw new InvalidOperationException("Unsupported original operand "+originalInstruction.Operand.GetType())
                };
                body.Instructions.Add(copy);
            }
            Instruction? At(Instruction? instruction)=>instruction is null?null:instructions[instruction];
            foreach(var handler in source.Body.ExceptionHandlers)body.ExceptionHandlers.Add(new ExceptionHandler(handler.HandlerType){CatchType=handler.CatchType is null?null:Type(handler.CatchType),TryStart=At(handler.TryStart),TryEnd=At(handler.TryEnd),HandlerStart=At(handler.HandlerStart),HandlerEnd=At(handler.HandlerEnd),FilterStart=At(handler.FilterStart)});
            target.Body=body;
        }
        Check(!fixture.MainModule.AssemblyReferences.Any(r=>r.Name==original.Name.Name),"Bound fixture has no reference that could load the original HC runtime");
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(args[2]))!);fixture.Write(args[2]);
        Check(BootstrapStructure([args[0],args[2],args[3]+".after.json"])==0,"Complete original instructions, calls, fields, locals and EH remain exact after binding");
        File.WriteAllText(args[3],JsonSerializer.Serialize(new{status="PASS",originalSha256=Hash(args[0]),compiledFixtureSha256=Hash(args[1]),boundFixtureSha256=Hash(args[2]),compilationDifferences,originalRuntimeAssemblyReferencePresent=false,methods=pairs.Select(p=>p.source.FullName).ToArray(),instructionOrAlgorithmChanges=false,declaredTerminalTypesRebound=true,hardwareIo=false,deviceConstructorBodiesExecuted=false},new JsonSerializerOptions{WriteIndented=true}));
        return 0;
    }

    static int MutateBootstrapAlgorithm(string[] args,string mode)
    {
        using var image=AssemblyDefinition.ReadAssembly(args[0]);
        var type=image.MainModule.GetType(mode=="cache"?"HandheldCompanion.MotherboardInfo":"HandheldCompanion.Devices.IDevice");
        var method=type.Methods.Single(m=>m.Name==(mode=="cache"?"Collect":mode=="model"?"GetCurrent":"PullSensors"));
        if(mode=="model")
        {
            var model=method.Body.Instructions.Single(i=>i.OpCode==OpCodes.Ldstr&&Equals(i.Operand,"RC71L"));model.Operand="RC71L_PRIVATE_BOOTSTRAP_FAULT";
        }
        else if(mode=="cache")
        {
            method.Body.Instructions.Clear();method.Body.Variables.Clear();method.Body.ExceptionHandlers.Clear();var il=method.Body.GetILProcessor();il.Append(il.Create(OpCodes.Ldc_I4_0));il.Append(il.Create(OpCodes.Ret));
        }
        else if(mode=="sensors")
        {
            var bit=method.Body.Instructions.First(i=>i.OpCode==OpCodes.Or);Check(bit.Previous.OpCode==OpCodes.Ldc_I4_1,"Exact original internal sensor addition bit");bit.OpCode=OpCodes.And;
        }
        else throw new InvalidOperationException("Unknown bootstrap fault");
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(args[1]))!);image.Write(args[1]);
        var changed=FactoryChangedMethods(args[0],args[1]);Check(changed.Length==1&&changed[0]==method.FullName,"Private bootstrap fault changes exactly its selected complete original method");
        File.WriteAllText(args[2],JsonSerializer.Serialize(new{status="PASS",sourceSha256=Hash(args[0]),faultSha256=Hash(args[1]),changedMethods=changed,mode,hardwareIo=false},new JsonSerializerOptions{WriteIndented=true}));return 0;
    }

}

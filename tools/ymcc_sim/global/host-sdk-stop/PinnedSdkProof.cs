// PE/IL-only proof for the exact admitted SDK. Never loads its assembly.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Reflection.Emit;
using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Reflection.PortableExecutable;
using System.Security.Cryptography;
using System.IO;
internal static class PinnedSdkProof
{
    private sealed record Instruction(int Offset,OpCode Op,int? Token);
    private static void Need(bool value,string detail){if(!value)throw new InvalidDataException("pinned SDK proof:"+detail);}
    private static string TypeName(MetadataReader r,EntityHandle handle)=>handle.Kind switch
    {
        HandleKind.TypeDefinition=>Full(r,r.GetTypeDefinition((TypeDefinitionHandle)handle).Namespace,r.GetTypeDefinition((TypeDefinitionHandle)handle).Name),
        HandleKind.TypeReference=>Full(r,r.GetTypeReference((TypeReferenceHandle)handle).Namespace,r.GetTypeReference((TypeReferenceHandle)handle).Name),_=>handle.Kind.ToString()
    };
    private static string Full(MetadataReader r,StringHandle ns,StringHandle name)=>r.GetString(ns)+"."+r.GetString(name);
    private static string MethodName(MetadataReader r,int? token)
    {
        if(token is null)return "";var h=MetadataTokens.EntityHandle(token.Value);
        if(h.Kind==HandleKind.MethodDefinition){var m=r.GetMethodDefinition((MethodDefinitionHandle)h);return TypeName(r,m.GetDeclaringType())+"::"+r.GetString(m.Name);}
        if(h.Kind==HandleKind.MemberReference){var m=r.GetMemberReference((MemberReferenceHandle)h);return TypeName(r,m.Parent)+"::"+r.GetString(m.Name);}
        return "";
    }
    private static List<Instruction> Decode(byte[] bytes)
    {
        var ops=typeof(OpCodes).GetFields(BindingFlags.Public|BindingFlags.Static).Where(f=>f.FieldType==typeof(OpCode)).Select(f=>(OpCode)f.GetValue(null)!).GroupBy(o=>unchecked((ushort)o.Value)).ToDictionary(g=>g.Key,g=>g.First());
        var rows=new List<Instruction>();
        for(var p=0;p<bytes.Length;){var offset=p;ushort key=bytes[p++];if(key==0xfe)key=(ushort)(0xfe00|bytes[p++]);var op=ops[key];int? token=null;
            var size=op.OperandType switch{OperandType.InlineNone=>0,OperandType.ShortInlineBrTarget or OperandType.ShortInlineI or OperandType.ShortInlineVar=>1,OperandType.InlineVar=>2,OperandType.InlineI8 or OperandType.InlineR=>8,OperandType.InlineSwitch=>4+4*BitConverter.ToInt32(bytes,p),_=>4};
            if(op.OperandType is OperandType.InlineField or OperandType.InlineMethod or OperandType.InlineTok)token=BitConverter.ToInt32(bytes,p);
            rows.Add(new(offset,op,token));p+=size;
        }return rows;
    }
    private static string FieldType(MetadataReader r,FieldDefinition f)
    {
        var b=r.GetBlobReader(f.Signature);b.ReadSignatureHeader();var code=b.ReadSignatureTypeCode();
        return code==SignatureTypeCode.TypeHandle?TypeName(r,b.ReadTypeHandle()):code.ToString();
    }
    internal static object Validate(string sdk)
    {
        var bytes=File.ReadAllBytes(sdk);var hash=Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();
        Need(bytes.Length==63076352&&hash=="8fe7bdb21ec497591f527b7b87c4bc7b41b45c35a06cc211a7a0dce48838ed2c","disk identity");
        using var stream=new MemoryStream(bytes,false);using var pe=new PEReader(stream);var r=pe.GetMetadataReader();var a=r.GetAssemblyDefinition();
        Need(r.GetString(a.Name)=="HIDMaestro.Core"&&a.Version==new Version(2,0,0,0)&&r.GetGuid(r.GetModuleDefinition().Mvid)==new Guid("86aed9a3-c024-4596-b02c-650082ffb852"),"assembly identity");
        var h=r.TypeDefinitions.Single(t=>TypeName(r,t)=="HIDMaestro.HMController");var type=r.GetTypeDefinition(h);Need((type.Attributes&TypeAttributes.Sealed)!=0,"controller sealed");
        FieldDefinitionHandle Field(string n)=>type.GetFields().Single(f=>r.GetString(r.GetFieldDefinition(f).Name)==n);
        var fields=new[]{("_outputThread","System.Threading.Thread",true),("_outputCts","System.Threading.CancellationTokenSource",true),
            ("_outputView","IntPtr",true),("_outputCtsLock","Object",true),("_outputCtsClosed","Boolean",false),
            ("_idleClock","HIDMaestro.Internal.IDeviceClock",false),("_idleThread","System.Threading.Thread",false),("_disposed","Boolean",false)};
        foreach(var (name,signature,readOnly) in fields)
        {
            var f=r.GetFieldDefinition(Field(name));
            Need((f.Attributes&FieldAttributes.FieldAccessMask)==FieldAttributes.Private&&(f.Attributes&FieldAttributes.Static)==0&&((f.Attributes&FieldAttributes.InitOnly)!=0)==readOnly&&FieldType(r,f)==signature,"field contract "+name);
        }
        Need(!type.GetFields().Any(f=>r.GetString(r.GetFieldDefinition(f).Name)=="_idleCts"),"no legacy idle CTS");
        var methods=r.MethodDefinitions.Select(m=>(Handle:m,Def:r.GetMethodDefinition(m))).Where(m=>m.Def.RelativeVirtualAddress!=0)
            .Select(m=>(m.Handle,m.Def,Name:TypeName(r,m.Def.GetDeclaringType())+"::"+r.GetString(m.Def.Name),Code:Decode(pe.GetMethodBody(m.Def.RelativeVirtualAddress).GetILBytes()!))).ToArray();
        var controller=methods.Where(m=>m.Def.GetDeclaringType()==h).ToArray();
        foreach(var name in new[]{"_outputThread","_outputCts","_outputCtsLock","_idleClock","_idleThread"})
        {
            var token=MetadataTokens.GetToken(Field(name));var writes=methods.Where(m=>m.Code.Any(i=>i.Op==OpCodes.Stfld&&i.Token==token)).ToArray();
            Need(writes.Length==1&&writes[0].Name=="HIDMaestro.HMController::.ctor",name+" constructor-only assignment");
        }
        var output=MetadataTokens.GetToken(Field("_outputThread"));var view=MetadataTokens.GetToken(Field("_outputView"));
        var ctor=controller.Single(m=>m.Code.Any(i=>i.Op==OpCodes.Stfld&&i.Token==output));
        var starts=controller.SelectMany(m=>m.Code.Where(i=>MethodName(r,i.Token)=="System.Threading.Thread::Start").Select(i=>(m.Name,Instruction:i))).ToArray();
        Need(starts.Length==1&&starts[0].Name=="HIDMaestro.HMController::.ctor","output no post-constructor Start");
        var setup=pe.GetMethodBody(ctor.Def.RelativeVirtualAddress).ExceptionRegions.Single(e=>e.Kind==ExceptionRegionKind.Catch&&starts[0].Instruction.Offset>=e.TryOffset&&starts[0].Instruction.Offset<e.TryOffset+e.TryLength);
        Need(ctor.Code.Any(i=>i.Offset>=setup.HandlerOffset&&i.Offset<setup.HandlerOffset+setup.HandlerLength&&i.Op==OpCodes.Stfld&&i.Token==view),"failed output setup zeroes view");
        var workers=controller.Single(m=>m.Name.EndsWith("::StopWorkers",StringComparison.Ordinal));
        Need(workers.Code.Any(i=>MethodName(r,i.Token)=="HIDMaestro.Internal.IDeviceClock::Stop"),"StopWorkers stops idle clock");
        Need(workers.Code.Any(i=>i.Token==MetadataTokens.GetToken(Field("_outputCtsLock")))&&workers.Code.Any(i=>i.Token==MetadataTokens.GetToken(Field("_outputCtsClosed")))&&workers.Code.Any(i=>MethodName(r,i.Token)=="System.Threading.Monitor::Enter"),"output CTS lock and closed guard");
        var joins=workers.Code.Select((i,n)=>(i,n)).Where(x=>MethodName(r,x.i.Token)=="System.Threading.Thread::Join").ToArray();
        Need(joins.Length==1&&workers.Code[joins[0].n+1].Op==OpCodes.Pop,"SDK still ignores output Join result");
        var budgetCalls=workers.Code.Select((i,n)=>(i,n)).Where(x=>MethodName(r,x.i.Token)=="HIDMaestro.Internal.TimeoutScale::Apply").ToArray();
        Need(budgetCalls.Length==2,"two scaled stop budgets");
        var workerIl=pe.GetMethodBody(workers.Def.RelativeVirtualAddress).GetILBytes()!;
        foreach(var call in budgetCalls)
        {
            var previous=workers.Code[call.n-1];
            Need(previous.Op==OpCodes.Ldc_I4&&BitConverter.ToInt32(workerIl,previous.Offset+1)==500,"500ms budget unchanged");
        }
        var dispose=controller.Single(m=>m.Name.EndsWith("::Dispose",StringComparison.Ordinal));
        var stopOrder=dispose.Code.FindIndex(i=>MethodName(r,i.Token)=="HIDMaestro.HMController::StopThreads");
        var unmapOrder=dispose.Code.FindIndex(i=>MethodName(r,i.Token)=="HIDMaestro.HMContext::OnControllerDisposing");
        Need(stopOrder>=0&&unmapOrder>stopOrder,"stop before context teardown");
        var readerLoop=controller.Single(m=>m.Name.EndsWith("::OutputPollLoop",StringComparison.Ordinal));
        Need(pe.GetMethodBody(readerLoop.Def.RelativeVirtualAddress).ExceptionRegions.Any(e=>e.Kind==ExceptionRegionKind.Finally)&&readerLoop.Code.Any(i=>MethodName(r,i.Token)=="HIDMaestro.HMController::CloseOutputCts"),"reader finally owns CTS close");
        var close=controller.Single(m=>m.Name.EndsWith("::CloseOutputCts",StringComparison.Ordinal));
        Need(close.Code.Any(i=>MethodName(r,i.Token)=="System.Threading.CancellationTokenSource::Dispose"),"close disposes CTS");
        Need(controller.Where(m=>m.Code.Any(i=>MethodName(r,i.Token)=="System.Threading.CancellationTokenSource::Dispose")).All(m=>m.Name.EndsWith("::CloseOutputCts",StringComparison.Ordinal)),"no eager output CTS dispose");
        var clockHandle=r.TypeDefinitions.Single(t=>TypeName(r,t)=="HIDMaestro.Internal.DeviceClock`1");
        var clockMethods=methods.Where(m=>m.Def.GetDeclaringType()==clockHandle).ToArray();
        var loop=clockMethods.Single(m=>m.Name.EndsWith("::Loop",StringComparison.Ordinal));
        Need(pe.GetMethodBody(loop.Def.RelativeVirtualAddress).ExceptionRegions.Any(e=>e.Kind==ExceptionRegionKind.Finally)&&loop.Code.Any(i=>MethodName(r,i.Token).EndsWith("::Close",StringComparison.Ordinal)),"clock finally closes its event/timer");
        var stop=clockMethods.Single(m=>m.Name.EndsWith("::Stop",StringComparison.Ordinal));
        Need(stop.Code.Any(i=>MethodName(r,i.Token)=="System.Threading.Thread::Join")&&stop.Code.Any(i=>MethodName(r,i.Token).EndsWith("::Close",StringComparison.Ordinal)),"clock stop joins or closes unstarted resources");
        return new{kind="2.0-pinned-SDK-PE-IL-proof",sdk,sha256=hash,mvid=r.GetGuid(r.GetModuleDefinition().Mvid),assemblyVersion=a.Version.ToString(),
            constructorOnlyOutputAndClockAssignments=true,noLegacyIdleCts=true,outputCloseOwnedByReaderFinally=true,clockCloseOwnedByLoopFinally=true,
            sdkOutputJoinResultIgnored=true,stopBeforeContextTeardown=true,clockAndOutputBudgetMilliseconds=500,
            methodIlHashes=controller.Where(m=>new[]{"Dispose","StopThreads","StopWorkers","CloseOutputCts","OutputPollLoop"}.Contains(r.GetString(m.Def.Name))).Select(m=>new{name=m.Name,sha256=Convert.ToHexString(SHA256.HashData(pe.GetMethodBody(m.Def.RelativeVirtualAddress).GetILBytes()!))}).ToArray(),
            targetAssemblyLoaded=false,realSdkControllerConstructed=false};
    }
}
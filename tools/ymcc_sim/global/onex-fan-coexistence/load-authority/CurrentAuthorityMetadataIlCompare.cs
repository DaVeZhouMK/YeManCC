using System;
using System.IO;
using System.Linq;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.Reflection;
using System.Reflection.Emit;
using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Reflection.PortableExecutable;
using System.Security.Cryptography;
using System.Text.Json;
sealed class Types:ISignatureTypeProvider<string,object?>
{
 public string GetArrayType(string t,ArrayShape s)=>t+"["+s.Rank+":"+string.Join(",",s.Sizes)+":"+string.Join(",",s.LowerBounds)+"]";
 public string GetByReferenceType(string t)=>t+"&";public string GetPointerType(string t)=>t+"*";public string GetPinnedType(string t)=>"pinned "+t;public string GetSZArrayType(string t)=>t+"[]";
 public string GetGenericInstantiation(string t,ImmutableArray<string> a)=>t+"<"+string.Join(",",a)+">";public string GetGenericMethodParameter(object? c,int i)=>"!!"+i;public string GetGenericTypeParameter(object? c,int i)=>"!"+i;
 public string GetModifiedType(string m,string t,bool required)=>(required?"modreq(":"modopt(")+m+")"+t;public string GetPrimitiveType(PrimitiveTypeCode c)=>c.ToString();public string GetFunctionPointerType(MethodSignature<string> s)=>"fn"+Sig(s);
 public string GetTypeFromDefinition(MetadataReader r,TypeDefinitionHandle h,byte k){var t=r.GetTypeDefinition(h);var parent=t.GetDeclaringType();return parent.IsNil?(r.GetString(t.Namespace)+"."+r.GetString(t.Name)).TrimStart('.'):GetTypeFromDefinition(r,parent,0)+"/"+r.GetString(t.Name);}
 public string GetTypeFromReference(MetadataReader r,TypeReferenceHandle h,byte k){var t=r.GetTypeReference(h);string scope=t.ResolutionScope.Kind==HandleKind.AssemblyReference?r.GetString(r.GetAssemblyReference((AssemblyReferenceHandle)t.ResolutionScope).Name):t.ResolutionScope.Kind.ToString();return "["+scope+"]"+(r.GetString(t.Namespace)+"."+r.GetString(t.Name)).TrimStart('.');}
 public string GetTypeFromSpecification(MetadataReader r,object? c,TypeSpecificationHandle h,byte k)=>r.GetTypeSpecification(h).DecodeSignature(this,c);
 public static string Sig(MethodSignature<string> s)=>s.Header.RawValue+":"+s.GenericParameterCount+":"+s.ReturnType+"("+string.Join(",",s.ParameterTypes)+")";
}
static class CurrentAuthorityMetadataIlCompare
{
 static readonly Types Types=new();static readonly Dictionary<short,OpCode> Ops=typeof(OpCodes).GetFields(BindingFlags.Public|BindingFlags.Static).Where(f=>f.FieldType==typeof(OpCode)).Select(f=>(OpCode)f.GetValue(null)!).ToDictionary(o=>o.Value);
 static string Hash(string f)=>Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(f))).ToLowerInvariant();
 static string Entity(MetadataReader r,EntityHandle h)=>h.Kind switch
 {
  HandleKind.TypeDefinition=>Types.GetTypeFromDefinition(r,(TypeDefinitionHandle)h,0),HandleKind.TypeReference=>Types.GetTypeFromReference(r,(TypeReferenceHandle)h,0),HandleKind.TypeSpecification=>Types.GetTypeFromSpecification(r,null,(TypeSpecificationHandle)h,0),
  HandleKind.MethodDefinition=>Method(r,(MethodDefinitionHandle)h),HandleKind.FieldDefinition=>Field(r,(FieldDefinitionHandle)h),HandleKind.MemberReference=>Member(r,(MemberReferenceHandle)h),HandleKind.MethodSpecification=>Spec(r,(MethodSpecificationHandle)h),
  HandleKind.StandaloneSignature=>"locals:"+string.Join(",",r.GetStandaloneSignature((StandaloneSignatureHandle)h).DecodeLocalSignature(Types,null)),_=>throw new InvalidOperationException("UNSUPPORTED_TOKEN:"+h.Kind)
 };
 static string Method(MetadataReader r,MethodDefinitionHandle h){var m=r.GetMethodDefinition(h);return Entity(r,m.GetDeclaringType())+"::"+r.GetString(m.Name)+Types.Sig(m.DecodeSignature(Types,null));}
 static string Field(MetadataReader r,FieldDefinitionHandle h){var f=r.GetFieldDefinition(h);return Entity(r,f.GetDeclaringType())+"::"+r.GetString(f.Name)+":"+f.DecodeSignature(Types,null);}
 static string Member(MetadataReader r,MemberReferenceHandle h){var m=r.GetMemberReference(h);return Entity(r,m.Parent)+"::"+r.GetString(m.Name)+(m.GetKind()==MemberReferenceKind.Field?":"+m.DecodeFieldSignature(Types,null):Types.Sig(m.DecodeMethodSignature(Types,null)));}
 static string Spec(MetadataReader r,MethodSpecificationHandle h){var s=r.GetMethodSpecification(h);return Entity(r,s.Method)+"<"+string.Join(",",s.DecodeSignature(Types,null))+">";}
 static object Inspect(string file,string name,string owner="ScopedLoadAuthority")
 {
  using var stream=File.OpenRead(file);using var pe=new PEReader(stream);var r=pe.GetMetadataReader();var h=r.MethodDefinitions.Single(h=>{var m=r.GetMethodDefinition(h);return r.GetString(m.Name)==name&&Entity(r,m.GetDeclaringType())==owner;});var def=r.GetMethodDefinition(h);var body=pe.GetMethodBody(def.RelativeVirtualAddress);var bytes=body.GetILBytes()!;var instructions=new List<object>();
  for(int p=0;p<bytes.Length;){int offset=p;short op=bytes[p++];if(op==0xfe)op=(short)(0xfe00|bytes[p++]);var opcode=Ops[op];object? operand=null;int i4(){var v=BitConverter.ToInt32(bytes,p);p+=4;return v;}
   switch(opcode.OperandType){case OperandType.InlineNone:break;case OperandType.ShortInlineI:operand=(sbyte)bytes[p++];break;case OperandType.ShortInlineVar:operand=bytes[p++];break;case OperandType.InlineVar:operand=BitConverter.ToUInt16(bytes,p);p+=2;break;case OperandType.InlineI:operand=i4();break;case OperandType.InlineI8:operand=BitConverter.ToInt64(bytes,p);p+=8;break;case OperandType.ShortInlineR:operand=BitConverter.ToSingle(bytes,p);p+=4;break;case OperandType.InlineR:operand=BitConverter.ToDouble(bytes,p);p+=8;break;case OperandType.ShortInlineBrTarget:{var delta=(sbyte)bytes[p++];operand=p+delta;break;}case OperandType.InlineBrTarget:{var delta=i4();operand=p+delta;break;}case OperandType.InlineSwitch:{var count=i4();var deltas=Enumerable.Range(0,count).Select(_=>i4()).ToArray();var end=p;operand=deltas.Select(v=>end+v).ToArray();break;}case OperandType.InlineString:operand=r.GetUserString(MetadataTokens.UserStringHandle(i4()&0xffffff));break;case OperandType.InlineField:case OperandType.InlineMethod:case OperandType.InlineType:case OperandType.InlineTok:case OperandType.InlineSig:operand=Entity(r,MetadataTokens.EntityHandle(i4()));break;default:throw new InvalidOperationException("UNSUPPORTED_OPCODE_OPERAND:"+opcode.OperandType);}
   instructions.Add(new{offset,opcode=opcode.Name,operand});
  }
  var eh=body.ExceptionRegions.Select(e=>new{kind=e.Kind.ToString(),e.TryOffset,e.TryLength,e.HandlerOffset,e.HandlerLength,e.FilterOffset,catchType=e.CatchType.IsNil?null:Entity(r,e.CatchType)}).ToArray();
  return new{signature=Method(r,h),def.Attributes,def.ImplAttributes,body.MaxStack,body.LocalVariablesInitialized,locals=body.LocalSignature.IsNil?null:Entity(r,body.LocalSignature),instructions,exceptionRegions=eh};
 }
 static int Main(string[] args)
 {
  if(args.Length!=3)return 252;var results=new List<object>();bool equal=true;
  const string oldClosure="ScopedLoadAuthority/<>c__DisplayClass26_0",newClosure="ScopedLoadAuthority/<>c__DisplayClass45_0";
  string Normalize(string value)=>value.Replace(JsonSerializer.Serialize(newClosure).Trim('"'),JsonSerializer.Serialize(oldClosure).Trim('"'),StringComparison.Ordinal);
  object ClosureShape(string file,string owner){using var stream=File.OpenRead(file);using var pe=new PEReader(stream);var r=pe.GetMetadataReader();var handle=r.TypeDefinitions.Single(h=>Types.GetTypeFromDefinition(r,h,0)==owner);var type=r.GetTypeDefinition(handle);return new{type.Attributes,baseType=Entity(r,type.BaseType),fields=type.GetFields().Select(h=>new{signature=Field(r,h),r.GetFieldDefinition(h).Attributes}).ToArray(),methods=type.GetMethods().Select(h=>Method(r,h)).ToArray()};}
  var oldShape=ClosureShape(args[0],oldClosure);var newShape=ClosureShape(args[1],newClosure);bool shapeEqual=JsonSerializer.Serialize(oldShape)==Normalize(JsonSerializer.Serialize(newShape));equal&=shapeEqual;results.Add(new{method="ExactReviewedPreloadClosureShape",equal=shapeEqual,before=oldShape,after=newShape});
  foreach(var name in new[]{".ctor","<ReviewPreloadBinding>b__1","<ReviewPreloadBinding>b__0","ReviewPreloadBinding","Verify","Plain"}){bool closure=name==".ctor"||name.StartsWith("<ReviewPreloadBinding>",StringComparison.Ordinal);var a=Inspect(args[0],name,closure?oldClosure:"ScopedLoadAuthority");var b=Inspect(args[1],name,closure?newClosure:"ScopedLoadAuthority");var x=JsonSerializer.Serialize(a);var y=JsonSerializer.Serialize(b);var same=x==Normalize(y);equal&=same;results.Add(new{method=name,equal=same,rawTokenNamesEqual=x==y,before=a,after=b});}
  File.WriteAllText(args[2],JsonSerializer.Serialize(new{schemaVersion=1,status=equal?"EQUAL_THREE_ORIGINAL_METADATA_GUARDS_AND_EXACT_CLOSURE":"REJECTED_ORIGINAL_METADATA_GUARD_IL_DIFFERENCE",exactClosureMapping=new{before=oldClosure,after=newClosure,allFieldsSignaturesAndThreeBodiesCompared=true},before=new{path=args[0],sha256=Hash(args[0])},after=new{path=args[1],sha256=Hash(args[1])},results,targetAssemblyLoaded=false,authorityExecuted=false,semantics="Exact opcodes, symbolic typed tokens, offsets, locals, stack/flags/EH; only named DisplayClass26_0 to DisplayClass45_0 mapping after all fields/signatures and all three closure methods compare. No other normalization and no image Assembly.Load."},new JsonSerializerOptions{WriteIndented=true}));return equal?0:3;
 }
}




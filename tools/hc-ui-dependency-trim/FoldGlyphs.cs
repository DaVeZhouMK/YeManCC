using Mono.Cecil;
using Mono.Cecil.Cil;
using HcDeviceCoreTrim;

static class FoldGlyphs
{
    const string Modern="iNKORE.UI.WPF.Modern";
    const string Controls="iNKORE.UI.WPF.Modern.Controls";
    const string IconType="iNKORE.UI.WPF.Modern.Common.IconKeys.FluentSystemIcons";
    public static object Apply(IReadOnlyDictionary<string,AssemblyDefinition> assemblies,string root)
    {
        if(Util.HashFile(Path.Combine(root,Modern+".dll"))!="5570bbb9d89f92816cd9416a2ba8cf8119fc9b6d4e6bb9648fd17f61bda87fc1"||Util.HashFile(Path.Combine(root,Controls+".dll"))!="953b57e143a2ba9c376105bef174db1d2e82003c9a6537990f4edfa1f44e65c6")throw new InvalidDataException("Glyph folding is only reviewed for the exact pinned inputs");
        var icons=Util.Types(assemblies[Modern].MainModule).Single(t=>t.FullName==IconType);
        var initializer=icons.Methods.Single(m=>m.IsConstructor&&m.IsStatic);var init=initializer.Body.Instructions;
        var target=Util.Types(assemblies[Controls].MainModule).Single(t=>t.FullName==Controls+".SettingsCard").Methods.Single(m=>m.IsConstructor&&m.IsStatic);
        var il=target.Body.Instructions;var matches=il.Where(i=>i.OpCode==OpCodes.Ldsfld&&i.Operand is FieldReference f&&f.DeclaringType.FullName==IconType).ToArray();
        if(matches.Length!=1)throw new InvalidDataException("Unexpected icon table use in reviewed constructor");
        var first=matches[0];var field=(FieldReference)first.Operand;
        if(field.Name!="ChevronRight_16_Regular")throw new InvalidDataException("Unexpected glyph field");
        int p=il.IndexOf(first);var old=il.Skip(p).Take(4).ToArray();
        if(old.Length!=4||old[1].OpCode!=OpCodes.Stloc_0||old[2].OpCode!=OpCodes.Ldloca_S||old[2].Operand is not VariableDefinition v||v.Index!=0||old[3].OpCode!=OpCodes.Call||old[3].Operand is not MethodReference read||read.FullName!="System.String iNKORE.UI.WPF.Modern.Common.IconKeys.FontIconData::get_Glyph()")throw new InvalidDataException("Glyph stack pattern changed");
        var boundaries=il.SelectMany(i=>i.Operand is Instruction branch?new[]{branch}:i.Operand is Instruction[] branches?branches:Array.Empty<Instruction>()).Concat(target.Body.ExceptionHandlers.SelectMany(h=>new[]{h.TryStart,h.TryEnd,h.HandlerStart,h.HandlerEnd,h.FilterStart}).Where(i=>i!=null)!);
        if(boundaries.Any(i=>old.Skip(1).Contains(i!)))throw new InvalidDataException("Branch or exception boundary enters fold");
        var stores=init.Where(i=>i.OpCode==OpCodes.Stsfld&&i.Operand is FieldReference f&&f.FullName==field.FullName).ToArray();if(stores.Length!=1)throw new InvalidDataException("Glyph initializer must be unique");
        int k=init.IndexOf(stores[0]);var block=init.Skip(k-3).Take(4).ToArray();
        if(block[0].OpCode!=OpCodes.Ldc_I4||block[0].Operand is not int codePoint||block[1].OpCode!=OpCodes.Ldc_I4_0||block[2].OpCode!=OpCodes.Call||block[2].Operand is not MethodReference create||create.FullName!="iNKORE.UI.WPF.Modern.Common.IconKeys.FontIconData iNKORE.UI.WPF.Modern.Common.IconKeys.FluentSystemIcons::CreateIcon(System.Int32,iNKORE.UI.WPF.Modern.Common.IconKeys.FluentSystemIconVariants)")throw new InvalidDataException("Glyph data initializer changed");
        string glyph=char.ConvertFromUtf32(codePoint);
        var oldEvidence=old.Select(i=>new{i.Offset,Op=i.OpCode.Name,Operand=i.Operand?.ToString()}).ToArray();var initEvidence=block.Select(i=>new{i.Offset,Op=i.OpCode.Name,Operand=i.Operand?.ToString()}).ToArray();
        first.OpCode=OpCodes.Ldstr;first.Operand=glyph;foreach(var i in old.Skip(1)){i.OpCode=OpCodes.Nop;i.Operand=null;}
        return new{Status="folded",ModifiedMethod=target.FullName,OriginalField=field.FullName,CodePoint=codePoint,Glyph=glyph,GlyphUtf16=string.Join(" ",glyph.Select(c=>((int)c).ToString("X4"))),OriginalInstructions=oldEvidence,OriginalInitializer=initEvidence,Replacement="ldstr exact original glyph; nop; nop; nop",SemanticScope="Exact glyph string for SettingsCard default ActionIcon; original giant font table construction and font resource lookup intentionally eliminated. No visual rendering or hardware success is fabricated."};
    }
}

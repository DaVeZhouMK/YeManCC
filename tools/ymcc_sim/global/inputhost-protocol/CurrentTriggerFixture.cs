using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text.Json;
using System.Text.Json.Nodes;
internal static class CurrentTriggerProtocolEntry
{
    private static int _checks;
    private static int _groups;
    private static readonly List<object> _applicableOriginals=new();
    private static void Check(bool value,string name) { _checks++;if(!value)throw new Exception(name); }
    private static JsonObject Frame(string persona,string profile,float left=.25f,float right=.75f)
    {
        var method=typeof(OptionalTriggerFixture).GetMethod("Frame",BindingFlags.Static|BindingFlags.NonPublic)!;
        var frame=(JsonObject)method.Invoke(null,new object[]{persona,left,right})!;
        frame["profileIdentity"]=profile;
        return frame;
    }
    private static int OriginalChecks=>(int)typeof(OptionalTriggerFixture).GetField("_checks",BindingFlags.Static|BindingFlags.NonPublic)!.GetValue(null)!;
    private static void OriginalApplicable(string name)
    {
        var method=typeof(OptionalTriggerFixture).GetMethod(name,BindingFlags.Static|BindingFlags.NonPublic)??throw new MissingMethodException(name);
        var before=OriginalChecks;method.Invoke(null,null);
        _applicableOriginals.Add(new {name,checks=OriginalChecks-before,originalMethodInvoked=true});_groups++;
    }
    private static void SupportedMatrix()
    {
        foreach(var pair in CurrentWireContract.Supported)
        foreach(var left in new[]{0f,.1f,.25f,1f})foreach(var right in new[]{0f,.5f,1f})foreach(var mask in new[]{0,1,2,3})
        {
            var frame=Frame(pair.Persona,pair.Profile,left,right);
            if((mask&1)!=0)frame["ltRaw"]=.625f;
            if((mask&2)!=0)frame["rtRaw"]=.875f;
            var result=CandidateParser.Parse(frame.ToJsonString());
            Check(result.Accepted,"current supported persona/profile accepted: "+pair.Persona);
            Check(result.LtRaw==((mask&1)!=0?.625f:left)&&result.RtRaw==((mask&2)!=0?.875f:right),"actual current parser independent missing-trigger fallback");
            Check(result.Lt==left&&result.Rt==right,"canonical triggers unchanged");
        }
        _groups++;
    }
    private static void RejectTuple(string persona,string profile,string reason)
    {
        var result=CandidateParser.Parse(Frame(persona,profile).ToJsonString());
        Check(!result.Accepted&&result.Error=="persona-not-admitted",reason);
    }
    private static void WireRefusals()
    {
        foreach(var removed in new[]{"xbox360","dualshock4","dualsense"})
        {
            RejectTuple(removed,removed,"removed persona refuses legacy wire profile");
            foreach(var pair in CurrentWireContract.Supported)RejectTuple(removed,pair.Profile,"saved configuration migration cannot admit a removed wire persona");
        }
        foreach(var a in CurrentWireContract.Supported)foreach(var b in CurrentWireContract.Supported)
            if(a.Persona!=b.Persona)RejectTuple(a.Persona,b.Profile,"cross-persona profile spoof refused");
        RejectTuple("elite","elite","old elite profile refused");RejectTuple("disabled","disabled","disabled config is not an output wire persona");
        _groups++;
    }
    private static void FaultControl()
    {
        var pair=CurrentWireContract.Supported[0];var frame=Frame(pair.Persona,pair.Profile);
        var current=CandidateParser.Parse(frame.ToJsonString());var fault=BaselineParser.Parse(frame.ToJsonString());
        Check(current.Accepted&&fault.Accepted,"counterfactual source changes fallback only, not frame admission");
        Check(current.LtRaw==.25f&&current.RtRaw==.75f&&fault.LtRaw==0&&fault.RtRaw==0,"current parser differs from the actual-source missing-fallback fault");
        Check(current.LeftWire==64&&current.RightWire==191&&fault.LeftWire==0&&fault.RightWire==0,"actual TriggerToByte exposes the regression at the output bytes");
        _groups++;
    }
    private static void FrameBoundaries()
    {
        var pair=CurrentWireContract.Supported[0];
        var ascii=Frame(pair.Persona,pair.Profile).ToJsonString();
        var atLimit=ascii+new string(' ',16384-System.Text.Encoding.UTF8.GetByteCount(ascii));
        Check(CandidateParser.Parse(atLimit).Accepted,"exact ASCII 16KiB frame boundary accepted");
        var over=CandidateParser.Parse(atLimit+" ");Check(!over.Accepted&&over.Error=="invalid-json","ASCII boundary plus one byte refused");
        var unicodeFrame=Frame(pair.Persona,pair.Profile);unicodeFrame["requestId"]=new string('界',120);
        var unicode=unicodeFrame.ToJsonString(new JsonSerializerOptions {Encoder=System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping});
        var unicodeAtLimit=unicode+new string(' ',16384-System.Text.Encoding.UTF8.GetByteCount(unicode));
        Check(unicodeAtLimit.Length<16384&&System.Text.Encoding.UTF8.GetByteCount(unicodeAtLimit)==16384,"multibyte boundary uses UTF8 bytes rather than UTF16 length");
        Check(CandidateParser.Parse(unicodeAtLimit).Accepted,"exact multibyte UTF8 boundary accepted");
        var unicodeOver=CandidateParser.Parse(unicodeAtLimit+" ");Check(!unicodeOver.Accepted&&unicodeOver.Error=="invalid-json","multibyte byte overflow refused");
        var duplicate=ascii[..^1]+",\"buttons\":2}";var duplicated=CandidateParser.Parse(duplicate);
        Check(!duplicated.Accepted&&duplicated.Error=="duplicate-field:buttons","duplicate frame field retains actual refusal reason");
        var wrongVersion=Frame(pair.Persona,pair.Profile);wrongVersion["protocolVersion"]=2;Check(!CandidateParser.Parse(wrongVersion.ToJsonString()).Accepted,"wrong protocol version refused");
        _groups++;
    }
    private static int Execute(string reportPath)
    {
        OriginalApplicable("MissingProof");OriginalApplicable("PresentParity");OriginalApplicable("RejectionsAndExtensions");
        SupportedMatrix();WireRefusals();FaultControl();FrameBoundaries();
        var result=new {status="PASS",pass=true,groups=_groups,checks=_checks+OriginalChecks,originalApplicableChecks=OriginalChecks,currentContractChecks=_checks,originalApplicableMethods=_applicableOriginals,originalLegacyWholeFixtureReSigned=false,legacyWholeFixtureExpectedFailure="Original 6-persona acceptance fixture is preserved separately; current sender/selector intentionally admit only 3 pairs",supported=CurrentWireContract.Supported.Select(p=>new{persona=p.Persona,profile=p.Profile}),productionParserAndHelpers=true,senderStaticCrosscheck=true,senderBehaviorExecuted=false,productExecuted=false,hardwareIo=false,performanceMeasurementExecuted=false};
        File.WriteAllText(reportPath,JsonSerializer.Serialize(result,new JsonSerializerOptions{WriteIndented=true}));Console.WriteLine(JsonSerializer.Serialize(new{result.status,result.groups,result.checks}));return 0;
    }
    private static int Main(string[] args)
    {
        if(args.Length==2&&args[0]=="--legacy-fixture")return InputHostProtocolEntry.Run(args[1],()=>InputHostProtocolEntry.OriginalFixture(args[1]));
        if(args.Length!=1)return 2;
        return InputHostProtocolEntry.Run(args[0],()=>Execute(args[0]));
    }
}

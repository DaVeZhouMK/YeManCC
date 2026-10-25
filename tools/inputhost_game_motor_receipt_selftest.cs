using System.Text.Json;
internal static partial class Program
{
    private static string wireDirectory="";
    private static byte[] MotorPacket(byte left,byte right,bool v2=false)
    {var b=new byte[63];b[0]=v2?(byte)2:(byte)3;b[2]=right;b[3]=left;if(v2)b[38]=4;return b;}
    private static Dictionary<string,object?> PersonaMessage(string command,ulong sequence,string persona="dualsense-edge",string profile="dualsense-edge")
    {var m=Message(command,sequence);m["persona"]=persona;m["profileIdentity"]=profile;return m;}
    private static JsonElement PersonaSend(HostSession s,string command,ulong sequence,string persona="dualsense-edge",string profile="dualsense-edge")=>Send(s,PersonaMessage(command,sequence,persona,profile),out _);
    private static JsonElement MotorFrame(HostSession s,ulong sequence,string persona="dualsense-edge",string profile="dualsense-edge")
    {
        var m=PersonaMessage("SUBMIT_FRAME",sequence,persona,profile);
        m["buttons"]=0;m["lx"]=0;m["ly"]=0;m["rx"]=0;m["ry"]=0;m["lt"]=0;m["rt"]=0;
        m["sampleSequence"]=sequence;m["monotonicTick"]=sequence;m["motionPairProofId"]="offline";
        m["normalizedInputHash"]=CanonicalFrameHash(0,0,0,0,0,0,0);
        return Send(s,m,out _);
    }
    private static (HostSession Session,JsonElement Prepare) MotorReady(string persona="dualsense-edge",string profile="dualsense-edge")
    {
        var s=new HostSession("fake-nonce");
        Fixture.OnObserve=()=>Fixture.Current.PresentInstanceIds?.Count>0 ? persona=="steamdeck"?Fixture.Current:new VirtualEndpointObservation(true,1,0,1,0,0,true,"controlled-persona-arrival") : Empty();
        Fixture.OnSweep=()=>Fixture.Current=Empty();
        Accept(PersonaSend(s,"HELLO",1,persona,profile));var prepared=PersonaSend(s,"PREPARE_TARGET",2,persona,profile);Accept(prepared);
        Accept(PersonaSend(s,"SUBMIT_NEUTRAL",3,persona,profile));return (s,prepared);
    }
    private static bool HasMotor(JsonElement response)=>response.TryGetProperty("gameFeedbackRevision",out _);
    private static void MotorEquals(JsonElement response,int left,int right,ulong revision,uint sdkSequence,int compatibility=1,ulong generation=1)
    {
        Accept(response);Check(HasMotor(response),"GAME_MOTOR_REPLY_MISSING");
        Check(response.GetProperty("gameFeedbackGeneration").GetUInt64()==generation,"MOTOR_GENERATION");
        Check(response.GetProperty("gameFeedbackRevision").GetUInt64()==revision,"MOTOR_REVISION");
        Check(response.GetProperty("gameFeedbackLeft").GetInt32()==left&&response.GetProperty("gameFeedbackRight").GetInt32()==right,"MOTOR_SIDE_MAPPING");
        Check(response.GetProperty("gameFeedbackSdkSequence").GetUInt32()==sdkSequence,"MOTOR_ORIGIN_SEQUENCE");
        Check(response.GetProperty("gameFeedbackCompatibility").GetInt32()==compatibility,"MOTOR_COMPATIBILITY");
    }
    private static int MotorMain(string dir)
    {
        wireDirectory=dir;Directory.CreateDirectory(dir);var cases=new List<Dictionary<string,object>>();
        cases.Add(Group("actual Sony callback decodes distinct motors into accepted frame response",()=>{
            var (s,p)=MotorReady();s.ObservedController!.Emit(0,2,MotorPacket(128,64),41);var frame=MotorFrame(s,4);MotorEquals(frame,128,64,1,41);
            File.WriteAllText(Path.Combine(wireDirectory,"motor-frame.json"),frame.GetRawText());
            s.ObservedController.Emit(0,2,MotorPacket(0,0),42);var zero=MotorFrame(s,5);MotorEquals(zero,0,0,2,42);
            File.WriteAllText(Path.Combine(wireDirectory,"motor-zero.json"),zero.GetRawText());
        }));
        cases.Add(Group("successful prepare binds exact motor generation and protocol",()=>{
            var (s,p)=MotorReady();Check(p.TryGetProperty("gameFeedbackBindingGeneration",out var g),"MOTOR_BINDING_HEADER_MISSING");
            Check(g.GetUInt64()==1&&p.GetProperty("gameFeedbackProtocol").GetInt32()==1);
            File.WriteAllText(Path.Combine(wireDirectory,"motor-prepare.json"),p.GetRawText());
            Check(!PersonaSend(s,"TUPLE_SUBMIT_STATS",4).TryGetProperty("gameFeedbackBindingGeneration",out _));
        }));
        cases.Add(Group("compatibility2 and one-sided motor preserve independent values",()=>{
            var (s,_)=MotorReady();s.ObservedController!.Emit(0,2,MotorPacket(0,213,true),7);MotorEquals(MotorFrame(s,4),0,213,1,7,2);
        }));
        cases.Add(Group("lighting after zero cannot erase pending motor stop",()=>{
            var (s,_)=MotorReady();var c=s.ObservedController!;c.Emit(0,2,MotorPacket(121,6),1);c.Emit(0,2,MotorPacket(0,0),2);
            var led=new byte[63];led[1]=4;led[2]=255;led[3]=255;led[44]=231;c.Emit(0,2,led,3);
            var r=MotorFrame(s,4);MotorEquals(r,0,0,2,2);Check(r.GetProperty("gameOutputObservation").GetProperty("sdkSequence").GetUInt32()==3);
        }));
        cases.Add(Group("stats can consume raw observation without consuming motor intent",()=>{
            var (s,_)=MotorReady();s.ObservedController!.Emit(0,2,MotorPacket(17,39),11);var stats=PersonaSend(s,"TUPLE_SUBMIT_STATS",4);
            Check(stats.TryGetProperty("gameOutputObservation",out _)&&!HasMotor(stats),"UNRELATED_ACK_CONSUMED_MOTORS");
            var frame=MotorFrame(s,5);MotorEquals(frame,17,39,1,11);Check(!frame.TryGetProperty("gameOutputObservation",out _));
            Check(!HasMotor(MotorFrame(s,6)),"DUPLICATE_MOTOR_DELIVERY");
        }));
        cases.Add(Group("neutral ACK cannot drain pending motors",()=>{
            var (s,_)=MotorReady();s.ObservedController!.Emit(0,2,MotorPacket(3,207),9);
            Check(!HasMotor(PersonaSend(s,"SUBMIT_NEUTRAL",4)),"NEUTRAL_ACK_CONSUMED_MOTORS");MotorEquals(MotorFrame(s,5),3,207,1,9);
        }));
        foreach(var pair in new[]{(Persona:"steamdeck",Profile:"steam-deck-composite",Protocol:2,Compatibility:3),(Persona:"elite",Profile:"xbox-elite-v2",Protocol:3,Compatibility:4)})
        cases.Add(Group(pair.Persona+" binds exact current protocol and only its classic motor scope",()=>{
            var (s,p)=MotorReady(pair.Persona,pair.Profile);
            Check(p.TryGetProperty("gameFeedbackBindingGeneration",out var generation)&&generation.GetUInt64()==1,"MOTOR_BINDING_HEADER_MISSING");
            Check(p.GetProperty("gameFeedbackProtocol").GetInt32()==pair.Protocol,"MOTOR_PROFILE_PROTOCOL");
            File.WriteAllText(Path.Combine(wireDirectory,pair.Persona+"-motor-prepare.json"),p.GetRawText());
            s.ObservedController!.Emit(0,2,MotorPacket(255,255),1);
            Check(!HasMotor(MotorFrame(s,4,pair.Persona,pair.Profile)),"WRONG_PROFILE_MOTOR_DECODE");
            byte source=pair.Persona=="steamdeck"?(byte)1:(byte)2,report=pair.Persona=="steamdeck"?(byte)235:(byte)0;
            byte[] packet;
            if(pair.Persona=="steamdeck"){
                packet=new byte[63];packet[0]=9;packet[1]=0;
                packet[4]=0x34;packet[5]=0x12;packet[6]=0xFE;packet[7]=0xA5;
            }else packet=new byte[]{123,6,0x12,0xA5,2};
            s.ObservedController.Emit(source,report,packet,2);
            var frame=MotorFrame(s,5,pair.Persona,pair.Profile);MotorEquals(frame,0x12,0xA5,1,2,pair.Compatibility);
            File.WriteAllText(Path.Combine(wireDirectory,pair.Persona+"-motor-frame.json"),frame.GetRawText());
            byte[] zero=pair.Persona=="steamdeck"?new byte[63]:new byte[]{123,6,0,0,2};
            s.ObservedController.Emit(source,report,zero,3);
            var stop=MotorFrame(s,6,pair.Persona,pair.Profile);MotorEquals(stop,0,0,2,3,pair.Compatibility);
            File.WriteAllText(Path.Combine(wireDirectory,pair.Persona+"-motor-zero.json"),stop.GetRawText());
            Check(!HasMotor(MotorFrame(s,7,pair.Persona,pair.Profile)),"DUPLICATE_MOTOR_DELIVERY");
        }));
        foreach(var pair in new[]{("unknown","dualsense-edge"),("dualshock4","dualshock-4-v2"),("xbox360","xbox-360-wired")})
        cases.Add(Group(pair.Item1+" unknown or retired persona cannot bind motor feedback",()=>{
            var s=new HostSession("fake-nonce");var message=PersonaMessage("PREPARE_TARGET",1,pair.Item1,pair.Item2);
            Check(!TryParseEnvelope(JsonSerializer.Serialize(message),out _,out var error)&&error=="persona-not-admitted","UNADMITTED_PERSONA_PARSED");
            var rejected=Send(s,message,out _);Reject(rejected,"protocol-error");
            Check(!rejected.TryGetProperty("gameFeedbackBindingGeneration",out _)&&!rejected.TryGetProperty("gameFeedbackProtocol",out _)&&!HasMotor(rejected),"UNADMITTED_PERSONA_MOTOR_BINDING");
            Check(!s.HasController&&!s.HasContext&&s.ObservedController is null&&Fixture.Creates==0,"UNADMITTED_PERSONA_CREATED_TARGET");
            File.WriteAllText(Path.Combine(wireDirectory,pair.Item1+"-prepare-rejected.json"),rejected.GetRawText());
        }));
        foreach(var test in new[]{("feature-write",(byte)1,(byte)2,63,(byte)3,(byte)0),("feature-read",(byte)3,(byte)2,63,(byte)3,(byte)0),("wrong-report",(byte)0,(byte)49,63,(byte)3,(byte)0),("short",(byte)0,(byte)2,62,(byte)3,(byte)0),("long",(byte)0,(byte)2,64,(byte)3,(byte)0),("invalid-flags",(byte)0,(byte)2,63,(byte)0,(byte)0),("audio-haptics",(byte)0,(byte)2,63,(byte)1,(byte)0)})
        cases.Add(Group(test.Item1+" is not a classic motor update",()=>{
            var (s,_)=MotorReady();var packet=new byte[test.Item4];packet[0]=test.Item5;packet[2]=255;packet[3]=255;if(packet.Length>38)packet[38]=test.Item6;
            s.ObservedController!.Emit(test.Item2,test.Item3,packet,1);Check(!HasMotor(MotorFrame(s,4)),"UNPROVEN_PACKET_BECAME_MOTORS");
        }));
        cases.Add(Group("failed same-tuple command cannot drain pending motor stop",()=>{
            var (s,_)=MotorReady();s.ObservedController!.Emit(0,2,MotorPacket(0,0),12);var rejected=PersonaSend(s,"TUPLE_SUBMIT_STATS",99);Check(!rejected.GetProperty("ok").GetBoolean()&&!HasMotor(rejected));
            MotorEquals(MotorFrame(s,4),0,0,1,12);
        }));
        cases.Add(Group("recreate cannot reuse pending or copied old motor callback",()=>{
            var (s,_)=MotorReady();var old=s.ObservedController!;var callback=old.SnapshotCallback()!;old.Emit(0,2,MotorPacket(9,7),1);
            var recreated=PersonaSend(s,"RECREATE_TARGET",4);Accept(recreated);Check(recreated.GetProperty("gameFeedbackBindingGeneration").GetUInt64()==2);
            Accept(PersonaSend(s,"SUBMIT_NEUTRAL",5));callback(old,new(HMOutputSource.Output,2,MotorPacket(255,255),2));
            Check(!HasMotor(MotorFrame(s,6)),"OLD_TARGET_MOTOR_REPLAY");s.ObservedController!.Emit(0,2,MotorPacket(7,9),3);MotorEquals(MotorFrame(s,7),7,9,1,3,generation:2);
        }));
        cases.Add(Group("failed release revokes motor callback before output barrier retry",()=>{
            var (s,_)=MotorReady();var c=s.ObservedController!;var callback=c.SnapshotCallback()!;c.Emit(0,2,MotorPacket(255,255),1);Fixture.OutputStops=false;
            Reject(PersonaSend(s,"RELEASE_TARGET",4),"release-local-dispose-failed");callback(c,new(HMOutputSource.Output,2,MotorPacket(255,255),2));
            var r=MotorFrame(s,5);Check(!r.GetProperty("ok").GetBoolean()&&!HasMotor(r));Fixture.OutputStops=true;Accept(PersonaSend(s,"RELEASE_TARGET",6));
        }));
        var passed=cases.All(c=>(bool)c["pass"]);Console.WriteLine(JsonSerializer.Serialize(new{status=passed?"PASS":"FAIL",cases,hostStarted=false,sdkConstructed=false,physicalAcceptance=false,gameFeedbackForwardingProven=false,wireDirectory}));return passed?0:1;
    }
}

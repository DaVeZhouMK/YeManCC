"""Prepare the optional recorded-frame arm of steamdeck_sdk_memory_selftest.

Extracts exact pure Host members and compiles them with the existing SDK-memory
fixture. No product Main, controller/context constructors, device or socket I/O.
"""
from pathlib import Path
import argparse, hashlib, importlib.util, json, shutil
ROOT=Path(__file__).resolve().parents[2]

def digest(b):return hashlib.sha256(b).hexdigest()
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--frames',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);a=ap.parse_args();out=a.out.resolve();frames=a.frames.resolve();assert out.is_dir() and frames.is_file()
 spec=importlib.util.spec_from_file_location('actual_host_member_extractor',ROOT/'tools/inputhost_quaternion_selftest.py');mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
 inputs=[];bindings=[];blocks=[]
 def freeze(rel,target=None):
  p=ROOT/rel;b=p.read_bytes();dest=out/(target or ('source/'+rel));dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(b);inputs.append({'source':str(p),'frozen':str(dest),'sha256':digest(b)});return b.decode('utf-8-sig')
 program=freeze('InputHost/Program.cs');pads=freeze('InputHost/ScreenTouchpadReport.cs');buttons=freeze('InputHost/ScreenButtonReport.cs')
 def add(text,marker,origin,semi=False):
  body=mod.member(text,marker,semi);bindings.append({'origin':origin,'marker':marker,'sourceExact':True,'bodySha256':digest(body.encode('utf-8'))});blocks.append(body)
 for n in ['BaseFields','FrameFields','HidHideFields']:add(program,'private static readonly HashSet<string> '+n,'InputHost/Program.cs',True)
 methods=['TryParseEnvelope','AttachReportChainMetadata1410Safely','TryReadString','TryReadUInt64','TryReadInt32','TryReadShort','TryReadByte','TryReadAxis','TryReadTrigger','TryReadImuAxis','TryReadImuQuaternion','IsSha256','CanonicalFrameHash','AppendFrameHashBits','TryGetVirtualOutputProfile','BuildSteamDeckStateFields','UnitStickAxis','TriggerToByte','QuantizeMotionAxis']
 # Match the exact declared signature without depending on return type formatting.
 import re
 for name in methods:
  m=re.search(r'^    private static [^\n]*\b'+name+r'\(',program,re.M);assert m,name;add(program,m.group(0),'InputHost/Program.cs')
 if 'private static bool TryParseEnvelopeDocument(' in program:add(program,'private static bool TryParseEnvelopeDocument(','InputHost/Program.cs')
 add(program,'private sealed record CommandEnvelope(','InputHost/Program.cs')
 for marker in ['private readonly record struct ScreenPad(','private static bool TryReadScreenPads(','private static bool TryReadScreenPad(','private static byte[] BuildSteamDeckScreenPadReport(']:add(pads,marker,'InputHost/ScreenTouchpadReport.cs')
 add(pads,'private static readonly (HMButton Button, int Bit)[] DeckButtonBits','InputHost/ScreenTouchpadReport.cs',True)
 add(buttons,'private static bool TryReadScreenButtons(','InputHost/ScreenButtonReport.cs')
 for rel in ['InputHost/SteamDeckStateFields.cs','InputHost/MsiVirtualReportChain1410.cs','InputHost/CanonicalFrameFingerprintMemo.cs']:freeze(rel,Path(rel).name)
 freeze('tools/inputhost_quaternion_selftest.py');freeze('tools/fixtures/rog_rawinput_host_replay.py')
 data=frames.read_bytes();rows=[json.loads(l) for l in data.decode('utf-8-sig').splitlines() if l.strip()];assert rows,'empty actual-frame input'
 assert all(isinstance(r.get('command'),dict) and r['command'].get('command')=='SUBMIT_FRAME' and r.get('source') in ['xinput','rog-hid-raw'] for r in rows),'Expected actual native issued envelopes with outer source witness'
 frozen=out/'native-issued-frames.jsonl';frozen.write_bytes(data);inputs.append({'source':str(frames),'frozen':str(frozen),'sha256':digest(data)})
 pre='''using System.Text;using System.Text.Json;using System.Security.Cryptography;using YeManInputHost.Diagnostics;using System.Buffers.Binary;using HIDMaestro;
internal static partial class Program {
 private const int ProtocolVersion=1,MaxMessageBytes=16*1024;
 [ThreadStatic] private static Dictionary<string,JsonElement>? t_fieldCache;
 // Parser-only schema constant; no lifecycle or release method is replaced.
 private static class MsiSteamDeckReleaseFence1726 {internal const string PrepareField="msiSteamDeckReleaseFence";}
'''
 replay=r'''
 private delegate bool ReadRecordedFrame(out byte[] report);
 private static object RunRecordedHostReplay(HMController controller,HMProfile profile,
     ReadRecordedFrame read,string input,string output) {
  int reports=0,active=0,neutral=0;uint counter=90000;var perSource=new Dictionary<string,int>();
  using var writer=new StreamWriter(output,false,Encoding.UTF8);
  foreach(var line in File.ReadLines(input)) {
   if(string.IsNullOrWhiteSpace(line))continue;
   using var row=JsonDocument.Parse(line);var item=row.RootElement;
   var label=item.GetProperty("case").GetString()!;var source=item.GetProperty("source").GetString()!;
   var command=item.GetProperty("command");var message=command.GetRawText();
   var parsed=TryParseEnvelope(message,out var envelope,out var error);
   Check("recorded-native-Host-parser:"+label,parsed&&envelope is not null);
   if(!parsed||envelope is null)throw new InvalidOperationException("recorded-native-frame-rejected:"+label+":"+error);
   var e=envelope;
   Check("player-SteamDeck-persona-preserved:"+label,e.Persona=="steamdeck"&&e.ProfileIdentity=="steam-deck-composite");
   if(e.Persona!="steamdeck"||e.ProfileIdentity!="steam-deck-composite")throw new InvalidOperationException("unexpected-persona");
   var encoded=BuildSteamDeckScreenPadReport(profile,e,++counter);
   var expectedHash=Convert.ToHexStringLower(SHA256.HashData(encoded));
   controller.SubmitRawReport(encoded);Array.Clear(encoded);
   bool received=read(out var decoded);
   Check("actual-SDK-report-copy-and-read:"+label,received&&decoded.Length==64&&Convert.ToHexStringLower(SHA256.HashData(decoded))==expectedHash);
   if(!received||decoded.Length!=64)throw new InvalidOperationException("actual-SDK-read-missing:"+label);
   ulong wire=BinaryPrimitives.ReadUInt64LittleEndian(decoded.AsSpan(8));
   ulong expected=0;
   foreach(var pair in new (int Mask,int Bit)[]{(0x1000,7),(0x2000,5),(0x4000,6),(0x8000,4),(0x100,3),(0x200,2),(0x20,12),(0x10,14),(0x40,22),(0x80,26),(0x400,13),(1,8),(2,11),(4,10),(8,9)})
    if((e.Buttons&pair.Mask)!=0)expected|=1UL<<pair.Bit;
   // Check ordinary controls only. Touch/auxiliary fields retain their own policies.
   ulong ordinary=(1UL<<0)|(1UL<<1)|(1UL<<2)|(1UL<<3)|(1UL<<4)|(1UL<<5)|(1UL<<6)|(1UL<<7)|(1UL<<8)|(1UL<<9)|(1UL<<10)|(1UL<<11)|(1UL<<12)|(1UL<<13)|(1UL<<14)|(1UL<<22)|(1UL<<26);
   if((byte)Math.Clamp((int)MathF.Round(e.LtRaw*255f),0,255)>0)expected|=1UL<<1;
   if((byte)Math.Clamp((int)MathF.Round(e.RtRaw*255f),0,255)>0)expected|=1UL<<0;
   Check("SteamDeck-ordinary-buttons-and-release:"+label,(wire&ordinary)==expected);
   var actualSticks=new[]{BinaryPrimitives.ReadInt16LittleEndian(decoded.AsSpan(48)),BinaryPrimitives.ReadInt16LittleEndian(decoded.AsSpan(50)),BinaryPrimitives.ReadInt16LittleEndian(decoded.AsSpan(52)),BinaryPrimitives.ReadInt16LittleEndian(decoded.AsSpan(54))};
   var wanted=new[]{e.Lx,e.Ly,e.Rx,e.Ry};bool sticksMatch=true;
   for(int index=0;index<4;index++)sticksMatch&=Math.Abs(actualSticks[index]-Math.Round(Math.Clamp((double)wanted[index],-1,1)*32767))<=1;
   Check("SteamDeck-four-stick-values:"+label,sticksMatch);
   ushort left=BinaryPrimitives.ReadUInt16LittleEndian(decoded.AsSpan(44)),right=BinaryPrimitives.ReadUInt16LittleEndian(decoded.AsSpan(46));
   Check("SteamDeck-raw-trigger-values:"+label,left==(ushort)Math.Round(Math.Clamp((double)e.LtRaw,0,1)*32767)&&right==(ushort)Math.Round(Math.Clamp((double)e.RtRaw,0,1)*32767));
   bool isNeutral=e.Buttons==0&&wanted.All(x=>x==0)&&e.LtRaw==0&&e.RtRaw==0;
   if(isNeutral)neutral++;else active++;
   reports++;perSource[source]=perSource.GetValueOrDefault(source)+1;
   writer.WriteLine(JsonSerializer.Serialize(new{label,source,commandSequence=e.Sequence,sampleSequence=e.SampleSequence,persona=e.Persona,profile=e.ProfileIdentity,decodedSha256=expectedHash,buttons=wire,sticks=actualSticks,leftTrigger=left,rightTrigger=right,isNeutral,realXboxConsumerObserved=false}));
  }
  Check("replay-includes-live-controls-and-release",active>0&&neutral>0);
  Check("replay-includes-ROG-raw-and-XInput-controls",perSource.GetValueOrDefault("rog-hid-raw")>0&&perSource.GetValueOrDefault("xinput")>0);
  return new{reports,active,neutral,perSource,actualHostParser=true,actualSteamDeckEncoder=true,actualLockedSdkSharedDecode=true,actualHostLifecycle=false,actualInputPump=false,actualXboxConsumer=false};
 }
}
'''
 generated=pre+'\n'.join(blocks)+'\n'+replay;(out/'RogHostReplay.cs').write_text(generated,encoding='utf-8')
 manifest={'schema':'ymcc.sdk-memory.optional-native-frame-replay.v1','inputs':inputs,'members':bindings,'inputRows':len(rows),'generatedSha256':digest(generated.encode()),'parserConstantStubOnly':'MsiSteamDeckReleaseFence1726.PrepareField','productSourceWritten':False,'liveInputOrDriverCalls':0,'actualXboxConsumptionProven':False}
 (out/'replay-source-extraction.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
 print(json.dumps({'prepared':True,'rows':len(rows),'members':len(bindings),'out':str(out)}))
if __name__=='__main__':main()

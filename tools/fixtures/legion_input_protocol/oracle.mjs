import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
export const sha = bytes => createHash('sha256').update(bytes).digest('hex');
export function derivePrimary(workspace, fixtures) {
  const pinsPath = path.join(fixtures, 'source-pins.json'), pins = JSON.parse(fs.readFileSync(pinsPath, 'utf8'));
  const recovery = path.join(workspace, 'Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/ProtocolRecovery');
  const treePath = path.join(recovery, 'historical-tree.json'), acquisitionPath = path.join(recovery, 'acquisition-historical.json');
  const tree = JSON.parse(fs.readFileSync(treePath, 'utf8')), acquired = JSON.parse(fs.readFileSync(acquisitionPath, 'utf8'));
  if (tree.sha !== pins.historicalCommit || acquired.reference !== pins.historicalCommit) throw Error('Historical primary commit mismatch');
  const files = pins.files.map(pin => {
    const file = path.join(recovery, 'historical-source', pin.path), bytes = fs.readFileSync(file);
    const blob = createHash('sha1').update(Buffer.from(`blob ${bytes.length}\0`)).update(bytes).digest('hex');
    const entry = tree.tree.find(entry => entry.path === pin.path), acquisition = acquired.files.find(entry => entry.path === pin.path);
    if (!entry || entry.sha !== pin.gitBlob || blob !== pin.gitBlob || sha(bytes) !== pin.sha256 || !acquisition || acquisition.gitBlob !== blob || acquisition.sha256 !== pin.sha256)
      throw Error(`Primary source SHA256/blob/acquisition mismatch: ${pin.path}`);
    return { path: file, primaryPath: pin.path, sha256: pin.sha256, gitBlob: blob, text: bytes.toString('utf8') };
  });
  const tablet = files[0].text, gos = files[1].text;
  const constants = {};
  for (const [name, expected] of Object.entries(pins.tabletUnshiftedConstants)) {
    const match = tablet.match(new RegExp(`private byte ${name} = (\\d+);`));
    if (!match || Number(match[1]) !== expected) throw Error(`Unexpected tablet source constant: ${name}`);
    constants[name] = expected;
  }
  const getAssignments = text => [...text.matchAll(/^\s*([ag][XYZ]\s*=\s*[^;]+;)/gm)].map(match => match[1]);
  const leftStart = tablet.indexOf('case 0: // LeftJoycon'), rightStart = tablet.indexOf('case 1: // RightJoycon');
  if (leftStart < 0 || rightStart <= leftStart) throw Error('Tablet actual C# case blocks unavailable');
  const left = getAssignments(tablet.slice(leftStart, rightStart)), right = getAssignments(tablet.slice(rightStart, tablet.indexOf('break;', rightStart)));
  const single = getAssignments(gos);
  if (left.length !== 6 || right.length !== 6 || single.length !== 6) throw Error('Actual C# IMU assignments must each contain exactly six axes');
  if (!tablet.includes('Controller?.GetStatus(1) == 0') || !gos.includes('Controller = new(details.VendorID, details.ProductID, 33);')) throw Error('Framing/reference capacity anchors changed');
  const attachment = tablet.slice(tablet.indexOf('if (IsHidReportMisaligned())'), tablet.indexOf('// manage gamepad motion from right controller'));
  if (/CONTROLLER_(?:ACCE|GYRO)_IDX\s*-=/.test(attachment)) throw Error('Historical source now shifts IMU; source-boundary note needs revision');
  const switchStart = tablet.lastIndexOf('switch (idx)', leftStart), switchOpen = tablet.indexOf('{', switchStart);
  if (switchStart < 0) throw Error('Actual C# Tablet switch unavailable');
  let depth = 1, switchEnd = switchOpen + 1;
  while (depth && switchEnd < tablet.length) { if (tablet[switchEnd] === '{') ++depth; if (tablet[switchEnd] === '}') --depth; ++switchEnd; }
  if (depth) throw Error('Unbalanced actual Tablet source switch');
  const tabletSwitch = tablet.slice(switchStart, switchEnd);
  const tabletMinimum = body => Math.max(...[...body.join('\n').matchAll(/data\[([A-Z_]+)(?:\s*\+\s*(\d+))?\]/g)].map(m => constants[m[1]] + Number(m[2] || 0))) + 1;
  const goSMinimum = Math.max(...[...single.join('\n').matchAll(/BitConverter\.ToInt16\(data,\s*(\d+)\)/g)].map(m => Number(m[1]) + 2));
  const minOffsets = { tabletLeft: tabletMinimum(left), tabletRight: tabletMinimum(right), goS: goSMinimum };
  if (minOffsets.tabletLeft !== 47 || minOffsets.tabletRight !== 60 || minOffsets.goS !== 26) throw Error('Actual source field coverage changed');
  return { pins, pinsPath, files, treePath, acquisitionPath, constants, left, right, single, attachment, tabletSwitch, minOffsets };
}
export function csharpOracle(primary) {
  const constants = Object.entries(primary.constants).map(([name, value]) => `int ${name}=${value};`).join('\n');
  return `using System;
using System.IO;
using System.Globalization;
static class SourceBodyOracle {
  static uint[] Bits(float gx,float gy,float gz,float ax,float ay,float az) {
    return new uint[]{BitConverter.SingleToUInt32Bits(gx),BitConverter.SingleToUInt32Bits(gy),BitConverter.SingleToUInt32Bits(gz),
      BitConverter.SingleToUInt32Bits(ax),BitConverter.SingleToUInt32Bits(ay),BitConverter.SingleToUInt32Bits(az)};
  }
  static uint[] Tablet(byte[] data,int sensorIndex) {
    ${constants}
    float gX=0,gY=0,gZ=0,aX=0,aY=0,aZ=0;
    byte idx=(byte)sensorIndex;
    ${primary.tabletSwitch}
    return Bits(gX,gY,gZ,aX,aY,aZ);
  }
  static uint[] GoS(byte[] data) {
    float gX=0,gY=0,gZ=0,aX=0,aY=0,aZ=0;
    ${primary.single.join('\n')}
    return Bits(gX,gY,gZ,aX,aY,aZ);
  }
  public static int Main(string[] args) {
    if(!BitConverter.IsLittleEndian) throw new Exception("Fixed Windows GoS oracle requires little endian");
    using(var writer=new StreamWriter(args[1])) foreach(var line in File.ReadLines(args[0])) {
      var parts=line.Split('|');var data=Convert.FromHexString(parts[4]);
      var result=parts[1]=="Tablet"?Tablet(data,int.Parse(parts[2],CultureInfo.InvariantCulture)):GoS(data);
      writer.Write(parts[0]);foreach(var value in result) {writer.Write('|');writer.Write(value.ToString("X8",CultureInfo.InvariantCulture));}writer.WriteLine();
    }
    return 0;
  }
}
`;
}
export function makeVectors() {
  const vectors = [];
  let randomState = 0x19a86e31;
  const random = () => { randomState ^= randomState << 13; randomState ^= randomState >>> 17; randomState ^= randomState << 5; return randomState >>> 0; };
  const layouts = [['Tablet',0,false],['Tablet',1,false],['Tablet',0,true],['Tablet',1,true],['GoS',0,false]];
  const add = (protocol, sensorIndex, byte1Zero, label, words, swapped = false) => {
    const data = Buffer.alloc(96);
    for (let index=0; index<data.length; ++index) data[index]=(index*37+11)&255;
    data[0] = vectors.length % 2 ? 0x00 : 0x7e; // Logical byte0 is data, never a guessed prefix.
    data[1] = protocol === 'Tablet' ? (byte1Zero ? 0 : 0x21) : (vectors.length & 255);
    const bases = protocol === 'GoS' ? [14,20] : sensorIndex === 0 ? [35,41] : [48,54];
    for(let word=0;word<6;++word){const base=bases[word<3?0:1] + (word%3)*2, little=(protocol==='GoS')!==swapped;
      if(little)data.writeInt16LE(words[word],base);else data.writeInt16BE(words[word],base);}
    const id=`${protocol}-${sensorIndex}-${byte1Zero?'byte1-zero':'byte1-nonzero'}-${label}`;
    const lengths=protocol==='GoS'?[26,27,32,33,34,64,80]:sensorIndex===0?[47,48,59,60,64,65,80]:[60,61,62,63,64,65,80];
    const actualBytes=lengths[vectors.length%lengths.length];
    vectors.push({id,protocol,sensorIndex,byte1Zero,actualBytes,hex:data.toString('hex')});
  };
  for(const [p,index,byte1Zero] of layouts){
    add(p,index,byte1Zero,'manual-distinct',[0x1234,-0x2345,0x3456,-0x4567,0x5678,-0x6789]);
    add(p,index,byte1Zero,'swapped-endian',[0x1234,-0x2345,0x3456,-0x4567,0x5678,-0x6789],true);
    add(p,index,byte1Zero,'all-zero',[0,0,0,0,0,0]);
    for(const value of [-32768,-32767,-256,-1,0,1,255,256,32766,32767]) {
      add(p,index,byte1Zero,`uniform-${value}`,Array(6).fill(value));
      for(let axis=0;axis<6;++axis){const words=[17,-29,43,-71,113,-179];words[axis]=value;add(p,index,byte1Zero,`axis-${axis}-${value}`,words);}
    }
    for(let vector=0;vector<80;++vector)add(p,index,byte1Zero,`random-${vector}`,Array.from({length:6},()=>((random()&65535)-32768)));
  }
  return vectors;
}
export function oracleHeader(vectors, resultText) {
  const answers = new Map();
  for(const line of resultText.trim().split(/\r?\n/)){const [id,...bits]=line.split('|');if(bits.length!==6||answers.has(id)||bits.some(b=>!/^[0-9A-F]{8}$/.test(b)))throw Error('Invalid executed C# oracle output');answers.set(id,bits);}
  if(answers.size!==vectors.length)throw Error('Incomplete executed C# oracle output');
  const rows=vectors.map(v=>{const bits=answers.get(v.id);if(!bits)throw Error(`Missing oracle ${v.id}`);
    return `{${JSON.stringify(v.id)},${v.protocol==='Tablet'?0:1},${v.sensorIndex},${v.actualBytes},{{${[...Buffer.from(v.hex,'hex')].join(',')}}},{{${bits.map(b=>'0x'+b+'u').join(',')}}}}`;});
  return '#pragma once\n#include <array>\n#include <cstddef>\n#include <cstdint>\nstruct OracleCase { const char* name; int protocol; unsigned index; std::size_t actual; std::array<std::uint8_t,96> bytes; std::array<std::uint32_t,6> expected; };\ninline const OracleCase oracleCases[]={\n'+rows.join(',\n')+'\n};\n';
}

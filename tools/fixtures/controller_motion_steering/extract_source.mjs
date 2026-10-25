// Exact body/range extraction. No production file is modified by this module.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
export const sha = v => crypto.createHash('sha256').update(v).digest('hex');
export function bodyEnd(text, start) {
  let depth=0, mode='code', rawEnd='';
  for(let i=start;i<text.length;++i) {
    const c=text[i],n=text[i+1];
    if(mode==='raw'){if(text.startsWith(rawEnd,i)){i+=rawEnd.length-1;mode='code';}continue;}
    if(mode==='line'){if(c==='\n')mode='code';continue;}
    if(mode==='block'){if(c==='*'&&n==='/'){++i;mode='code';}continue;}
    if(mode==='string'||mode==='char'){if(c==='\\'){++i;continue;}if(c===(mode==='string'?'"':"'"))mode='code';continue;}
    if(c==='/'&&n==='/'){++i;mode='line';continue;}
    if(c==='/'&&n==='*'){++i;mode='block';continue;}
    if(c==='R'&&n==='"'){const open=text.indexOf('(',i+2);assert(open>=0&&open-i<=18);rawEnd=')'+text.slice(i+2,open)+'"';i=open;mode='raw';continue;}
    if(c==='"'){mode='string';continue;}if(c==="'"){mode='char';continue;}
    if(c==='{')++depth;if(c==='}'&&--depth===0)return i+1;
  }
  throw Error('Unclosed source body');
}
function entry(text,name,start,end,kind) {
  const code=text.slice(start,end);return {name,kind,code,sha256:sha(code),
    startLine:text.slice(0,start).split('\n').length,endLine:text.slice(0,end).split('\n').length,
    startUtf8Byte:Buffer.byteLength(text.slice(0,start)),endUtf8ByteExclusive:Buffer.byteLength(text.slice(0,end))};
}
export function method(text,name,language='cpp') {
  const pattern=language==='csharp'?new RegExp(`^\\s*private static [^\\n;{]*\\b${name}\\s*\\(`,'gm'):
    new RegExp(`^static[^\\n;{]*\\b${name}\\s*\\(`,'gm');
  const matches=[];
  for(const m of text.matchAll(pattern)){
    const open=text.indexOf('{',m.index),semi=text.indexOf(';',m.index);if(open<0||(semi>=0&&semi<open))continue;
    // C# leading whitespace is retained exactly, including the method indentation.
    matches.push(entry(text,name,m.index,bodyEnd(text,open),'method'));
  }
  assert.equal(matches.length,1,'Expected one actual method '+name);return matches[0];
}
function struct(text,name){const matches=[...text.matchAll(new RegExp(`^struct ${name}\\s*\\{`,'gm'))];assert.equal(matches.length,1);
  const m=matches[0],end=bodyEnd(text,text.indexOf('{',m.index));assert.equal(text[end],';');return entry(text,name,m.index,end+1,'struct');}
function once(hay,needle){const at=hay.indexOf(needle);assert(at>=0&&hay.indexOf(needle,at+1)<0,'Expected unique fragment '+needle);return at;}
export function acquireOracle(sourcePath, manifest, out) {
  const bytes=fs.readFileSync(sourcePath),text=bytes.toString('utf8');
  const blob=crypto.createHash('sha1').update(Buffer.from(`blob ${bytes.length}\0`)).update(bytes).digest('hex');
  assert.equal(sha(bytes),manifest.sha256);assert.equal(blob,manifest.gitBlob);assert.equal(blob,manifest.fixedTreeBlob);
  const decision=method(text,'DetermineSteeringAxis','csharp'),swap=method(text,'SwapYawRoll','csharp');
  const start=once(text,'            ref Vector3 defaultGyro = ref gyroState.GetGyroscopeRef(SensorState.Default);');
  const stop=once(text,'            // DSU: invert axes if needed');
  const plane=entry(text,'SetupMotion-default-plane',start,stop,'fragment');
  const localStart=once(text,'                    Vector3 defaultGyroscope = gyroState.GetGyroscope(SensorState.Default);');
  const localEnd=text.indexOf('                    break;',localStart);
  assert(localEnd>localStart);const local=entry(text,'ProcessMotion-LocalSpace',localStart,localEnd,'fragment');
  const generated=`using System;\nusing System.Numerics;\ninternal static partial class MotionOracle\n{\n${decision.code}\n${swap.code}\n`+
    `    private static OracleRow RunExactPlane(Profile current, ControllerState controllerState)\n    {\n`+
    `        GyroState gyroState = controllerState.GyroState;\n${plane.code}\n`+
    `        Vector2 output = Vector2.Zero;\n${local.code}\n`+
    `        return new OracleRow { yaw = steeringAxis == SteeringAxis.Yaw, resolved = steeringAxis.ToString(),\n`+
    `            defaultGyro = new[] {defaultGyro.X,defaultGyro.Y,defaultGyro.Z},\n`+
    `            defaultAccel = new[] {defaultAccel.X,defaultAccel.Y,defaultAccel.Z},\n`+
    `            local = new[] {output.X,output.Y}, shortContribution = new[] {(int)output.X,(int)output.Y} };\n    }\n}\n`;
  fs.writeFileSync(path.join(out,'HC-MotionManager.cs.snapshot'),bytes);
  fs.writeFileSync(path.join(out,'HC-exact.generated.cs'),generated);
  return {sourcePath,sha256:sha(bytes),gitBlob:blob,commit:manifest.hcCommit,fragments:[decision,swap,plane,local],generatedSha256:sha(generated)};
}
export function freezeNative(repo,out) {
  const bytes=fs.readFileSync(path.join(repo,'native/main.cpp')),text=bytes.toString('utf8');
  const names=['inputCaptureHcSteeringAxisResolvesYaw','inputHostClampShort','inputCaptureHcSteering',
    'inputCaptureApplyCustomSensitivity','inputCaptureProcessMotionContribution'];
  const methods=names.map(n=>method(text,n));
  const types=['MotionSensorFrame','MotionContribution'].map(n=>struct(text,n));
  const gamepad=method(text,'inputCaptureOnGamepad');
  const mainBody=gamepad.code;
  // Production decision and plane ranges are selected only after the main owner
  // has landed its stable per-frame API. Missing/ambiguous ranges fail closed.
  const decisionMatch=/    const bool frameSteeringYaw\s*=\s*inputCaptureHcSteeringAxisResolvesYaw\([\s\S]*?\);/.exec(mainBody);
  assert(decisionMatch,'Missing actual per-frame decision');
  const frameAt=once(mainBody,'    const float frameAccelMultiplier = (float)g_realStickTest.accelerometerMultiplier;')+text.indexOf(gamepad.code);
  const decisionAt=text.indexOf(decisionMatch[0],text.indexOf(gamepad.code));
  const decision=entry(text,'frame-steering-decision',frameAt,decisionAt+decisionMatch[0].length,'fragment');
  const sensorAt=once(mainBody,'    MotionSensorFrame motionSensor;')+text.indexOf(gamepad.code);
  const contributionAt=once(mainBody,'    const MotionContribution motionContribution = inputCaptureProcessMotionContribution(')+text.indexOf(gamepad.code);
  const contributionEnd=text.indexOf(';',contributionAt)+1;
  const plane=entry(text,'actual-sensor-default-plane',sensorAt,contributionAt,'fragment');
  const call=entry(text,'actual-contribution-call',contributionAt,contributionEnd,'fragment');
  const captureStart=once(mainBody,'        motionPlaneSteeringYaw = frameSteeringYaw;')+text.indexOf(gamepad.code);
  const captureLast=once(mainBody,'        motionPlaneAccelMultiplier = frameAccelMultiplier;')+text.indexOf(gamepad.code);
  const capture=entry(text,'actual-diagnostic-capture',captureStart,captureLast+'        motionPlaneAccelMultiplier = frameAccelMultiplier;'.length,'fragment');
  const diagnosticAt=once(mainBody,'    const auto buildGamepadMotionPlane = [&]() -> json {')+text.indexOf(gamepad.code);
  const diagnosticEnd=bodyEnd(text,text.indexOf('{',diagnosticAt))+1;
  assert.equal(text[diagnosticEnd-1],';');
  const diagnostic=entry(text,'actual-diagnostic-lambda',diagnosticAt,diagnosticEnd,'fragment');
  const flat=s=>s.replace(/\s+/g,'');
  assert.equal([...mainBody.matchAll(/inputCaptureHcSteeringAxisResolvesYaw\s*\(/g)].length,1,'Main must decide once');
  assert(flat(call.code).endsWith(',frameSteeringYaw);'),'Actual call must pass same decision');
  assert(mainBody.includes('motionPlaneSteeringYaw = frameSteeringYaw')||mainBody.includes('motionPlaneSteeringYaw=frameSteeringYaw'),
    'Diagnostic plane must share the decision');
  assert(!plane.code.includes('inputCaptureHcSteeringAxisResolvesYaw('),'Plane must not decide again');
  assert(methods[4].code.includes('bool steeringYaw=inputCaptureHcSteeringAxisResolvesYaw()')||
    flat(methods[4].code).includes('boolsteeringYaw=inputCaptureHcSteeringAxisResolvesYaw()'),'Windows default argument missing');
  const generated=types.map(e=>e.code).join('\n\n')+'\n'+methods.map(e=>e.code).join('\n\n')+'\n';
  const pipeline=`static PipelineResult actualPipeline(const Case& in) {\n`+
    `    const bool controllerMotionRoute = in.controller;\n`+
    `    const float gravityX=in.gx, gravityY=in.gy, gravityZ=in.gz;\n`+
    `    const float cgx=in.cx,cgy=in.cy,cgz=in.cz,psx=0,psy=0,wsx=0,wsy=0;\n`+
    `    const double motionDeltaSeconds=1.0/60.0;\n`+
    `    const bool gyroSubmitAllowed=true,motionTriggered=true,motionProcessorReady=true;\n`+
    `    const XINPUT_GAMEPAD pad{};\n${decision.code}\n`+
    `    const bool gamepadMotionPlaneProven=true; bool motionPlaneSteeringYaw=false;\n`+
    `    float motionPlaneGyroMultiplier=0,motionPlaneAccelMultiplier=0;\n`+
    `    const float linearAccelX=0,linearAccelY=0,linearAccelZ=0,orientationW=1,orientationX=0,orientationY=0,orientationZ=0;\n`+
    `${capture.code}\n${diagnostic.code}\n${plane.code}\n${call.code}\n`+
    `    return {frameSteeringYaw,motionSensor,motionContribution,buildGamepadMotionPlane()};\n}\n`;
  fs.writeFileSync(path.join(out,'main.cpp.snapshot'),bytes);
  fs.writeFileSync(path.join(out,'native-functions.generated.h'),generated);
  fs.writeFileSync(path.join(out,'native-pipeline.generated.h'),pipeline);
  const all=[...methods,...types,decision,capture,diagnostic,plane,call];
  const binding={sourceSha256:sha(bytes),sourceBytes:bytes.length,entries:all.map(({code,...e})=>e),
    gamepadAnchor:(({code,...e})=>e)(gamepad),oncePerFrameDecision:true,diagnosticSharesDecision:true,
    windowsDefaultArgument:true,scope:'Actual functions and small actual frame snippets; whole onGamepad/JSON diagnostics are source-only.'};
  fs.writeFileSync(path.join(out,'native-bindings.json'),JSON.stringify(binding,null,2)+'\n');
  fs.writeFileSync(path.join(out,'onGamepad-source-anchor.cpp.txt'),gamepad.code);
  return {bytes,methods,types,decision,capture,diagnostic,plane,call,gamepad,generated,pipeline,binding};
}

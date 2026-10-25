import {createHash} from 'node:crypto';
import {canonicalFactoryMethod} from './prepare-factory-initialization.mjs';
import basis from './constructor-raw-basis.json' with {type:'json'};

const need=(value,code)=>{if(!value)throw Error('CONSTRUCTOR_RAW_'+code);};
const digest=value=>createHash('sha256').update(JSON.stringify(value)).digest('hex');
const sorted=values=>JSON.stringify([...values].sort());
export function canonicalConstructorMethod(method){
 if(method.hasBody)return canonicalFactoryMethod(method);
 need(method.instructions===null&&method.locals===null&&method.exceptions===null,'BODYLESS_METHOD_SHAPE');
 return {method:method.method,isStatic:method.isStatic,isConstructor:method.isConstructor,isPInvoke:method.isPInvoke,hasBody:method.hasBody,pinvoke:method.pinvoke,locals:method.locals,instructions:method.instructions,exceptions:method.exceptions};
}
function image(report,pin){
 need(report?.status==='PASS_RAW_SELECTED_METHOD_READ'&&report.targetAssembliesObserved===false&&report.targetCodeExecuted===false&&report.targetConstructorsInvoked===false&&report.externalResolutionAllowed===false,'METADATA_ONLY');
 const images=report.rows.filter(row=>row.path.toLowerCase()===pin.path.toLowerCase()&&row.sha256===pin.sha256);need(images.length===1,'EXACT_IMAGE');
 const value=images[0];need(value.identity===basis.identity,'IDENTITY');
 const methods=new Map();for(const m of value.selections.flatMap(t=>t.methods)){need(!methods.has(m.method),'DUPLICATE_SIGNATURE');methods.set(m.method,m);}
 need(sorted(methods.keys())===sorted(basis.selectedMethods.map(m=>m.signature)),'EXACT128_METHOD_SET');
 need(digest(value.selections.map(t=>({type:t.type,baseType:t.baseType,fields:t.fields})))===basis.sourceStructureSha256,'FIELDS_AND_BASE_TYPES');
 need(value.moduleInitializers.length===basis.moduleInitializers.length&&value.moduleInitializers.every((m,n)=>m.method===basis.moduleInitializers[n].signature&&digest(canonicalConstructorMethod(m))===basis.moduleInitializers[n].canonicalSha256),'ORIGINAL_MODULE_INITIALIZER');
 return {value,methods};
}
export function validateOriginalConstructorRaw({sourceReport,sourcePin,generatedReport,generatedPin,proof}){
 need(sourcePin.sha256===basis.sourceSha256,'LOCKED_SOURCE');
 const source=image(sourceReport,sourcePin),generated=image(generatedReport,generatedPin),leaves=Object.keys(basis.bodies);
 need(proof.status==='PASS'&&proof.sourceSha256===sourcePin.sha256&&proof.instrumentedSha256===generatedPin.sha256&&proof.allDeviceInstanceConstructorBodiesUnchanged===true&&proof.gamepadMotionManagedAndNativeAbiUnchanged===true&&proof.hardwareIo===false&&proof.nativeMathOnly===true&&sorted(proof.changedMethods)===sorted(leaves),'EXACT_ORIGINAL_SEVEN_LEAF_PROOF');
 for(const selected of basis.selectedMethods){
  const original=source.methods.get(selected.signature),current=generated.methods.get(selected.signature);
  need(digest(canonicalConstructorMethod(original))===selected.canonicalSha256,'LOCKED_CLEAN_METHOD');
  if(Object.hasOwn(basis.bodies,selected.signature)){
   need(current.hasBody===true&&current.isPInvoke===false&&current.pinvoke===null&&current.locals.length===0&&current.exceptions.length===0,'EXPLICIT_MANAGED_LEAF');
   need(current.isStatic===original.isStatic&&current.isConstructor===original.isConstructor,'LEAF_METHOD_FLAGS');
   need(JSON.stringify(current.instructions.map(i=>({opcode:i.opcode,operand:i.operand})))===JSON.stringify(basis.bodies[selected.signature]),'EXACT_TYPED_SCOPED_LEAF');
  }else need(digest(canonicalConstructorMethod(current))===selected.canonicalSha256,'ORIGINAL_METHOD_UNCHANGED');
 }
 for(const signature of basis.originalFactoryConstructors)need(source.methods.has(signature)&&generated.methods.has(signature),'ALL80_ORIGINAL_FACTORY_CONSTRUCTORS');
 for(const declaration of basis.nativeDeclarations){const m=generated.methods.get(declaration.signature);need(m?.isPInvoke===true&&m.pinvoke?.module==='GamepadMotion.dll'&&m.pinvoke.entry===declaration.entry,'ALL21_ORIGINAL_NATIVE_DECLARATIONS');}
 return {schemaVersion:1,status:'PASS_EXACT_SEVEN_CONSTRUCTOR_LEAVES_AND121_SELECTED_METHODS',selectedTypes:86,selectedMethods:128,changedMethods:7,unchangedSelectedMethods:121,originalFactoryConstructors:80,nativeGamepadMotionDeclarations:21,moduleInitializersPreserved:basis.moduleInitializers.length,moduleInitializerAndCctorRuntimeReviewStillRequired:true,targetExecutedByValidator:false,original89CaseBusinessExecuted:false,wholeGoalComplete:false};
}

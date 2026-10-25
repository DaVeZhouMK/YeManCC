/** YMCC independent weapon renderer. Three.js is MIT; asset provenance: models/m4-vudu.json.
 * No Kitbash engine code. A single baked pose and 256px textures keep the optional view small. */
import * as T from 'three';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import { MeshoptDecoder } from 'three/addons/libs/meshopt_decoder.module.js';
const ROOT=new URL('.',import.meta.url).href;
export async function createRange3D(){
 const canvas=document.createElement('canvas'),gl=new T.WebGLRenderer({canvas,antialias:true,alpha:true,powerPreference:'low-power'});
 gl.setPixelRatio(1);gl.setClearColor(0,0);gl.outputColorSpace=T.SRGBColorSpace;gl.toneMapping=T.ACESFilmicToneMapping;gl.toneMappingExposure=1.25;
 const camera=new T.PerspectiveCamera(60,1,.015,4),scene=new T.Scene(),scopeFlashScene=new T.Scene(),gun=new T.Group();scene.add(gun);
 let disposed=false;const resources=new Set(),lensMeshes=[];
 const lensGlass=new T.MeshPhysicalMaterial({color:'#010204',transparent:true,opacity:.9,roughness:.10,metalness:.3,clearcoat:1,clearcoatRoughness:.06,reflectivity:.85,depthWrite:false,side:T.DoubleSide});resources.add(lensGlass);
 scene.add(new T.HemisphereLight('#f6f3e9','#424b53',3));
 for(const[color,intensity,position]of [['#fff3e2',3.5,[-2,4,3]],['#b8d2ea',1.8,[3,1,-2]]]){const light=new T.DirectionalLight(color,intensity);light.position.fromArray(position);scene.add(light);}
 const loader=new GLTFLoader().setMeshoptDecoder(MeshoptDecoder);
 try{
 const response=await fetch(ROOT+'models/m4-vudu.json');if(!response.ok)throw Error('Weapon manifest '+response.status);const manifest=await response.json();
 const loaded=await Promise.all(manifest.parts.map(async p=>({p,object:(await loader.loadAsync(ROOT+'models/'+p.file)).scene})));
 const byId=new Map();
 for(const{p,object}of loaded){byId.set(p.id,object);object.traverse(o=>{
  if(o.userData?.active===false||o.name.includes('empty_link_')||/_LOD[1-9]$/.test(o.name))o.visible=false;
  if(!o.isMesh)return;resources.add(o.geometry);
  for(const m of Array.isArray(o.material)?o.material:[o.material]){resources.add(m);for(const v of Object.values(m))if(v?.isTexture)resources.add(v);m.roughness=.66;
   // A dark rear lens reads as glass at hip fire; the independent ADS pass keeps its view clear.
   if(m.userData?.backLens){o.material=lensGlass;o.visible=true;lensMeshes.push(o);}else if(m.userData?.opticLens||m.userData?.opticGlass)o.visible=false;}
 });}
 const root=byId.get(manifest.root);gun.add(root);
 for(const{p,object}of loaded)if(p.parent){const parent=byId.get(p.parent);let socket;parent.traverse(o=>{if(!socket&&o.name.toLowerCase()===p.slot?.toLowerCase())socket=o;});if(!socket)throw Error('Missing attachment '+p.slot+' in '+p.name);object.quaternion.fromArray(p.attach);socket.add(object);}
 root.rotation.set(0,Math.PI,0);root.updateMatrixWorld(true);
 // Position the grip on the forward M-LOK section without moving the holding pose.
 for(const {p,object}of loaded)if(p.railForward){const forward=new T.Vector3(0,0,-1).transformDirection(new T.Matrix4().copy(object.parent.matrixWorld).invert());object.position.addScaledVector(forward,p.railForward);}root.updateMatrixWorld(true);
 const eye=byId.get('scope-vudu').getObjectByName('mod_aim_camera');if(!eye)throw Error('Scope eye anchor missing');root.position.sub(eye.getWorldPosition(new T.Vector3()));root.updateMatrixWorld(true);
 root.updateMatrixWorld(true);const optic=byId.get('scope-vudu'),bounds=new T.Box3().setFromObject(root);
 const muzzlePart=manifest.parts.find(p=>p.slot==='mod_muzzle'),muzzleModel=byId.get(muzzlePart?.id),muzzle=muzzleModel?.getObjectByName('muzzleflash_000');if(!muzzle)throw Error('Muzzle attachment missing');
 const foregrip=byId.get('front-foregrip');if(!foregrip)throw Error('Foregrip attachment missing');
 const magazinePart=manifest.parts.find(p=>p.slot==='mod_magazine'),magazine=byId.get(magazinePart?.id);if(!magazine)throw Error('Magazine attachment missing');
 const magazineHome=magazine.position.clone(),magazineDown=new T.Vector3(0,-1,0).transformDirection(new T.Matrix4().copy(magazine.parent.matrixWorld).invert());
 // Rotate the assembly around its barrel axis. The holding pose and camera stay fixed.
 const reloadPivot=new T.Group(),pivotOffset=new T.Group();reloadPivot.position.set(0,-.064,0);pivotOffset.position.set(0,.064,0);gun.add(reloadPivot);reloadPivot.add(pivotOffset);pivotOffset.add(root);
 const smooth=v=>{v=Math.max(0,Math.min(1,v));return v*v*(3-2*v);};
 const atlas=await new T.TextureLoader().loadAsync(ROOT+'muzzle-flash.webp');atlas.colorSpace=T.SRGBColorSpace;resources.add(atlas);
 const flashMaps=Array.from({length:4},(_,i)=>{const map=atlas.clone();map.repeat.set(.5,.5);map.offset.set((i%2)*.5,Math.floor(i/2)*.5);map.needsUpdate=true;resources.add(map);return map;});
 const flashMaterial=new T.SpriteMaterial({map:flashMaps[0],color:'#ffd1a0',transparent:true,opacity:0,depthWrite:false,depthTest:true,blending:T.AdditiveBlending});resources.add(flashMaterial);
 const outsideFlash=new T.Sprite(flashMaterial),insideMaterial=flashMaterial.clone(),insideFlash=new T.Sprite(insideMaterial);resources.add(insideMaterial);muzzle.add(outsideFlash);scopeFlashScene.add(insideFlash);
 const flashLight=new T.PointLight('#ffc58b',0,1.1,2);muzzle.add(flashLight);
 const projected=new T.Vector3(),screenPoint=node=>{node.getWorldPosition(projected);projected.project(camera);return {x:(projected.x+1)*w0/2,y:(1-projected.y)*h0/2};};
 let w0=0,h0=0;

 const audit={parts:loaded.length,meshes:0,depthTest:true,offline:true,loaded:true,muzzleAttachment:muzzle.name,lensMaskOpacity:.9,lensMaskMeshes:lensMeshes.length,foregripPresent:true,foregripName:manifest.parts.find(p=>p.id==='front-foregrip').name,flashDraws:{outside:0,inside:0},muzzleScreen:null,muzzleScopeScreen:null,reloadProgress:0,magazineDrop:0,reloadRoll:0,foregripScreen:null,textureCount:[...resources].filter(x=>x.isTexture).length,assetBytes:manifest.files.reduce((s,f)=>s+f.bytes,0),weaponBounds:{min:bounds.min.toArray(),max:bounds.max.toArray()},optic:optic.getWorldPosition(new T.Vector3()).toArray()};
 root.traverse(o=>{if(o.isMesh&&o.visible)audit.meshes++;});
 function render(ctx,w,h,state){if(disposed)return;
  const dpr=Math.min(window.devicePixelRatio||1,1.5),pw=Math.round(w*dpr),ph=Math.round(h*dpr);
  if(pw!==Math.round(w0*dpr)||ph!==Math.round(h0*dpr)){gl.setSize(pw,ph,false);camera.aspect=w/h;camera.fov=2*Math.atan(h/(w*1.72))*180/Math.PI;camera.setViewOffset(w,h,0,h*.03,w,h);camera.updateProjectionMatrix();}
  w0=w;h0=h;const a=state.ads,walk=Math.sin(state.time*2.3)*.0015;
  gun.position.set(.13*(1-a)+state.kickYaw*.35,-.075*(1-a)+walk,-.16+.125*a+state.recoil*.7);
  gun.rotation.set(state.recoil*.65,.10*(1-a)+state.recoilYaw*.7,Math.sin(state.time*1.7)*.003*(1-a)+state.recoilYaw*.2);
  lensGlass.opacity=.9*(1-a);for(const mesh of lensMeshes)mesh.visible=a<.995;audit.lensMaskOpacity=lensGlass.opacity;
  const reload=state.reload>0?1-state.reload/1.35:0,turn=state.reload>0?(reload<.2?smooth(reload/.2):reload<.8?1:smooth((1-reload)/.2)):0;
  reloadPivot.rotation.set(0,0,-.72*turn);
  const drop=state.reload>0?.14*(reload<.2?0:reload<.4?smooth((reload-.2)/.2):reload<.58?1:reload<.82?1-smooth((reload-.58)/.24):0):0;
  magazine.position.copy(magazineHome).addScaledVector(magazineDown,drop);audit.reloadProgress=reload;audit.magazineDrop=drop;audit.reloadRoll=reloadPivot.rotation.z;
  const firing=state.flash>0,phase=Math.max(0,state.flash/.045),size=.065+(state.shots%3)*.012;outsideFlash.visible=firing;outsideFlash.scale.setScalar(size);flashMaterial.map=flashMaps[state.shots%4];flashMaterial.opacity=phase*.85;flashLight.intensity=firing?phase*1.6:0;scene.updateMatrixWorld(true);audit.muzzleScreen=screenPoint(muzzle);audit.foregripScreen=screenPoint(foregrip);if(firing)audit.flashDraws.outside++;
  gl.render(scene,camera);ctx.drawImage(canvas,0,0,w,h);

 }
 function renderScopeFlash(ctx,w,h,state){if(disposed||state.flash<=0)return;
  muzzle.getWorldPosition(insideFlash.position);insideFlash.scale.copy(outsideFlash.scale);insideMaterial.map=flashMaterial.map;insideMaterial.opacity=Math.max(0,state.flash/.045)*.22;
  camera.zoom=2;camera.updateProjectionMatrix();audit.muzzleScopeScreen=screenPoint(muzzle);gl.render(scopeFlashScene,camera);ctx.drawImage(canvas,0,0,w,h);audit.flashDraws.inside++;
  camera.zoom=1;camera.updateProjectionMatrix();
 }
 function dispose(){if(disposed)return;disposed=true;for(const object of resources)object.dispose?.();gl.dispose();gl.forceContextLoss();}
 return {render,renderScopeFlash,dispose,inspect:()=>({...audit,position:gun.position.toArray(),rotation:gun.rotation.toArray()})};
 }catch(e){for(const object of resources)object.dispose?.();gl.dispose();gl.forceContextLoss();throw e;}
}



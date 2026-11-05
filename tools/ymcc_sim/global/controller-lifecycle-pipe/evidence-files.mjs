// Collect original wire evidence, never ambient Windows profile cache state.
import fs from 'node:fs';
import path from 'node:path';
const need=(ok,message)=>{if(!ok)throw Error(message);};
export function collectWireEvidence(root){
 root=path.resolve(root);need(fs.realpathSync(root).toLowerCase()===root.toLowerCase(),'OUTPUT_ROOT_REDIRECTION');
 const profileRoots=new Set([root,...['p','n0','n1'].flatMap(suite=>[path.join(root,suite),...['Release','Checked'].map(mode=>path.join(root,suite,mode))])].map(value=>value.toLowerCase()));
 const files=[],excludedProfileDirectories=[];
 function visit(directory){
  for(const entry of fs.readdirSync(directory,{withFileTypes:true})){
   const file=path.join(directory,entry.name);
   // USERPROFILE is explicitly private in wireChildEnvironment. Windows may
   // create INetCache junctions here. This entire AppData tree is unasserted
   // profile state, never a result, source, compiler output or receipt root.
   if(profileRoots.has(directory.toLowerCase())&&entry.name.toLowerCase()==='appdata'){
    need(entry.isDirectory()&&!entry.isSymbolicLink(),'PROFILE_ROOT_REDIRECTION:'+file);
    excludedProfileDirectories.push({path:file,reason:'private-Windows-AppData-not-an-evidence-root'});continue;
   }
   need(!entry.isSymbolicLink(),'OUTPUT_LINK:'+file);
   if(entry.isDirectory())visit(file);else{need(entry.isFile(),'OUTPUT_NOT_REGULAR_FILE:'+file);files.push(file);}
  }
 }
 visit(root);return {files,excludedProfileDirectories};
}

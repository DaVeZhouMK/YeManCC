import path from 'node:path';
import {nativeTools,SOURCE_MEMBERS} from '../../../fixtures/radial_private/scope.mjs';
export {SOURCE_MEMBERS};
export const FIXED_NODE='G:/YeManCC-Work/Environment/Toolchains/nodejs/node-v22.14.0-win-x64/node.exe';
// Reuse the existing complete native MSVC/SDK pins, not a newly inferred closure.
export function ownerTools(repo){
 const base=nativeTools(repo),selected=base.manifest.selected;
 return {files:base.files,cl:selected.compiler,node:FIXED_NODE,powershell:selected.powershell,
  includeDirs:selected.includeDirectories,libDirs:selected.libraryDirectories,
  manifest:base.manifestFile,maxArtifactInputs:base.manifest.maxArtifactInputs};
}
export function verifyOwnerTools(repo){return ownerTools(repo);}
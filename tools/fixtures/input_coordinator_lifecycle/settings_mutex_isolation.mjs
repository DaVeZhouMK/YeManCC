import assert from 'node:assert/strict';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {terminal26RuntimePaths,terminal26RuntimeLayout,TERMINAL26_PUBLIC_RUNTIME_POLICY} from '../../ymcc_sim/global/controller-coordinator-pipe/r3-terminal26-runtime-layout.mjs';
import {currentLifecycleModePlan,currentLifecycleModeLabel} from '../../ymcc_sim/global/controller-coordinator-pipe/r3-current-run.mjs';
const sha=text=>createHash('sha256').update(text).digest('hex');
export const privateSettingsMutexDefinition=String.raw`
// Test-only OS boundary. Same private settings directory shares a real named
// mutex across processes. No invocation can reach the installed YMCC mutex.
static std::wstring fixturePrivateSettingsMutexName() {
    const auto root=normalizeWindowsPathForCompare(g_yemanccDataDir);
    const auto file=normalizeWindowsPathForCompare(YM_SETTINGS_FILE);
    if(root.empty() || file!=root+L"\\yeman-settings.json" ||
       (root.find(L"\\build\\validation\\")==std::wstring::npos &&
        root.find(L"\\build\\v\\")==std::wstring::npos))
        throw std::logic_error("private-settings-root-not-admitted");
    HANDLE folder=CreateFileW(root.c_str(),FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(folder==INVALID_HANDLE_VALUE)throw std::logic_error("private-settings-directory-open-failed");
    BY_HANDLE_FILE_INFORMATION identity{};
    const bool observed=GetFileInformationByHandle(folder,&identity)!=FALSE;
    const bool closed=CloseHandle(folder)!=FALSE;
    if(!observed || !closed || !(identity.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) ||
       (identity.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))
        throw std::logic_error("private-settings-directory-identity-invalid");
    // Volume + directory file ID retains identity across case/short-path aliases.
    return L"Local\\YMCC-PrivateFixtureSettings-"+
        std::to_wstring(identity.dwVolumeSerialNumber)+L"-"+
        std::to_wstring(identity.nFileIndexHigh)+L"-"+
        std::to_wstring(identity.nFileIndexLow);
}
static HANDLE fixturePrivateSettingsMutex(LPSECURITY_ATTRIBUTES attributes,BOOL initialOwner,LPCWSTR requestedName) {
    if(attributes || initialOwner || !requestedName ||
       std::wstring(requestedName)!=L"Local\\YeManCC-UnifiedSettings") {
        ++forbiddenIo;throw std::logic_error("unadmitted-private-fixture-mutex-request");
    }
    const auto name=fixturePrivateSettingsMutexName();
    if(name.rfind(L"Local\\YMCC-PrivateFixtureSettings-",0)!=0 ||
       name==L"Local\\YeManCC-UnifiedSettings") {
        ++forbiddenIo;throw std::logic_error("private-settings-mutex-namespace-required");
    }
    return ::CreateMutexW(attributes,initialOwner,name.c_str());
}
`;
// A moved core-test run must explicitly bind its one runtime directory.
// Keep the legacy default byte-for-byte; never admit all of Testing or a sibling.
export function originalLifecycleRuntimeLabels(mutation=null){
 const plan=currentLifecycleModePlan(),matching=plan.filter(row=>row.mutation===mutation);
 // The three full Pipe negatives use the root directly and have no mode directory.
 if(['rollback-cancellation-guards','rollback-ps-endpoint-ownership','rollback-ps-binding-revision'].includes(mutation))return [];
 assert(matching.length>0,'KNOWN_ORIGINAL_LIFECYCLE_MUTATION_REQUIRED');
 return matching.map(currentLifecycleModeLabel);
}
export function privateSettingsMutexDefinitionForRuntimeRoot(runtimeRoot,{managedInputContract,runtimeParameterLayout,runtimeReusePolicy,gpdPreHideBatchCount,lifecycleMutation}={}){
 assert(managedInputContract===undefined||['current105-exact21-v1','current105-terminal22-v1','current105-terminal23-sd-decoder-v1','current105-terminal26-sdk-lifetime-compact-v1'].includes(managedInputContract),'UNKNOWN_MUTEX_RUNTIME_CONTRACT');
 const finiteModes=managedInputContract==='current105-terminal26-sdk-lifetime-compact-v1';
 if(gpdPreHideBatchCount!==undefined)assert(Number.isInteger(gpdPreHideBatchCount)&&gpdPreHideBatchCount>=2&&gpdPreHideBatchCount<=16&&runtimeRoot!==undefined&&managedInputContract===undefined&&runtimeParameterLayout===undefined&&runtimeReusePolicy===undefined,'EXACT_GPD_PRE_HIDE_BATCH_ROOTS_REQUIRED');
 if(lifecycleMutation!==undefined)assert(runtimeRoot!==undefined&&[managedInputContract,runtimeParameterLayout,runtimeReusePolicy,gpdPreHideBatchCount].every(v=>v===undefined),'ORIGINAL_MODE_ROOT_OPTIONS_EXCLUSIVE');
 if(runtimeRoot===undefined){assert(!finiteModes,'EXPLICIT26_RUNTIME_ROOT_REQUIRED');return privateSettingsMutexDefinition;}
 assert(typeof runtimeRoot==='string'&&path.isAbsolute(runtimeRoot)&&!/[\0\r\n]/.test(runtimeRoot),'ABSOLUTE_OWNED_RUNTIME_ROOT_REQUIRED');
 const canonical=path.resolve(runtimeRoot),base=path.resolve(import.meta.dirname,'../../../../../../Testing/YMCC/Evidence'),relative=path.relative(base,canonical);
 const workspaceBase=path.resolve(base,'../Workspace'),workspaceRelative=path.relative(workspaceBase,canonical),parts=workspaceRelative.split(path.sep);
 const evidenceOwned=relative&&!path.isAbsolute(relative)&&relative!=='..'&&!relative.startsWith('..'+path.sep);
 const workspaceOwned=workspaceRelative&&!path.isAbsolute(workspaceRelative)&&workspaceRelative!=='..'&&!workspaceRelative.startsWith('..'+path.sep)&&
  parts.length>=4&&parts[1].toLowerCase()==='build'&&parts[2].toLowerCase()==='validation';
 assert(evidenceOwned||workspaceOwned,'RUNTIME_ROOT_OUTSIDE_CORE_EVIDENCE');
 const start=privateSettingsMutexDefinition.indexOf('(root.find(');
 const end=privateSettingsMutexDefinition.indexOf('))\n        throw',start);
 assert(start>=0&&end>start,'EXACT_LEGACY_RUNTIME_ADMISSION_REQUIRED');
 const old=privateSettingsMutexDefinition.slice(start,end+1);
 assert.equal(privateSettingsMutexDefinition.split(old).length,2,'EXACT_LEGACY_RUNTIME_ADMISSION_REQUIRED');
 if(lifecycleMutation!==undefined){
 const roots=[canonical,...originalLifecycleRuntimeLabels(lifecycleMutation).map(label=>path.join(canonical,label))];
 return privateSettingsMutexDefinition.replace(old,roots.map(root=>'root!=normalizeWindowsPathForCompare(L'+JSON.stringify(root)+')').join(' &&\n       '));
 }
 assert(runtimeParameterLayout===undefined||finiteModes,'PARAMETER_PATHS_REQUIRE_FINITE_EXPLICIT26');
 const paths=finiteModes?terminal26RuntimePaths(canonical,{runtimeParameterLayout,runtimeReusePolicy}):gpdPreHideBatchCount===undefined?[canonical]:Array.from({length:gpdPreHideBatchCount},(_,i)=>path.join(canonical,'b'+i));
 if(runtimeReusePolicy!==undefined){
  assert(finiteModes&&runtimeReusePolicy===TERMINAL26_PUBLIC_RUNTIME_POLICY,'EXPLICIT26_PUBLIC_RUNTIME_POLICY_REQUIRED');
  const layout=terminal26RuntimeLayout(canonical,{runtimeParameterLayout,runtimeReusePolicy});
  const admitted=paths.map(value=>'root==normalizeWindowsPathForCompare(L'+JSON.stringify(value)+')').join(' ||\n        ');
  const helpers=String.raw`
// Only an exact qualification path or one fresh hexadecimal child of this compiled owner.
static bool fixturePrivateRuntimeRootAllowed(const std::wstring& supplied,bool fresh) {
    try {
        const std::filesystem::path raw(supplied);
        if(!raw.is_absolute())return false;
        const auto root=normalizeWindowsPathForCompare(supplied);
        const std::filesystem::path selected(root);
        const bool qualification=(
        ${admitted});
        bool publicChild=false;
        const auto leaf=raw.filename().wstring();
        if(leaf.size()==16 && normalizeWindowsPathForCompare(selected.parent_path().wstring())==normalizeWindowsPathForCompare(L${JSON.stringify(layout.publicRuntime.base)})) {
            publicChild=true;
            for(const auto ch:leaf)if(!((ch>=L'0'&&ch<=L'9')||(ch>=L'a'&&ch<=L'f')))publicChild=false;
        }
        if(!qualification&&!publicChild)return false;
        for(auto cursor=selected;;cursor=cursor.parent_path()) {
            const auto attributes=GetFileAttributesW(cursor.c_str());
            if(attributes==INVALID_FILE_ATTRIBUTES || !(attributes&FILE_ATTRIBUTE_DIRECTORY) || (attributes&FILE_ATTRIBUTE_REPARSE_POINT))return false;
            if(cursor.parent_path()==cursor)break;
        }
        if(fresh) {
            std::error_code error;
            if(!std::filesystem::is_empty(selected,error)||error)return false;
        }
        return true;
    }catch(...){return false;}
}
`;
  return helpers+privateSettingsMutexDefinition.replace(old,'!fixturePrivateRuntimeRootAllowed(g_yemanccDataDir,false)');
 }
 const expression=paths.map(value=>'root!=normalizeWindowsPathForCompare(L'+JSON.stringify(value)+')').join(' &&\n        ');
 return privateSettingsMutexDefinition.replace(old,paths.length===1?expression:'('+expression+')');
}
export function isolateCurrentFixtureSettingsMutex(original,{runtimeRoot,moduleMaintenance=false,managedInputContract,runtimeParameterLayout,runtimeReusePolicy,gpdPreHideBatchCount,lifecycleMutation}={}){
 assert(typeof moduleMaintenance==='boolean','BOOLEAN_MODULE_MAINTENANCE_BOUNDARY_REQUIRED');
 let boundary=privateSettingsMutexDefinitionForRuntimeRoot(runtimeRoot,{managedInputContract,runtimeParameterLayout,runtimeReusePolicy,gpdPreHideBatchCount,lifecycleMutation});
 if(moduleMaintenance){
  assert(runtimeRoot!==undefined,'EXPLICIT_MODULE_MAINTENANCE_RUNTIME_REQUIRED');
  const oldGuard='if(attributes || initialOwner || !requestedName ||\n       std::wstring(requestedName)!=L"Local\\\\YeManCC-UnifiedSettings")';
  const newGuard='const bool moduleRequest=requestedName && std::wstring(requestedName)==L"Global\\\\YeManCC.ModuleMaintenance";\n    if(attributes || initialOwner || !requestedName ||\n       (!moduleRequest && std::wstring(requestedName)!=L"Local\\\\YeManCC-UnifiedSettings"))';
  assert.equal(boundary.split(oldGuard).length,2,'EXACT_ORIGINAL_MUTEX_GUARD_REQUIRED');
  boundary=boundary.replace(oldGuard,newGuard);
  const oldName='const auto name=fixturePrivateSettingsMutexName();';
  assert.equal(boundary.split(oldName).length,2,'EXACT_ORIGINAL_PRIVATE_MUTEX_NAME_REQUIRED');
  boundary=boundary.replace(oldName,'const auto baseName=fixturePrivateSettingsMutexName();\n    const auto name=moduleRequest ? baseName+L"-ModuleMaintenance" : baseName;');
 }

 const before='#include "main.inert.cpp"';
 assert.equal(original.split(before).length,2,'EXACT_ONE_ORIGINAL_INERT_INCLUDE_REQUIRED');
 assert(!original.includes('fixturePrivateSettingsMutex'),'PRIVATE_SETTINGS_MUTEX_ALREADY_PRESENT');
 const after='static HANDLE fixturePrivateSettingsMutex(LPSECURITY_ATTRIBUTES,BOOL,LPCWSTR);\n'+
  '#define CreateMutexW fixturePrivateSettingsMutex\n'+before+'\n#undef CreateMutexW\n'+boundary;
 let text=original.replace(before,after);
 const edits=[{before,after}];
 // The existing source reader reconstructs only this exact, non-executable include stub.
 const runtimeBoundaryOnly=original==='#include "main.inert.cpp"\n';
 if(runtimeReusePolicy!==undefined&&!runtimeBoundaryOnly){
  const entry='if(argc!=3&&argc!=4&&argc!=5)return 2;const std::wstring mode=argv[2];std::wstring selectedCase;';
  const guarded='if(argc!=3&&argc!=4&&argc!=5)return 2;if(!fixturePrivateRuntimeRootAllowed(argv[1],true))return 2;const auto fixtureRuntimeRoot=normalizeWindowsPathForCompare(argv[1]);if(normalizeWindowsPathForCompare(POWER_CONTROL_DIR)!=fixtureRuntimeRoot||normalizeWindowsPathForCompare(YM_SETTINGS_FILE)!=fixtureRuntimeRoot+L"\\\\yeman-settings.json"||normalizeWindowsPathForCompare(YM_SETTINGS_BACKUP_FILE)!=fixtureRuntimeRoot+L"\\\\yeman-settings.json.bak")return 2;const std::wstring mode=argv[2];std::wstring selectedCase;';
  assert.equal(text.split(entry).length,2,'EXACT_ORIGINAL_MAIN_ADMISSION_REQUIRED');
  text=text.replace(entry,guarded);edits.push({before:entry,after:guarded});
 }
 let inverse=text;for(const edit of edits.slice().reverse()){assert.equal(inverse.split(edit.after).length,2);inverse=inverse.replace(edit.after,edit.before);}
 assert.equal(inverse,original);
 return {text,originalSha256:sha(original),isolatedSha256:sha(text),wholeOriginalRestored:true,...(runtimeReusePolicy===undefined?{}:{runtimeEntryAdmissionEnforced:!runtimeBoundaryOnly,runtimeBoundaryOnly}),
  scope:'Only the generated private fixture CreateMutexW boundary. Original SettingsFileGuard/local mutex, INFINITE named wait, release and CloseHandle code remain. Directory identity names share the same real mutex only for the same admitted private settings directory. Installed product source is unchanged.',
  productionMutexReached:false,...(lifecycleMutation!==undefined?{lifecycleMutation,originalLifecycleLabels:originalLifecycleRuntimeLabels(lifecycleMutation),exactOriginalModeRootsOnly:true}:{}),...(moduleMaintenance?{moduleMaintenanceNamespace:'same owned directory identity plus -ModuleMaintenance',moduleMaintenanceProductionNameReached:false}:{}),edits};
}

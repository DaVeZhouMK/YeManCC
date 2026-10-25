import assert from 'node:assert/strict';
import path from 'node:path';
import {createHash} from 'node:crypto';
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
export function privateSettingsMutexDefinitionForRuntimeRoot(runtimeRoot){
 if(runtimeRoot===undefined)return privateSettingsMutexDefinition;
 assert(typeof runtimeRoot==='string'&&path.isAbsolute(runtimeRoot)&&!/[\0\r\n]/.test(runtimeRoot),'ABSOLUTE_OWNED_RUNTIME_ROOT_REQUIRED');
 const canonical=path.resolve(runtimeRoot),base=path.resolve(import.meta.dirname,'../../../../../../Testing/YMCC/Evidence'),relative=path.relative(base,canonical);
 assert(relative&&!path.isAbsolute(relative)&&relative!=='..'&&!relative.startsWith('..'+path.sep),'RUNTIME_ROOT_OUTSIDE_CORE_EVIDENCE');
 const start=privateSettingsMutexDefinition.indexOf('(root.find(');
 const end=privateSettingsMutexDefinition.indexOf('))\n        throw',start);
 assert(start>=0&&end>start,'EXACT_LEGACY_RUNTIME_ADMISSION_REQUIRED');
 const old=privateSettingsMutexDefinition.slice(start,end+1);
 assert.equal(privateSettingsMutexDefinition.split(old).length,2,'EXACT_LEGACY_RUNTIME_ADMISSION_REQUIRED');
 return privateSettingsMutexDefinition.replace(old,'root!=normalizeWindowsPathForCompare(L'+JSON.stringify(canonical)+')');
}
export function isolateCurrentFixtureSettingsMutex(original,{runtimeRoot,moduleMaintenance=false}={}){
 assert(typeof moduleMaintenance==='boolean','BOOLEAN_MODULE_MAINTENANCE_BOUNDARY_REQUIRED');
 let boundary=privateSettingsMutexDefinitionForRuntimeRoot(runtimeRoot);
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
 const text=original.replace(before,after);
 assert.equal(text.split(after).length,2);assert.equal(text.replace(after,before),original);
 return {text,originalSha256:sha(original),isolatedSha256:sha(text),wholeOriginalRestored:true,
  scope:'Only the generated private fixture CreateMutexW boundary. Original SettingsFileGuard/local mutex, INFINITE named wait, release and CloseHandle code remain. Directory identity names share the same real mutex only for the same admitted private settings directory. Installed product source is unchanged.',
  productionMutexReached:false,...(moduleMaintenance?{moduleMaintenanceNamespace:'same owned directory identity plus -ModuleMaintenance',moduleMaintenanceProductionNameReached:false}:{}),edits:[{before,after}]};
}

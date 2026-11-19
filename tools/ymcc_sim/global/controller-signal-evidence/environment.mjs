import path from 'node:path';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import {createHash} from 'node:crypto';
// Required OS locations only. No inherited compiler, Node, Python or fixture
// override can choose another source, toolchain, import path or output folder.
const WINDOWS_INPUTS = Object.freeze(['SystemRoot', 'windir', 'SystemDrive', 'ComSpec', 'OS',
  'PROCESSOR_ARCHITECTURE', 'PROCESSOR_ARCHITEW6432', 'NUMBER_OF_PROCESSORS',
  'ProgramFiles', 'ProgramFiles(x86)', 'ProgramW6432', 'CommonProgramFiles',
  'CommonProgramFiles(x86)', 'CommonProgramW6432', 'ProgramData', 'LOCALAPPDATA',
  'APPDATA', 'USERPROFILE', 'USERNAME', 'HOMEDRIVE', 'HOMEPATH', 'PATHEXT']);
export function consumerEnvironment({ambient, tools, out, nodeExecutable}) {
  const primary = tools.primary;
  assert(primary?.git && primary?.node && primary?.compiler, 'PRIMARY_TOOLS_REQUIRED');
  assert.equal(path.resolve(nodeExecutable).toLowerCase(), path.resolve(primary.node).toLowerCase(), 'PRIMARY_NODE_PATH_MISMATCH');
  for (const [role, file] of Object.entries(primary)) {
    const row = tools.files.find(r => path.resolve(r.path).toLowerCase() === path.resolve(file).toLowerCase());
    assert(row && createHash('sha256').update(fs.readFileSync(file)).digest('hex') === row.sha256, 'PRIMARY_TOOL_HASH_MISMATCH:' + role);
  }
  const entries = new Map(Object.entries(ambient).map(([key, value]) => [key.toLowerCase(), value]));
  const env = {};
  for (const key of WINDOWS_INPUTS) if (entries.has(key.toLowerCase())) env[key] = entries.get(key.toLowerCase());
  const windows = env.SystemRoot || 'C:/Windows';
  return Object.assign(env, {
    TEMP: path.join(out, 'tool-temp'), TMP: path.join(out, 'tool-temp'),
    YMCC_MSI_RAW_OUT: path.join(out, 'consumers'), YMCC_PYTHON_PATH: primary.python,
    YMCC_GIT_PATH: primary.git, YMCC_NODE_PATH: primary.node, YMCC_CL_PATH: primary.compiler,
    PYTHONNOUSERSITE: '1', PYTHONIOENCODING: 'utf-8', PYTHONDONTWRITEBYTECODE: '1',
    VSCMD_SKIP_SENDTELEMETRY: '1',
    PATH: [...new Set([path.dirname(primary.node), path.dirname(primary.python), path.dirname(primary.git),
      path.dirname(primary.compiler), path.join(windows, 'System32'), windows])].join(';'),
  });
}
// Isolated environment for the offline managed host fixture; callers create
// the returned private directories. No ambient toolchain or runtime overrides.
export function managedFixtureEnvironment({ambient, dotnetRoot, out}) {
  const absoluteWindowsPath = value => typeof value === 'string' &&
    !/[\0;]/.test(value) && /^(?:[a-z]:[\\/]|[\\/]{2}[^\\/]+[\\/][^\\/]+(?:[\\/]|$))/i.test(value);
  assert(absoluteWindowsPath(dotnetRoot), 'MANAGED_DOTNET_ROOT_ABSOLUTE_REQUIRED');
  assert(absoluteWindowsPath(out), 'MANAGED_OUT_ABSOLUTE_REQUIRED');
  const entries = new Map(Object.entries(ambient).map(([key, value]) => [key.toLowerCase(), value]));
  const env = {};
  for (const key of WINDOWS_INPUTS) if (entries.has(key.toLowerCase())) env[key] = entries.get(key.toLowerCase());
  const windowsInput = [env.SystemRoot, env.windir].find(absoluteWindowsPath);
  assert(windowsInput, 'MANAGED_WINDOWS_ROOT_ABSOLUTE_REQUIRED');
  const windows = path.win32.normalize(windowsInput);
  const dotnet = path.win32.normalize(dotnetRoot);
  const managed = path.win32.join(out, '.managed');
  const home = path.win32.join(managed, 'home');
  const homeRoot = path.win32.parse(home).root;
  return Object.assign(env, {
    SystemRoot: windows, windir: windows, ComSpec: path.win32.join(windows, 'System32', 'cmd.exe'),
    PATH: [dotnet, path.win32.join(windows, 'System32'), windows].join(';'),
    PATHEXT: '.COM;.EXE;.BAT;.CMD',
    DOTNET_ROOT: dotnet, DOTNET_ROOT_X64: dotnet,
    DOTNET_HOST_PATH: path.win32.join(dotnet, 'dotnet.exe'),
    USERPROFILE: home, HOME: home, USERNAME: 'ymcc-fixture',
    HOMEDRIVE: homeRoot.replace(/[\\/]$/, ''), HOMEPATH: '\\' + home.slice(homeRoot.length),
    DOTNET_CLI_HOME: path.win32.join(managed, 'dotnet-home'),
    APPDATA: path.win32.join(managed, 'appdata', 'roaming'),
    LOCALAPPDATA: path.win32.join(managed, 'appdata', 'local'),
    TEMP: path.win32.join(managed, 'temp'), TMP: path.win32.join(managed, 'temp'),
    NUGET_PACKAGES: path.win32.join(managed, 'packages'),
    MSBuildUserExtensionsPath: path.win32.join(managed, 'msbuild-user-extensions'),
    DOTNET_MULTILEVEL_LOOKUP: '0', DOTNET_CLI_TELEMETRY_OPTOUT: '1',
    DOTNET_SKIP_FIRST_TIME_EXPERIENCE: '1', DOTNET_NOLOGO: '1',
    DOTNET_CLI_USE_MSBUILD_SERVER: '0', MSBUILDDISABLENODEREUSE: '1',
    DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE: 'true',
    DOTNET_GENERATE_ASPNET_CERTIFICATE: 'false', DOTNET_ADD_GLOBAL_TOOLS_TO_PATH: 'false',
    DOTNET_CLI_UI_LANGUAGE: 'en-US', MSBuildEnableWorkloadResolver: 'false',
  });
}

// Shared finite SDK lookup inputs. No process or resolver is executed here.
const managedLookupNeed=(value,message)=>{if(!value)throw Error("HOST_PROTOCOL_CACHE_"+message);};
const managedLookupKey=file=>path.resolve(file).toLowerCase();
const managedLookupSha=bytes=>createHash("sha256").update(bytes).digest("hex");
const managedLookupSame=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
const managedLookupInside=(root,file)=>{const rel=path.relative(root,file);return rel===""||(!path.isAbsolute(rel)&&rel!==".."&&!rel.startsWith(".."+path.sep));};
function managedFixtureToolchainSpecification(repoRoot){const file=path.join(repoRoot,"tools/ymcc_sim/global/inputhost-protocol/toolchain-inputs.json"),stat=fs.lstatSync(file);managedLookupNeed(stat.isFile()&&!stat.isSymbolicLink()&&managedLookupKey(fs.realpathSync(file))===managedLookupKey(file)&&stat.size<=16*1024*1024,"DECLARATION_NOT_REGULAR");return JSON.parse(fs.readFileSync(file,"utf8").replace(/^\uFEFF/,""));}
export function managedFixtureResolverInputs(spec){
    const sdk=path.join(spec.dotnetRoot,'sdk',spec.sdkVersion),base=path.join(sdk,'SdkResolvers');
    const names=fs.readdirSync(base,{withFileTypes:true}).map(entry=>{managedLookupNeed(entry.isDirectory()&&!entry.isSymbolicLink(),'UNREVIEWED_RESOLVER_ENTRY');return entry.name;}).sort();
    managedLookupNeed(managedLookupSame(names,['Microsoft.Build.NuGetSdkResolver','Microsoft.NET.Sdk.WorkloadMSBuildSdkResolver']),'UNREVIEWED_RESOLVER_MEMBERSHIP');
    const manifests=names.map(name=>{
        const file=path.join(base,name,name+'.xml'),bytes=fs.readFileSync(file),xml=bytes.toString('utf8');
        const matches=[...xml.matchAll(/<Path>\s*([^<>]+?)\s*<\/Path>/g)];managedLookupNeed(matches.length===1,'RESOLVER_XML_PATH_UNREVIEWED');
        const target=path.resolve(path.dirname(file),matches[0][1]);
        managedLookupNeed(managedLookupInside(sdk,target)&&spec.pinned.some(row=>managedLookupKey(row.path)===managedLookupKey(target)&&row.sha256===managedLookupSha(fs.readFileSync(target))),'RESOLVER_TARGET_UNPINNED');
        managedLookupNeed(!fs.lstatSync(file).isSymbolicLink(),'REDIRECTED_RESOLVER_MANIFEST');
        return {path:file,sha256:managedLookupSha(bytes)};
    });
    return {names,manifests};
}
export function managedFixtureLookupGuards(repoRoot,caseDir,{negativeGroup='negative-dispose-retention-omitted'}={}){
    managedLookupNeed(['negative-dispose-retention-omitted','negative-cancellation-omitted'].includes(negativeGroup),'LOOKUP_GROUP_UNREVIEWED');
    const spec=managedFixtureToolchainSpecification(repoRoot),resolver=managedFixtureResolverInputs(spec),sdk=path.join(spec.dotnetRoot,'sdk',spec.sdkVersion);
    const candidates=new Set([path.join(sdk,'MSBuild.rsp')]);
    for(const name of ['positive',negativeGroup]){
        const out=path.join(caseDir,'host-lifecycle',name);candidates.add(path.join(out,'MSBuild.rsp'));
        for(let dir=out;;dir=path.dirname(dir)){candidates.add(path.join(dir,'Directory.Build.rsp'));if(path.dirname(dir)===dir)break;}
    }
    const response=[...candidates].sort().map(file=>{managedLookupNeed(!fs.existsSync(file),'UNREVIEWED_AUTOMATIC_RESPONSE_FILE:'+file);return {path:file,state:'ABSENT'};});
    const base=path.join(sdk,'SdkResolvers'),fallback=resolver.names.map(name=>{
        const file=path.join(base,name,name+'.dll');managedLookupNeed(!fs.existsSync(file),'RESOLVER_FALLBACK_ELIGIBILITY_CHANGED');return {path:file,state:'ABSENT'};
    });
    const packs=fs.readdirSync(path.join(spec.dotnetRoot,'packs','Microsoft.NETCore.App.Ref'),{withFileTypes:true}).map(entry=>{
        managedLookupNeed(entry.isDirectory()&&!entry.isSymbolicLink(),'UNREVIEWED_TARGETING_PACK_ENTRY');return entry.name;
    }).sort();managedLookupNeed(managedLookupSame(packs,[spec.targetingPackVersion]),'TARGETING_PACK_MEMBERSHIP_CHANGED');
    return {schemaVersion:1,caseDir:path.resolve(caseDir),resolverDirectoryNames:resolver.names,
        resolverManifests:resolver.manifests,resolverFallback:fallback,targetingPackMembership:packs,automaticResponses:response};
}

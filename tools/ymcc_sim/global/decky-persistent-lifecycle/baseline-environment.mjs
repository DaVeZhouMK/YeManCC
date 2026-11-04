// Only declared baseline execution inputs are recorded; never dump the process environment.
export const BASELINE_ENVIRONMENT_KEYS=Object.freeze(['SystemRoot','ComSpec','VCToolsInstallDir','WindowsSdkDir','WindowsSDKVersion','YMCC_SIM_POWERSHELL','YMCC_SIM_RAW_PROGRAM_PWSH','YMCC_DECKY_ARTIFACT_ROOT','YMCC_RELEASE_HOSTED_ENVIRONMENT','GITHUB_ACTIONS','ImageOS','ImageVersion']);
export function baselineEnvironment(env=process.env){return Object.fromEntries(BASELINE_ENVIRONMENT_KEYS.map(name=>[name,env[name]??null]));}

// Only this declared subset can affect the repeated package fixture invocation.
export const PACKAGE_MAINTENANCE_ENVIRONMENT_KEYS=Object.freeze([...BASELINE_ENVIRONMENT_KEYS,'PSModulePath','PATH','TEMP','TMP']);
export function packageMaintenanceEnvironment(env=process.env){return Object.fromEntries(PACKAGE_MAINTENANCE_ENVIRONMENT_KEYS.map(name=>[name,env[name]??null]));}

// Explicit inputs of the shared resource producers and their native command overrides.
export const RESOURCE_FIXTURE_ENVIRONMENT_KEYS=Object.freeze([...PACKAGE_MAINTENANCE_ENVIRONMENT_KEYS,'YMCC_SIM_CORE_ROOT','INCLUDE','LIB','CL','_CL_','LINK','_LINK_','VSLANG']);
export function resourceFixtureEnvironment(env=process.env){const folded=new Map(Object.entries(env).map(([key,value])=>[key.toUpperCase(),value]));return Object.fromEntries(RESOURCE_FIXTURE_ENVIRONMENT_KEYS.map(name=>[name,folded.get(name.toUpperCase())??null]));}

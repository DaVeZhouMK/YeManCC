import{productInputs,root}from'./common.mjs';
import{lockMonitorToolScope,monitorToolArtifactInputs,MONITOR_TOOL_SOURCES}from'./tool-authority.mjs';
import{resolveTemplateArtifact}from'./template-artifact.mjs';
export const ownInputs=['tool-authority.mjs','tool-authority.test.mjs','retained-label-contract.mjs','retained-label-contract.test.mjs','template-artifact.mjs','template-artifact.test.mjs','template-artifact-pin.json','common.mjs','retained-runtime.mjs','retained.mjs','browser-fixture.ts','continuous-fixture.ts','memory-ports.mjs','browser.mjs','index.mjs','adapter.mjs','legacy-template-audit.json','preset-contract.mjs','migration-checks.mjs'];
export function getMonitorOutputAdapters(repoRoot=root,bindingOptions={}){let artifactInputs=[],blockedReason;try{const templateArtifact=resolveTemplateArtifact(repoRoot,bindingOptions);const toolScope=lockMonitorToolScope(repoRoot,{bindingOptions,templateArtifact});artifactInputs=monitorToolArtifactInputs(toolScope);}catch(error){blockedReason='MONITOR_DECLARED_INPUT_UNAVAILABLE: '+error.message;}return[{
 id:'sensor.monitor-output-signals',domain:'sensor',title:'Formal monitor output bridge, dedicated entry consumers, layout acknowledgements and native clock leases',
 kind:'node',path:'tools/ymcc_sim/global/monitor-output-signals/index.mjs',args:['--evidence-dir','{outDir}/monitor-output'],timeoutMs:180000,
 profiles:['desktop','full'],scope:'behavioral-mock',resultFiles:['monitor-output/result.json'],
 evidenceInputs:[...MONITOR_TOOL_SOURCES,...productInputs,'tools/monitor_hud_control_clock_selftest.mjs','tools/monitor_output_templates_selftest.mjs','src/components/MonitorOutputSettings.vue','docs/monitor-simple-default-preset-20261014.md','tools/fixtures/battery_ui_native_recovery/snapshot-node-binding.mjs','tools/ymcc_sim/global/snapshot-node-tools.mjs','tools/ymcc_sim/global/source-snapshot.mjs',...ownInputs.map(f=>'tools/ymcc_sim/global/monitor-output-signals/'+f)],
 artifactInputs,...(blockedReason?{blockedReason}:{}),
 referenceInputs:['src/bridge/monitorHudHost.ts','src/bridge/ipc.ts','src/bridge/settingsRepository.ts','src/bridge/deckySidebar.ts','src/bridge/steamSettings.ts','src/bridge/yeman.ts'],
 referenceMeaning:'Host final retained source-text assertion and injected IPC/settings/Steam/RTSS ports. The sole legacy template label expectation migration is source-contract guarded; its complete original behavior suite executes. Static OVL inspection remains source-contract. No entire module behavior is inferred from source references.',
 boundary:'Actual product entry + Vue renderer + bridge/owner/clock/layout; injected native receipts/settings and owned loopback browser pages. No actual Steam/Decky, sensors, App/Host, desktop window control, OS input or performance stress.',
 }];}


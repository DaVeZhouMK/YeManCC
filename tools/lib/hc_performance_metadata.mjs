import fs from 'node:fs';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
export const pinnedPerformanceFactsFile='capabilities/upstream/f2e58468/performance-source-facts.json';
export const pinnedPerformanceFactsSha256='ba10865993062679d63f659fcab86c0d9b5664882ee2e1e9a1e0db3109e9cd3c';
const pinnedCommit='f2e58468d9917189d4be7ccfc242385ce2c3035b';
const pinnedFactsPath=fileURLToPath(new URL('../../'+pinnedPerformanceFactsFile,import.meta.url));
export function readPinnedPerformanceFacts(bytes=fs.readFileSync(pinnedFactsPath)) {
    if(createHash('sha256').update(bytes).digest('hex')!==pinnedPerformanceFactsSha256)
        throw Error('PINNED_PERFORMANCE_FACTS_IDENTITY');
    const facts=JSON.parse(bytes.toString('utf8'));
    if(facts.schemaVersion!==1||facts.mode!=='fixed-upstream-source-facts-only'||facts.commit!==pinnedCommit||
        facts.repository!=='Valkirie/HandheldCompanion'||facts.devices.length!==102||facts.sourceFiles.length!==104||
        facts.runtimeAvailabilityAuthority!==false||facts.hardwareAcceptance!==false||
        new Set(facts.devices.map(d=>d.deviceId)).size!==102||new Set(facts.sourceFiles.map(f=>f.path)).size!==104)
        throw Error('PINNED_PERFORMANCE_FACTS_SCHEMA');
    return facts;
}

// Build-time reference data only. No generated C++/C# entry, runtime matcher or IO provider.
// Factory contexts come from the existing Roslyn extractor; never turn a declaration into admission.
const capabilities = ['OEMCPU', 'OEMGPU'];
const parameterFields = new Set(['nTDP', 'cTDP', 'GfxClock', 'CpuClock']);
const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);
const flags = device => Object.fromEntries(['minimum', 'possible'].map(level =>
    [level, device.declared.Capabilities[level].filter(flag => capabilities.includes(flag))]));
const parameters = device => device.parameters.filter(p => parameterFields.has(p.field));

export function buildPerformanceMetadata({catalog, inventory}) {
    const factory = catalog.devices.find(d => d.name === 'IDevice');
    if (!factory || !Array.isArray(inventory.identities) || inventory.issues.length)
        throw Error('PERFORMANCE_FACTORY_SYNTAX_REQUIRED');
    const byName = new Map(catalog.devices.map(d => [d.name, d]));
    for (const route of inventory.identities) {
        if (!byName.has(route.deviceClass) || !Number.isInteger(route.line) || !Array.isArray(route.switches)
            || typeof route.context !== 'string' || !Array.isArray(route.conditions))
            throw Error('PERFORMANCE_FACTORY_CONTEXT_INVALID');
    }
    const rows = catalog.devices.map(device => {
        // Keep complete manufacturer/product/version/CPU context and original ternaries/defaults.
        // Equal Processor labels are evidence names, not an exhaustive list of supported CPUs.
        const factoryContexts = inventory.identities.filter(r => r.deviceClass === device.name);
        const processorSections = factoryContexts.flatMap(r => r.switches.filter(s => s.field === 'Processor'));
        const exactSourceNames = [...new Set(processorSections.flatMap(s => s.values))];
        const hasCpuCondition = factoryContexts.some(r => r.conditions.some(s => /\bProcessor\b/.test(s)));
        return {
            deviceId: device.deviceId, modelClass: device.name, family: device.family,
            factoryInstantiable: device.factoryInstantiable,
            source: {file: device.sourcePath, sha256: device.sourceSha256},
            inheritance: device.inheritance,
            cpuIdentity: {
                exactSourceNames,
                evidence: exactSourceNames.length ? 'frozen-factory-processor-labels'
                    : hasCpuCondition ? 'frozen-factory-condition-only' : 'cpu-name-unspecified-in-frozen-factory',
                hasProcessorDefault: processorSections.some(s => s.hasDefault),
                exhaustiveSupportedCpuList: false,
                factoryContexts,
            },
            gpuIdentity: {adapterName: null, evidence: 'not-inventoried; processor-marketing-name-is-not-adapter-identity'},
            frozenOemDeclarations: flags(device),
            localSourceLimits: parameters(device),
            limitsMeaning: 'local-frozen-declarations-only; follow-inheritance-by-reference; not-current-YMCC-limits',
        };
    });
    const metadata = {
        schemaVersion: 1,
        mode: 'reference-metadata-only',
        sourceBaseline: catalog.sourceBaseline,
        sourceProjectVersion: catalog.sourceProjectVersion,
        sourceFilesIndexSha256: catalog.sourceFilesIndexSha256,
        factorySource: {file: factory.sourcePath, sha256: factory.sourceSha256, method: 'GetCurrent',
            extraction: 'existing-PcCapabilityAudit-Roslyn-identities; original-contexts-retained'},
        runtimeConsumers: [],
        runtimeAvailability: 'not-established-by-registration',
        cpu: {identityRegistrationOnly: true, addHcCalls: false, replaceCurrentConsumers: false},
        gpu: {identityRegistrationOnly: true, featureImplementedByThisRegistry: false, enableFromOemFlag: false},
        tdp: {route: 'existing-independent-consumers', takeOverDispatch: false, applySourceLimits: false,
            ...(catalog.domains?.tdp ? {catalogBinding: 'hc-device-catalog.json#/domains/tdp', sharedCapabilityPolicy: 'uxtu-tdp-policy.json'} : {})},
        unknownIdentity: 'keep-unknown; no-model-or-cpu-inference-from-flags-or-class-name',
        scope: {
            registeredSourceClasses: rows.length,
            factoryInstantiableClasses: rows.filter(r => r.factoryInstantiable).length,
            classesWithExactCpuNameEvidence: rows.filter(r => r.cpuIdentity.exactSourceNames.length).length,
            classesWithCpuConditionOnly: rows.filter(r => r.cpuIdentity.evidence === 'frozen-factory-condition-only').length,
            classesWithoutCpuNameEvidence: rows.filter(r => r.cpuIdentity.evidence === 'cpu-name-unspecified-in-frozen-factory').length,
            classesWithPossibleOemCpuDeclaration: rows.filter(r => r.frozenOemDeclarations.possible.includes('OEMCPU')).length,
            classesWithPossibleOemGpuDeclaration: rows.filter(r => r.frozenOemDeclarations.possible.includes('OEMGPU')).length,
        },
        rows,
        pinnedUpstream: buildPinnedPerformanceMetadata(),
    };
    const errors = checkPerformanceMetadata({metadata, catalog});
    if (errors.length) throw Error(errors.join(';'));
    return metadata;
}

export function checkPerformanceMetadata({metadata: m, catalog}) {
    const errors = [], need = (ok, code) => { if (!ok) errors.push(code); };
    need(m?.schemaVersion === 1 && m.mode === 'reference-metadata-only'
        && same(m.runtimeConsumers, []) && m.runtimeAvailability === 'not-established-by-registration',
        'PERFORMANCE_METADATA_RUNTIME_BOUNDARY');
    need(m?.cpu?.identityRegistrationOnly === true && m.cpu.addHcCalls === false && m.cpu.replaceCurrentConsumers === false,
        'PERFORMANCE_METADATA_CPU_BOUNDARY');
    need(m?.gpu?.identityRegistrationOnly === true && m.gpu.featureImplementedByThisRegistry === false && m.gpu.enableFromOemFlag === false,
        'PERFORMANCE_METADATA_GPU_BOUNDARY');
    need(m?.tdp?.route === 'existing-independent-consumers' && m.tdp.takeOverDispatch === false && m.tdp.applySourceLimits === false,
        'PERFORMANCE_METADATA_TDP_BOUNDARY');
    const factory = catalog.devices.find(d => d.name === 'IDevice');
    need(m?.sourceBaseline === catalog.sourceBaseline && m.sourceProjectVersion === catalog.sourceProjectVersion
        && m.sourceFilesIndexSha256 === catalog.sourceFilesIndexSha256 && m.factorySource?.file === factory?.sourcePath
        && m.factorySource?.sha256 === factory?.sourceSha256 && m.factorySource?.method === 'GetCurrent',
        'PERFORMANCE_METADATA_SOURCE_LINEAGE');
    const rows = m?.rows ?? [], byId = new Map(rows.map(r => [r.deviceId, r]));
    need(rows.length === catalog.devices.length && byId.size === rows.length
        && m?.scope?.registeredSourceClasses === catalog.devices.length,
        'PERFORMANCE_METADATA_MODEL_COVERAGE');
    for (const d of catalog.devices) {
        const r = byId.get(d.deviceId);
        need(r?.modelClass === d.name && r?.family === d.family && r?.factoryInstantiable === d.factoryInstantiable
            && r?.source?.file === d.sourcePath && r?.source?.sha256 === d.sourceSha256
            && same(r?.inheritance, d.inheritance) && same(r?.frozenOemDeclarations, flags(d))
            && same(r?.localSourceLimits, parameters(d)), 'PERFORMANCE_METADATA_MODEL_SOURCE:' + d.deviceId);
        const cpu = r?.cpuIdentity;
        const contexts = cpu?.factoryContexts;
        need(Array.isArray(cpu?.exactSourceNames) && cpu.exhaustiveSupportedCpuList === false
            && Array.isArray(contexts) && contexts.every(c => c.deviceClass === d.name && Number.isInteger(c.line)
                && typeof c.context === 'string' && Array.isArray(c.switches) && Array.isArray(c.conditions))
            && same(cpu.exactSourceNames, [...new Set((contexts ?? []).flatMap(c => c.switches.filter(s => s.field === 'Processor').flatMap(s => s.values)))]),
            'PERFORMANCE_METADATA_CPU_CONTEXT:' + d.deviceId);
        need(r?.gpuIdentity?.adapterName === null, 'PERFORMANCE_METADATA_UNVERIFIED_GPU:' + d.deviceId);
    }
    errors.push(...checkPinnedPerformanceMetadata(m?.pinnedUpstream));
    return errors;
}

// The frozen identity/consumer root stays unchanged. This is the separately pinned
// source layer for all 102 classes, including factory CPU conditions and defaults.
export function buildPinnedPerformanceMetadata(facts=readPinnedPerformanceFacts()) {
    const factory=facts.devices.find(d=>d.name==='IDevice');
    const rows=facts.devices.map(device=>{
        const contexts=facts.identities.filter(c=>c.deviceClass===device.name);
        const processors=contexts.flatMap(c=>c.switches.filter(s=>s.field==='Processor'));
        const names=[...new Set(processors.flatMap(s=>s.values))];
        const conditions=contexts.some(c=>c.conditions.some(s=>/\bProcessor\b/.test(s)));
        return {deviceId:device.deviceId,modelClass:device.name,family:device.family,
            factoryInstantiable:device.factoryInstantiable,source:{file:device.sourcePath,sha256:device.sourceSha256},
            inheritance:device.inheritance,cpuIdentity:{exactSourceNames:names,
                evidence:names.length?'pinned-upstream-factory-processor-labels':conditions?'pinned-upstream-factory-condition-only':'cpu-name-unspecified-in-pinned-upstream-factory',
                hasProcessorDefault:processors.some(s=>s.hasDefault),exhaustiveSupportedCpuList:false,factoryContexts:contexts},
            gpuIdentity:{adapterName:null,evidence:'not-inventoried; processor-marketing-name-is-not-adapter-identity'},
            upstreamOemDeclarations:flags(device),localSourceLimits:parameters(device),
            limitsMeaning:'fixed-upstream-declarations-only; follow-inheritance-by-reference; not-current-YMCC-limits'};
    });
    return {schemaVersion:1,mode:'reference-metadata-only',repository:facts.repository,commit:facts.commit,
        sourcePinSha256:facts.sourcePinSha256,factsInput:{file:pinnedPerformanceFactsFile,sha256:pinnedPerformanceFactsSha256},
        sourceFiles:facts.sourceFiles,factorySource:{file:factory.sourcePath,sha256:factory.sourceSha256,method:'GetCurrent'},
        scope:{registeredSourceClasses:rows.length,factoryInstantiableClasses:rows.filter(r=>r.factoryInstantiable).length,
            factoryContexts:facts.identities.length,sourceFiles:facts.sourceFiles.length,
            classesWithExactCpuNameEvidence:rows.filter(r=>r.cpuIdentity.exactSourceNames.length).length},
        runtimeConsumers:[],runtimeAvailability:'not-established-by-registration',hardwareAcceptance:false,
        cpu:{identityRegistrationOnly:true,addHcCalls:false,replaceCurrentConsumers:false},
        gpu:{identityRegistrationOnly:true,featureImplementedByThisRegistry:false,enableFromOemFlag:false},
        tdp:{route:'existing-independent-consumers',takeOverDispatch:false,applySourceLimits:false},
        unknownIdentity:'keep-unknown; no-model-or-cpu-inference-from-flags-or-class-name',rows};
}
export function checkPinnedPerformanceMetadata(m) {
    const errors=[],need=(ok,code)=>{if(!ok)errors.push(code);};let expected;
    try{expected=buildPinnedPerformanceMetadata();}catch(error){return [error.message];}
    need(m?.schemaVersion===1&&m.repository===expected.repository&&m.commit===expected.commit&&m.sourcePinSha256===expected.sourcePinSha256,
        'PINNED_UPSTREAM_SOURCE_LINEAGE');
    need(m?.mode==='reference-metadata-only'&&same(m.runtimeConsumers,[])&&m.runtimeAvailability==='not-established-by-registration'&&m.hardwareAcceptance===false,
        'PINNED_UPSTREAM_RUNTIME_BOUNDARY');
    need(same(m?.cpu,expected.cpu),'PINNED_UPSTREAM_CPU_BOUNDARY');
    need(same(m?.gpu,expected.gpu),'PINNED_UPSTREAM_GPU_BOUNDARY');
    need(same(m?.tdp,expected.tdp),'PINNED_UPSTREAM_TDP_BOUNDARY');
    need(same(m?.factsInput,expected.factsInput),'PINNED_UPSTREAM_FACTS_IDENTITY');
    const rows=m?.rows??[];
    need(rows.length===102&&new Set(rows.map(r=>r.deviceId)).size===102&&same(m?.scope,expected.scope),'PINNED_UPSTREAM_MODEL_COVERAGE');
    need(same(m?.sourceFiles,expected.sourceFiles)&&same(m?.factorySource,expected.factorySource),'PINNED_UPSTREAM_SOURCE_FILES');
    need(same(rows.map(r=>r.cpuIdentity),expected.rows.map(r=>r.cpuIdentity)),'PINNED_UPSTREAM_FACTORY_CONTEXT');
    need(same(rows,expected.rows)&&m?.unknownIdentity===expected.unknownIdentity,'PINNED_UPSTREAM_SOURCE_DECLARATIONS');
    return errors;
}

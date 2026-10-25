/** Fixed semantic faults from the reviewed original motion consumer suite. */
export const EXPECTED_MUTATIONS = Object.freeze([
 'catalog-only-no-entry','catalog-only-no-bound','resolver-trusts-forged-identity',
 'cache-wrong-source-sha','cache-family-unchecked','cache-identity-gyro-unchecked','new-model-asus-cache-shortcut',
 'pair-old-current-generation','pair-zero-generation','pair-mixed-generations','pair-mixed-provider','pair-gyro-without-accel','pair-disabled-or-suspended',
 'actual-consumer-bypasses-pair','accel-uses-gyro-matrix','receipt-reports-unbound-source','bound-does-registry-io',
 'activation-drops-accel-z','gamepadmotion-bypasses-admission','catalog-resource-sha-wrong',
 'capability-evidence-ignores-binding','capability-deletes-windows-consumer','capability-observation-promotes-controller',
 'legacy-cache-processor-unchecked','legacy-cache-version-unchecked','legacy-crosspair-lookup','legacy-controller-windows-admitted',
 'catalog-steammachine-added','generic-claims-exact-machine','generic-cache-product-unchecked','generic-cache-source-digest-unchecked',
 'private-total-binding-missing','private-original-reference-unchecked','private-source-digest-unchecked','private-current-model-unchecked',
 'reviewed-activation-omitted','reviewed-processor-unchecked','reviewed-canonical-gyro-unchecked','reviewed-provenance-static-ids',
]);
export const CONFIGURATIONS = Object.freeze(['Release','Checked']);
export const SOURCE_FINISH_FLAGS = Object.freeze([
 'extractedDefinitionsUnchanged','legacyCatalogUnchanged','legacyProvenanceUnchanged','legacyPinnedSourcesUnchanged',
 'capabilityBeforeUnchanged','historicalUnchanged','catalogUnchanged','msiUnchanged','beforeUnchanged','fixturesUnchanged','runnerUnchanged',
]);
export const ORIGINAL_CONSUMER_ASSERTIONS = 114560;
export const DEFAULT_TRANSITION_ADDITIONAL_ASSERTIONS = 4;
// This specialist compiled suite is outside the ordinary 60-second JS adapter lane.
export const PRODUCER_TIMEOUT_MS = 840000;
export const ADAPTER_TIMEOUT_MS = 900000;
export const needsCapabilityGrid = name => /^(?:generic|capability|private)-/.test(name);
export const validHash = value => typeof value==='string' && /^[a-f0-9]{64}$/i.test(value);
export const validCount = value => Number.isSafeInteger(value) && value >= 0;

// An existing dual-configuration fault report is 34,346,160 bytes.
export const MAX_RAW_REPORT_JSON_BYTES = 64 * 1024 * 1024;

export const MAX_COMPILER_INPUT_PATH_CHARS = 248;

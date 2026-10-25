#pragma once
#include "oem_input_contract.h"
#include "json.hpp"
namespace ymcc::oem::gamepad {
inline nlohmann::json chainReceipt(const InputContract& c,const ChainProofs& p,uint64_t now) {
    const auto result=evaluate(c,p,now);
    const auto signal=assessSignal(c,p,now);
    const auto route=[](const PhysicalRoute& r){return nlohmann::json{
        {"vendor",int(r.vendor)},{"primaryProtocol",int(r.protocol)},{"auxiliaryProtocols",r.auxiliaryProtocols},{"adapterRoute",r.id}};};
    return {{"event","oem-input-contract-evidence"},{"contractVersion",contractVersion},
        {"receiptTime",now},{"processNonce",c.owner.processNonce},{"session",c.owner.session},
        {"hostEpoch",c.owner.hostEpoch},{"powerGeneration",c.owner.powerGeneration},
        {"intentRevision",c.owner.intentRevision},{"machineVendor",int(c.identity.machineVendor)},
        {"frontend",int(c.identity.frontend)},{"capabilityBinding",c.capability.binding},
        {"stage",stageName(result.stage)},{"signalStage",stageName(signal.stage)},
        {"policyCapabilityVerified",c.capability.policyCapabilityVerified},
        {"firmwareModeCapabilityVerified",c.capability.firmwareModeCapabilityVerified},
        {"captureCapabilityVerified",c.capability.captureCapabilityVerified},
        {"signalCaptureVerified",signal.captureVerified},{"signalSubmitVerified",signal.submitVerified},
        {"signalExternalReadProven",signal.externalReadProven},{"signalGameConsumptionProven",signal.gameConsumptionProven},{"requestedRoute",route(c.route)},
        {"observedRoute",route(p.mode.observedRoute)},{"modeWitness",int(p.mode.witness)},{"observedHardwareState",int(p.mode.state)},{"transitionOrigin",int(p.mode.origin)},
        {"physicalSource",c.physicalSource},{"sourceGeneration",c.sourceGeneration},
        {"virtualTarget",c.virtualTarget},{"virtualTargetEpoch",c.virtualTargetEpoch},
        {"persona",c.persona},{"policyTick",p.policy.tick},{"modeTick",p.mode.tick},{"captureTick",p.capture.tick},
        {"isolationTick",p.isolation.tick},{"submitTick",p.submit.tick},{"requiresUserAction",result.requiresUserAction},
        {"captureSequence",p.capture.sampleSequence},
        {"submittedSourceSequence",p.submit.sourceSampleSequence},{"reportSequence",p.submit.reportSequence},
        {"policyVerified",result.policyVerified},{"hardwareModeWitnessed",result.hardwareModeWitnessed},
        {"captureVerified",result.captureVerified},{"hostRunId",c.hostRunId},{"canonicalSourceBound",p.capture.canonicalSourceBound},
        {"availableInputFields",p.capture.availableFields},{"requiredInputFields",c.requiredFields},
        {"capturedInputHash",p.capture.normalizedInputHash},{"submittedSourceInputHash",p.submit.sourceNormalizedInputHash},
        {"submittedFrameHash",p.submit.submittedFrameHash},{"frameBuilt",p.submit.frameBuilt},
        {"backendSubmitted",p.submit.backendSubmitted},{"backendReceiptVerified",p.submit.backendReceiptVerified},{"isolationRequired",c.isolationRequired},
        {"isolationVerified",result.isolationVerified},{"submitVerified",result.submitVerified},
        {"mappedStimulusExpected",p.submit.mappedStimulusExpected},{"isolationScope","declared-physical-source-only"},
        {"nonNeutralCorrelated",result.nonNeutralCorrelated},{"gameConsumptionProven",result.gameConsumptionProven},{"externalReadProven",result.externalReadProven},
        {"lastingStabilityProven",false},{"humanGestureProven",false},
        {"oemWriterAttributed",matches(c,p.mode.token)&&p.mode.physicalIdentityVerified&&
            p.mode.witness!=ModeWitness::Unknown&&fresh(p.mode.tick,now,2000)&&
            (p.mode.origin==TransitionOrigin::OemAutoProven||p.mode.origin==TransitionOrigin::OemUiProven)},{"modeWriteIssued",false},{"virtualTargetRebuildRequested",false},
        {"scope","semantic evidence only; no hardware/config/virtual-target control"}};
}
} // namespace ymcc::oem::gamepad






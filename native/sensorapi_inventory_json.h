#pragma once
#include <map>
#include "sensorapi_inventory.h"

namespace ymcc::sensors::sensorapi_inventory {
// Rendering is independent of COM and the worker scheduler. GUID identity,
// physical provider path and advertised field support remain separate evidence.
template<class Json,class ProviderKey,class Utf8>
Json snapshotJson(Census::View view,const Ticket& current,const std::string& deviceClass,
    std::uint64_t started,std::uint64_t finished,bool stable,ProviderKey providerKey,Utf8 utf8) {
    auto& cached=view.observation;
    if(!stable&&cached.metadataContextCurrent){cached.metadataContextCurrent=false;cached.reason="query-context-changed";}
    Json body=nullptr,groups=nullptr;
    if(cached.lastObservation){
        const auto& result=*cached.lastObservation;
        Json entries=Json::array();std::map<std::string,unsigned> ids;
        for(const auto& entry:result.entries)if(SUCCEEDED(entry.idError))++ids[msi_sensor::guidString(entry.descriptor.id)];
        struct Group {unsigned gyros=0,accels=0;bool duplicate=false;};std::map<std::string,Group> providers;
        unsigned gyros=0,accels=0,unknown=0;
        for(const auto& entry:result.entries){
            const auto& d=entry.descriptor;const bool idKnown=SUCCEEDED(entry.idError),typeKnown=SUCCEEDED(entry.typeError),fieldsKnown=SUCCEEDED(entry.fieldsError);
            const std::string id=idKnown?"sensorapi:"+msi_sensor::guidString(d.id):std::string{};
            const auto path=SUCCEEDED(entry.pathError)&&!entry.pathTruncated?utf8(d.devicePath):std::string{};
            const auto provider=path.empty()?std::string{}:providerKey(path);const bool duplicate=idKnown&&ids[msi_sensor::guidString(d.id)]>1;
            gyros+=entry.gyro();accels+=entry.accel();if(!idKnown||!typeKnown||!fieldsKnown)++unknown;
            if(!provider.empty()){auto& group=providers[provider];group.gyros+=entry.gyro();group.accels+=entry.accel();group.duplicate=group.duplicate||duplicate;}
            entries.push_back({{"sourceId",idKnown?Json(id):Json(nullptr)},{"sensorGuid",idKnown?Json(msi_sensor::guidString(d.id)):Json(nullptr)},
                {"sensorTypeGuid",typeKnown?Json(msi_sensor::guidString(d.type)):Json(nullptr)},{"name",SUCCEEDED(entry.nameError)?Json(utf8(d.name)):Json(nullptr)},
                {"devicePath",SUCCEEDED(entry.pathError)?Json(utf8(d.devicePath)):Json(nullptr)},{"providerKey",provider.empty()?Json(nullptr):Json(provider)},
                {"api","sensorapi"},{"duplicateGuid",duplicate},{"nameTruncated",entry.nameTruncated},{"devicePathTruncated",entry.pathTruncated},
                {"declaredGyroType",typeKnown?Json(IsEqualGUID(d.type,msi_sensor::kGyroType)!=0):Json(nullptr)},
                {"declaredAccelType",typeKnown?Json(IsEqualGUID(d.type,msi_sensor::kAccelType)!=0):Json(nullptr)},
                {"standardGyroFieldsAdvertised",d.documentedGyroFields?Json(true):fieldsKnown?Json(false):Json(nullptr)},
                {"standardAccelFieldsAdvertised",d.documentedAccelFields?Json(true):fieldsKnown?Json(false):Json(nullptr)},
                {"observedGyroCandidate",entry.gyro()},{"observedAccelCandidate",entry.accel()},
                {"sensorState",SUCCEEDED(d.stateResult)?Json(static_cast<int>(d.state)):Json(nullptr)},
                {"reportedIntervalMs",SUCCEEDED(entry.intervalError)?Json(d.reportIntervalMs):Json(nullptr)},
                {"metadataErrors",{{"id",static_cast<std::int64_t>(entry.idError)},{"type",static_cast<std::int64_t>(entry.typeError)},
                    {"name",static_cast<std::int64_t>(entry.nameError)},{"devicePath",static_cast<std::int64_t>(entry.pathError)},
                    {"state",static_cast<std::int64_t>(d.stateResult)},{"interval",static_cast<std::int64_t>(entry.intervalError)},{"fields",static_cast<std::int64_t>(entry.fieldsError)}}},
                {"sampleReadabilityProven",false},{"actualStandardLayoutDecoded",false},{"physicalAxesVerified",false}});
        }
        Json providerRows=Json::array();for(const auto& [key,group]:providers)providerRows.push_back({{"providerKey",key},
            {"observedGyroCandidates",group.gyros},{"observedAccelCandidates",group.accels},{"duplicateIds",group.duplicate},
            {"physicalPairProven",false},{"automaticSelectionAllowed",false}});
        const bool complete=result.receipt.state==inventory::State::Complete;
        body={{"status",inventory::stateName(result.receipt.state)},{"phase",result.phase},{"hresult",static_cast<std::int64_t>(result.error)},
            {"reportedTotal",result.reportedTotal?Json(*result.reportedTotal):Json(nullptr)},
            {"visitedEntries",result.visited},{"enumeratedObjects",result.entries.size()},{"unavailableEntries",result.unavailableEntries},
            {"enumerationComplete",complete},{"truncated",result.truncated},{"candidateLimit",256},
            {"observedGyroCandidates",gyros},{"observedAccelCandidates",accels},{"entriesWithUnknownKindEvidence",unknown},
            {"observedCandidateCountsAreLowerBounds",!complete||unknown!=0},{"startedTick",result.receipt.started},
            {"finishedTick",result.receipt.finished},{"budgetMs",result.receipt.budget},{"bindingCurrentAtCompletion",result.receipt.sourceCurrent},
            {"candidates",entries},{"providers",providerRows}};
        if(cached.metadataContextCurrent&&complete)groups=providerRows;
    }
    return {{"schemaVersion",1},{"scope","windows-sensorapi-metadata"},{"readOnly",true},{"queryDoesNotEnumerate",true},
        {"queryDoesNotStartSampling",true},{"deviceClass",deviceClass},{"observedAtUptimeMs",started},{"completedAtUptimeMs",finished},
        {"contextStableAtCheck",stable},{"metadataContextCurrent",cached.metadataContextCurrent},{"reason",cached.reason},
        {"recorded",cached.lastObservation.has_value()},{"observationSerial",cached.serial},{"observationState",inventory::stateName(cached.state)},
        {"observationAgeMs",cached.age?Json(*cached.age):Json(nullptr)},{"observationBindingGeneration",cached.generation},
        {"observationPowerGeneration",cached.power},{"queryBindingGeneration",current.binding.bindingGeneration},{"queryPowerGeneration",current.power},
        {"lastObservation",body},{"candidateGroupsFromCurrentContext",groups},
        {"worker",{{"running",view.workerRunning},{"retireRequested",view.workerRetireRequested},{"overBudget",view.workerOverBudget},
            {"pendingCurrentContext",view.pendingCurrentContext},{"bindingGeneration",view.workerBindingGeneration},{"powerGeneration",view.workerPowerGeneration}}},
        {"metadataAvailabilityCurrentProven",false},{"sensorObjectsOpenedByQuery",0},{"selectionWritten",false},{"samplesPublished",false},
        {"sampleFreshnessProven",false},{"physicalAxesVerified",false},{"hostSubmissionProven",false},{"gameConsumptionProven",false},
        {"automaticSelectionAllowed",false},{"newProductionDlls",Json::array()}};
}
} // namespace ymcc::sensors::sensorapi_inventory

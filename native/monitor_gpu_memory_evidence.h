#pragma once
// Pure diagnostics of rows consumed by the existing unified HWiNFO pass.
// Reports raw/effective values; never acquires capacity or changes readings.
#include "monitor_gpu_selection.h"
#include "../deps/json/json.hpp"
namespace ymcc::monitordetail {
inline nlohmann::json gpuMemoryEvidence(const monitorgpu::Device* gpu,bool healthy,const monitorgpu::Readings& readings) {
    using json=nlohmann::json;
    json result=json::object();
    result["schemaVersion"]=1;result["source"]="existing-hwinfo-row-pass";
    result["selected"]=gpu!=nullptr;result["fresh"]=healthy&&gpu!=nullptr;
    result["capacityScope"]="same-selected-sensor-group";result["capacityQueried"]=false;
    if(!gpu)return result;
    const auto amount=[](double v)->json{return monitormemory::amount(v)?json(v):json(nullptr);};
    const auto percent=[](double v)->json{return monitormemory::percent(v)?json(v):json(nullptr);};
    const auto normalized=monitormemory::normalize(gpu->memory);
    bool captured=false;
    const auto d3d=readings.diagnosticD3D(gpu,&captured);
    result["d3dEvidenceCaptured"]=captured;
    result["sensorIndex"]=gpu->sensorIndex;
    result["sensorId"]=gpu->key.id;result["sensorInstance"]=gpu->key.instance;
    result["sensorName"]=monitorTextFieldView(gpu->key.name.data(),gpu->key.name.size());
    result["usedReading"]=gpu->memoryUsedRank==2?json("GPU Memory Allocated"):gpu->memoryUsedRank==1?json("GPU Memory Usage"):json(nullptr);
    result["usedUnit"]=gpu->memoryUsedRank>0?json("MiB-normalized"):json(nullptr);
    result["rawUsedMb"]=amount(gpu->memory.used);
    result["rawAvailableMb"]=amount(gpu->memory.available);
    result["rawTotalMb"]=amount(gpu->memory.total);
    result["rawLoadPct"]=percent(gpu->memory.load);
    result["d3dDedicatedUsedMb"]=amount(d3d.dedicated);
    result["d3dDynamicUsedMb"]=amount(d3d.dynamic);
    result["normalizedTotalMb"]=amount(normalized.total);
    result["normalizedLoadPct"]=percent(normalized.load);
    const auto effective=readings.memoryFor(gpu);
    result["effectiveUsedMb"]=amount(effective.value.used);
    result["effectiveTotalMb"]=amount(effective.value.total);
    result["effectiveLoadPct"]=percent(effective.value.load);
    result["effectiveSource"]=effective.source;
    // Status describes the published tuple; raw counters above stay diagnostic.
    result["status"]=effective.value.total>0&&effective.value.used>=0?"capacity-and-used":
        effective.value.used>=0?"used-without-capacity":effective.value.load>=0?"load-without-capacity":"no-coherent-memory-tuple";
    return result;
}
}

#pragma once
#include <cstdint>
#include <array>

// Evidence from the EXISTING one-shot MSI SD preparation call only. No I/O,
// no retry admission, no mode/firmware policy and no source verification.
namespace ymcc::msi::rearSdPreparation {
struct CommandWriteReceipt {
    bool apiAccepted = false;
    uint32_t written = 0, error = 0;
};
struct Receipt {
    const char* outcome = "not-called";
    const char* capabilityId="";const char* modelId="";const char* catalogSha256="";
    bool returned = false, hardwareWriteIssued = false;
    uint32_t enumerated = 0, detailFailures = 0, openFailures = 0;
    uint32_t attributeFailures = 0, capabilityFailures = 0;
    uint32_t vendorMatches = 0, commandMatches = 0, writeSets = 0;
    uint32_t enumerationEndError = 0, lastOpenError = 0;
    uint16_t productId = 0, firmware = 0, usagePage = 0, usage = 0;
    bool m1 = false, m2 = false, sync = false;
    uint16_t outputReportByteLength = 0;
    uint8_t wireReportId = 0;
    std::array<CommandWriteReceipt,3> commands{};
};
template<class Json> Json fields(const Receipt& r) {
    auto write = [](const CommandWriteReceipt& w) -> Json {
        return {{"apiAccepted",w.apiAccepted},{"written",w.written},{"error",w.error}};
    };
    return {{"schema",2},{"capabilityId",r.capabilityId},{"modelId",r.modelId},{"catalogSha256",r.catalogSha256},{"outcome",r.outcome},{"returned",r.returned},
        {"enumerated",r.enumerated},{"detailFailures",r.detailFailures},
        {"openFailures",r.openFailures},{"lastOpenError",r.lastOpenError},
        {"attributeFailures",r.attributeFailures},{"capabilityFailures",r.capabilityFailures},
        {"vendorMatches",r.vendorMatches},{"commandMatches",r.commandMatches},
        {"enumerationEndError",r.enumerationEndError},{"productId",r.productId},
        {"firmware",r.firmware},{"usagePage",r.usagePage},{"usage",r.usage},
        {"hardwareWriteIssued",r.hardwareWriteIssued},{"writeSets",r.writeSets},
        {"outputReportByteLength",r.outputReportByteLength},{"wireReportId",r.wireReportId},
        {"payloadRoute","dinput-buttons"},{"reportBodyOffset",0},
        {"lastCommandWrites",{{"m1",write(r.commands[0])},{"m2",write(r.commands[1])},{"sync",write(r.commands[2])}}},
        {"lastM1WriteReceipt",r.m1},{"lastM2WriteReceipt",r.m2},{"lastSyncWriteReceipt",r.sync},
        {"mappingVerified",false},{"retryIssuedByDiagnostic",false},
        {"scope","this preparation call only; not observed rear keys or firmware readback"}};
}
}

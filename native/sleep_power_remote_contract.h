#pragma once
#include <cstddef>
#include <cstdint>
#include "sleep_power_state.h"

namespace ymcc::sleeppower::remote {
// Candidate data contract only. No provider/transport is installed by including
// this file. Fan's existing Native purpose gate must mint each descriptor.
inline constexpr std::uint32_t kVersion=1;
enum class Purpose : std::uint32_t { Observation=1, FanResume=2, FanWrite=3 };
struct Key128 {
    std::uint64_t low=0,high=0;
};
struct ProcessIdentityV1 {
    std::uint32_t pid=0,session=0;
    std::uint64_t birth100ns=0;
};
struct DescriptorV1 {
    std::uint32_t size=sizeof(DescriptorV1),version=kVersion;
    Key128 scope,request;
    ProcessIdentityV1 provider,consumer;
    Purpose purpose=Purpose::Observation;
    std::uint32_t reserved=0;
    SharedPowerV1 captured;
};
static_assert(sizeof(Key128)==16);
static_assert(sizeof(ProcessIdentityV1)==16);
static_assert(sizeof(DescriptorV1)==128);
static_assert(offsetof(DescriptorV1,scope)==8);
static_assert(offsetof(DescriptorV1,request)==24);
static_assert(offsetof(DescriptorV1,provider)==40);
static_assert(offsetof(DescriptorV1,consumer)==56);
static_assert(offsetof(DescriptorV1,purpose)==72);
static_assert(offsetof(DescriptorV1,captured)==80);

enum class SettlementOutcome : std::uint32_t {
    Unknown=0, Returned=1, RetainedResources=2
};
enum SettlementFlags : std::uint32_t {
    WorkerStopped=1, BorrowersReturned=2, OwnedOccupancyLeaseSettled=4,
    NoOutstandingDeviceCall=8, OwnedSdkResourcesReturned=16,
    AllReturned=31
};
struct SettlementV1 {
    std::uint32_t size=sizeof(SettlementV1),version=kVersion;
    Key128 scope,request;
    ProcessIdentityV1 provider,consumer;
    SettlementOutcome outcome=SettlementOutcome::Unknown;
    std::uint32_t flags=0;
};
static_assert(sizeof(SettlementV1)==80);
static_assert(offsetof(SettlementV1,outcome)==72);

// These values are identity/cancellation metadata, never permission on their
// own. Observation cannot be upgraded to FanResume/FanWrite by a consumer.
// Only the unique Native Reader/Registry may supply current power. Ordinary
// publication advancement is permitted; a revoked scope stays revoked.
// A Returned receipt additionally requires actual authenticated owner evidence
// for every AllReturned flag. HTTP completion/cancellation is not settlement.
}

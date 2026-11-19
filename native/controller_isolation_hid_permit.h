#pragma once
#include "controller_isolation_hid_reader.h"
#include <atomic>
#include <memory>

static std::atomic<std::shared_ptr<const ymcc::isolationhid::Permit>> g_controllerIsolationHidPermit;
static void controllerIsolationHidWithdrawPermit() noexcept {
    g_controllerIsolationHidPermit.store({},std::memory_order_release);
}

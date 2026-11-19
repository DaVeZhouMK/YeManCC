#pragma once
#include <cstdint>

namespace ymcc::dsu { struct Frame; }

extern "C" {
bool dsu_start();
// A nonzero ticket admits work for the current live data subscription period.
std::uint64_t dsu_frame_demand() noexcept;
bool dsu_publish_for_demand(std::uint64_t ticket, ymcc::dsu::Frame&& frame);
// Nonblocking stop request/retirement attempt. Busy/live owners are retained;
// only dsu_stopped() confirms completion. Retry from the existing owner.
void dsu_stop();
// Read-only ownership snapshot: joined server, closed socket/events, and a
// successfully returned WSAStartup reference. Never-started is also complete.
// Tries lifecycleMx then stateMx; busy or uninspectable ownership returns false.
// A false result allows a later explicit dsu_stop() to retry retained owners.
bool dsu_stopped() noexcept;
}

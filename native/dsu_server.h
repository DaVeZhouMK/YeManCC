#pragma once

extern "C" {
bool dsu_start();
void dsu_stop();
// Read-only ownership snapshot: joined server, closed socket/events, and a
// successfully returned WSAStartup reference. Never-started is also complete.
// Acquires lifecycleMx then stateMx; may wait behind the existing unbounded join.
// A false result allows a later explicit dsu_stop() to retry retained owners.
bool dsu_stopped() noexcept;
}

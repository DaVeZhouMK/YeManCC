#include "../native/decky_sidebar_recovery.h"
#include <cassert>
#include <iostream>
int main(){using ymcc::deckysidebar::ConnectionRetryWindow;
 assert(ymcc::deckysidebar::recoverableWatchFailure("mirror-bootstrap-async-error"));assert(ymcc::deckysidebar::recoverableWatchFailure("mirror-bootstrap-deadline"));
 assert(ymcc::deckysidebar::recoverableWatchFailure("mirror-bootstrap-ws-closed"));assert(ymcc::deckysidebar::recoverableWatchFailure("mirror-bootstrap-ws-receive-failed"));assert(!ymcc::deckysidebar::recoverableWatchFailure("mirror-bootstrap-ws-invalid"));
 assert(!ymcc::deckysidebar::recoverableWatchFailure("mirror-bootstrap-owner-mismatch"));assert(!ymcc::deckysidebar::recoverableWatchFailure("mirror-bootstrap-readback-failed"));assert(!ymcc::deckysidebar::recoverableWatchFailure("mirror-bootstrap-canceled"));
 ConnectionRetryWindow window;assert(window.timeout(100)==INFINITE);
 ULONGLONG now=100;constexpr DWORD delays[]{250,1000,3000,10000,30000,30000,30000,30000};
 for(auto delay:delays){window.begin(now);assert(window.timeout(now)==delay);window.begin(now+10);assert(window.timeout(now)==delay);assert(!window.take(now+delay-1));assert(window.take(now+delay));assert(window.timeout(now+delay)==INFINITE);now+=delay;}
 window.begin(now);window.cancel();assert(window.timeout(now)==INFINITE);window.begin(now);assert(window.timeout(now)==30000);window.reset();window.begin(now);assert(window.timeout(now)==250);window.reset();assert(window.timeout(now)==INFINITE);
 std::cout<<"Decky outage recovery PASS: capped retry, coalescing, cancel, health reset; no real process IO\n";
}

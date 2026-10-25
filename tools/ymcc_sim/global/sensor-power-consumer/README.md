# Gyro sleep power boundary consumer

Run through the existing global simulator: `node tools/ymcc_sim/global/cli.mjs --mode run --profile sleep --test sleep.gyro-power --out-dir <evidence-root>`.

The fixture extracts twelve exact production functions, including the real STA message pump, OS suspend callback, display-state handler, queue admission, unsubscribe and same-object resubscribe. WinRT sensors, Sensor API readers, logging and inventory are memory ports. Windows threads, events and the COM apartment/message pump are real test-owned resources. No YMCC/Steam/Host is launched, and no sensor, PnP, sleep or game API is invoked.

Positive checks cover immediate callback admission closure before GUI dispatch; no five-second notification wait; display OFF before delayed Kernel-Power delivery; stop completion; OFF/DIM/ON; stale and automatic resume; retained-object interval reset; no power-time polling; repeated notifications; failed revoke/stuck reader; legacy fallback and normal owner shutdown. Three private source faults must compile and run, then fail the unchanged behavioral assertions. Source hashes and verbatim extraction bindings are retained. These are software checks; physical power-button latency and low-power residency require later ordinary-device logs.

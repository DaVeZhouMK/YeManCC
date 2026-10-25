# Exact physical ancestor observation (task 110)

`native/virtual_target_return_physical_path.h` provides a Windows-only synchronous leaf in `ymcc::targetReturn::physicalPath`:

```cpp
Result verify(std::string_view selectedPhysicalRoot, std::string_view targetInstance);
template<class Reader>
Result verify(std::string_view selectedPhysicalRoot, std::string_view targetInstance, Reader& reader);
```

`Result` contains `State state` (`Proven`, `Rejected`, `Unknown`), `Reason reason`, `unsigned depth`, `CONFIGRET configurationReturn`, and `proven()`. `stateName` / `reasonName` supply stable caller diagnostics. Inputs use the existing record instance-ID validator, including its ASCII alphabet, exactly three nonempty backslash-delimited segments and maximum 512 bytes. This leaf does not expand the record format or invent path normalization. Comparisons use the existing ASCII case folding.

Both inputs must validate before any CM leaf runs. The algorithm locates only the exact requested target, reads its actual ID and checks it still matches that target, then follows CM parents until its actual ID exactly matches the caller-selected root. Direct target=root must still locate and read; equal strings alone cannot prove a device exists. A matching ID must be syntactically valid and completely NUL-terminated in the bounded 513-WCHAR buffer. Same VID/PID, names, siblings and instance/path prefixes never qualify.

The hard bound is **16 node-ID observations and 16 parent queries**, counting the target as depth **1**. A matching root at the sixteenth node is Proven. Requiring a seventeenth read is Unknown (`depth-limit`). Devinst cycles and repeated case-folded IDs are Unknown. Depth is the number of attempted ID reads; locate/input failure has depth 0, and a repeated devinst is reported before another ID read.

Invalid root/target inputs are Rejected without queries. Actual device lookup/read/parent failures, missing or invalid IDs, successful reads lacking NUL, mismatched first target ID, zero node handles and an incomplete chain are Unknown. Returned CONFIGRET is preserved, even when a successful API returned unusable data. Proven means an exact root was reached through actual leaf observations, not that the caller owns a source or may hide/unhide it.

A complete unmatched chain is Rejected only when tree completion is positively confirmed. `CR_NO_SUCH_DEVNODE` from a parent query alone is insufficient: it could also indicate disappearance. Only on that status, the same locate leaf is called once with null ID to obtain the system tree root; the current node must exactly equal that nonzero root. A failed/mismatched/zero root confirmation stays Unknown. Thus there is at most one exact-target locate plus one exceptional system-root locate, with no enumeration or retry. No additional APIs are added for this confirmation.

`ProductionReader` forwards the three leaves directly to `CM_Locate_DevNodeW`, `CM_Get_Device_IDW`, and `CM_Get_Parent`. A custom Reader has the same three method signatures (`locate`, `deviceId`, `parent`) and only supplies their results. The actual verification control flow stays in this header. The caller must provide current source generation, the real dispatch boundary and the required absent-before evidence. This observation is not an atomic PnP snapshot, source admission, persisted provenance, return ownership or recovery authorization.

## Direct regression

```powershell
node G:/YeManCC-Work/Mainline/YeManCC-source/YeManCC/tools/virtual_target_return_physical_path_selftest.mjs --out ABS_FRESH_PRIVATE_DIRECTORY --toolchain ABS_CAPTURED_TOOLCHAIN_DIRECTORY
```

Use the captured `Build/Validation/VirtualController-Lifecycle-20261019/player-switch-lock12/production-link100` environment, with explicit absolute arguments and a fresh private output. The runner rejects output inside source/toolchain roots and refuses overwriting old evidence. It freezes these four new files plus the existing record and JSON dependencies, verifies compiler/link/dumpbin hashes, binds every actual compiler dependency and verbose linker library input, records generated source/binary identities and checks for drift after execution.

Release is `/O2 /MT /W4 /WX`; Checked is `/Od /RTC1 /MT /W4 /WX`, an MSVC runtime-check configuration, not an arithmetic-overflow sanitizer. Both execute the actual production verify template with explicitly controlled CM leaves. No physical device API, product/Host entry, installed appdata, driver/service change, device write, enumeration, retry, timer or thread is executed. The fixture is sequential, uses no device discovery and only writes its private JSON results.

The `productionReaderCompileOnly` export retains the real default verify overload and ProductionReader ABI as compiled code. It is **never called** by fixture entry or cases. PE imports must be exactly kernel32.dll and cfgmgr32.dll, and the named export must exist. This proves production reader compilation/import binding, not a real PnP query or hardware acceptance. The tests distinguish controlled reader observations from real OS observations throughout.

Three separately compiled semantic controls must fail original assertions: ignore the exact ancestor comparison; promote an ID-read failure to Proven; bypass locate/read for target=root strings. The original assertions and fake topology remain unchanged. All historical failed runs remain separate, and the coordinator owns runtime/session integration and physical tests.

# HC manager initialization regression

Run with PowerShell 7 and an absolute evidence directory outside the repository:

`./tools/HcManagerInitializationSelfTest/Run-SelfTest.ps1 -EvidenceRoot D:/Codex/.../manager-regression`

The approved runtime first passes its exact manifest check. An isolated child process constructs the real 13 HC managers on STA, using private settings and logging configuration. It never constructs an IDevice, calls manager Start, starts FanHost, opens a controller, or issues EC/fan control writes. The constructors do enumerate desktop/audio/display state and register process-local notifications; the child process exit owns their lifetime. This is desktop initialization coverage, not handheld firmware or hardware acceptance.

A separate child then uses an explicitly altered temporary copy containing the retained known-bad NAudio Core/Wasapi bytes. It must reproduce the same real callback-registration `ArgumentException / 0x80070057`, while the approved runtime must construct all 13 managers. The deliberately mismatched negative manifest is recorded and never presented as a distributable runtime. Existing product/install files are read only.

The original trimmed audio DLLs under deps/.../bin are kept solely as hash-locked regression inputs. They are absent from the profile replacements and cannot enter a generated runtime.
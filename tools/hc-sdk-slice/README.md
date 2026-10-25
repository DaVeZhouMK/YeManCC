# HC device-only Windows SDK projection slice

This build-time tool takes the hash-pinned original HC runtime and produces a smaller Microsoft.Windows.SDK.NET.dll using the installed .NET 10.0.11 ILLink tool. The original HC/Shared assemblies are complete analysis roots; all other DLLs remain unmodified in the intended shipping runtime. Only the SDK slice is eligible for promotion from ILLink output.

The checked-in descriptor preserves the full members and nested types of 1,405 known projection/ABI types, including the full sensors family. Removing unused interfaces, unreachable-body rewriting, override removal and unused type checks are disabled. Original inputs and tool/descriptor/output hashes are recorded. The initial footprint was 24,877,600 bytes; current verified output is 1,568,256 bytes.

Run build-slice.ps1 with explicit InputRuntimeRoot and EvidenceDirectory. It creates scratch output only and refuses unreviewed original SDK/HC hashes. It checks all managed files in the input runtime for missing SDK static type references and method names/arity or field names. That static member check is narrower than a complete overload/ABI proof and does not establish physical device behavior.

RuntimeProbe loads metadata in a native-import-blocked context without constructing any device/profile objects. Golden and candidate must be run in separate processes. Its result captures all retained type hierarchies, member surfaces and selected HC device surfaces; compare rooted types, not every intentionally removed unrelated Windows API.

The original upstream DLL is not deleted from development inputs. A shipping runtime must have a distinct YMCC-derived identity and regenerated integrity manifest. Do not place this output under the original hash, update a manifest to lie about input identity, or promote the other HC/Shared copies ILLink rewrote.

Current software evidence does not cover arbitrary WinRT types discovered only by external string/configuration. Before promotion the normal consumer surface and loader/deployment gates must also pass. Future upstream capability imports must regenerate/audit the descriptor; source version increments alone do not establish coverage.

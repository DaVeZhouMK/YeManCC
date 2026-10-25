# Fan Host component source archives

The formal mainline payload verifier selects `candidate-coldwake-20261023/source/`.
This component removes the explicit Fan initialization sensor enumeration and
starts HTTP acceptance before the authenticated HC handshake. It is a software-tested
mainline component; installation, complete product export and physical handheld
acceptance require their own records. HC internal device-event enumeration is
not removed by this component.

| Archive | Host | Backend | Externally pinned source manifest |
| --- | --- | --- | --- |
| Current `candidate-coldwake-20261023` | `4eaa63b42ff4766b7164e4db369bff9ff85b0ce26c6dd7dc055ac1039dfed0fd` | `877774a2f57181130ac93925c2066431951d6440b8823a2a736df895e6a3122c` | `e5cb0d378d6ac8a0b30cc281d90ce7c84abf67310ca212ef207e482d01a4b9f6` |
| Historical `candidate98` | `98acebb4d81f40309b5180766a92c7ee915a7f05b7002185982d21342d7cf3ff` | `d8cb348ca9b081388c22247f4f44fca140c273260a78cbf6d7263ca6614cbc8a` | `e5f9dbe64b987e41a43fbe0d4c0f5ee6ac9f3f80e3d04aa9cc1e8c52da8d8b6d` |
| Historical `stable6b` | `6b2f96972a47515bbe06e820e4196a22897782b7314adc03553f9b8e0aea9cf3` | `211dce9f5594ff2a040ce3ac7824135f0e4df23987edcda563d038050d1dd04d` | `6cc777b57a44536835a3e6bbe4913392589b7f06596789e873f400d46826ed1e` |

The current archive has 34 source inputs (31 handwritten Compile sources, two
projects and the Host configuration), 46 archived members plus its manifest,
and actual DLL/PDB records covering 36 compiler documents including five generated
documents. The verifier checks the externally pinned manifest, every archive
member and the exact 33-input Compile/project closure. The historical candidate98
archive and fixed installed-component import contract remain available as history.

An explicit `-SourceRoot` must point to `source/` in the selected, externally
pinned archive and match the exact artifact pair. Missing, changed, mixed or
self-resealed archives are refused. Active source drift is reported separately.
Candidate builds still use their intended source snapshot and need exact artifact
admission; the historical stable archive is not a fallback. Superseded candidate
149 failed the complete Host self-test and is not an admitted installation tuple.

The two files in `stable6b/ancillary/` are reconstruction references, not historical
import attestations. Original absolute paths in evidence are provenance only.
Historical SDK/import completeness and byte-identical rebuild reproducibility
have not been proven by these archives.

This directory contains build and audit inputs and is excluded from runtime
export as E-BUILD-EVIDENCE. Neither archive nor PDB evidence is copied into the
Fan Host runtime payload. Source acceptance, software integration, installation,
full product publication and physical hardware acceptance remain separate.

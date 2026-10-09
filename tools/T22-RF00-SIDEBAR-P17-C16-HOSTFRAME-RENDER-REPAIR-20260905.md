# P17 static snapshot：render artifact repair note

Status: `P17-SNAPSHOT-RETAINED / ORIGINAL-RENDER-ARTIFACT-DEFECT / READING-AID-ONLY / RUNTIME-BLOCKED`

## Scope

This note repairs the human-readable entry points for the P17 static snapshot. It does **not** rewrite, replace, or re-hash the original P17 manifest, porcelain raw, raw search, line-map, T21/T14 JSON, or drift ledger. Those output artifacts are self-excluded from the P17 source digest. No build, test, InputHost, HIDMaestro, driver, virtual device, physical device, Steam, or game operation was performed for this repair.

## Evidence: retained P17 capture facts

| Item | Static fact |
|---|---|
| Manifest | `T22-RF00-SIDEBAR-P17-C16-HOSTFRAME-current-source-20260905-manifest.json`; 21,880 bytes; SHA-256 `74AF496481F05C4F062006236932EC4B0E424BE18852315CD9055F10A5873D12` |
| Snapshot | `T22-RF00-20260905-SIDEBAR-P17-C16-HOSTFRAME-STATIC-SNAPSHOT`; captured `2026-09-05T04:48:38.6795365Z`; YMCC HEAD `aa8f38b83cc960267d2f68bf78d0e8adaca2234f`; locked HC `06c0b9544db2b1f39abf9cd3796225ccfb096103` clean at capture |
| Key source scope | 53 files: DOC 6, HC 19, YMCC 28; source digest `1F0BC93F0F327FE10E54551FCE77DFA9884E633CC59079DCC6D666D82F343647` |
| Porcelain raw | `T22-RF00-SIDEBAR-P17-C16-HOSTFRAME-current-source-20260905-porcelain.raw.txt`; 23,016 bytes; 406 entries; SHA-256 `7F0A6C798818AC280399223F1193332772266336221F1879D270155AEBCCAEF4` |
| Raw static search | `T22-RF00-SIDEBAR-P17-C16-HOSTFRAME-current-source-20260905-raw-search.txt`; 30,990 bytes; SHA-256 `2F787E46BE69373A4F06A98D399488EF3B1B973C40D0927A3549DEDC4394BF9F` |
| T20 | `HistoricalClaimInventory.v1.json`: 21 claims with `4 current-authority / 13 historical-reference / 4 superseded`; TASK BUS remains the only current authority |
| T21 | `MainlineClaimReconciliation.v1.json`: 9 claims; P17 manifest/source digest bound in `provenanceReaudit`; `runtimeUpgrade=false` |
| T14 | `HCParityDriftLedger.v2.json`: 15 entries, `DGF-01…DGF-15` only; no DGF-16; closure remains `UNENCLOSED / RUNTIME-BLOCKED` |

## Defect: original render artifacts

The original P17 text artifacts are retained byte-for-byte because their hashes are part of the P17 provenance chain, but they cannot be used as clean human-readable line maps:

| Original artifact | Recorded SHA-256 | Defect found by static byte/text inspection |
|---|---|---|
| `T22-RF00-SIDEBAR-P17-C16-HOSTFRAME-current-source-20260905-line-map.md` | `738D8360FD8E5B6E7C2BF07072E07948AD745AEBC98CC3922105A06CC9E1C11E` | 1 NUL byte (offset 4,225) and 6 literal, unexpanded PowerShell `$(...)` placeholders in its header |
| `T21-T14-RF00-SIDEBAR-P17-C16-HOSTFRAME-current-source-reaudit-20260905.md` | `E5734A2A617C9F45B9C0F3370770FEA997659492909CCF5B834A7A51D2FB9EC1` | 1 NUL byte (offset 7,335) and 5 literal, unexpanded PowerShell `$(...)` placeholders |

This is a **documentation/provenance-render defect**, not an HC behavior finding and not a runtime result. The manifest, raw porcelain and raw static-search bytes parse/read normally; this note does not infer that their source facts are false. It does mean the two original Markdown render artifacts must not be cited as independently clean, human-reviewable P17 line-map/report text.

## Correct usage after repair

1. Use the retained [P17 manifest](T22-RF00-SIDEBAR-P17-C16-HOSTFRAME-current-source-20260905-manifest.json), [porcelain raw](T22-RF00-SIDEBAR-P17-C16-HOSTFRAME-current-source-20260905-porcelain.raw.txt), and [raw search](T22-RF00-SIDEBAR-P17-C16-HOSTFRAME-current-source-20260905-raw-search.txt) for P17 capture facts.
2. Use the current source JSON documents for the T20/T21/T14 structural facts above, while preserving their statement that P17 is a static snapshot only.
3. Treat the original [line map](T22-RF00-SIDEBAR-P17-C16-HOSTFRAME-current-source-20260905-line-map.md) and [T21→T14 report](T21-T14-RF00-SIDEBAR-P17-C16-HOSTFRAME-current-source-reaudit-20260905.md) as retained defective artifacts, not as the sole readable proof.
4. The next RF00 producer must validate the emitted Markdown after write: UTF-8 without NUL, zero literal `$(...)` placeholders, resolved local links, paired fences, and a recorded artifact hash. This is an evidence-quality requirement only; it does not permit runtime operations.

## No upgrade

P17 remains a historical static source snapshot. This repair does not make it live/current, does not prove Host ACK, HID encoding/readback, virtual-device enumeration, consumer observation, input isolation, release, recovery, or any T15–T18/R1 closure.

# Private fault payload staging

The original seven private fault generators now receive a fresh Host DLL output. Other payload files retain their original bytes and attributes. This permits Cecil to create a mutated Host when the source payload is sealed read-only; no source or installed file permissions are changed. The old copied-read-only failure and the actual original EC-bool negative receipts remain in the evidence record.

The source payload manifest stays unchanged as provenance. A fault image is intentionally different and is admitted only by its exact mutation proof and original consumer failure IDs, never as a deployable payload. Case assertions, mutation bodies, expected failures and original budgets are unchanged. Run node --test tools/ymcc_sim/global/onex-fan-coexistence/fault-payload-staging.test.mjs for the read-only and fresh-output contract; these tests alone are not a business regression run.

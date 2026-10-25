Lossless HC metadata compaction

Run with the bundled Python interpreter:

    python tools/hc-metadata-compact/build.py --input <approved-runtime> --output <new-scratch-directory>

The tool rejects duplicate JSON keys, parses both complete original documents,
emits compact JSON, and checks complete parsed-value equality. It produces only
two per-file candidates and their original/candidate hashes. Input files are
never edited. This preserves all dependency entries, versions, paths, logging
configuration and values; it removes JSON formatting whitespace only.

Promotion goes through the normal generated-source profile and runtime manifest.
The full runtime/consumer/package gates remain required. This small disk saving
is not a CPU or power measurement.

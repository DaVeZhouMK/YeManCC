# Frozen upstream device-runtime inputs

This directory is development input only. The original approved runtime is saved byte-for-byte for reproducible SDK/resource extraction, comparison and recovery. It must never be included by the release exporter. The generated product runtime has a separate YMCC identity and output manifest.

`upstream-input.json` records all copied files and hashes. The original binary tree is ignored locally; do not silently replace it with a public HC release, which is not the patched project binary. Builders accept the explicit input directory and validate the pinned source hash. This snapshot preserves recoverability while the normal shipping PowerControl tree is migrated.

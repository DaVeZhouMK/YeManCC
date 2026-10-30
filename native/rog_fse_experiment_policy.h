#pragma once
// Normal mainline builds keep the ROG Xbox-FSE source experiments isolated.
// This is a developer build policy, never a player preset or a capability gate.
// Experimental readers and their source files remain available for local AI work.
#ifndef YMCC_ROG_FSE_EXPERIMENTS
#define YMCC_ROG_FSE_EXPERIMENTS 0
#endif
#if YMCC_ROG_FSE_EXPERIMENTS != 0 && YMCC_ROG_FSE_EXPERIMENTS != 1
#error YMCC_ROG_FSE_EXPERIMENTS must be 0 or 1
#endif
#ifndef YMCC_ROG_RAWINPUT_FALLBACK
#define YMCC_ROG_RAWINPUT_FALLBACK YMCC_ROG_FSE_EXPERIMENTS
#endif
#if !YMCC_ROG_FSE_EXPERIMENTS && YMCC_ROG_RAWINPUT_FALLBACK
#error ROG RawInput source substitution requires an explicit experimental build
#endif

// ROG Xbox-FSE adaptation diagnostics are developer evidence, not normal
// product telemetry. They may add per-WM_INPUT counters and extra read-only
// probes, so normal builds compile both their producers and work out.
#ifndef YMCC_ROG_FSE_DIAGNOSTICS
#define YMCC_ROG_FSE_DIAGNOSTICS YMCC_ROG_FSE_EXPERIMENTS
#endif
#if YMCC_ROG_FSE_DIAGNOSTICS != 0 && YMCC_ROG_FSE_DIAGNOSTICS != 1
#error YMCC_ROG_FSE_DIAGNOSTICS must be 0 or 1
#endif
#if !YMCC_ROG_FSE_EXPERIMENTS && YMCC_ROG_FSE_DIAGNOSTICS
#error ROG FSE diagnostics require an explicit experimental build
#endif

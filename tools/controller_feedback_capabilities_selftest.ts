import { evaluateControllerFeedback } from '../src/bridge/controllerFeedbackCapabilities';

const empty = evaluateControllerFeedback({}, false, 'disabled');
if (empty.allClosed || empty.capabilities.some((entry) => entry.availability === 'available')) {
  throw new Error('feedback presentation admitted without a target');
}

const persistedClosedElite = evaluateControllerFeedback({
  outputTarget: { closure: 'CLOSED', descriptorHash: 'elite-descriptor' },
  capability: { closure: 'CLOSED', capabilities: { feedbackRumble: true, feedbackHaptic: true, feedbackLight: true } },
  feedback: {
    closure: 'CLOSED', capabilities: { rumble: true, haptic: true, light: true },
    runtime: { outputCallbackBound: true, physicalRouteClosed: true, releaseClosed: true },
  },
}, true, 'elite');
if (persistedClosedElite.allClosed || persistedClosedElite.capabilities.find((entry) => entry.id === 'light')?.availability !== 'unavailable') {
  throw new Error('persisted settings must not close the Xbox feedback path or invent a generic lightbar');
}
if (persistedClosedElite.capabilities.find((entry) => entry.id === 'rumble')?.availability !== 'unavailable') {
  throw new Error('persisted settings incorrectly admitted an Xbox feedback route');
}

const edgePending = evaluateControllerFeedback({
  outputTarget: { closure: 'CLOSED', descriptorHash: 'edge-descriptor' },
  capability: { closure: 'CLOSED', capabilities: { feedbackRumble: true, feedbackHaptic: true, feedbackLight: true } },
  feedback: { closure: 'CLOSED', capabilities: { rumble: true, haptic: true, light: true }, runtime: { outputCallbackBound: false, physicalRouteClosed: false, releaseClosed: false } },
}, true, 'dualsense-edge');
if (edgePending.allClosed || edgePending.capabilities.some((entry) => entry.availability === 'available')) {
  throw new Error('feedback presentation ignored the missing output/release route');
}

console.log('controller feedback capabilities selftest: PASS');

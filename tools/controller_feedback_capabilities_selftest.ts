import { evaluateControllerFeedback } from '../src/bridge/controllerFeedbackCapabilities';

const empty = evaluateControllerFeedback({}, false, 'disabled');
if (empty.allClosed || empty.capabilities.some((entry) => entry.availability === 'available')) {
  throw new Error('feedback presentation admitted without a target');
}

const persistedClosedX360 = evaluateControllerFeedback({
  outputTarget: { closure: 'CLOSED', descriptorHash: 'x360-descriptor' },
  capability: { closure: 'CLOSED', capabilities: { feedbackRumble: true, feedbackHaptic: true, feedbackLight: true } },
  feedback: {
    closure: 'CLOSED', capabilities: { rumble: true, haptic: true, light: true },
    runtime: { outputCallbackBound: true, physicalRouteClosed: true, releaseClosed: true },
  },
}, true, 'xbox360');
if (persistedClosedX360.allClosed || persistedClosedX360.capabilities.find((entry) => entry.id === 'light')?.availability !== 'not-applicable') {
  throw new Error('persisted settings must not close the Xbox feedback path or invent a generic lightbar');
}
if (persistedClosedX360.capabilities.find((entry) => entry.id === 'rumble')?.availability !== 'unavailable') {
  throw new Error('persisted settings incorrectly admitted an Xbox feedback route');
}

const ds4Pending = evaluateControllerFeedback({
  outputTarget: { closure: 'CLOSED', descriptorHash: 'ds4-descriptor' },
  capability: { closure: 'CLOSED', capabilities: { feedbackRumble: true, feedbackHaptic: true, feedbackLight: true } },
  feedback: { closure: 'CLOSED', capabilities: { rumble: true, haptic: true, light: true }, runtime: { outputCallbackBound: false, physicalRouteClosed: false, releaseClosed: false } },
}, true, 'dualshock4');
if (ds4Pending.allClosed || ds4Pending.capabilities.some((entry) => entry.availability === 'available')) {
  throw new Error('feedback presentation ignored the missing output/release route');
}

console.log('controller feedback capabilities selftest: PASS');

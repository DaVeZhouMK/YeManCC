import { CONTROLLER_PERSONA_OPTIONS } from './controllerPersona';
// Same reachable choices as YMCC ButtonMappingView; legacy personas remain stored but are not selectable.
export const DECKY_PAD_CHOICES = CONTROLLER_PERSONA_OPTIONS.map(({ value, label }) => ({ data: value, label }));
export const DECKY_GYRO_CHOICES = [{data:'fps',label:'FPS射击'},{data:'racing',label:'赛车'},{data:'custom',label:'自定义'},{data:'steam',label:'Steam'}];

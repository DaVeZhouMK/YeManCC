import assert from 'node:assert/strict';
import { SCREEN_TOUCHPAD_DEFAULT, STANDALONE_SPECIAL_MODES, LEFT_TOUCHPAD_MODES, RIGHT_TOUCHPAD_MODES, validateScreenTouchpadPatch, SCREEN_TOUCHPAD_LAYOUTS, SINGLE_TOUCHPAD_MODES, screenTouchpadDefaults, screenTouchpadProfile } from '../src/bridge/screenTouchpads';
assert.equal(SCREEN_TOUCHPAD_DEFAULT.enabled, false);
assert.equal(SCREEN_TOUCHPAD_DEFAULT.layout, 'off');
for(const {value} of SCREEN_TOUCHPAD_LAYOUTS)assert.ok(validateScreenTouchpadPatch({layout:value}));
for(const {value} of SINGLE_TOUCHPAD_MODES)assert.ok(validateScreenTouchpadPatch({singleMode:value}));
assert.equal(SCREEN_TOUCHPAD_DEFAULT.transparency, 80);
assert.equal(SCREEN_TOUCHPAD_DEFAULT.scale, 100);
for(const field of ['summonEnabled','specialEnabled','rearEnabled'] as const){assert.equal(SCREEN_TOUCHPAD_DEFAULT[field],false);assert.ok(validateScreenTouchpadPatch({[field]:true}));assert.equal(validateScreenTouchpadPatch({[field]:1} as any),false);}
for (const mask of [0,1,2,3]) assert.ok(validateScreenTouchpadPatch({ specialMask: mask }));
for (const mask of [0,5,10,15]) assert.ok(validateScreenTouchpadPatch({ rearMask: mask }));
assert.equal(validateScreenTouchpadPatch({ specialMask: 4 }), false);
assert.equal(validateScreenTouchpadPatch({ rearMask: 16 }), false);
for(let scale=50;scale<=200;scale+=5) assert.ok(validateScreenTouchpadPatch({scale}));
assert.equal(SCREEN_TOUCHPAD_DEFAULT.leftMode, 'steamdeck');
assert.equal(SCREEN_TOUCHPAD_DEFAULT.rightMode, 'steamdeck');
for (const mode of LEFT_TOUCHPAD_MODES) assert.ok(validateScreenTouchpadPatch({ leftMode: mode.value }));
for (const mode of RIGHT_TOUCHPAD_MODES) assert.ok(validateScreenTouchpadPatch({ rightMode: mode.value }));
assert.ok(validateScreenTouchpadPatch({ leftMode: 'wasd', rightMode: 'mouse', enabled: true }));
assert.ok(validateScreenTouchpadPatch({ leftMode: 'arrows', rightMode: 'mouse', transparency: 100 }));
for (const patch of [{ transparency: -1 }, { transparency: 101 }, { transparency: NaN }, { transparency: 1.5 },
  { layout: 'unknown' }, { singleMode: 'steamdeck' }, { singleMode: 'off' }, { singleMode: 'unknown' },
  { scale: 49 }, { scale: 201 }, { scale: 52 }, { scale: 100.5 }, { scale: NaN }, { scale: "100" },
  { mouseSensitivity: 9 }, { mouseSensitivity: 301 }, { enabled: 1 }, { rightMode: 'wasd' },
  { leftMode: 'unknown' }, { unexpected: true }]) assert.equal(validateScreenTouchpadPatch(patch as any), false);
console.log('SCREEN_TOUCHPADS_FRONTEND_OK: modes, defaults, bounds, unknown-field rejection');

for (const profile of ['disabled','steamdeck','dualsense-edge','elite'] as const) {
  const defaults=screenTouchpadDefaults(profile);
  assert.equal(defaults.transparency,80);
  assert.equal(defaults.scale,100);
  assert.equal(defaults.enabled,profile==='steamdeck'||profile==='dualsense-edge');
  assert.equal(defaults.layout,profile==='steamdeck'?'dual':profile==='dualsense-edge'?'single':'off');
  assert.equal(defaults.singleMode,profile==='dualsense-edge'?'dualsense':'mouse');
  assert.equal(defaults.leftMode,profile==='steamdeck'?'steamdeck':profile==='dualsense-edge'?'dualsense':'wasd');
  assert.equal(defaults.specialEnabled,profile!=='disabled');
  assert.equal(defaults.specialMask,profile==='disabled'?0:profile==='elite'?1:3);
  assert.equal(defaults.rearEnabled,false);assert.equal(defaults.summonEnabled,false);
}
assert.equal(LEFT_TOUCHPAD_MODES[0].label,'SteamDeck 左触摸板 (Steam内设置)');
assert.equal(RIGHT_TOUCHPAD_MODES[0].label,'SteamDeck 右触摸板 (Steam内设置)');
assert.equal(SINGLE_TOUCHPAD_MODES[0].label,'PS5触摸板 (Steam内设置)');
assert.ok(validateScreenTouchpadPatch({layout:'dual',leftMode:'dualsense',rightMode:'dualsense'}));
assert.equal(LEFT_TOUCHPAD_MODES[1].label,'PS5 左触摸板 (Steam内设置)');
assert.equal(RIGHT_TOUCHPAD_MODES[1].label,'PS5 右触摸板 (Steam内设置)');
const custom=screenTouchpadDefaults('steamdeck');custom.layout='off';custom.enabled=false;
assert.equal(screenTouchpadDefaults('steamdeck').layout,'dual','Mutating a config must not mutate the preset');
assert.equal(screenTouchpadProfile('dualshock4'),'dualsense-edge');
assert.equal(screenTouchpadProfile('xbox360'),'elite');

assert.deepEqual(STANDALONE_SPECIAL_MODES.map(mode=>mode.label),['关闭','开启Steam全部','开启PS5全部']);
assert.equal(SCREEN_TOUCHPAD_DEFAULT.standaloneSpecialMode,'off');
for(const {value} of STANDALONE_SPECIAL_MODES)assert.ok(validateScreenTouchpadPatch({standaloneSpecialMode:value}));
for(const value of ['elite','dualsense-edge','all',3,null,true])assert.equal(validateScreenTouchpadPatch({standaloneSpecialMode:value} as any),false);
for(const profile of ['disabled','steamdeck','dualsense-edge','elite'] as const)assert.equal(screenTouchpadDefaults(profile).standaloneSpecialMode,'off');
console.log('STANDALONE_SPECIAL_FRONTEND_OK: exact three options, default off, strict validation');

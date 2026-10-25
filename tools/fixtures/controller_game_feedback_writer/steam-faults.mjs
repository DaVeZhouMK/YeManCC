export const STEAM_MANAGED_FAULTS=Object.freeze([
 {id:'swap-steam-motor-word-byte',before:'motors = new(data[5], data[7], 3);',after:'motors = new(data[4], data[7], 3);',expected:'independent-u16-motors-use-frozen-high-byte'},
 {id:'wrong-steam-output-source',before:'if (source != 1 || reportId != 0xEB)',after:'if (source != 0 || reportId != 0xEB)',expected:'wrong-api-or-command-not-a-motor-update'},
 {id:'borrow-sony-compatibility-for-steam',before:'motors = new(data[5], data[7], 3);',after:'motors = new(data[5], data[7], 1);',expected:'independent-u16-motors-use-frozen-high-byte'},
 {id:'raw-drain-erases-game-stop',before:'Volatile.Write(ref _pending, _pending & ~delivered);',after:'Volatile.Write(ref _pending, _pending & ~(delivered | MotorPending));',expected:'real-mailbox-raw-only-drain-preserves-motor-stop'}
]);
export const STEAM_NATIVE_FAULTS=Object.freeze([
 {id:'restore-sony-only-native-binding',kind:'source',before:'profile&&profile->token>=1&&profile->token<=2&&protocol==profile->token',after:'profile&&profile->token==1&&protocol==1',expected:'STEAM_PROFILE_TOKEN_BOUND'},
 {id:'host-protocol-catalog-mismatch-admitted',kind:'source',before:'profile&&profile->token>=1&&profile->token<=2&&protocol==profile->token',after:'profile&&profile->token>=1&&profile->token<=2',expected:'HOST_PROTOCOL_MUST_MATCH_CATALOG_PROFILE'},
 {id:'cross-profile-compatibility-admitted',kind:'receipt',before:'((protocol_==1&&sample.compatibility!=1&&sample.compatibility!=2)||(protocol_==2&&sample.compatibility!=3))',after:'false',expected:'SONY_COMPAT_CANNOT_ENTER_STEAM_INBOX'},
 {id:'steam-compatibility-rejected-before-consumer',kind:'receipt',before:'(words[4]!=1&&words[4]!=2&&words[4]!=3)',after:'(words[4]!=1&&words[4]!=2)',expected:'STEAM_XINPUT_ACTUAL_WRITER_ACCEPTED'}
]);

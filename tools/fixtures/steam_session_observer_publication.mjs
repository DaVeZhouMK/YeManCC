import assert from 'node:assert/strict';
import crypto from 'node:crypto';

// This one private compiler input is derived from the real product preimage.
// No file IO, caller-supplied replacement, publication or delegation algorithm.
export const OBSERVER_PUBLICATION_SOURCE='native/steam_session_observer.h';
export const OBSERVER_PUBLICATION_GENERATOR='tools/fixtures/steam_session_observer_publication.mjs';
const originalPin=Object.freeze({path:OBSERVER_PUBLICATION_SOURCE,bytes:30391,sha256:'3b4d7ad634e8a78fe19ffca05e4ca0f5594ed3edd5e4ee5fdf90c26eda0abad3'});
const derivedPin=Object.freeze({path:OBSERVER_PUBLICATION_SOURCE,bytes:31695,sha256:'041139e02cf983cd71d296048ca813729e2808e972467a022f35c8f29578d59f'});
const declaredEpochs=Object.freeze([
    // The window observer matches the held path in place; publication is unchanged.
    Object.freeze({"original":{"path":"native/steam_session_observer.h","bytes":43608,"sha256":"b28d2b83c09c57e2c9a36b7102b85cccf60e493e55ab70b67ac4f44961aa3f0a"},"derived":{"path":"native/steam_session_observer.h","bytes":44912,"sha256":"b6b809fdd1d5128c6db078b3c38c308c8723f20e1b9d702fb1c307996890dfa9"}}),
    // Current shared Steam observer; exact private publication adapter unchanged.
    Object.freeze({"original":{"path":"native/steam_session_observer.h","bytes":42734,"sha256":"62b782a1da23627432dfa952c756fb6aa93fdf951b01081a6b7acf9e09b94134"},"derived":{"path":"native/steam_session_observer.h","bytes":44038,"sha256":"5c1f08c9a9fe0a540d44b1a599871ff3c3a32427c0e9f89db083af6e9534c32a"}}),
    // Resident action-only SteamPath read; the publication adapter is unchanged.
    Object.freeze({"original":{"path":"native/steam_session_observer.h","bytes":41457,"sha256":"422697c0431715df01c2f31e48cfe5741079b05395a6ed34c0b841fd2b0b6c1f"},"derived":{"path":"native/steam_session_observer.h","bytes":42761,"sha256":"d89b22cfa45cbb3915c208c713cdf1f64ca44c18b09b419590e1e52a83bfe361"}}),
    // Shared Steam observer successor; the original private publication adapter is unchanged.
    Object.freeze({"original":{"path":"native/steam_session_observer.h","bytes":39704,"sha256":"a4c12c7dcad52b415fdecd395a74db9f8cebdede1017df4f725af9c38d81101e"},"derived":{"path":"native/steam_session_observer.h","bytes":41008,"sha256":"369de16145f414122593e97b84fad30b67815a495e32423689cea048e53fc89e"}}),
    // SourceReadCurrent accessor successor; the private publication adapter is unchanged.
    Object.freeze({"original":{"path":"native/steam_session_observer.h","bytes":39951,"sha256":"a268f487ebdc2f75eb91700aa316740ceeb914995d3d9f8bbd7abe6a26d6b6f9"},"derived":{"path":"native/steam_session_observer.h","bytes":41255,"sha256":"53f706cf58bcceede96e114fd68dbb0f441444c019fe301c0f33c7488b59f717"}}),
    // Same-receipt Steam presence reuse; the publication adapter is unchanged.
    Object.freeze({"original":{"path":"native/steam_session_observer.h","bytes":39694,"sha256":"4bd7051a369ca2d4a600a674bac3466aa108964c5a23f3c5d7571302365ddc14"},"derived":{"path":"native/steam_session_observer.h","bytes":40998,"sha256":"d5e82b5fe4d2c017a2a4163c02e9241e80104bcb7f5e9bf6ab3a463fed37f818"}}),
    // Startup window notifications; the complete private publication body is unchanged.
    Object.freeze({"original":{"path":"native/steam_session_observer.h","bytes":39192,"sha256":"888b060d7c962a39c8d44cf0d89eb7e4f8ba0d45b11a43c0c00314e085049d60"},"derived":{"path":"native/steam_session_observer.h","bytes":40496,"sha256":"a0bb65254dcc2742d66c3dca425b6094d8b44aae4b5877a5b145f905edf9d0f0"}}),
    // Completed worker retirement/checked event cleanup; exact adapter unchanged.
    Object.freeze({"original":{"path":"native/steam_session_observer.h","bytes":38212,"sha256":"baeddcd06fdc1fe005ee3d84876249a9f216aba2f8c0676e9ffd7667672ac35d"},"derived":{"path":"native/steam_session_observer.h","bytes":39516,"sha256":"f587a4f666b43aac913b60bc5d9e85b169e86ab1ae754509731c71227c91a29e"}}),
    // Common SourceOwner/early ConsumerRelay successor; exact private adapter unchanged.
    Object.freeze({"original":{"path":"native/steam_session_observer.h","bytes":37724,"sha256":"b7ff63680e97d617f9c03846af56c8f0444de26c6cbe1e50e79cced096b95ed5"},"derived":{"path":"native/steam_session_observer.h","bytes":39028,"sha256":"07bdd604c6cccd113f9dbef6cf32bb745939e0313676b5627ed6d6a81e3fd2d2"}}),
    // Explicit final source retirement; exact publication adapter unchanged.
    Object.freeze({"original":{"path":"native/steam_session_observer.h","bytes":36455,"sha256":"2aeb58bcc3b120208699ca32bf4af9055d72cd54379beccab29dce237ba68466"},"derived":{"path":"native/steam_session_observer.h","bytes":37759,"sha256":"b969dada3bb473b567f0cab7c9d137eddf8b1a09b2121902f89c119e8bdcf33e"}}),
    // Owned executor successor; original publication method and terminal adapter preserved.
    Object.freeze({"original":{"path":"native/steam_session_observer.h","bytes":36051,"sha256":"ab3dab9b0f1660605b1ca6413f80c6666a2fe9a8df0351323d61c114251bf392"},"derived":{"path":"native/steam_session_observer.h","bytes":37355,"sha256":"1a63c38cf1e42a97599ca53c3f1b0cc6d859ae7c729f00154f4683e7ae05fa26"}}),
    // Exact private r28 listener source; later executor/identityReadView revisions require their own paired epoch.
    Object.freeze({"original":{"path":"native/steam_session_observer.h","bytes":36476,"sha256":"ad6cf13134c60e2427f804e833a442786bf1696ed8d91bc2b935019f9b41bc2b"},"derived":{"path":"native/steam_session_observer.h","bytes":37780,"sha256":"587d232bf248bf3a4ae62f0a50aea434d98868a32b7ff0d968722045cacf041b"}}),
    Object.freeze({original:originalPin,derived:derivedPin}),
    // Reviewed NativeRead delegation successor; publication/terminal adapter unchanged.
    Object.freeze({"original":{"path":"native/steam_session_observer.h","bytes":34301,"sha256":"9edeb95ba65f4654b8b6073b0f55c0545743788adc0c7fb99da02080fad96cda"},"derived":{"path":"native/steam_session_observer.h","bytes":35605,"sha256":"473b787793c61b9b89d85c70e86164f70514b91d45de937f7fbb482ea42d05f7"}}),
    // Callback borrows the immutable receipt; publication adapter remains exact.
    Object.freeze({"original":{"path":"native/steam_session_observer.h","bytes":34383,"sha256":"09f0f0d0c52893d45b824edb38260edc35d1128bf0aeca201019577904c4d1d1"},"derived":{"path":"native/steam_session_observer.h","bytes":35687,"sha256":"8fcee6563931c415f107fdad54e0c85830683a00bc2f27af1372de522c6560b0"}}),
    // Worker description only; the exact private publication adapter is unchanged.
    Object.freeze({"original":{"path":"native/steam_session_observer.h","bytes":34473,"sha256":"639721f366174e36e3e5ce5fb4fdae110cddcc016881babcdd76ce08022b2d87"},"derived":{"path":"native/steam_session_observer.h","bytes":35777,"sha256":"f5c9ccd139cb5680a4683b8769704330a5acd579cbf14f715a2395c5da70b6b8"}})
]);
const prefix=Buffer.from(`// Private fixture visibility/terminal adapter. Never import into product API.
#include <windows.h>
namespace ymcc::steamsession::fixtureport {
inline thread_local DWORD windowsSession=0,error=ERROR_SUCCESS;
inline thread_local bool windowsSessionKnown=false;
inline BOOL WINAPI processSession(DWORD,PDWORD value){
    SetLastError(error);if(!windowsSessionKnown)return FALSE;*value=windowsSession;return TRUE;
}
}
#define ProcessIdToSessionId ymcc::steamsession::fixtureport::processSession
`);
const anchor=Buffer.from('class Observer {\n');
const friend=Buffer.from('    friend struct IdentityPublicationFixture; // Test-only fact publication access.\n');
const suffix=Buffer.from(`
#undef ProcessIdToSessionId
namespace ymcc::steamsession {
struct IdentityPublicationFixture {
    static void publish(Observer& owner,const IdentitySnapshot& facts){
        fixtureport::windowsSession=facts.windowsSession;
        fixtureport::windowsSessionKnown=facts.windowsSessionKnown;
        fixtureport::error=facts.windowsSessionError;
        // The original method assigns epochs, publishes facts and revokes leases.
        // No observer start, registry read or replacement delegation algorithm.
        owner.publishIdentity(facts.session,facts.presence,facts.registeredPid,
            facts.rootReadError,facts.registeredPidReadError,facts.accountReadError,GetCurrentProcess(),facts.registryRoot);
    }
};
}
`);
const hash=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');

export function steamSessionObserverPublication(product){
    assert(Buffer.isBuffer(product),'OBSERVER_PUBLICATION_BUFFER_REQUIRED');
    for(const marker of ['IdentityPublicationFixture','namespace ymcc::steamsession::fixtureport','#define ProcessIdToSessionId'])
        assert(!product.includes(Buffer.from(marker)),'OBSERVER_PUBLICATION_ALREADY_ADAPTED');
    assert(!product.includes(13),'OBSERVER_PUBLICATION_LF_REQUIRED');
    const at=product.indexOf(anchor);
    assert(at>=0&&product.indexOf(anchor,at+anchor.length)<0,'OBSERVER_PUBLICATION_UNIQUE_ANCHOR');
    assert(product.subarray(0,13).equals(Buffer.from('#pragma once\n')),'OBSERVER_PUBLICATION_PRODUCT_PREFIX');
    assert(product.subarray(-5).equals(Buffer.from('};\n}\n')),'OBSERVER_PUBLICATION_PRODUCT_SUFFIX');
    const epoch=declaredEpochs.find(row=>row.original.bytes===product.length&&row.original.sha256===hash(product));
    assert(epoch,'OBSERVER_PUBLICATION_UNDECLARED_PREIMAGE');
    const currentOriginalPin=epoch.original,currentDerivedPin=epoch.derived;
    assert.equal(product.length,currentOriginalPin.bytes,'OBSERVER_PUBLICATION_UNDECLARED_PREIMAGE');
    assert.equal(hash(product),currentOriginalPin.sha256,'OBSERVER_PUBLICATION_UNDECLARED_PREIMAGE');
    assert.equal(prefix.length,492);assert.equal(friend.length,84);assert.equal(suffix.length,728);
    const split=at+anchor.length;
    const bytes=Buffer.concat([prefix,product.subarray(0,split),friend,product.subarray(split),suffix]);
    assert.equal(bytes.length,currentDerivedPin.bytes,'OBSERVER_PUBLICATION_UNDECLARED_DERIVATION');
    assert.equal(hash(bytes),currentDerivedPin.sha256,'OBSERVER_PUBLICATION_UNDECLARED_DERIVATION');
    return {bytes,original:{...currentOriginalPin},derived:{...currentDerivedPin},generator:OBSERVER_PUBLICATION_GENERATOR,
        transformation:'one-friend+ProcessIdToSessionId terminal facts; exact product body preserved'};
}

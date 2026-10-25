// Preserve the exact header closure when compiling extracted OEM consumers.
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
export const oemInputBindingFunctions = ['oemUnifiedInputBindingFor', 'oemBoundChordProfileIdFor', 'oemBoundCatalogForFamily'];
export function captureOemInputHeaders({repo, out, source}) {
    if (!source.includes('oemUnifiedInputBindingFor(')) return {preamble: '', inputs: [], unchanged: () => true};
    const files = ['capabilities/HcOemInputCapabilityBinding.h', 'capabilities/hc-device-catalog.generated.h'];
    const inputs = files.map(file => {
        const original = path.join(repo, file), bytes = fs.readFileSync(original);
        const copy = path.join(out, 'oem-input-headers', file);
        fs.mkdirSync(path.dirname(copy), {recursive: true}); fs.writeFileSync(copy, bytes);
        return {file, original, copy, sha256: sha(bytes)};
    });
    return {
        preamble: '#include ' + JSON.stringify(inputs[0].copy.replaceAll('\\', '/')) + '\n',
        inputs,
        unchanged: () => inputs.every(input => sha(fs.readFileSync(input.original)) === input.sha256),
    };
}

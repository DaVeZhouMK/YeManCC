// Admission for importing HC JSON into YMCC's signed-axis recipe format.
// This validates representability; it does not establish a physical axis or source.
const names=['X','Y','Z'];
export function validateHcMotionMatrix(matrix) {
    const errors=[];
    const object=v=>v!==null && typeof v==='object' && !Array.isArray(v);
    if(!object(matrix))return {valid:false,errors:['matrix-not-object']};
    if(!object(matrix.Axis))errors.push('axis-not-object');
    if(!object(matrix.AxisSwap))errors.push('axis-swap-not-object');
    if(errors.length)return {valid:false,errors};
    const axis=names.map(n=>matrix.Axis[n]);
    if(axis.some(v=>typeof v!=='number'||!Number.isFinite(v)||Math.abs(v)!==1))
        errors.push('axis-not-signed-unit');
    if(Object.keys(matrix.Axis).some(n=>!names.includes(n)))errors.push('unknown-axis-key');
    if(Object.keys(matrix.AxisSwap).some(n=>!names.includes(n)))errors.push('unknown-axis-swap-key');
    const destinations=names.map(n=>matrix.AxisSwap[n]);
    if(destinations.some(n=>!names.includes(n)))errors.push('axis-swap-destination-invalid');
    if(new Set(destinations).size!==3)errors.push('axis-swap-not-bijective');
    return {valid:errors.length===0,errors,axis,swap:destinations.map(n=>names.indexOf(n))};
}
export function requireHcMotionMatrix(matrix,source) {
    const receipt=validateHcMotionMatrix(matrix);
    if(!receipt.valid)throw Error(`HC motion recipe rejected (${source}): ${receipt.errors.join(', ')}`);
    return receipt;
}

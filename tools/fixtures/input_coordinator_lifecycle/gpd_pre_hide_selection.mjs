export const gpdPreHidePrefix='gpd-pending-before-hide:';
export const gpdPreHideParameterPath='tools/fixtures/input_coordinator_lifecycle/pre_hide_cases.inc';

export function readGpdPreHideSelection(text,selection){
 const tables=[...text.matchAll(/const std::set<std::string> supported=\{([^}]+)\};/g)];
 const shapes=tables.length===1?[...tables[0][1].matchAll(/"([^"\r\n]+)"/g)].map(row=>row[1]):[];
 if(shapes.length!==16||new Set(shapes).size!==shapes.length)throw Error('GPD_PRE_HIDE_EXACT_SOURCE_TABLE_REQUIRED');
 if(typeof selection!=='string'||!selection.startsWith(gpdPreHidePrefix))throw Error('GPD_PRE_HIDE_UNKNOWN_PARAMETER');
 const selected=selection.slice(gpdPreHidePrefix.length).split(',');
 if(selected.some(shape=>!shapes.includes(shape)))throw Error('GPD_PRE_HIDE_UNKNOWN_PARAMETER');
 if(new Set(selected).size!==selected.length)throw Error('GPD_PRE_HIDE_DUPLICATE_PARAMETER');
 const [shape]=selected;
 return {selectedCase:selection,...(selected.length===1?{selectedShape:shape}:{selectedShapes:selected}),requestedModes:['family-lifecycle'],expectedCases:selected.map(value=>'family-lifecycle/GPD/pending-before-hide/'+value),expectedCount:selected.length,availableParameterCount:shapes.length,nativeOnly:true,managed:false};
}

export function inspectGpdPreHideCase(producer,expected){
 const report=producer?.result,rows=Array.isArray(report?.cases)?report.cases:[];
 const exact=producer?.mode==='family-lifecycle'&&!producer.error&&!producer.signal&&!producer.timedOut&&
  rows.length===1&&rows[0]?.name===expected;
 const semanticPassed=exact&&producer.exitCode===0&&report.status==='PASS'&&report.forbiddenIo===0&&rows[0].passed===true;
 return {actualCases:rows.map(row=>row?.name),exact,semanticPassed};
}

export function inspectGpdPreHideBatch(selection,results){
 const cases=results.map((row,index)=>inspectGpdPreHideCase(row,selection.expectedCases[index]));
 const full=results.length===selection.expectedCount;
 return {actualCases:cases.flatMap(row=>row.actualCases),exact:full&&cases.every(row=>row.exact),
  semanticPassed:full&&cases.every(row=>row.semanticPassed),notRunCases:selection.expectedCases.slice(results.length)};
}

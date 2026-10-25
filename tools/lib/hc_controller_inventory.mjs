export function controllerInventoryErrors(contract, syntax) {
 const errors=[];
 const inventory=contract?.sourceInventory;
 if(!inventory||!Array.isArray(inventory.files)||!Array.isArray(syntax?.files))return ['CONTROLLER_SYNTAX_INVENTORY_MISSING'];
 const need=(value,code)=>{if(!value)errors.push(code);};
 const sorted=values=>JSON.stringify([...values].sort());
 const unique=values=>Array.isArray(values)&&new Set(values).size===values.length;
 need(Array.isArray(syntax.issues)&&syntax.issues.length===0,'CONTROLLER_SYNTAX_ERRORS');
 need(inventory.fileCount===syntax.fileCount&&inventory.files.length===syntax.files.length,'CONTROLLER_SYNTAX_FILE_COUNT');
 need(inventory.classDeclarationCount===syntax.classDeclarationCount,'CONTROLLER_SYNTAX_CLASS_COUNT');
 need(inventory.dummyFiles===syntax.files.filter(row=>row.file.startsWith('Controllers/Dummies/')).length,'CONTROLLER_SYNTAX_DUMMY_COUNT');
 need(new Set(inventory.files.map(row=>row.file)).size===inventory.files.length,'CONTROLLER_SYNTAX_DUPLICATE_FILE');
 for(const row of inventory.files){
  const actual=syntax.files.find(item=>item.file===row.file);
  if(!actual){errors.push('CONTROLLER_SYNTAX_FILE_MISSING:'+row.file);continue;}
  need(row.sha256===actual.sha256,'CONTROLLER_SYNTAX_SOURCE_HASH:'+row.file);
  for(const field of ['literalCapabilities','sourceButtonsMentioned'])need(unique(row[field])&&sorted(row[field])===sorted(actual[field]),'CONTROLLER_SYNTAX_'+field+':'+row.file);
  const classes=actual.classes.map(({name,namespace,abstract,bases})=>({name,namespace,abstract,bases}));
  need(JSON.stringify(row.classes)===JSON.stringify(classes),'CONTROLLER_SYNTAX_CLASSES:'+row.file);
 }
 return errors;
}

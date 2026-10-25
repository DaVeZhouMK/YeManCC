// Official settings consumer API generation. YMCC owns every product update.
// 3.x >=3.3 is reviewed; a new major requires UI/lifecycle compatibility review.
export function supportsOfficialUpdaterConsumer(current:unknown):boolean {
 if(typeof current!=='string'||current.length>32)return false;
 const v=/^v?(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/.exec(current);
 return !!v&&v.slice(1).every(n=>Number.isSafeInteger(Number(n)))&&Number(v[1])===3&&Number(v[2])>=3;
}
